#ifndef RHYTHM_RECORDS_H
#define RHYTHM_RECORDS_H

// The saved records: for every song and difficulty the best score, the best
// grade and whether it was ever played through without a miss, and the
// player's calibration. One struct in one save slot (save.h), read at boot
// and written only when a best falls, the calibration is saved or the
// records are erased.
//
// The rules (records_add(), records_valid()) are portable arithmetic on a
// Records the caller holds, tested on the host (tests/rhythm_tests.c); the
// rest keeps the game's copy and its save.

#include "judge.h"
#include "songs.h"

#include <serval/serval.h>

// The calibration's limits, milliseconds either way (calibrate.c clamps to
// them; a save beyond them is not this game's).
#define CALIBRATION_MAX_MS 200

// The three bests of a song and difficulty are kept apart, each the best of
// its own: the highest score, the best grade and whether any play was a
// full combo may come from three different plays (a full combo of GOODs
// can score less than a run of PERFECTs with one miss).
typedef struct {
    u32 score;      // the best score
    u8 grade;       // the best grade, 'S' to 'D'; 0: never played
    u8 full_combo;  // 1 once played through without a miss
    u8 reserved[2]; // 0
} Best;

// Fixed-size fields only, no pointers, so it reads the same after a
// power-off. Raise RECORDS_VERSION whenever it changes.
typedef struct {
    Best best[SONG_COUNT][DIFF_COUNT];
    s16 calibration_ms; // how much later than the music the presses come
    u16 reserved;       // 0
} Records;

#define RECORDS_SLOT 0
#define RECORDS_VERSION 1

// What records_add() found beaten.
#define RECORD_SCORE (1u << 0)      // a higher score (or the first)
#define RECORD_GRADE (1u << 1)      // a better grade (or the first)
#define RECORD_FULL_COMBO (1u << 2) // the first play without a miss

// --- The rules (portable) -------------------------------------------------------------------

// No bests, no calibration.
void records_clear(Records* r);

// Takes a song played into r: each of its bests that the play beats (a
// higher score, a better grade, a first full combo). An autoplay, a song or
// difficulty out of range, or a score of no notes changes nothing. Returns
// the RECORD_* bits of what fell, 0 if nothing did.
u32 records_add(Records* r, u8 song, u8 difficulty, const Score* s, bool autoplay);

// Whether r is one this game wrote: grades 'S' to 'D' or 0, full combo 0 or
// 1 and only with a grade, reserved fields 0, the calibration within
// +-CALIBRATION_MAX_MS.
bool records_valid(const Records* r);

// --- The game's records, saved --------------------------------------------------------------

// Reads them from the save. With no save, or one of another version or
// size, or not valid (records_valid()), the records start empty (the slot
// is left alone until the first write). Call once at boot.
void records_load(void);

const Records* records_get(void);

// records_add() on the game's records, saved if anything fell. Returns
// its RECORD_* bits.
u32 records_take(u8 song, u8 difficulty, const Score* s, bool autoplay);

// Sets the calibration (clamped to +-CALIBRATION_MAX_MS) and saves.
void records_save_calibration(s16 ms);

// Clears every best and saves; the calibration stays (it belongs to the
// player's screen and speakers, not to a record).
void records_erase(void);

// Whether the last write failed (the records then last only until power-off).
bool records_save_failed(void);

#endif // RHYTHM_RECORDS_H
