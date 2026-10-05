// The high-score table (top 5, kept in save data) and the screen where a
// player enters initials. game.c decides when these screens show and plays
// their sounds; this file owns the table and the save slot.

#ifndef SHMUP_SCORES_H
#define SHMUP_SCORES_H

#include "serval/serval.h"

#define SCORES_COUNT 5

void scores_load(void);          // once at boot: the saved table, or the default one
int scores_best(void);           // the top score, for the HUD
int scores_rank(int score);      // the place (0 = best) a score would take, or -1
void scores_draw(int highlight); // the table, the entry at place highlight marked (-1: none)

typedef enum { ENTRY_NONE, ENTRY_LETTER, ENTRY_MOVE, ENTRY_DONE } EntryEvent;
void entry_begin(int score, bool cleared); // the initials screen for a score that ranks
EntryEvent entry_update(void);             // a frame of it; ENTRY_DONE once saved

#endif // SHMUP_SCORES_H
