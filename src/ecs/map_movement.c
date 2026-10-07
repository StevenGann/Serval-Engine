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
// velocity on that axis (rebound()); the rest of that axis's movement this
// frame is dropped.
//
// Only metatiles the leading edge newly enters block it, so a body already
// overlapping a solid metatile (spawned inside, or a cell changed under it)
// can move out of it. This is also what makes one-way platforms work: a body
// moving down enters a MAP_ONEWAY row only from above it, while a body that
// jumped into one from below is already inside its row and falls through.

#include "serval/map.h"
#include "serval/physics.h"

#include "../core/map_internal.h"
#include "../core/warn.h"
#include "physics_internal.h"

#define MAX_STEP FX(7)

static const MapLayer* playfield;

// How bodies treat each collision type (MAP_TYPE) in this version: the three
// it implements as themselves; the planned slopes as MAP_SOLID (a whole
// metatile) and the planned ladder as MAP_EMPTY, until they are implemented;
// the reserved types 10-15 as MAP_EMPTY. map_load() warns about all but the
// first three (src/core/map.c).
static const u8 movement_type[16] = {
    [MAP_EMPTY] = MAP_EMPTY,
    [MAP_SOLID] = MAP_SOLID,
    [MAP_ONEWAY] = MAP_ONEWAY,
    [MAP_LADDER] = MAP_EMPTY,
    [MAP_SLOPE_R] = MAP_SOLID,
    [MAP_SLOPE_L] = MAP_SOLID,
    [MAP_SLOPE_R_LOW] = MAP_SOLID,
    [MAP_SLOPE_R_HIGH] = MAP_SOLID,
    [MAP_SLOPE_L_HIGH] = MAP_SOLID,
    [MAP_SLOPE_L_LOW] = MAP_SOLID,
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
    u32 cell = serval_map_cell_in(playfield, (u32)mx, (u32)my);
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
// keeping body_bounce/256 of the speed (0, the default, stops it), or all of
// it for 255: a u8 can't hold 256, so its largest value means a perfect
// bounce (as in sys_physics()); otherwise even the bounciest body would lose
// a little height on every bounce. On a floor (the side `gravity` pulls
// toward on this axis) a rebound too slow to clear twice one frame's gravity
// is a rest instead, as in sys_physics(), whatever the bounce: a body
// standing on a floor, which gravity pulls into it every frame, stays there
// with zero speed rather than hopping a fraction of a pixel forever.
static FIXED rebound(FIXED vel, u32 bounce, FIXED gravity) {
    FIXED speed = serval_fx_abs(vel);
    if (bounce != 255)
        speed = (FIXED)(((u32)speed * bounce) >> 8);
    bool floor = (gravity > 0 && vel > 0) || (gravity < 0 && vel < 0);
    if (floor && speed < 2 * serval_fx_abs(gravity))
        return 0;
    return vel > 0 ? -speed : speed;
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
                pos_x[i] = FX(to * 16 - (int)w);
                vel_x[i] = rebound(vel_x[i], body_bounce[i], gravity);
                return MAP_CONTACT_RIGHT;
            }
        } else {
            int from = first_pixel(pos_x[i]) >> 4, to = first_pixel(x) >> 4;
            if (to != from && column_blocked(to, top, bottom)) {
                pos_x[i] = FX(to * 16 + 16);
                vel_x[i] = rebound(vel_x[i], body_bounce[i], gravity);
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
                pos_y[i] = FX(to * 16 - (int)h);
                vel_y[i] = rebound(vel_y[i], body_bounce[i], gravity);
                return MAP_CONTACT_FLOOR;
            }
        } else {
            int from = first_pixel(pos_y[i]) >> 4, to = first_pixel(y) >> 4;
            if (to != from && row_blocked(to, left_px, right_px, false)) {
                pos_y[i] = FX(to * 16 + 16);
                vel_y[i] = rebound(vel_y[i], body_bounce[i], gravity);
                return MAP_CONTACT_CEILING;
            }
        }
        pos_y[i] = y;
    }
    return 0;
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
    serval_map_bodies_moved = false;
    ECS_FOR_EACH(i, C_MAPBODY) {
        serval_map_bodies_moved = true;
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
        u32 contact = move_x(i, w, h, gx);
        contact |= move_y(i, w, h, gy);
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
