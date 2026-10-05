// Movement patterns for enemies: a small path helper written for this example.
//
// The engine has no path or pattern API (yet), so this file is the
// workaround, kept apart so it could become one. A path is a list of steps,
// each saying for how many frames the entity flies at what speed while its
// heading turns by how much per frame ("turtle" steering). That covers the
// patterns of the genre with a few numbers each:
//   - a straight line: one step, no turn
//   - a swoop or U-turn: straight in, then a step turning 180 degrees
//   - a weave (close to a sine wave): steps turning left and right in turn,
//     looping
//   - hover and leave: a step at speed 0, then one at a negative speed
// A path can be mirrored (left-right), so one table serves formations
// entering from either side.

#ifndef SHMUP_PATH_H
#define SHMUP_PATH_H

#include "serval/serval.h"

typedef struct {
    u16 frames;  // how long the step lasts
    s32 turn;    // heading change per frame, in u16 angle units (ANGLE_DEG(2)), clockwise
    FIXED speed; // pixels per frame along the heading (negative: backward)
} PathStep;

typedef struct {
    u16 heading; // the starting heading: ANGLE_DEG(90) is straight down
    u8 step_count;
    u8 loop; // the step to go back to after the last one; PATH_NO_LOOP: keep the last step's motion
    const PathStep* steps;
} Path;

#define PATH_NO_LOOP 0xFF

// Starts entity slot i (with C_VEL) on a path; mirror flips it left-right.
void path_start(u32 i, const Path* path, bool mirror);
// Advances every entity with C_PATH one frame: sets vel_x and vel_y, which
// sys_movement() then applies. Call once per frame, before sys_movement().
void path_update_all(void);
// True once entity slot i has finished a path that doesn't loop.
bool path_finished(u32 i);

#endif // SHMUP_PATH_H
