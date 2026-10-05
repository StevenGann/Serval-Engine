#ifndef SERVAL_PHYSICS_H
#define SERVAL_PHYSICS_H

// Bouncing bodies: gravity, plus bounces off the edges of the world. Suits
// balls, particles, debris and bunnies; it is not a platformer character
// controller (bodies don't collide with each other or with tilemaps). For
// characters that walk on a tilemap, see map bodies (C_MAPBODY and
// sys_map_movement() in map.h), which sys_physics() skips.
//
// An entity with C_POS, C_VEL and C_BODY is a body. Each frame, after
// sys_movement() has moved it, sys_physics():
//   - bounces it off the world bounds, using its size (body_w x body_h);
//   - applies gravity to its velocity, then limits its fall speed
//     (body_max_fall).
// A wall that gravity pulls toward is a floor. Bounces off a floor keep
// body_bounce/256 of the speed, and a body touching a floor loses
// body_friction/256 of its speed along it each frame (rounded up, so any
// non-zero friction eventually stops it); other walls bounce perfectly. A
// body too slow to bounce comes to rest on the floor (zero velocity) and stays
// put until gravity changes direction or the game moves it.
//
// Each body can scale gravity (body_gravity), and sys_physics() can report the
// walls bodies touch and the open edges they leave through (body_contact,
// physics_set_contacts()).

#include "serval/ecs.h"
#include "serval/fixed.h"
#include "serval/platform.h"

// Body component pools (C_BODY), indexed by entity_index(). Zeroed by
// entity_create(): a zero-sized body that doesn't bounce or slide.
extern u8 body_w[MAX_ENT], body_h[MAX_ENT]; // size in pixels, kept inside the bounds
// Speed kept by a floor bounce, in 256ths (224 = 7/8). At most 255/256: a u8
// can't express 256, so a floor bounce always loses a little speed.
extern u8 body_bounce[MAX_ENT];
// Speed lost per frame sliding along a floor, in 256ths (0 = no friction).
extern u8 body_friction[MAX_ENT];
// Maximum fall speed in pixels per frame (0 = no limit): after gravity is
// added, the velocity in the direction gravity pulls is limited to this, on
// each axis gravity acts on. Speeds the game sets beyond it (a jump against
// gravity, a dive) are kept until gravity is next applied.
extern u8 body_max_fall[MAX_ENT];
// How strongly gravity pulls this body, as a scale in 16ths written with
// BODY_GRAVITY(): BODY_GRAVITY(16) is normal gravity, BODY_GRAVITY(8) half,
// BODY_GRAVITY(0) none (a ball that flies straight while power-ups fall) and
// BODY_GRAVITY(-16) reversed. The pool stores the scale minus 16, so the zero
// entity_create() leaves is normal gravity. Scales from -112 to 143.
// Floors follow the body's own gravity: a body without gravity bounces
// perfectly off every wall, one with reversed gravity rests on the ceiling.
// sys_physics() and sys_map_movement() apply it. While every body has normal
// gravity, sys_physics() costs the same (checking costs ~110 cycles a frame,
// only while there is gravity); otherwise it takes its slower general loop and
// handles the scaled bodies out of line, in ROM: 32 bodies, 4 of them scaled,
// cost about 4,000 cycles more per frame than with none. Scale the gravity of
// the few odd bodies, not of the many.
extern s8 body_gravity[MAX_ENT];
#define BODY_GRAVITY(sixteenths) ((s8)((sixteenths) - 16))
// Map bodies (C_MAPBODY, map.h) use the same four to bounce off, slide along
// and fall onto the map; see sys_map_movement().

// Acceleration added to every body's velocity each frame, in pixels per frame
// per frame (FX_ONE / 4 is a quarter pixel). Zero (the default) turns gravity
// off.
void physics_set_gravity(FIXED x, FIXED y);

// The rectangle bodies stay inside, in world pixels (the coordinates of
// pos_x/pos_y, not of the screen): left and top inclusive, right and bottom
// exclusive. Defaults to (0, 0, SCREEN_W, SCREEN_H), which is the screen only
// while the camera (map.h) stays at 0, 0. In a scrolling world, set them to
// the area bodies may use, e.g. the whole map of a level:
//
//     physics_set_bounds(0, 0, level.width * 16, level.height * 16);
//
// Nothing else changes them (map_load() and camera_set() leave them alone).
// Ignored (warning in debug builds) if right < left or bottom < top. A body
// bigger than the bounds is pinned to their left or top edge.
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

// Sides of a body, for body_hit_side(). The same bits as map.h's
// MAP_CONTACT_FLOOR, _CEILING, _LEFT and _RIGHT.
#define BODY_SIDE_BOTTOM (1 << 0)
#define BODY_SIDE_TOP (1 << 1)
#define BODY_SIDE_LEFT (1 << 2)
#define BODY_SIDE_RIGHT (1 << 3)
// From body_hit_side(): the bodies overlap but already did before this
// frame's movement, so no side was crossed this frame.
#define BODY_SIDE_INSIDE (1 << 4)

// Which side of body a met body b this frame: 0 if they don't overlap
// (body_overlap), otherwise exactly one BODY_SIDE_*, e.g. BODY_SIDE_BOTTOM when
// a came down onto b (a stomp) and BODY_SIDE_LEFT when a ran into b's right
// side. Judged from where they were before this frame's movement (position
// minus velocity), using their motion relative to each other, so it is right
// for fast bodies that moved deep into each other in one frame, and for two
// moving bodies. The side is the one a crossed last to overlap b; an exact
// corner hit counts as top or bottom.
//
// BODY_SIDE_INSIDE: they overlapped before the frame too (a spawned inside b,
// they move together, or one moved into the other on an earlier frame and
// stayed). No side is guessed: a ball that slipped past a paddle's face
// doesn't later count as hitting it. Treat it as "still touching", e.g. hurt
// the player but don't count a stomp; any non-zero result means overlap.
//
// An entity without C_VEL counts as still: its position now is also its
// position before the frame. A body the game moves by setting pos_x/pos_y
// (a paddle, a moving platform) is judged as if it had always been where it
// is now; for its own motion to count, give it C_VEL and let sys_movement()
// move it. Call it after the movement systems, before changing velocities: a
// bounce that reversed a velocity this frame (sys_physics, sys_map_movement)
// or a position the game set directly makes "position minus velocity" a
// guess. Takes slot indices; 0 if a == b.
u32 body_hit_side(u32 a, u32 b);

// Which walls of the bounds each body touched in the last sys_physics(), as
// BODY_SIDE_* bits for the side of the body that touched: BODY_SIDE_BOTTOM
// for the bottom bound, BODY_SIDE_LEFT for the left one, and so on. A body
// touches a wall on the frame it bounces off it, and on every frame it rests
// against it (on a floor, or at a wall with no speed away from it). The pool
// map bodies use (map.h, whose MAP_CONTACT_* are the same bits); read it, don't
// write it. Use it for wall sounds and bounces instead of comparing velocity
// signs: `if (body_contact[ball] & (BODY_SIDE_TOP | BODY_SIDE_BOTTOM))`.
// A wrapping axis has no walls, so it reports nothing.
extern u8 body_contact[MAX_ENT];
// In body_contact, with the side bit of the open edge (physics_set_open_edges)
// the body left through: set on the frame the body ends up entirely outside
// the bounds past that edge, e.g. BODY_CONTACT_EXIT | BODY_SIDE_LEFT for a ball
// that left through the open left edge.
#define BODY_CONTACT_EXIT (1 << 5)

// Makes sys_physics() report contacts in body_contact (true) or not (false,
// the default, which leaves body_contact 0 for bouncing bodies). Contacts take
// sys_physics' slower general loop, about 80 cycles more per body per frame;
// switching them off clears them.
void physics_set_contacts(bool on);

// Bounces bodies off the bounds and applies gravity, maximum fall speed and
// friction, and with physics_set_contacts(true) sets body_contact. Run once
// per frame, after sys_movement().
void sys_physics(void);

#endif // SERVAL_PHYSICS_H
