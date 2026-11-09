// The song table. The modules and the charts come from one source (the
// rhythm set of tools/make-example-audio.py), so a chart's ticks are the
// module's: nothing here is timed in seconds.

#include "songs.h"

#include "rhythm_bank.h"
#include "rhythm_charts.h"

// Every chart fits a Play (judge.h), which would ignore notes past its limit.
#define FITS(n) ((n) <= PLAY_MAX_NOTES)
_Static_assert(FITS(RHYTHM_CALM_EASY_COUNT) && FITS(RHYTHM_CALM_HARD_COUNT) &&
                   FITS(RHYTHM_STRIDE_EASY_COUNT) && FITS(RHYTHM_STRIDE_HARD_COUNT) &&
                   FITS(RHYTHM_RUSH_EASY_COUNT) && FITS(RHYTHM_RUSH_HARD_COUNT),
               "a chart holds more notes than PLAY_MAX_NOTES: raise it");

_Static_assert(RHYTHM_CALM_SPEED * 4 == BEAT_TICKS && RHYTHM_STRIDE_SPEED * 4 == BEAT_TICKS &&
                   RHYTHM_RUSH_SPEED * 4 == BEAT_TICKS,
               "every song keeps speed 6: a beat is BEAT_TICKS ticks");

#define SONG(name, key, KEY)                                                                       \
    {                                                                                              \
        name, MOD_RHYTHM_##KEY, RHYTHM_##KEY##_TEMPO, RHYTHM_##KEY##_LENGTH_TICKS, {               \
            {rhythm_##key##_easy, RHYTHM_##KEY##_EASY_COUNT}, {                                    \
                rhythm_##key##_hard, RHYTHM_##KEY##_HARD_COUNT                                     \
            }                                                                                      \
        }                                                                                          \
    }

const Song songs[SONG_COUNT] = {
    [SONG_CALM] = SONG("CALM", calm, CALM),
    [SONG_STRIDE] = SONG("STRIDE", stride, STRIDE),
    [SONG_RUSH] = SONG("RUSH", rush, RUSH),
};

const char* const difficulty_names[DIFF_COUNT] = {"EASY", "HARD"};

const u16 module_title = MOD_RHYTHM_TITLE;
const u16 module_calibrate = MOD_RHYTHM_CALIBRATE;

const void* const songs_bank = rhythm_bank;
