#ifndef SERVAL_PATH_H
#define SERVAL_PATH_H

// Paths: movement patterns as data, for enemy formations, patrols, bosses and
// projectiles. See docs/runtime-systems.md#paths.
//
// A path is a list of steps, "turtle" style: for `frames` frames the entity
// moves along its heading at `speed` pixels per frame, while the heading turns
// by `turn` and the speed changes by `accel` every frame. A few numbers make
// the usual patterns:
//   - a straight line: one step, no turn
//   - a swoop or U-turn: in, then a step turning 180 degrees, then out
//   - a circle: one step that turns and never ends (frames 0)
//   - a weave (close to a sine wave): steps turning left and right, looping
//   - stop and go: a step slowing to 0 (accel), one at speed 0, one leaving
//
// sys_path() turns the path into velocity (vel_x, vel_y), which
// sys_movement() applies, so pathed entities collide and render like any
// other. A path can be mirrored, so one table serves formations entering from
// either side.
//
//     static const PathStep swoop_steps[] = {
//         {.frames = 40, .speed = FX(2)},                          // straight down
//         {.frames = 90, .speed = FX(2), .turn = ANGLE_DEG(2)},    // U-turn
//         {.speed = FX(3)},                                        // away, forever
//     };
//     static const Path swoop = {PATH_STEPS(swoop_steps), .heading = ANGLE_DEG(90)};
//
//     Entity e = entity_create(C_POS | C_VEL | C_SPR);
//     path_start(e, &swoop, from_right ? PATH_MIRROR_X : 0);

#include "serval/ecs.h"

// Entities following a path: with C_VEL, sys_path() sets their velocity. Add
// it only with path_start() (debug builds warn about, and remove, a C_PATH
// added by hand); sys_path() removes it when a path ends.
#define C_PATH (1u << 6)

// Caveat: write steps with designated initializers, as above. A positional
// initializer that leaves fields out, such as {40, 0, FX(2)}, triggers
// -Wmissing-field-initializers under -Wextra (an error with warnings as
// errors); positional ones must give all four fields.
typedef struct {
    u16 frames;  // how long the step lasts, in frames; 0: forever (the path never ends)
    s32 turn;    // heading change per frame, in u16 angle units, clockwise on screen
                 // (ANGLE_DEG(2); negative or ANGLE_DEG(-2) turns the other way)
    FIXED speed; // pixels per frame along the heading when the step starts; negative
                 // moves backward. 0 stands still, so give every moving step a speed.
    FIXED accel; // speed change per frame (0: constant speed)
} PathStep;

typedef struct {
    const PathStep* steps;
    u8 step_count; // 1 to 255; PATH_STEPS sets steps and step_count from an array
    bool loop;     // after the last step, go back to step loop_step (else the path ends)
    u8 loop_step;
    u16 heading; // starting heading: 0 = right, ANGLE_DEG(90) = down (math.h)
} Path;

// `.steps` and `.step_count` for an array of steps, in a Path initializer:
// {PATH_STEPS(swoop_steps), .heading = ANGLE_DEG(90)}. More than 255 steps is a
// compile error ("size of array is negative").
#define PATH_STEPS(array)                                                                          \
    .steps = (array),                                                                              \
    .step_count = (u8)(sizeof(array) / sizeof((array)[0]) +                                        \
                       0u * sizeof(char[sizeof(array) / sizeof((array)[0]) <= 255u ? 1 : -1]))

// path_start() flags: mirror the path left-right (PATH_MIRROR_X: a heading of
// "right" becomes "left") and/or top-bottom (PATH_MIRROR_Y). Either one also
// reverses the turns; both together rotate the path half a turn.
#define PATH_MIRROR_X 1u
#define PATH_MIRROR_Y 2u

// Per-entity path state, indexed by slot (entity_index(e), or i in
// ECS_FOR_EACH); set by path_start(), advanced by sys_path(). Games may read
// them, and may change path_heading and path_speed (the next sys_path() goes
// on from there): e.g. after path_start(), path_heading[i] =
// angle_of(dx, dy) aims the whole path at a target (later turns are
// relative). spr_angle[i] = path_heading[i] turns a sprite drawn facing right
// along its path.
extern u16 path_heading[MAX_ENT]; // the current heading (math.h angle)
extern FIXED path_speed[MAX_ENT]; // the current speed, pixels per frame
extern u8 path_step[MAX_ENT];     // the current step
extern u16 path_time[MAX_ENT];    // frames done in the current step

// Starts entity `e` on `path` (which must stay valid while it runs: keep paths
// const or static) from its first step, at the path's heading, mirrored by
// `flags`, and adds C_PATH. Replaces any path the entity was following. The
// entity needs C_VEL for sys_path() to move it (warns). Ignored with a warning
// for a dead entity, a NULL or empty path, or a loop_step past the end.
void path_start(Entity e, const Path* path, u32 flags);

// Stops the entity's path (removes C_PATH). Its velocity stays as it was.
void path_stop(Entity e);

// True while entity `e` is alive and following a path: false once a path
// without loop has done its last step, or after path_stop().
bool path_active(Entity e);

// Advances every entity with C_PATH | C_VEL one frame: turns its heading,
// changes its speed and sets vel_x, vel_y from them (only when either changed,
// so a straight, constant-speed stretch costs little; a velocity the game sets
// lasts until then). Run it once per frame before sys_movement(). When a path
// without loop finishes its last step, removes C_PATH; the entity keeps its
// last velocity (flies on straight). Speeds are capped at FX(4096).
void sys_path(void);

#endif // SERVAL_PATH_H
