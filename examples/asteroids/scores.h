// asteroids: the high-score table, kept in save data, and the screen where a
// player enters initials. main.c decides when these screens show and plays
// their sounds; this file owns the table and the save slot.

#ifndef ASTEROIDS_SCORES_H
#define ASTEROIDS_SCORES_H

#include "serval/serval.h"

#define SCORES_COUNT 10 // entries in the table

// Reads the table from save data, or sets up the default one (first boot, or
// the save can't be used). Call once at boot.
void scores_load(void);

// Erases the saved table and puts the default one back.
void scores_reset(void);

// The place (0 = best) a score would take in the table, or -1 if it doesn't
// make it.
int scores_rank(int score);

// Draws the table on the whole screen, the entry at place highlight (-1 for
// none) in yellow. Draws a warning if the last save failed.
void scores_draw(int highlight);

// The initials entry screen, for a score that made the table.
typedef enum {
    ENTRY_NONE,   // nothing happened this frame
    ENTRY_LETTER, // the current letter changed (UP or DOWN)
    ENTRY_MOVE,   // the cursor moved to another letter
    ENTRY_DONE,   // confirmed: the score is in the table and saved
} EntryEvent;

// Shows the entry screen for a score; returns its place in the table.
int entry_begin(int score);

// Runs one frame of the entry screen: reads the buttons and redraws.
EntryEvent entry_update(void);

#endif // ASTEROIDS_SCORES_H
