// sys_map_movement: bodies that move through the playfield's map and stop at
// its solid metatiles. See map.h.
//
// A body covers the rectangle [x, x + body_w) x [y, y + body_h), in FIXED
// world pixels: a body at y = 10.25 that is 16 high covers pixel rows 10 to
// 26. It moves one axis at a time, X then Y, in steps of at most 7 pixels:
// less than a metatile, so its leading edge enters at most one new row or
// column of metatiles per step, and only those need checking. A step that
// would take the leading edge into a blocking metatile instead puts the body
// flush against it (on a whole pixel), records the contact and bounces the
// velocity on that axis (rebound(), which for a perfect bounce off a floor
// needs how much of the frame's movement was cut short); the rest of that
// axis's movement this frame is dropped.
//
// Only metatiles the leading edge newly enters block it, so a body already
// overlapping a solid metatile (spawned inside, or a cell changed under it)
// can move out of it. This is also what makes one-way platforms work: a body
// moving down enters a MAP_ONEWAY row only from above it, while a body that
// jumped into one from below is already inside its row and falls through.
//
// ladders and slopes: a playfield whose metatiles include slopes or ladders
// (serval_map_kinds, found by map_load()) takes the general path below
// (move_general()), which is the path above plus them; others keep the
// path above, which costs what it did before they existed.

#include "serval/map.h"
#include "serval/physics.h"

#include "../core/map_internal.h"
#include "../core/warn.h"
#include "physics_internal.h"

#define MAX_STEP FX(7)

static const MapLayer* playfield;

// How bodies treat each collision type (MAP_TYPE): the ten this version
// implements as themselves, the reserved types 10-15 as MAP_EMPTY (map_load()
// warns about those, src/core/map.c). The fast path (move_x(), move_y()) only
// runs on playfields without ladders and slopes, so it never meets them.
static const u8 movement_type[16] = {
    [MAP_EMPTY] = MAP_EMPTY,
    [MAP_SOLID] = MAP_SOLID,
    [MAP_ONEWAY] = MAP_ONEWAY,
    [MAP_LADDER] = MAP_LADDER,
    [MAP_SLOPE_R] = MAP_SLOPE_R,
    [MAP_SLOPE_L] = MAP_SLOPE_L,
    [MAP_SLOPE_R_LOW] = MAP_SLOPE_R_LOW,
    [MAP_SLOPE_R_HIGH] = MAP_SLOPE_R_HIGH,
    [MAP_SLOPE_L_HIGH] = MAP_SLOPE_L_HIGH,
    [MAP_SLOPE_L_LOW] = MAP_SLOPE_L_LOW,
    [10] = MAP_EMPTY,
    [11] = MAP_EMPTY,
    [12] = MAP_EMPTY,
    [13] = MAP_EMPTY,
    [14] = MAP_EMPTY,
    [15] = MAP_EMPTY,
};

// Collision type of metatile (mx, my) of the playfield as bodies treat it
// (movement_type), by map_collision_at's rules for cells outside it.
static u32 type_at(int mx, int my) {
    if ((u32)mx >= playfield->width)
        return MAP_SOLID;
    if ((u32)my >= playfield->height)
        return MAP_EMPTY;
    // ladders and slopes: with no changed cells (the usual case), the cell
    // is read here rather than through a call that looks for changes.
    u32 cell = serval_map_change_count ? serval_map_cell_in(playfield, (u32)mx, (u32)my)
                                       : playfield->cells[(u32)my * playfield->width + (u32)mx];
    return cell < playfield->metatile_count
               ? movement_type[MAP_TYPE(playfield->metatiles[cell].collision)]
               : MAP_EMPTY;
}

// Pixel coordinates of the first and last pixel a span [pos, pos + size)
// covers.
static int first_pixel(FIXED pos) {
    return pos >> FX_SHIFT;
}
static int last_pixel(FIXED pos, u32 size) {
    return ((pos + FX((int)size) + FX_ONE - 1) >> FX_SHIFT) - 1;
}

// True if metatile column mx blocks a body covering pixel rows top-bottom.
static bool column_blocked(int mx, int top, int bottom) {
    for (int my = top >> 4; my <= bottom >> 4; my++) {
        if (type_at(mx, my) == MAP_SOLID)
            return true;
    }
    return false;
}

// True if metatile row my blocks a body covering pixel columns left-right,
// entering it moving down (one-way platforms block) or up (they don't).
static bool row_blocked(int my, int left, int right, bool down) {
    for (int mx = left >> 4; mx <= right >> 4; mx++) {
        u32 type = type_at(mx, my);
        if (type == MAP_SOLID || (down && type == MAP_ONEWAY))
            return true;
    }
    return false;
}

static FIXED clamp_step(FIXED v) {
    return v > MAX_STEP ? MAX_STEP : v < -MAX_STEP ? -MAX_STEP : v;
}

// The velocity of a body that hit the map moving at `vel` on an axis: reversed,
// keeping body_bounce/256 of the speed (0, the default, stops it). On a floor
// (the side `gravity` pulls toward on this axis) a rebound too slow to clear
// twice one frame's gravity is a rest instead, as in sys_physics(), whatever
// the bounce: a body standing on a floor, which gravity pulls into it every
// frame, stays there with zero speed rather than hopping a fraction of a
// pixel forever.
//
// 255 is a perfect bounce (a u8 can't hold 256; otherwise even the bounciest
// body would lose a little height on every bounce). Off walls and ceilings
// it keeps the speed. Off a floor, keeping the speed isn't enough: the body
// stops flush against the floor `past` short of where this frame's movement
// would have taken it, and with the speed kept it would rebound lower than it
// fell from by up to a frame's fall (always the same, so later bounces repeat
// the first). So a body that hit at least twice one frame's gravity fast
// rebounds at the speed that brings it back exactly to the height it fell
// from (serval_perfect_rebound(), physics_internal.h, with `past` lost), or
// rests if that is slower than twice one frame's gravity.
static FIXED rebound(FIXED vel, u32 bounce, FIXED gravity, FIXED past) {
    FIXED speed = serval_fx_abs(vel);
    const bool floor = (gravity > 0 && vel > 0) || (gravity < 0 && vel < 0);
    const FIXED slowest = 2 * serval_fx_abs(gravity);
    if (bounce != SERVAL_BOUNCE_PERFECT)
        speed = (FIXED)(((u32)speed * bounce) >> 8);
    else if (floor && speed >= slowest)
        speed = serval_perfect_rebound(speed, past, slowest >> 1);
    if (floor && speed < slowest)
        return 0;
    return vel > 0 ? -speed : speed;
}

// How far short of a frame's movement a body stopped: `remaining` is what was
// left of it before the step that hit (that step included), and the body
// moved from `from` to `flush` to touch the map. The same sign throughout.
static FIXED short_by(FIXED remaining, FIXED from, FIXED flush) {
    return serval_fx_abs(remaining - (flush - from));
}

static u32 move_x(u32 i, u32 w, u32 h, FIXED gravity) {
    int top = first_pixel(pos_y[i]), bottom = last_pixel(pos_y[i], h);
    for (FIXED left = vel_x[i]; left != 0;) {
        FIXED step = clamp_step(left);
        left -= step;
        FIXED x = pos_x[i] + step;
        if (step > 0) {
            int from = last_pixel(pos_x[i], w) >> 4, to = last_pixel(x, w) >> 4;
            if (to != from && column_blocked(to, top, bottom)) {
                FIXED flush = FX(to * 16 - (int)w);
                FIXED past = short_by(left + step, pos_x[i], flush);
                pos_x[i] = flush;
                vel_x[i] = rebound(vel_x[i], body_bounce[i], gravity, past);
                return MAP_CONTACT_RIGHT;
            }
        } else {
            int from = first_pixel(pos_x[i]) >> 4, to = first_pixel(x) >> 4;
            if (to != from && column_blocked(to, top, bottom)) {
                FIXED flush = FX(to * 16 + 16);
                FIXED past = short_by(left + step, pos_x[i], flush);
                pos_x[i] = flush;
                vel_x[i] = rebound(vel_x[i], body_bounce[i], gravity, past);
                return MAP_CONTACT_LEFT;
            }
        }
        pos_x[i] = x;
    }
    return 0;
}

static u32 move_y(u32 i, u32 w, u32 h, FIXED gravity) {
    int left_px = first_pixel(pos_x[i]), right_px = last_pixel(pos_x[i], w);
    for (FIXED left = vel_y[i]; left != 0;) {
        FIXED step = clamp_step(left);
        left -= step;
        FIXED y = pos_y[i] + step;
        if (step > 0) {
            int from = last_pixel(pos_y[i], h) >> 4, to = last_pixel(y, h) >> 4;
            if (to != from && row_blocked(to, left_px, right_px, true)) {
                FIXED flush = FX(to * 16 - (int)h);
                FIXED past = short_by(left + step, pos_y[i], flush);
                pos_y[i] = flush;
                vel_y[i] = rebound(vel_y[i], body_bounce[i], gravity, past);
                return MAP_CONTACT_FLOOR;
            }
        } else {
            int from = first_pixel(pos_y[i]) >> 4, to = first_pixel(y) >> 4;
            if (to != from && row_blocked(to, left_px, right_px, false)) {
                FIXED flush = FX(to * 16 + 16);
                FIXED past = short_by(left + step, pos_y[i], flush);
                pos_y[i] = flush;
                vel_y[i] = rebound(vel_y[i], body_bounce[i], gravity, past);
                return MAP_CONTACT_CEILING;
            }
        }
        pos_y[i] = y;
    }
    return 0;
}

// --- ladders and slopes: the general path ------------------------------------
//
// On a playfield with ladders or slopes, bodies move as above, plus what
// follows (docs/tilemaps.md#slopes and #ladders say it for games).
//
// Slopes. A slope's solid part is a height per pixel column (slope_height()):
// every pixel its line passes through is solid. It holds a body up at the
// body's foot only, the pixel column at its horizontal centre (pos_x +
// body_w / 2): standing on a slope, the body's bottom edge is on the slope's
// surface in that column, its uphill corner sunk into the ground and the
// downhill one over it, as in most games with slopes. So the body's corners
// must not catch on the ground around a slope: on the solid metatile behind
// a slope's top, under the next slope up, or under a hill. Three rules see
// to that, without which a body would stop at the top of every slope:
//   - While the foot is on (or above) a slope, the slope's line, extended
//     across the body, is the ground (ground_line()): solid, one-way and
//     ladder-top metatiles and slope sides at or below it stop nothing.
//   - Moving sideways, the leading edge entering a metatile meets its side
//     (the column it enters, a slope's height there) less what a slope behind
//     it covers (the slope's height in the column it leaves): the side of a
//     solid metatile behind a slope's top is covered. A side of at most a
//     pixel (a slope's low end) is a step, as is anything reaching only into
//     the body's lowest pixel row. Other sides are walls: a slope's exposed
//     high side, or a solid metatile.
//   - Moving down, a solid, one-way or ladder-top metatile under a slope is
//     its foundation, not a floor, except under the foot.
// Moving sideways, the foot follows the ground: it climbs slopes (and onto
// the solid metatile at a slope's top), rising at most a pixel per pixel
// column it moves (the steepest slope's rise); a taller rise is a wall,
// though the leading edge has normally stopped the body at it already.
// Bodies walking on the ground (on the floor last frame, gravity pulling
// down, not moving up) also follow it down, falling at most a pixel per
// column, so they stick to a slope they walk down instead of flying off its
// top, and still walk off ledges. Moving down, the foot lands on a slope's
// surface. Moving up, a slope is a ceiling across its width, as each of its
// columns is solid at its bottom.
//
// Ladders. Climbable, not solid: a ladder's top (a ladder metatile without
// one above it) is a floor to bodies whose gravity pulls down, like a one-way
// platform, and bodies without gravity pass through it both ways. A body that
// overlaps a ladder metatile, or stands on one (the pixel row below its feet),
// gets MAP_CONTACT_LADDER.

static bool is_slope(u32 type) {
    return type - MAP_SLOPE_R <= MAP_SLOPE_L_LOW - MAP_SLOPE_R;
}

// The height in pixels of slope `type`'s solid part in pixel column c (0-15)
// of its metatile: its line's height at the column's higher edge, rounded up,
// so every pixel the line passes through is solid (1 to 16 at 45 degrees, 1
// to 8 and 9 to 16 at 1:2). For other c, its line extended past the metatile.
static int slope_height(u32 type, int c) {
    switch (type) {
    case MAP_SLOPE_R:
        return c + 1;
    case MAP_SLOPE_L:
        return 16 - c;
    case MAP_SLOPE_R_LOW:
        return (c >> 1) + 1;
    case MAP_SLOPE_R_HIGH:
        return (c >> 1) + 9;
    case MAP_SLOPE_L_HIGH:
        return 16 - (c >> 1);
    default: // MAP_SLOPE_L_LOW
        return 8 - (c >> 1);
    }
}

// The collision type of metatile (mx, my) of the playfield as type_at()
// says, for mx and my inside it, `column` being its cells from column mx and
// the other arguments its fields; `direct` if no cell is changed.
static inline u32 column_type(const MapLayer* layer, const u16* column, const Metatile* metatiles,
                              u32 width, u32 count, bool direct, u32 mx, u32 my) {
    u32 cell = direct ? column[my * width] : serval_map_cell_in(layer, mx, my);
    return cell < count ? movement_type[MAP_TYPE(metatiles[cell].collision)] : MAP_EMPTY;
}

// What is under a body's foot, from probe().
enum {
    GROUND_NONE,   // nothing near
    GROUND_SLOPE,  // a slope's surface
    GROUND_FLOOR,  // the top of a solid, one-way or ladder-top metatile
    GROUND_INSIDE, // the foot is deep in solid metatiles
};
typedef struct {
    u32 kind;
    int surface;      // SLOPE, FLOOR: the y a body's bottom edge rests at
    u32 type;         // SLOPE: the slope's type,
    int left, bottom; // and its metatile's left and bottom edges
    int ledge;        // the top of a one-way metatile or ladder top the foot is in, or NO_LEDGE
    int x, row;       // where probe() looked: the foot's pixel column, the bottom's metatile row
} Ground;
#define NO_LEDGE 0x7FFFFFFF

// The ground under pixel column x (a body's foot) near a body whose lowest
// pixel row is `bottom`: in the metatile there, or in the one below if that
// one is open; or, if the foot is in a solid metatile, that metatile's top or
// a slope on it. gy is the body's gravity: ladder tops are floors only to
// bodies it pulls down. Also the top of a one-way metatile or ladder top the
// foot is in, which a body walking up a slope steps onto (.ledge).
static void probe(Ground* g, int x, int bottom, FIXED gy) {
    int mx = x >> 4, my = bottom >> 4;
    g->x = x;
    g->row = my;
    g->ledge = NO_LEDGE;
    // The metatile column's types, read here (as type_at() does) with the
    // playfield's fields loaded once: this runs for every moving body.
    const MapLayer* layer = playfield;
    const u16* column = layer->cells + mx;
    const u32 width = layer->width, height = layer->height, count = layer->metatile_count;
    const Metatile* metatiles = layer->metatiles;
    const bool direct = serval_map_change_count == 0;
#define TYPE_IN_COLUMN(row)                                                                        \
    ((u32)(row) >= height                                                                          \
         ? MAP_EMPTY                                                                               \
         : column_type(layer, column, metatiles, width, count, direct, (u32)mx, (u32)(row)))
    if ((u32)mx >= width) {
        g->kind = GROUND_INSIDE; // the map's sides are solid
        return;
    }
    u32 type = TYPE_IN_COLUMN(my);
    if (type == MAP_ONEWAY ||
        (type == MAP_LADDER && gy > 0 && TYPE_IN_COLUMN(my - 1) != MAP_LADDER))
        g->ledge = my * 16;
    if (type == MAP_SOLID) {
        u32 above = TYPE_IN_COLUMN(my - 1);
        if (!is_slope(above)) {
            g->kind = above == MAP_SOLID ? GROUND_INSIDE : GROUND_FLOOR;
            g->surface = my * 16;
            return;
        }
        type = above;
        my--;
    } else if (!is_slope(type)) {
        u32 below = TYPE_IN_COLUMN(my + 1);
        if (!is_slope(below)) {
            bool floor = below == MAP_SOLID || below == MAP_ONEWAY ||
                         (below == MAP_LADDER && gy > 0 && type != MAP_LADDER);
            g->kind = floor ? GROUND_FLOOR : GROUND_NONE;
            g->surface = my * 16 + 16;
            return;
        }
        type = below;
        my++;
    }
#undef TYPE_IN_COLUMN
    g->kind = GROUND_SLOPE;
    g->type = type;
    g->left = mx * 16;
    g->bottom = my * 16 + 16;
    g->surface = g->bottom - slope_height(type, x - g->left);
}

// The y of the ground line at pixel column x, for a body whose foot is on or
// above slope ground g: the slope's line, extended past its metatile.
static int ground_line(const Ground* g, int x) {
    return g->bottom - slope_height(g->type, x - g->left);
}

// g, if a body whose bottom edge is at y_b has its foot on or above a slope
// (not inside one), so the ground line applies; NULL otherwise.
static const Ground* slope_under(const Ground* g, FIXED y_b) {
    return g->kind == GROUND_SLOPE && y_b <= FX(g->surface) ? g : NULL;
}

// True if metatile column mx, whose pixel column x the leading edge enters
// from pixel column `from` beside it, blocks a body covering pixel rows top
// to bottom; `line` is its ground line, or NULL.
static bool side_blocked(int mx, int x, int from, int top, int bottom, const Ground* line) {
    for (int my = top >> 4; my <= bottom >> 4; my++) {
        u32 type = type_at(mx, my);
        int side = my * 16; // the top of what the edge meets in this row
        if (is_slope(type))
            side += 16 - slope_height(type, x & 15);
        else if (type != MAP_SOLID)
            continue;
        // Below `covered`, a slope behind the edge covers the side.
        int covered = my * 16 + 16;
        u32 behind = type_at(from >> 4, my);
        if (is_slope(behind))
            covered -= slope_height(behind, from & 15);
        if (side >= covered - 1 || side >= bottom || covered <= top)
            continue; // covered, a step of a pixel, or not beside the body
        if (line && side >= ground_line(line, x))
            continue; // under the ground line
        return true;
    }
    return false;
}

// True if metatile `type` at (mx, my) stops a body moving down into it from
// above (whose foot is at pixel column foot): a solid or one-way metatile, or
// a ladder top if the body's gravity gy pulls down; not at or below the
// ground line, nor a slope's foundation away from the foot.
static bool floor_blocks(u32 type, int mx, int my, int foot, FIXED gy, const Ground* line) {
    if (type == MAP_LADDER) {
        if (gy <= 0 || type_at(mx, my - 1) == MAP_LADDER)
            return false;
    } else if (type != MAP_SOLID && type != MAP_ONEWAY) {
        return false;
    }
    int x = foot < mx * 16 ? mx * 16 : foot > mx * 16 + 15 ? mx * 16 + 15 : foot;
    if (line && my * 16 >= ground_line(line, x))
        return false;
    return x == foot || !is_slope(type_at(mx, my - 1));
}

// True if metatile row my stops a body covering pixel columns left-right from
// above (floor_blocks()). The foot's column first: on flat ground, it is
// the only one looked at.
static bool floor_row_blocked(int my, int left, int right, int foot, FIXED gy, const Ground* line) {
    int fx = foot >> 4;
    if (floor_blocks(type_at(fx, my), fx, my, foot, gy, line))
        return true;
    for (int mx = left >> 4; mx <= right >> 4; mx++) {
        if (mx != fx && floor_blocks(type_at(mx, my), mx, my, foot, gy, line))
            return true;
    }
    return false;
}

// True if metatile row my stops a body covering pixel columns left-right
// from below: solid metatiles and slopes.
static bool ceiling_row_blocked(int my, int left, int right) {
    for (int mx = left >> 4; mx <= right >> 4; mx++) {
        u32 type = type_at(mx, my);
        if (type == MAP_SOLID || is_slope(type))
            return true;
    }
    return false;
}

static int foot_of(FIXED x, u32 w) {
    return (x + (FX((int)w) >> 1)) >> FX_SHIFT;
}

// True if the ground g is above a body's bottom edge y_b: the foot is in it.
static bool foot_in(const Ground* g, FIXED y_b) {
    return g->kind == GROUND_INSIDE || (g->kind != GROUND_NONE && FX(g->surface) < y_b);
}

// Lifts or lowers body i, whose foot moved `columns` pixel columns onto
// ground g, as the foot follows the ground (above). Returns false if the
// ground is too high to step onto, or its head would hit a ceiling at x;
// otherwise sets *contact to MAP_CONTACT_FLOOR if it lifted it.
static bool follow_ground(u32 i, FIXED x, u32 w, u32 h, const Ground* g, int columns, bool walking,
                          u32* contact) {
    FIXED y_b = pos_y[i] + FX((int)h), most = FX(columns);
    // Walking up a slope onto a one-way metatile (or a ladder top) level
    // with its top, the foot may reach it a pixel or more inside its row.
    bool ledge =
        walking && g->ledge != NO_LEDGE && FX(g->ledge) < y_b && y_b - FX(g->ledge) <= most;
    if (!ledge && g->kind != GROUND_SLOPE && g->kind != GROUND_FLOOR)
        return true;
    FIXED ground = FX(ledge ? g->ledge : g->surface);
    if (ground < y_b) {
        FIXED lift = y_b - ground;
        if (lift > most)
            return false;
        int top = first_pixel(pos_y[i]), head = first_pixel(pos_y[i] - lift);
        if (head >> 4 != top >> 4 &&
            ceiling_row_blocked(head >> 4, first_pixel(x), last_pixel(x, w)))
            return false;
        pos_y[i] -= lift;
        *contact = MAP_CONTACT_FLOOR;
    } else if (walking && ground > y_b && ground - y_b <= most) {
        pos_y[i] += ground - y_b; // walking down: it sticks to the ground
    }
    return true;
}

// True if ground g, under a foot that moved, would lift or lower a body
// whose bottom edge is at y_b (follow_ground() has something to do).
static bool ground_differs(const Ground* g, FIXED y_b, bool walking) {
    if (walking && g->ledge != NO_LEDGE && FX(g->ledge) < y_b)
        return true;
    if (g->kind != GROUND_SLOPE && g->kind != GROUND_FLOOR)
        return false;
    FIXED ground = FX(g->surface);
    return ground < y_b || (walking && ground > y_b);
}

// Moves a body along x as move_x() does, its foot following the ground
// (above). Returns the contacts: a side, or MAP_CONTACT_FLOOR if the ground
// lifted it.
// grounds[0] is left holding the ground under its foot where it ends, if
// known (.row then says which row it was looked at from), for
// general_move_y().
static u32 general_move_x(u32 i, u32 w, u32 h, FIXED gx, FIXED gy, bool walking,
                          Ground grounds[2]) {
    // The ground under the foot where it is (*g), looked at only when needed
    // (on flat ground, a body crossing no metatile edge never needs it), and
    // where a step takes it (*next); swapped, not copied, as the foot moves.
    Ground *g = &grounds[0], *next = &grounds[1];
    bool known = false;
    bool inside = false; // the foot is already in the ground: it moves freely
    u32 contact = 0;
    for (FIXED left = vel_x[i]; left != 0;) {
        FIXED step = clamp_step(left);
        left -= step;
        FIXED x = pos_x[i] + step;
        FIXED y_b = pos_y[i] + FX((int)h);
        int top = first_pixel(pos_y[i]), bottom = last_pixel(pos_y[i], h);
        int f0 = foot_of(pos_x[i], w), f1 = foot_of(x, w);
        if (f1 != f0)
            probe(next, f1, bottom, gy);
        // The leading edge, entering a new metatile column, meets the ground
        // line of the slope the foot will be on (or is on).
        int from = step > 0 ? last_pixel(pos_x[i], w) : first_pixel(pos_x[i]);
        int to = step > 0 ? last_pixel(x, w) : first_pixel(x);
        u32 side = 0;
        if (to >> 4 != from >> 4) {
            if (!known) {
                probe(g, f0, bottom, gy);
                inside = foot_in(g, y_b);
                known = true;
            }
            const Ground* line =
                f1 != f0 && !inside && next->kind == GROUND_SLOPE ? next : slope_under(g, y_b);
            // The first (moving right) or last pixel column of the metatile
            // column it enters, and the one beside it, which it leaves.
            int edge = step > 0 ? to & ~15 : to | 15;
            if (side_blocked(to >> 4, edge, step > 0 ? edge - 1 : edge + 1, top, bottom, line)) {
                side = step > 0 ? MAP_CONTACT_RIGHT : MAP_CONTACT_LEFT;
                x = step > 0 ? FX(edge - (int)w) : FX(edge + 1);
                // Stopped by the wall, the foot may not get as far.
                int stopped = foot_of(x, w);
                if (stopped != f1 && stopped != f0)
                    probe(next, stopped, bottom, gy);
                f1 = stopped;
            }
        }
        if (f1 != f0) {
            if (ground_differs(next, y_b, walking)) {
                if (!known) {
                    probe(g, f0, bottom, gy);
                    inside = foot_in(g, y_b);
                }
                if (!inside && !follow_ground(i, x, w, h, next, f1 > f0 ? f1 - f0 : f0 - f1,
                                              walking, &contact)) {
                    // Too steep, or no head room: a wall at the foot. It stays.
                    vel_x[i] = rebound(vel_x[i], body_bounce[i], gx, serval_fx_abs(left + step));
                    contact |= step > 0 ? MAP_CONTACT_RIGHT : MAP_CONTACT_LEFT;
                    break;
                }
            }
            Ground* was = g;
            g = next;
            next = was;
            inside = foot_in(g, pos_y[i] + FX((int)h));
            known = true;
        }
        if (side) {
            FIXED past = short_by(left + step, pos_x[i], x);
            pos_x[i] = x;
            vel_x[i] = rebound(vel_x[i], body_bounce[i], gx, past);
            contact |= side;
            break;
        }
        pos_x[i] = x;
    }
    if (g != &grounds[0])
        grounds[0] = *g;
    if (!known)
        grounds[0].row = -0x7FFFFFFF; // nothing looked at
    return contact;
}

// Moves a body along y as move_y() does, landing on the slope under its
// foot, and on ladder tops if its gravity gy pulls down.
// g may hold the ground under the foot (from general_move_x()), used if it
// was looked at from where the body's bottom is.
static u32 general_move_y(u32 i, u32 w, u32 h, FIXED gy, Ground* g) {
    int left_px = first_pixel(pos_x[i]), right_px = last_pixel(pos_x[i], w);
    int foot = foot_of(pos_x[i], w);
    for (FIXED left = vel_y[i]; left != 0;) {
        FIXED step = clamp_step(left);
        left -= step;
        FIXED y = pos_y[i] + step;
        if (step > 0) {
            FIXED y_b = pos_y[i] + FX((int)h);
            int from = last_pixel(pos_y[i], h), to = last_pixel(y, h);
            if (g->x != foot || g->row != from >> 4)
                probe(g, foot, from, gy);
            const Ground* line = slope_under(g, y_b);
            // The highest surface the bottom edge crosses in this step: the
            // slope under the foot, or the top of a row it enters. A floor
            // under the foot in that row stops it whatever else is there.
            int stop = 0x7FFFFFFF;
            if (line && FX(g->surface) < y_b + step)
                stop = g->surface;
            if (to >> 4 != from >> 4 && (to & ~15) < stop &&
                ((g->kind == GROUND_FLOOR && g->surface == (to & ~15)) ||
                 floor_row_blocked(to >> 4, left_px, right_px, foot, gy, line)))
                stop = to & ~15;
            if (stop != 0x7FFFFFFF) {
                FIXED flush = FX(stop - (int)h);
                FIXED past = short_by(left + step, pos_y[i], flush);
                pos_y[i] = flush;
                vel_y[i] = rebound(vel_y[i], body_bounce[i], gy, past);
                return MAP_CONTACT_FLOOR;
            }
        } else {
            int from = first_pixel(pos_y[i]) >> 4, to = first_pixel(y) >> 4;
            if (to != from && ceiling_row_blocked(to, left_px, right_px)) {
                FIXED flush = FX(to * 16 + 16);
                FIXED past = short_by(left + step, pos_y[i], flush);
                pos_y[i] = flush;
                vel_y[i] = rebound(vel_y[i], body_bounce[i], gy, past);
                return MAP_CONTACT_CEILING;
            }
        }
        pos_y[i] = y;
    }
    return 0;
}

// MAP_CONTACT_LADDER if the body overlaps a ladder metatile or stands on one
// (the pixel row just below its feet).
static u32 ladder_contact(u32 i, u32 w, u32 h) {
    int left = first_pixel(pos_x[i]) >> 4, right = last_pixel(pos_x[i], w) >> 4;
    int top = first_pixel(pos_y[i]) >> 4, below = (last_pixel(pos_y[i], h) + 1) >> 4;
    // Clipped to the playfield: there are no ladders outside it.
    int width = playfield->width, height = playfield->height;
    left = left < 0 ? 0 : left;
    top = top < 0 ? 0 : top;
    right = right >= width ? width - 1 : right;
    below = below >= height ? height - 1 : below;
    for (int my = top; my <= below; my++) {
        for (int mx = left; mx <= right; mx++) {
            if (type_at(mx, my) == MAP_LADDER)
                return MAP_CONTACT_LADDER;
        }
    }
    return 0;
}

// Both axes on a playfield with ladders or slopes. `before` is the body's
// contacts from the last frame: a body on the floor then, whose gravity pulls
// down and that isn't moving up, is walking (follows the ground down).
static __attribute__((noinline)) u32 move_general(u32 i, u32 w, u32 h, FIXED gx, FIXED gy,
                                                  u32 before) {
    bool walking = gy > 0 && vel_y[i] >= 0 && (before & MAP_CONTACT_FLOOR);
    Ground grounds[2];
    grounds[0].row = -0x7FFFFFFF; // nothing looked at yet
    u32 contact = vel_x[i] ? general_move_x(i, w, h, gx, gy, walking, grounds) : 0;
    contact |= general_move_y(i, w, h, gy, &grounds[0]);
    if (serval_map_kinds & SERVAL_MAP_HAS_LADDERS)
        contact |= ladder_contact(i, w, h);
    return contact;
}

#ifdef SERVAL_DEBUG
static __attribute__((noinline)) void warn_body(u32 i, u32 problem) {
    static u32 warned;
    if (warned & (1u << problem))
        return;
    warned |= 1u << problem;
    if (problem == 0)
        SERVAL_WARN("sys_map_movement: entity %u has C_MAPBODY but not all of C_POS, C_VEL and "
                    "C_BODY; it doesn't move",
                    i);
    else if (problem == 1)
        SERVAL_WARN("sys_map_movement: entity %u is a map body of size %ux%u; set body_w and "
                    "body_h (moving it as 1x1 for now)",
                    i, body_w[i], body_h[i]);
    else
        SERVAL_WARN("sys_map_movement: no map on background 2 (the playfield), so map bodies "
                    "collide with nothing");
}
#define WARN_BODY(i, problem) warn_body(i, problem)
#else
#define WARN_BODY(i, problem) ((void)0)
#endif

void sys_map_movement(void) {
    playfield = serval_map_layers[2];
    const FIXED gravity_x = serval_gravity_x, gravity_y = serval_gravity_y;
    const bool general = serval_map_kinds != 0; // ladders and slopes
    serval_map_bodies_moved = false;
    ECS_FOR_EACH(i, C_MAPBODY) {
        serval_map_bodies_moved = true;
        const u32 before = body_contact[i];
        body_contact[i] = 0;
        if (!ent_has(i, C_POS | C_VEL | C_BODY)) {
            WARN_BODY(i, 0);
            continue;
        }
        u32 w = body_w[i], h = body_h[i];
        if (w == 0 || h == 0) {
            WARN_BODY(i, 1);
            w = w ? w : 1;
            h = h ? h : 1;
        }
        // The body's own gravity (body_gravity).
        const FIXED gx = serval_body_gravity(gravity_x, body_gravity[i]);
        const FIXED gy = serval_body_gravity(gravity_y, body_gravity[i]);
        vel_x[i] = serval_limit_fall(vel_x[i] + gx, gx, &body_max_fall[i]);
        vel_y[i] = serval_limit_fall(vel_y[i] + gy, gy, &body_max_fall[i]);
        if (!playfield) {
            WARN_BODY(i, 2);
            pos_x[i] += vel_x[i];
            pos_y[i] += vel_y[i];
            continue;
        }
        u32 contact;
        if (general) {
            contact = move_general(i, w, h, gx, gy, before);
        } else {
            contact = move_x(i, w, h, gx);
            contact |= move_y(i, w, h, gy);
        }
        body_contact[i] = (u8)contact;
        // Friction along a floor: the side gravity pulls toward. Skipped
        // without friction, so slow bodies keep their speed (serval_slide
        // stops any speed under a sixteenth of a pixel).
        u32 friction = body_friction[i];
        if (friction) {
            if ((gy > 0 && (contact & MAP_CONTACT_FLOOR)) ||
                (gy < 0 && (contact & MAP_CONTACT_CEILING)))
                vel_x[i] = serval_slide(vel_x[i], friction);
            if ((gx > 0 && (contact & MAP_CONTACT_RIGHT)) ||
                (gx < 0 && (contact & MAP_CONTACT_LEFT)))
                vel_y[i] = serval_slide(vel_y[i], friction);
        }
    }
}
