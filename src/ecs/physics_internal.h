#ifndef SERVAL_ECS_PHYSICS_INTERNAL_H
#define SERVAL_ECS_PHYSICS_INTERNAL_H

// Engine-internal: physics settings and helpers shared with sys_map_movement
// (src/ecs/map_movement.c). Not part of the public API.

#include "serval/fixed.h"
#include "serval/platform.h"

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
