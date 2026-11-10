#ifndef BOARDGAMES_SCREENS_H
#define BOARDGAMES_SCREENS_H

// The screens before play: the title (the PAWS & KINGS logo over the
// dimmed board, CONTINUE, CHESS and CHECKERS, each game's record in the
// panel, the erase prompt, and the attract mode, the computer playing
// itself at CUB after a while with no button pressed), and the setup (1 or 2
// players; for one, the human's side and the computer's strength).

#include <stdbool.h>

#include "match.h"

typedef enum { SCREEN_TITLE, SCREEN_SETUP, SCREEN_PLAY } Screen;

// One frame of the title. Call between frame_begin() and frame_end(), with
// the Match play will use (kept in EWRAM): the title shows a start position
// in it, the attract mode plays in it, and CONTINUE restores the saved game
// into it. Returns the screen for the next frame: SCREEN_PLAY with m ready
// for play_begin(), SCREEN_SETUP once a game is chosen, else SCREEN_TITLE.
Screen screens_title_frame(Match* m);

// While deaf, screens_title_frame() draws the title and hears no button, so
// that a press during the fade-in is not half handled.
void screens_title_deaf(bool deaf);

// One frame of the setup, for the game chosen on the title. Returns
// SCREEN_PLAY with m set up as a new game, SCREEN_TITLE on B, else
// SCREEN_SETUP.
Screen screens_setup_frame(Match* m);

#endif // BOARDGAMES_SCREENS_H
