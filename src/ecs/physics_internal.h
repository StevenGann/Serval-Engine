#ifndef SERVAL_ECS_PHYSICS_INTERNAL_H
#define SERVAL_ECS_PHYSICS_INTERNAL_H

// Engine-internal: physics settings and helpers shared with sys_map_movement
// (src/ecs/map_movement.c). Not part of the public API.

#include "serval/fixed.h"
#include "serval/platform.h"

// 1 in unoptimized GBA builds (-O0: CMake's Debug configuration, as in the
// gba-debug preset or a game's own Debug build), 0 otherwise. GCC still
// inlines always_inline functions at -O0 but folds no constants into the
// copies, so every copy keeps all of its branches: the specialized copies
// that make sys_physics() fast when optimized only filled IWRAM (physics.c's
// IWRAM code was 13,644 bytes at -O0, 6,288 at -O3). These builds call shared
// code instead: physics.c's fast loop bounces through hit_wall_call(), and
// serval_perfect_rebound() is out of line in ROM, which brings physics.c's
// IWRAM code to 6,128 bytes (docs/development.md#debug-builds). Host builds,
// also -O0, keep the optimized builds' structure, so the unit tests run it.
#if defined(SERVAL_GBA) && !defined(__OPTIMIZE__)
#define SERVAL_PHYSICS_UNOPTIMIZED 1
#else
#define SERVAL_PHYSICS_UNOPTIMIZED 0
#endif

// physics_set_gravity()'s acceleration, in pixels per frame per frame.
extern FIXED serval_gravity_x, serval_gravity_y;

// A body's own gravity on one axis: `gravity` scaled by its body_gravity
// (the scale in 16ths, minus 16; 0 is the common case and costs one test).
// Rounds the magnitude down, so reversing a scale only flips the sign.
static inline FIXED serval_body_gravity(FIXED gravity, s8 scale) {
    if (!scale)
        return gravity;
    FIXED scaled = gravity * (16 + scale);
    return scaled < 0 ? -(-scaled >> 4) : scaled >> 4;
}

// Whether the last sys_map_movement() moved any map bodies (since the last
// ecs_reset()): sys_physics() must then leave their body_contact alone.
extern bool serval_map_bodies_moved;

// Sliding stops below this speed (a sixteenth of a pixel per frame).
#define SERVAL_STOP_SPEED (FX_ONE / 16)

static inline FIXED serval_fx_abs(FIXED v) {
    return v < 0 ? -v : v;
}

// The body_bounce of a perfect bounce, which loses nothing: every other value
// keeps body_bounce/256 of the speed. A u8 can't hold 256, and without this
// one exception a bounce could keep at most 255/256, so a ball meant to bounce
// for ever would slowly die down. sys_physics() tests for it only on a floor
// bounce fast enough not to be a rest (a resting body never reaches the test).
#define SERVAL_BOUNCE_PERFECT 255

// The rebound speed of a perfect floor bounce (body_bounce
// SERVAL_BOUNCE_PERFECT), shared by sys_physics() and sys_map_movement(): the
// one that brings the body back up exactly as high as it fell from. The body
// hit the floor at `speed` (at least 2 * g) and `lost` (>= 0) is what the
// floor took from its fall in this integration's terms (below); g is
// gravity's magnitude along the axis (> 0). Returns u + g, with u the largest
// of g to `speed` (rounded to the nearer) for which
//     u * u + g * u <= speed * speed + g * speed - 2 * g * lost,
// or 0 when even u = g is too much (the caller then makes it a rest).
//
// Each system's integration keeps a quantity the same from frame to frame
// while a body falls freely, which is its height in that integration; the
// bounce returns the speed that keeps it.
//   - sys_physics() moves first and adds gravity after (sys_movement(), then
//     sys_physics()): v * v - g * v - 2 * g * x is kept (v the velocity after
//     gravity, x the position, both along gravity). It mirrors the overshoot
//     `past` back inside the floor, so lost = 2 * past, and the result is the
//     speed it leaves with before this frame's gravity (physics.c).
//   - sys_map_movement() adds gravity first and moves after: v * v + g * v -
//     2 * g * x is kept (v the velocity the body moved at). It stops the body
//     flush against the floor, `past` short of where the frame's movement
//     would have taken it, so lost = past, and the result is the rebound
//     velocity's magnitude, to which the next frame adds gravity
//     (map_movement.c).
// Found by bisection (no divide): one step per bit of the speed, about a
// dozen, on the bounce frame only. always_inline: in sys_physics' fast loop a
// call cost bunnymark 2,600 cycles a frame (physics.c). Unoptimized GBA builds
// (SERVAL_PHYSICS_UNOPTIMIZED) call it instead, in ROM (a long call from
// IWRAM): GCC inlines nothing but always_inline functions at -O0, and inlined
// there, its 64-bit arithmetic took 1,012 bytes of IWRAM.
#if SERVAL_PHYSICS_UNOPTIMIZED
#define SERVAL_REBOUND_INLINE __attribute__((long_call))
#else
#define SERVAL_REBOUND_INLINE __attribute__((always_inline))
#endif
static inline SERVAL_REBOUND_INLINE FIXED serval_perfect_rebound(FIXED speed, FIXED lost, FIXED g) {
    const int64_t target = (int64_t)speed * (speed + g) - (int64_t)2 * g * lost;
    if (target < (int64_t)2 * g * g)
        return 0;
    // The largest u in [g, speed] with u * (u + g) <= target: for g it is at
    // most target (checked above), for speed at least (lost >= 0).
    FIXED lo = g, hi = speed;
    while (lo < hi) {
        FIXED mid = lo + ((hi - lo + 1) >> 1);
        if ((int64_t)mid * (mid + g) <= target)
            lo = mid;
        else
            hi = mid - 1;
    }
    if (lo < speed && (int64_t)(lo + 1) * (lo + 1 + g) - target < target - (int64_t)lo * (lo + g))
        lo++;
    return lo + g;
}

// Takes friction/256 of the speed away, rounding the loss up on the
// magnitude: any friction slows a body in either direction until it stops
// (rounding toward zero would let slow bodies creep forever).
static inline FIXED serval_slide(FIXED speed, u32 friction) {
    FIXED loss = (FIXED)(((u32)serval_fx_abs(speed) * friction + 255) >> 8);
    speed = speed < 0 ? speed + loss : speed - loss;
    return serval_fx_abs(speed) < SERVAL_STOP_SPEED ? 0 : speed;
}

// body_max_fall: limits the velocity on an axis to max_fall (24.8 fixed point,
// as a u16) pixels per frame
// in the direction gravity pulls along it. Nothing changes on an axis without
// gravity or for max_fall 0 (no limit). Checks gravity first: it is the same
// for every body, so bodies on an axis without gravity skip the lookup.
static inline FIXED serval_limit_fall(FIXED vel, FIXED gravity, const u16* max_fall) {
    if (gravity > 0) {
        FIXED max = (FIXED)*max_fall;
        if (max && vel > max)
            return max;
    } else if (gravity < 0) {
        FIXED max = (FIXED)*max_fall;
        if (max && vel < -max)
            return -max;
    }
    return vel;
}

#endif // SERVAL_ECS_PHYSICS_INTERNAL_H
