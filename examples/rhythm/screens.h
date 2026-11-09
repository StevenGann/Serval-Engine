#ifndef RHYTHM_SCREENS_H
#define RHYTHM_SCREENS_H

// The screens around a song: the title, the song select and the results
// (the calibration is calibrate.c's). Each has a _show(), which sets it up
// under the black of a fade, and a _frame(), called every frame while it
// is on, input or not: `input` is false during the fades, when it only
// animates.

#include "judge.h"

#include <serval/serval.h>

// Text style 2, for what went wrong (light red), and 3, for the hints and
// labels (a soft lavender): their colours set by game_init().
#define TEXT_WARNING 2
#define TEXT_HINT 3

// Builds the song select's and the results' cards (art_build_cards()):
// once at boot, after art_build() and before art_load().
void screens_build_art(void);

// The serval at (x, foot y), dancing to `ticks` of music at 4 rows a beat
// (a paw up on the beat, left then right, a bob on the half beat), or
// standing for ticks below 0.
void serval_dance(FIXED ticks, int x, int y);

// --- The title ------------------------------------------------------------------------------

// The stage, the serval dancing to the title loop, BEAT PAWS bouncing and
// paws landing on the pads to the beat, PRESS START.
void title_show(void);

// True when START or A is pressed.
bool title_frame(bool input);

// --- The song select ------------------------------------------------------------------------

typedef enum {
    SELECT_STAY,
    SELECT_PLAY,      // A: play the song chosen (select_choice())
    SELECT_BACK,      // B: back to the title
    SELECT_CALIBRATE, // SELECT
} SelectAction;

typedef struct {
    u8 song, difficulty;
    bool autoplay;
} SelectChoice;

// The three songs with their tempos and lengths, the difficulty, the best
// for it, AUTO, and the song previewing. `save_failed`: say SAVE FAILED (a
// calibration just saved, or not).
void select_show(bool save_failed);

SelectAction select_frame(bool input);

// The song, difficulty and autoplay chosen (kept from one visit to the
// next; CALM, EASY and no AUTO at boot).
const SelectChoice* select_choice(void);

// --- The results ----------------------------------------------------------------------------

// The song's results: its counts, the longest combo, the accuracy, the score
// and the grade, NEW BEST and FULL COMBO. `fell`: records_take()'s bits.
void results_show(const SelectChoice* played, const Score* s, u32 fell);

// True when A or START is pressed.
bool results_frame(bool input);

#endif // RHYTHM_SCREENS_H
