#ifndef SERVAL_PHYSICS_H
#define SERVAL_PHYSICS_H

// Bouncing bodies: gravity, plus bounces off the edges of the world. Suits
// balls, particles, debris and bunnies; it is not a platformer character
// controller (bodies don't collide with each other or with tilemaps).
//
// An entity with C_POS, C_VEL and C_BODY is a body. Each frame, after
// sys_movement() has moved it, sys_physics():
//   - bounces it off the world bounds, using its size (body_w x body_h);
//   - applies gravity to its velocity.
// A wall that gravity pulls toward is a floor. Bounces off a floor keep
// body_bounce/256 of the speed, and sliding along a floor loses
// body_friction/256 of the speed each frame; other walls bounce perfectly.
// A body too slow to bounce comes to rest on the floor (zero velocity) and
// stays put until gravity changes direction or the game moves it.

#include "serval/ecs.h"
#include "serval/fixed.h"
#include "serval/platform.h"

// Body component pools (C_BODY), indexed by entity_index(). Zeroed by
// entity_create(): a zero-sized body that doesn't bounce or slide.
extern u8 body_w[MAX_ENT], body_h[MAX_ENT]; // size in pixels, kept inside the bounds
extern u8 body_bounce[MAX_ENT];             // speed kept by a floor bounce, in 256ths (224 = 7/8)
extern u8 body_friction[MAX_ENT];           // speed lost per frame sliding along a floor, in 256ths

// Acceleration added to every body's velocity each frame, in pixels per frame
// per frame (FX_ONE / 4 is a quarter pixel). Zero (the default) turns gravity
// off.
void physics_set_gravity(FIXED x, FIXED y);

// The rectangle bodies stay inside, in pixels: left and top inclusive, right
// and bottom exclusive. Defaults to the whole screen.
void physics_set_bounds(int left, int top, int right, int bottom);

// Bounces bodies off the bounds and applies gravity and friction. Run once per
// frame, after sys_movement().
void sys_physics(void);

#endif // SERVAL_PHYSICS_H
