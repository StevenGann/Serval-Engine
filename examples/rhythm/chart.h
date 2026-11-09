#ifndef RHYTHM_CHART_H
#define RHYTHM_CHART_H

// A chart: the notes a player hits in one song at one difficulty, sorted by
// tick. The charts are generated with the songs' modules from the same notes
// (tools/make-example-audio.py's rhythm set writes rhythm_charts.h), so every
// note lands on a note of the music. Portable: no hardware.

#include <serval/serval.h>

// One note: when, in the module's ticks since its row 0 (a song keeps speed
// 6, so a row is 6 ticks and a beat 24), and which lane: 0 LEFT and 1 RIGHT
// (the left thumb), 2 B and 3 A (the right thumb).
typedef struct {
    u32 tick;
    u8 lane;
} RhythmNote;

#endif
