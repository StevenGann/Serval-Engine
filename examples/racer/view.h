#ifndef RACER_VIEW_H
#define RACER_VIEW_H

// Drawing the road and the sky: loads the pictures art.c built, and each
// frame bends the road, stripes it, swings the sky, sets the time of day's
// colours and shakes the picture after a bump. The sprites on the road are
// objects.c's.

#include "road.h"
#include "track.h"

// Loads the tileset, the road and sky layers and the sprites, and sets the
// road's raster effect. Writes VRAM at once: call with the screen black.
void view_init(void);

// For a new screen or race: puts the sky back where it starts and the time
// of day at `stage` (art.h's TIME_STAGE()) at once.
void view_reset(int stage);

// A frame on: the sky drifts with the curve under the car and its speed,
// and the colours take a step toward the time of day `stage` (a stage
// every few frames: about two seconds from one time of day to the next).
// Not called while paused, so a paused picture stays as it is.
void view_step(const RoadFrame* f, FIXED speed, int stage);

// Draws a frame of road and sky as view_step() left them, shaken down by
// `shake` lines (0 to SHAKE_MAX). Call between frame_begin() and frame_end(),
// every frame, paused or not.
void view_draw(const RoadFrame* f, int shake);

// The shake of the picture (0 to SHAKE_MAX lines) for a bump's timer
// (Race.bump_timer: CAR_BUMP_FRAMES just after a bump, counting down).
int view_shake(int bump_timer);

#endif // RACER_VIEW_H
