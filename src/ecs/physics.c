#include "serval/physics.h"
#include "serval/map.h"
#include "serval/screen.h"

#include "../core/warn.h"
#include "physics_internal.h"

u8 body_w[MAX_ENT], body_h[MAX_ENT];
u8 body_bounce[MAX_ENT];
u8 body_friction[MAX_ENT];
// Word-aligned so limit_loop() can skip two bodies without a limit at once.
u16 body_max_fall[MAX_ENT] __attribute__((aligned(4)));
// Word-aligned so sys_physics() can check four at once for a scaled gravity.
s8 body_gravity[MAX_ENT] __attribute__((aligned(4)));
// Shared with sys_map_movement (map.h). Word-aligned so sys_physics() can
// clear four at once.
u8 body_contact[MAX_ENT] __attribute__((aligned(4)));
bool serval_map_bodies_moved;

FIXED serval_gravity_x, serval_gravity_y;
static int bounds_left = 0, bounds_top = 0, bounds_right = SCREEN_W, bounds_bottom = SCREEN_H;
static u32 open_edges;
static bool wrap_x, wrap_y;
static bool contacts_on;

void physics_set_gravity(FIXED x, FIXED y) {
    serval_gravity_x = x;
    serval_gravity_y = y;
}

void physics_set_open_edges(u32 edges) {
    open_edges = edges;
}

void physics_set_wrap(bool x, bool y) {
    wrap_x = x;
    wrap_y = y;
}

void physics_set_bounds(int left, int top, int right, int bottom) {
    if (right < left || bottom < top) {
#ifdef SERVAL_DEBUG
        static bool warned;
        if (!warned) {
            warned = true;
            SERVAL_WARN("physics_set_bounds(%d, %d, %d, %d): right < left or bottom < top; "
                        "ignored",
                        left, top, right, bottom);
        }
#endif
        return;
    }
    bounds_left = left;
    bounds_top = top;
    bounds_right = right;
    bounds_bottom = bottom;
}

// A body bigger than the bounds (lo > hi): pins it to the left or top edge,
// at rest on that axis. Debug builds warn, out of line (in ROM, hence
// long_call from the IWRAM loop).
#ifdef SERVAL_DEBUG
#ifdef SERVAL_GBA
#define LONG_CALL __attribute__((long_call))
#else
#define LONG_CALL
#endif
static LONG_CALL __attribute__((noinline)) FIXED too_big(FIXED lo) {
    static bool warned;
    if (!warned) {
        warned = true;
        SERVAL_WARN("sys_physics: a body is bigger than the bounds; it is pinned to their "
                    "left/top edge");
    }
    return lo;
}
#else
static inline FIXED too_big(FIXED lo) {
    return lo;
}
#endif

// A body that reached a wall on one axis (moving into it, or resting
// against it): the wall it is at (lo if at_lo, else hi), and whether that is
// the floor (on_floor, the wall gravity pulls toward). Bounces it off the wall
// and applies gravity; returns on_floor.
//
// - A body that moved past a wall is mirrored back inside by the distance it
//   overshot (scaled like its speed on a floor), as if it had bounced
//   mid-frame. Snapping it onto the wall would lift it a little on every
//   bounce and keep it hopping forever.
// - Gravity is applied after the bounce, so it slows the rebound rather than
//   adding to it.
// - A floor bounce too slow to clear twice one frame's gravity becomes a rest:
//   the body sits exactly on the floor with zero speed, and the floor cancels
//   gravity, so it stays put even if gravity is switched off.
static inline __attribute__((always_inline)) bool hit_wall(FIXED* pos, FIXED* vel, FIXED lo,
                                                           FIXED hi, FIXED gravity, u32 bounce,
                                                           bool at_lo, bool on_floor) {
    // Only checked here: a body bigger than the bounds always touches a wall,
    // and the per-body loop stays free of the test.
    if (lo > hi) {
        *pos = too_big(lo);
        *vel = 0;
        return false;
    }
    FIXED wall = at_lo ? lo : hi;
    FIXED speed = serval_fx_abs(*vel);
    FIXED overshoot = wall - *pos; // how far past the wall, signed toward inside
    if (on_floor) {
        speed = (FIXED)(((u32)speed * bounce) >> 8);
        if (speed < 2 * serval_fx_abs(gravity)) {
            *pos = wall;
            *vel = 0;
            return true;
        }
        // Scale the magnitude, so both directions round the same way.
        FIXED scaled = (FIXED)(((u32)serval_fx_abs(overshoot) * bounce) >> 8);
        overshoot = overshoot < 0 ? -scaled : scaled;
    }
    *pos = wall + overshoot;
    if (*pos < lo)
        *pos = lo;
    if (*pos > hi)
        *pos = hi;
    *vel = (at_lo ? speed : -speed) + gravity;
    return on_floor;
}

// hit_wall() out of line, for the general loop: inlined, its copies made that
// loop 4.6 KB of IWRAM instead of 2.7 KB; out of line it costs a call per
// wall contact.
// at_lo and on_floor come in bits 8 and 9 of `bounce`.
static SERVAL_IWRAM_CODE bool hit_wall_call(FIXED* pos, FIXED* vel, FIXED lo, FIXED hi,
                                            FIXED gravity, u32 bounce) {
    return hit_wall(pos, vel, lo, hi, gravity, bounce & 0xFF, bounce & (1 << 8), bounce & (1 << 9));
}

// Moves one axis of a body at the ends of [lo, hi] (as hit_wall(), written
// out here for the fast loop, which is sensitive to how it is compiled), or
// applies that axis's gravity. Returns true if the body is on the wall gravity
// pulls toward (its floor), bouncing or resting. In the general loop
// (`general`, a constant), adds the side of the body that touched a wall
// (lo_side or hi_side) to its body_contact while contacts are on.
static inline __attribute__((always_inline)) bool update_axis(FIXED* pos, FIXED* vel, FIXED lo,
                                                              FIXED hi, FIXED gravity, u32 bounce,
                                                              u32 index, u32 lo_side, u32 hi_side,
                                                              bool general) {
    bool at_lo = *pos <= lo && *vel <= 0;
    bool at_hi = *pos >= hi && *vel >= 0;
    bool on_floor = (at_lo && gravity < 0) || (at_hi && gravity > 0);

    if (at_lo || at_hi) {
        if (general) {
            if (contacts_on)
                body_contact[index] |= (u8)(at_lo ? lo_side : hi_side);
            return hit_wall_call(pos, vel, lo, hi, gravity,
                                 bounce | (u32)at_lo << 8 | (u32)on_floor << 9);
        }
        if (lo > hi) {
            *pos = too_big(lo);
            *vel = 0;
            return false;
        }
        FIXED wall = at_lo ? lo : hi;
        FIXED speed = serval_fx_abs(*vel);
        FIXED overshoot = wall - *pos;
        if (on_floor) {
            speed = (FIXED)(((u32)speed * bounce) >> 8);
            if (speed < 2 * serval_fx_abs(gravity)) {
                *pos = wall;
                *vel = 0;
                return true;
            }
            FIXED scaled = (FIXED)(((u32)serval_fx_abs(overshoot) * bounce) >> 8);
            overshoot = overshoot < 0 ? -scaled : scaled;
        }
        *pos = wall + overshoot;
        if (*pos < lo)
            *pos = lo;
        if (*pos > hi)
            *pos = hi;
        *vel = at_lo ? speed : -speed;
    }

    *vel += gravity;
    return on_floor;
}

// Wraps one axis: a body entirely past `hi` (the far bound) or entirely before
// `lo` (it is `size` long) moves by the span of the bounds plus its size, so
// it re-enters from the other side. A body at exactly `hi` lands at exactly
// `lo - size`, which doesn't count as past `lo`: otherwise a stationary body
// there would jump back and forth every frame.
static inline void wrap_axis(FIXED* pos, FIXED* vel, FIXED lo, FIXED hi, FIXED size,
                             FIXED gravity) {
    FIXED span = hi - lo + size;
    if (*pos >= hi)
        *pos -= span;
    else if (*pos < lo - size)
        *pos += span;
    *vel += gravity;
}

// Functions kept out of IWRAM (in ROM on the GBA, so calls from IWRAM code
// must be long calls).
#ifdef SERVAL_GBA
#define ROM_CALL __attribute__((long_call, noinline))
#else
#define ROM_CALL __attribute__((noinline))
#endif

// Reads and writes byte pools a word (four entities) at a time.
typedef u32 __attribute__((may_alias)) Word;

// Bound used for open edges: far enough that no body reaches it.
#define OPEN_EDGE (FX(1) << 20)

// One body's step: bounces (or wraps) both axes, applies gravity gx, gy and
// friction. `general` is a constant: false in the fast loop, which handles
// neither wrapping nor contacts.
static inline __attribute__((always_inline)) void update_body(u32 i, FIXED gx, FIXED gy, FIXED left,
                                                              FIXED top, bool open_right,
                                                              bool open_bottom, bool general) {
    const FIXED right = open_right ? OPEN_EDGE : FX(bounds_right - body_w[i]);
    const FIXED bottom = open_bottom ? OPEN_EDGE : FX(bounds_bottom - body_h[i]);
    if (general && wrap_x)
        wrap_axis(&pos_x[i], &vel_x[i], FX(bounds_left), FX(bounds_right), FX(body_w[i]), gx);
    else if (update_axis(&pos_x[i], &vel_x[i], left, right, gx, body_bounce[i], i, BODY_SIDE_LEFT,
                         BODY_SIDE_RIGHT, general))
        vel_y[i] = serval_slide(vel_y[i], body_friction[i]);
    if (general && wrap_y)
        wrap_axis(&pos_y[i], &vel_y[i], FX(bounds_top), FX(bounds_bottom), FX(body_h[i]), gy);
    else if (update_axis(&pos_y[i], &vel_y[i], top, bottom, gy, body_bounce[i], i, BODY_SIDE_TOP,
                         BODY_SIDE_BOTTOM, general))
        vel_x[i] = serval_slide(vel_x[i], body_friction[i]);
}

#define LEFT_BOUND() ((open_edges & PHYSICS_EDGE_LEFT) ? -OPEN_EDGE : FX(bounds_left))
#define TOP_BOUND() ((open_edges & PHYSICS_EDGE_TOP) ? -OPEN_EDGE : FX(bounds_top))

// A body with a body_gravity, out of line and in ROM: scaling gravity per
// body inside the loops, which are out of registers, cost every body (~27
// cycles each in the general loop), and in IWRAM this function would take
// 1.3 KB. This way only the bodies that have one pay (about 300 cycles more
// than in the loop): games scale the gravity of a few bodies.
static ROM_CALL void update_scaled_body(u32 i) {
    const s8 scale = body_gravity[i];
    update_body(i, serval_body_gravity(serval_gravity_x, scale),
                serval_body_gravity(serval_gravity_y, scale), LEFT_BOUND(), TOP_BOUND(),
                open_edges & PHYSICS_EDGE_RIGHT, open_edges & PHYSICS_EDGE_BOTTOM, true);
}

// The physics loop. `general` is a constant at each call site, so the
// compiler builds two copies. The fast one (the usual case) handles bodies
// that don't wrap, without contacts or per-body gravity, and never tests a
// setting that can't change within the frame. The general one tests the wrap
// settings, reports contacts and, while some body has a body_gravity
// (`scaled`), hands those bodies to update_scaled_body().
static inline __attribute__((always_inline)) void physics_loop(bool general, bool scaled) {
    const FIXED left = LEFT_BOUND(), top = TOP_BOUND();
    const bool open_right = open_edges & PHYSICS_EDGE_RIGHT;
    const bool open_bottom = open_edges & PHYSICS_EDGE_BOTTOM;
    const FIXED gravity_x = serval_gravity_x, gravity_y = serval_gravity_y;
    // Map bodies (C_MAPBODY) move with sys_map_movement() instead.
    for (u32 i = 0; i < MAX_ENT; i++) {
        if ((ent_mask[i] & (C_ALIVE | C_POS | C_VEL | C_BODY | C_MAPBODY)) !=
            (C_ALIVE | C_POS | C_VEL | C_BODY))
            continue;
        if (general && scaled && body_gravity[i]) {
            update_scaled_body(i);
            continue;
        }
        update_body(i, gravity_x, gravity_y, left, top, open_right, open_bottom, general);
    }
}

// Clears body_contact for every entity that isn't a map body (whose contacts
// sys_map_movement() sets).
static ROM_CALL void clear_contacts_but_map_bodies(void) {
    for (u32 i = 0; i < MAX_ENT; i++) {
        if (!(ent_mask[i] & C_MAPBODY))
            body_contact[i] = 0;
    }
}

// Clears body_contact for the bodies sys_physics() reports on. Until map
// bodies have moved, that is every entity: four at a time, from IWRAM (a
// byte loop in ROM cost ~3,400 cycles, this ~60).
static inline __attribute__((always_inline)) void clear_contacts(void) {
    if (serval_map_bodies_moved) {
        clear_contacts_but_map_bodies();
        return;
    }
    Word* words = (Word*)body_contact;
#pragma GCC unroll 1
    for (u32 w = 0; w < MAX_ENT / 4; w += 4)
        words[w] = words[w + 1] = words[w + 2] = words[w + 3] = 0;
}

void physics_set_contacts(bool on) {
    if (contacts_on && !on)
        clear_contacts_but_map_bodies();
    contacts_on = on;
}

// Open edges: a body that ended up entirely outside the bounds past an open
// edge this frame gets BODY_CONTACT_EXIT and that edge's side. Runs before
// the physics loop, so position minus velocity is where the body was before
// sys_movement() moved it. Only games with open edges and contacts run it.
static ROM_CALL void report_exits(void) {
    const FIXED left = FX(bounds_left), right = FX(bounds_right);
    const FIXED top = FX(bounds_top), bottom = FX(bounds_bottom);
    for (u32 i = 0; i < MAX_ENT; i++) {
        if ((ent_mask[i] & (C_ALIVE | C_POS | C_VEL | C_BODY | C_MAPBODY)) !=
            (C_ALIVE | C_POS | C_VEL | C_BODY))
            continue;
        u32 exits = 0;
        if (!wrap_x) {
            FIXED x = pos_x[i], before = x - vel_x[i], w = FX(body_w[i]);
            if ((open_edges & PHYSICS_EDGE_LEFT) && x + w <= left && before + w > left)
                exits |= BODY_SIDE_LEFT;
            if ((open_edges & PHYSICS_EDGE_RIGHT) && x >= right && before < right)
                exits |= BODY_SIDE_RIGHT;
        }
        if (!wrap_y) {
            FIXED y = pos_y[i], before = y - vel_y[i], h = FX(body_h[i]);
            if ((open_edges & PHYSICS_EDGE_TOP) && y + h <= top && before + h > top)
                exits |= BODY_SIDE_TOP;
            if ((open_edges & PHYSICS_EDGE_BOTTOM) && y >= bottom && before < bottom)
                exits |= BODY_SIDE_BOTTOM;
        }
        if (exits)
            body_contact[i] = (u8)(exits | BODY_CONTACT_EXIT);
    }
}

// body_max_fall, after gravity: a separate pass rather than more work in
// physics_loop(), whose registers are all in use (adding it there cost
// bunnymark 2,900 cycles per frame). Most bodies have no limit, so the pass
// reads the limits sixteen at a time (eight u32 words; may_alias makes that
// legal) and skips sixteen zeros at once: about 300 cycles a frame in
// bunnymark (no limits), with the loop kept rolled to save IWRAM. Groups with limits are handled
// out of line, in ROM (hence long_call from IWRAM), to keep IWRAM small: games limit a few bodies.

static ROM_CALL void limit_group(u32 first) {
    const FIXED gravity_x = serval_gravity_x, gravity_y = serval_gravity_y;
    for (u32 i = first; i < first + 16; i++) {
        if (!body_max_fall[i] || (ent_mask[i] & (C_ALIVE | C_POS | C_VEL | C_BODY | C_MAPBODY)) !=
                                     (C_ALIVE | C_POS | C_VEL | C_BODY))
            continue;
        vel_x[i] = serval_limit_fall(vel_x[i], serval_body_gravity(gravity_x, body_gravity[i]),
                                     &body_max_fall[i]);
        vel_y[i] = serval_limit_fall(vel_y[i], serval_body_gravity(gravity_y, body_gravity[i]),
                                     &body_max_fall[i]);
    }
}

static inline __attribute__((always_inline)) void limit_loop(void) {
    const Word* words = (const Word*)body_max_fall;
#pragma GCC unroll 1
    for (u32 w = 0; w < MAX_ENT / 2; w += 8) {
        if (words[w] | words[w + 1] | words[w + 2] | words[w + 3] | words[w + 4] | words[w + 5] |
            words[w + 6] | words[w + 7])
            limit_group(w * 2);
    }
}

// True if any body_gravity is non-zero, reading sixteen per iteration. Out of
// line, like physics_general(), so it doesn't change how the fast loop in
// sys_physics() is compiled.
static SERVAL_IWRAM_CODE __attribute__((noinline)) bool any_gravity_scaled(void) {
    const Word* words = (const Word*)body_gravity;
    u32 any = 0;
#pragma GCC unroll 1
    for (u32 w = 0; w < MAX_ENT / 4; w += 4)
        any |= words[w] | words[w + 1] | words[w + 2] | words[w + 3];
    return any != 0;
}

// The general loop, out of line so that it doesn't change how the fast loop
// in sys_physics() is compiled.
static SERVAL_IWRAM_CODE __attribute__((noinline)) void physics_general(bool scaled) {
    physics_loop(true, scaled);
}

// Runs as ARM code from IWRAM on the GBA: it touches every body every frame.
// Per-body gravity only matters while there is gravity, so the scan for it is
// skipped without.
SERVAL_IWRAM_CODE void sys_physics(void) {
    if (contacts_on) {
        clear_contacts();
        if (open_edges)
            report_exits();
    }
    const bool gravity = serval_gravity_x || serval_gravity_y;
    const bool scaled = gravity && any_gravity_scaled();
    if (contacts_on || wrap_x || wrap_y || scaled)
        physics_general(scaled);
    else
        physics_loop(false, false);
    if (gravity)
        limit_loop();
}

// body_hit_side: everything is relative to b, so a fast pair or a pair moving
// together is judged by how a moved compared to b. Along each axis where a
// was clear of b before the frame (touching counts as clear, as in
// body_overlap), it closed the gap g at relative speed |d|, so it met b at
// time g / |d| into the frame; the side is that of the axis it met last (the
// one that made them overlap). Compared as g_x * |d_y| vs g_y * |d_x| (in 64
// bits: no division).
static FIXED vel_of(const FIXED* vel, u32 i) {
    return (ent_mask[i] & C_VEL) ? vel[i] : 0;
}

// How far to move a's position to compare it with b's: the camera when a is
// on the screen (SPRITE_SCREEN) and b in the world, minus it the other way
// around, otherwise nothing.
static void screen_to_world(u32 a, u32 b, FIXED* x, FIXED* y) {
    *x = *y = 0;
    u32 a_screen = spr_flags[a] & SPRITE_SCREEN, b_screen = spr_flags[b] & SPRITE_SCREEN;
    if (a_screen == b_screen)
        return;
    FIXED cx = FX(camera_x()), cy = FX(camera_y());
    *x = a_screen ? cx : -cx;
    *y = a_screen ? cy : -cy;
}

bool serval_body_overlap_mixed(u32 a, u32 b) {
    FIXED ox, oy;
    screen_to_world(a, b, &ox, &oy);
    FIXED ax = pos_x[a] + ox, ay = pos_y[a] + oy;
    return ax < pos_x[b] + FX(body_w[b]) && pos_x[b] < ax + FX(body_w[a]) &&
           ay < pos_y[b] + FX(body_h[b]) && pos_y[b] < ay + FX(body_h[a]);
}

u32 body_hit_side(u32 a, u32 b) {
    if (a >= MAX_ENT || b >= MAX_ENT || a == b || !body_overlap(a, b))
        return 0;
    FIXED ox, oy;
    screen_to_world(a, b, &ox, &oy);
    const FIXED aw = FX(body_w[a]), ah = FX(body_h[a]), bw = FX(body_w[b]), bh = FX(body_h[b]);
    // Relative motion this frame, and a's position relative to b before it.
    const FIXED dx = vel_of(vel_x, a) - vel_of(vel_x, b);
    const FIXED dy = vel_of(vel_y, a) - vel_of(vel_y, b);
    const FIXED rx = (pos_x[a] + ox - pos_x[b]) - dx, ry = (pos_y[a] + oy - pos_y[b]) - dy;

    // Gap a had to close along each axis (negative: they already overlapped).
    FIXED gap_x = -1, gap_y = -1;
    u32 side_x = 0, side_y = 0;
    if (rx + aw <= 0) {
        gap_x = -(rx + aw);
        side_x = BODY_SIDE_RIGHT; // a was left of b
    } else if (rx >= bw) {
        gap_x = rx - bw;
        side_x = BODY_SIDE_LEFT;
    }
    if (ry + ah <= 0) {
        gap_y = -(ry + ah);
        side_y = BODY_SIDE_BOTTOM; // a was above b
    } else if (ry >= bh) {
        gap_y = ry - bh;
        side_y = BODY_SIDE_TOP;
    }

    if (side_x && side_y) {
        // Clear on both axes: the later contact decides; a tie (an exact
        // corner) goes to top/bottom.
        int64_t tx = (int64_t)gap_x * serval_fx_abs(dy), ty = (int64_t)gap_y * serval_fx_abs(dx);
        return tx > ty ? side_x : side_y;
    }
    if (side_y)
        return side_y;
    if (side_x)
        return side_x;
    // They already overlapped before the frame (spawned or teleported inside,
    // moved together, or moved by the game rather than by velocity): no side
    // was crossed this frame, so none is guessed.
    return BODY_SIDE_INSIDE;
}
