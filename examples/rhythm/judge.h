#ifndef RHYTHM_JUDGE_H
#define RHYTHM_JUDGE_H

// Judging a press against a chart, the score and the calibration median.
// Portable arithmetic only (no hardware), so tests/rhythm_tests.c runs it on
// the host. Times are in the module's ticks, 24.8 (FIXED), as music_ticks()
// counts them; a chart is RhythmNote[] sorted by tick (chart.h).

#include "chart.h"

typedef enum { JUDGE_NONE, JUDGE_PERFECT, JUDGE_GOOD, JUDGE_MISS } Judgement;

typedef struct {
    u16 perfect, good, miss, combo, max_combo;
    u32 score;
} Score;

// The most notes a chart may hold; play_start() ignores any beyond.
#define PLAY_MAX_NOTES 1024

// A chart being played. Treat as opaque; it is about 4 KB, so keep it in
// EWRAM.
typedef struct {
    const RhythmNote* notes;
    u16 count;
    u16 judged_count;
    FIXED perfect, good;
    u16 cursor[4];                 // each lane's first unjudged note
    u16 next[PLAY_MAX_NOTES];      // the next note in the same lane
    u8 judged[PLAY_MAX_NOTES / 8]; // one bit a note
    Score score;
} Play;

// Starts a chart: nothing judged, score zero. The windows are half-widths in
// ticks (ms_to_ticks()): a press within +-perfect_ticks of a note is PERFECT,
// within +-good_ticks GOOD (both inclusive).
void play_start(Play* p, const RhythmNote* notes, u16 count, FIXED perfect_ticks, FIXED good_ticks);

// A press in a lane at a time: judges the earliest unjudged note of that lane
// within the good window and returns PERFECT or GOOD, or returns JUDGE_NONE
// and changes nothing when there is none (an early or stray press costs
// nothing). An older note left unjudged is for play_expire().
Judgement play_press(Play* p, u8 lane, FIXED press_ticks);

// Marks MISS every unjudged note whose time plus the good window has passed
// (each resets the combo) and returns how many. Call it once a frame.
u16 play_expire(Play* p, FIXED now_ticks);

// True when every note is judged.
bool play_done(const Play* p);

const Score* play_score(const Play* p);

// (perfect * 3 + good) * 1000 / (3 * notes), notes = perfect + good + miss,
// so it is final once play_done(). 0 for no notes.
u16 score_accuracy_permille(const Score* s);

// 'S' from 950, 'A' from 900, 'B' from 800, 'C' from 700, else 'D'.
char grade_of(u16 accuracy_permille);

// ms * MUSIC_RATE * 256 / (1000 * tick_samples), to the nearest (halves away
// from zero), tick_samples being MusicPosition's.
FIXED ms_to_ticks(s32 ms, u16 tick_samples);

// The median of count (at most 255) errors; an even count gives the mean of
// the middle two, rounded toward zero. 0 for none. The array is not changed.
s16 calibration_median_ms(const s16* errors_ms, u8 count);

#endif
