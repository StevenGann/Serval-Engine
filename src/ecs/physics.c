#include "serval/physics.h"
#include "serval/screen.h"

u8 body_w[MAX_ENT], body_h[MAX_ENT];
u8 body_bounce[MAX_ENT];
u8 body_friction[MAX_ENT];

static FIXED gravity_x, gravity_y;
static int bounds_left = 0, bounds_top = 0, bounds_right = SCREEN_W, bounds_bottom = SCREEN_H;

// Sliding stops below this speed (a sixteenth of a pixel per frame).
#define STOP_SPEED (FX_ONE / 16)

void physics_set_gravity(FIXED x, FIXED y) {
    gravity_x = x;
    gravity_y = y;
}

void physics_set_bounds(int left, int top, int right, int bottom) {
    bounds_left = left;
    bounds_top = top;
    bounds_right = right;
    bounds_bottom = bottom;
}

static inline FIXED fx_abs(FIXED v) {
    return v < 0 ? -v : v;
}

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
        FIXED wall = at_lo ? lo : hi;
        FIXED speed = fx_abs(*vel);
        FIXED overshoot = wall - *pos; // how far past the wall, signed toward inside
        if (on_floor) {
            speed = (FIXED)(((u32)speed * bounce) >> 8);
            if (speed < 2 * fx_abs(gravity)) {
                *pos = wall;
                *vel = 0;
                return true;
            }
            overshoot = (FIXED)(((s32)overshoot * (s32)bounce) >> 8);
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

static inline FIXED slide(FIXED speed, u32 friction) {
    speed -= (FIXED)(((s32)speed * (s32)friction) >> 8);
    return fx_abs(speed) < STOP_SPEED ? 0 : speed;
}

// Runs as ARM code from IWRAM on the GBA: it touches every body every frame.
SERVAL_IWRAM_CODE void sys_physics(void) {
    const FIXED left = FX(bounds_left), top = FX(bounds_top);
    ECS_FOR_EACH(i, C_POS | C_VEL | C_BODY) {
        const FIXED right = FX(bounds_right - body_w[i]);
        const FIXED bottom = FX(bounds_bottom - body_h[i]);
        if (update_axis(&pos_x[i], &vel_x[i], left, right, gravity_x, body_bounce[i]))
            vel_y[i] = slide(vel_y[i], body_friction[i]);
        if (update_axis(&pos_y[i], &vel_y[i], top, bottom, gravity_y, body_bounce[i]))
            vel_x[i] = slide(vel_x[i], body_friction[i]);
    }
}
