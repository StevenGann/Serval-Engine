#ifndef RHYTHM_SONGS_H
#define RHYTHM_SONGS_H

// The songs: their names, modules, tempos, lengths and charts, from the
// sound bank's header and rhythm_charts.h (both generated at build time by
// tools/make-example-audio.py's rhythm set), and the other modules the
// screens play.

#include "judge.h"

#include <serval/serval.h>

enum { SONG_CALM, SONG_STRIDE, SONG_RUSH, SONG_COUNT };

// Ticks a beat in every song: speed 6 (ticks a row) throughout, 4 rows a
// beat (songs.c checks it).
#define BEAT_TICKS 24
enum { DIFF_EASY, DIFF_HARD, DIFF_COUNT };

typedef struct {
    const RhythmNote* notes; // sorted by tick
    u16 count;
} Chart;

typedef struct {
    const char* name;
    u16 module;       // its MOD_* in the bank, played once
    u16 tempo;        // beats a minute, as written
    u32 length_ticks; // the module's end, in ticks since row 0
    Chart charts[DIFF_COUNT];
} Song;

extern const Song songs[SONG_COUNT];
extern const char* const difficulty_names[DIFF_COUNT];

// The modules that are not songs: the title's loop and the calibration's
// steady clicks (both looped).
extern const u16 module_title, module_calibrate;

// The sound bank to register with audio_bank_set().
extern const void* const songs_bank;

#endif // RHYTHM_SONGS_H
