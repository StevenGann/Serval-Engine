#ifndef RACER_GAME_H
#define RACER_GAME_H

// The game's flow and its HUD: boots into a race and runs it.

#include <serval/serval.h>

// Builds and loads everything (with the screen black) and starts a race.
// Call once, after serval_splash().
void game_init(void);

// One frame of the game. Call between frame_begin() and frame_end().
void game_frame(void);

#endif // RACER_GAME_H
