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

// Edges of the bounds, for physics_set_open_edges().
#define PHYSICS_EDGE_LEFT (1 << 0)
#define PHYSICS_EDGE_RIGHT (1 << 1)
#define PHYSICS_EDGE_TOP (1 << 2)
#define PHYSICS_EDGE_BOTTOM (1 << 3)

// Lets bodies pass through the given edges of the bounds instead of bouncing,
// e.g. PHYSICS_EDGE_LEFT | PHYSICS_EDGE_RIGHT for a ball that scores by leaving
// the screen sideways. The game decides what happens once a body is out.
// Defaults to 0: every edge bounces.
void physics_set_open_edges(u32 edges);

// Makes bodies wrap around horizontally (x) and/or vertically (y) instead of
// bouncing: a body that has completely left one edge of the bounds reappears
// just outside the opposite edge and slides back in, as in Asteroids. A
// wrapping axis has no floor. Defaults to no wrapping.
void physics_set_wrap(bool x, bool y);

// True if the rectangles of two bodies overlap (position plus body_w x
// body_h; touching edges don't count). Takes entity slot indices, as from
// entity_index() or ECS_FOR_EACH. Works for any entities with C_POS, so a
// body without C_VEL makes a static collider, like a paddle or a wall the
// game moves itself.
static inline bool body_overlap(u32 a, u32 b) {
    return pos_x[a] < pos_x[b] + FX(body_w[b]) && pos_x[b] < pos_x[a] + FX(body_w[a]) &&
           pos_y[a] < pos_y[b] + FX(body_h[b]) && pos_y[b] < pos_y[a] + FX(body_h[a]);
}

// Bounces bodies off the bounds and applies gravity and friction. Run once per
// frame, after sys_movement().
void sys_physics(void);

#endif // SERVAL_PHYSICS_H
