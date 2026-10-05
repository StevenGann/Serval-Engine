#ifndef BUNNYMARK_H
#define BUNNYMARK_H

// bunnymark's game, shared by the interactive demo (main.c) and the headless
// benchmark (bench.c).

#include "serval/serval.h"

#define BUNNY_START_COUNT 16

// Loads the bunny sprites, sets the backdrop and sets up the HUD.
void bunnymark_init(void);

// Adds a bunny at the top center with a random color and velocity (up to
// MAX_ENT bunnies), or removes the newest one.
void bunny_add(void);
void bunny_remove(void);
int bunny_count(void);

// Direction of gravity on each axis: -1, 0 or 1. (0, 0) turns gravity off and
// sends bunnies resting on the floor off in random directions.
void gravity_set(int x, int y);

// Everything bunnymark does in a frame: movement, bunny physics, drawing and
// the HUD. Call between frame_begin() and frame_end().
void bunnymark_update(void);

#endif // BUNNYMARK_H
