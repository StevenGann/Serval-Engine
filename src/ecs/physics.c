#include "serval/physics.h"
#include "serval/map.h"
#include "serval/screen.h"

#include "../core/warn.h"
#include "physics_internal.h"

u8 body_w[MAX_ENT], body_h[MAX_ENT];
u8 body_bounce[MAX_ENT];
u8 body_friction[MAX_ENT];
// Word-aligned so limit_loop() can skip four bodies without a limit at once.
u8 body_max_fall[MAX_ENT] __attribute__((aligned(4)));

FIXED serval_gravity_x, serval_gravity_y;
static int bounds_left = 0, bounds_top = 0, bounds_right = SCREEN_W, bounds_bottom = SCREEN_H;
static u32 open_edges;
static bool wrap_x, wrap_y;

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

// Bounces one axis of a body off the ends of [lo, hi], then applies that
// axis's gravity. Returns true if the body is on the wall gravity pulls toward
// (its floor), bouncing or resting.
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
static inline bool update_axis(FIXED* pos, FIXED* vel, FIXED lo, FIXED hi, FIXED gravity,
                               u32 bounce) {
    bool at_lo = *pos <= lo && *vel <= 0;
    bool at_hi = *pos >= hi && *vel >= 0;
    bool on_floor = (at_lo && gravity < 0) || (at_hi && gravity > 0);

    if (at_lo || at_hi) {
        // Only checked here: a body bigger than the bounds always touches a
        // wall, and the per-body loop stays free of the test.
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

// Bound used for open edges: far enough that no body reaches it.
#define OPEN_EDGE (FX(1) << 20)

// The physics loop. `wrapping` is a constant at each call site, so the
// compiler builds one copy without any wrap checks (the usual case) and one
// with them, and the per-body loop never tests a setting that can't change
// within the frame.
static inline __attribute__((always_inline)) void physics_loop(bool wrapping) {
    const FIXED left = (open_edges & PHYSICS_EDGE_LEFT) ? -OPEN_EDGE : FX(bounds_left);
    const FIXED top = (open_edges & PHYSICS_EDGE_TOP) ? -OPEN_EDGE : FX(bounds_top);
    const bool open_right = open_edges & PHYSICS_EDGE_RIGHT;
    const bool open_bottom = open_edges & PHYSICS_EDGE_BOTTOM;
    const FIXED gravity_x = serval_gravity_x, gravity_y = serval_gravity_y;
    // Map bodies (C_MAPBODY) move with sys_map_movement() instead.
    for (u32 i = 0; i < MAX_ENT; i++) {
        if ((ent_mask[i] & (C_ALIVE | C_POS | C_VEL | C_BODY | C_MAPBODY)) !=
            (C_ALIVE | C_POS | C_VEL | C_BODY))
            continue;
        const FIXED right = open_right ? OPEN_EDGE : FX(bounds_right - body_w[i]);
        const FIXED bottom = open_bottom ? OPEN_EDGE : FX(bounds_bottom - body_h[i]);
        if (wrapping && wrap_x)
            wrap_axis(&pos_x[i], &vel_x[i], FX(bounds_left), FX(bounds_right), FX(body_w[i]),
                      gravity_x);
        else if (update_axis(&pos_x[i], &vel_x[i], left, right, gravity_x, body_bounce[i]))
            vel_y[i] = serval_slide(vel_y[i], body_friction[i]);
        if (wrapping && wrap_y)
            wrap_axis(&pos_y[i], &vel_y[i], FX(bounds_top), FX(bounds_bottom), FX(body_h[i]),
                      gravity_y);
        else if (update_axis(&pos_y[i], &vel_y[i], top, bottom, gravity_y, body_bounce[i]))
            vel_x[i] = serval_slide(vel_x[i], body_friction[i]);
    }
}

// body_max_fall, after gravity: a separate pass rather than more work in
// physics_loop(), whose registers are all in use (adding it there cost
// bunnymark 2,900 cycles per frame). Most bodies have no limit, so the pass
// reads the limits sixteen at a time (four u32 words; may_alias makes that
// legal) and skips sixteen zeros at once: about 200 cycles a frame in
// bunnymark (no limits), with the loop kept rolled to save IWRAM. Groups with limits are handled
// out of line, in ROM (hence long_call from IWRAM), to keep IWRAM small: games limit a few bodies.
#ifdef SERVAL_GBA
#define ROM_CALL __attribute__((long_call, noinline))
#else
#define ROM_CALL __attribute__((noinline))
#endif

static ROM_CALL void limit_group(u32 first) {
    const FIXED gravity_x = serval_gravity_x, gravity_y = serval_gravity_y;
    for (u32 i = first; i < first + 16; i++) {
        if (!body_max_fall[i] || (ent_mask[i] & (C_ALIVE | C_POS | C_VEL | C_BODY | C_MAPBODY)) !=
                                     (C_ALIVE | C_POS | C_VEL | C_BODY))
            continue;
        vel_x[i] = serval_limit_fall(vel_x[i], gravity_x, &body_max_fall[i]);
        vel_y[i] = serval_limit_fall(vel_y[i], gravity_y, &body_max_fall[i]);
    }
}

typedef u32 __attribute__((may_alias)) LimitWord;

static inline __attribute__((always_inline)) void limit_loop(void) {
    const LimitWord* words = (const LimitWord*)body_max_fall;
#pragma GCC unroll 1
    for (u32 w = 0; w < MAX_ENT / 4; w += 4) {
        if (words[w] | words[w + 1] | words[w + 2] | words[w + 3])
            limit_group(w * 4);
    }
}

// Runs as ARM code from IWRAM on the GBA: it touches every body every frame.
SERVAL_IWRAM_CODE void sys_physics(void) {
    if (wrap_x || wrap_y)
        physics_loop(true);
    else
        physics_loop(false);
    if (serval_gravity_x || serval_gravity_y)
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

// Penetration of a into b along one axis, and whether a's far side ("max",
// right or bottom) is the one inside b.
static FIXED penetration(FIXED a, FIXED a_size, FIXED b, FIXED b_size, bool* max_side) {
    FIXED from_min = a + a_size - b; // a's right/bottom side past b's left/top
    FIXED from_max = b + b_size - a; // a's left/top side past b's right/bottom
    *max_side = from_min < from_max || (from_min == from_max && a < b);
    return *max_side ? from_min : from_max;
}

u32 body_hit_side(u32 a, u32 b) {
    if (a >= MAX_ENT || b >= MAX_ENT || a == b || !body_overlap(a, b))
        return 0;
    const FIXED aw = FX(body_w[a]), ah = FX(body_h[a]), bw = FX(body_w[b]), bh = FX(body_h[b]);
    // Relative motion this frame, and a's position relative to b before it.
    const FIXED dx = vel_of(vel_x, a) - vel_of(vel_x, b);
    const FIXED dy = vel_of(vel_y, a) - vel_of(vel_y, b);
    const FIXED rx = (pos_x[a] - pos_x[b]) - dx, ry = (pos_y[a] - pos_y[b]) - dy;

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

    // Already overlapping before the frame (moved together, spawned inside,
    // or moved by the game rather than by velocity): the side of least
    // penetration now, top/bottom on a tie.
    bool max_x, max_y;
    FIXED px = penetration(pos_x[a], aw, pos_x[b], bw, &max_x);
    FIXED py = penetration(pos_y[a], ah, pos_y[b], bh, &max_y);
    if (px < py)
        return max_x ? BODY_SIDE_RIGHT : BODY_SIDE_LEFT;
    return max_y ? BODY_SIDE_BOTTOM : BODY_SIDE_TOP;
}
