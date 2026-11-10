#ifndef BOARDGAMES_PLAY_H
#define BOARDGAMES_PLAY_H

// The play screen, for either game: the cursor, picking pieces and their
// moves (chess's promotion chooser, checkers' jumps landed one at a time),
// the computer thinking a little every frame, the sounds, the pause menu
// (START) and the result.

#include "match.h"

typedef enum { PLAY_CONTINUE, PLAY_QUIT_TO_TITLE } PlayExit;

// Starts showing m, a new game or one restored (m stays the caller's; keep it
// in EWRAM): the board from the human's side in one-player games, White's (or
// Light's) in two.
void play_begin(Match* m);

// One frame of play: input, the computer's thinking, the drawing. Call
// between frame_begin() and frame_end(). PLAY_QUIT_TO_TITLE when the player
// quits from the menu (the game stays saved) or leaves a finished game.
PlayExit play_frame(void);

#endif // BOARDGAMES_PLAY_H
