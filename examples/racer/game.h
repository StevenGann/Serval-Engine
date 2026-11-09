#ifndef RACER_GAME_H
#define RACER_GAME_H

// The game's flow: the title, the race, pausing, the results, and the
// fades between them.

#include <serval/serval.h>

// Builds and loads everything (with the screen black), reads the records
// and fades in the title. Call once, after serval_splash().
void game_init(void);

// One frame of the game. Call between frame_begin() and frame_end().
void game_frame(void);

#endif // RACER_GAME_H
