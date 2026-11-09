#ifndef RACER_VIEW_H
#define RACER_VIEW_H

// Drawing: loads the pictures art.c built, and draws each frame of a race
// from the race and its road frame.

#include "road.h"
#include "track.h"

// Loads the tileset, the road and sky layers and the sprites, and sets the
// road's raster effect. Writes VRAM at once: call with the screen black.
void view_init(void);

// Puts back the sky's drift and the jeep's jolt, for a new race.
void view_reset(void);

// Draws a frame: the road bent line by line, its stripes, the sky swung by
// the curves, the jeep. Call between frame_begin() and frame_end().
void view_frame(const Race* r, const RoadFrame* f);

#endif // RACER_VIEW_H
