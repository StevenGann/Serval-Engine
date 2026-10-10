// The planned API of audio.h at run time (docs/development.md#planned-api): each
// planned function, called twice, does nothing harmful (returns 0, false or
// its type's "none", changes nothing; loaders refuse data needing a planned
// feature) and in debug builds warns on the first call only. Run natively and
// in the test ROM; cases for stubs in src/gba/, which the host build doesn't
// link, go inside #ifdef SERVAL_GBA.
//
// Here: the sound bank, tracker music and sampled effects
// (src/gba/sampled_audio.c). The stubs must leave the PSG playing and the
// hardware the planned features will claim (Direct Sound, timer 0, DMA 1 and
// 2) untouched. (The wave channel, planned until it was implemented, has its
// tests in psg_wave_tests.c and rom/wave_tests.c.)

// Calls planned API on purpose: without this, every call would warn.
#define SERVAL_NO_PLANNED_WARNINGS

#include "../src/core/psg_sequencer.h"
#include "serval/audio.h"
#include "serval/debug.h"
#include "serval/fixed.h"
#include "test.h"

// Warnings a planned call reports on its first call: one in debug builds,
// none in release builds.
#ifdef SERVAL_DEBUG
#define WARNED 1u
#else
#define WARNED 0u
#endif

// Runs a statement (a call, or a CHECK on a call's result) twice: the first
// run must report WARNED warnings, the second none.
#define TWICE(statement)                                                                           \
    do {                                                                                           \
        u32 before_ = debug_warning_count();                                                       \
        statement;                                                                                 \
        u32 first_ = debug_warning_count() - before_;                                              \
        statement;                                                                                 \
        CHECK(first_ == WARNED);                                                                   \
        CHECK(debug_warning_count() - before_ == WARNED);                                          \
    } while (0)

#ifdef SERVAL_GBA
// --- Stubs and refusals in src/gba/ (the test ROM only) ---------------------

#include <tonc.h>

#include "../src/gba/internal.h"

#define VOLUME(cnt) ((cnt) >> 12)

enum { SND_SQUARE, SOUND_COUNT };
static const PsgSound square_sound = {.channel = PSG_SQUARE2, .frequency = 440}; // until replaced
static const PsgSound* const sounds[SOUND_COUNT] = {[SND_SQUARE] = &square_sound};

// What the sampled-audio stubs must leave alone: the PSG playing (a song on
// square 1, a held sound on square 2) and the hardware Maxmod will claim.
typedef struct {
    u16 soundcnt_h; // Direct Sound control, and the PSG's share of the volume
    u16 timer0;     // the sample clock
    u32 dma1, dma2; // the FIFO feeds
    u16 square2_rate;
} Untouched;

static const PsgNote held[] = {{PSG_C4, 255}};
static const PsgTrack held_track = {.channel = PSG_SQUARE1, .notes = held, .note_count = 1};
static const PsgSong held_song = {.tracks = &held_track, .track_count = 1};

static Untouched start_psg(void) {
    psg_table_set(sounds, SOUND_COUNT);
    psg_music_play(&held_song);
    psg_play(SND_SQUARE);
    return (Untouched){.soundcnt_h = REG_SNDDSCNT,
                       .timer0 = REG_TM0CNT,
                       .dma1 = REG_DMA[1].cnt,
                       .dma2 = REG_DMA[2].cnt,
                       .square2_rate = serval_psg_rate(PSG_SQUARE2)};
}

static void check_untouched(Untouched u) {
    CHECK(REG_SNDDSCNT == u.soundcnt_h);
    CHECK(REG_TM0CNT == u.timer0);
    CHECK(REG_DMA[1].cnt == u.dma1 && REG_DMA[2].cnt == u.dma2);
    CHECK(psg_music_playing() && !psg_music_paused());
    CHECK(serval_psg_rate(PSG_SQUARE1) == serval_psg_square_rates[PSG_C4]);
    CHECK(VOLUME(REG_SND2CNT) == 15 && serval_psg_rate(PSG_SQUARE2) == u.square2_rate);
    psg_stop_all();
}

static void sound_bank_and_tracker_music_do_nothing(void) {
    static const u32 not_a_bank[4] = {0};
    Untouched u = start_psg();
    TWICE(CHECK(!music_playing()));
    TWICE(CHECK(!music_paused()));
    TWICE(audio_bank_set(not_a_bank));
    audio_bank_set(NULL);
    TWICE(music_play(0, true));
    TWICE(music_set_volume(0));
    TWICE(music_set_speed(200));
    TWICE(music_pause());
    TWICE(music_resume());
    TWICE(music_stop()); // stops neither PSG music nor sounds
    // Changed nothing: still no tracker music.
    CHECK(!music_playing() && !music_paused());
    check_untouched(u);
}

static void sampled_effects_do_nothing(void) {
    Untouched u = start_psg();
    TWICE(CHECK(sfx_play(0) == SFX_NONE));
    TWICE(CHECK(sfx_play_ex(1, 255, -128, FX_ONE * 2, 255) == SFX_NONE));
    TWICE(CHECK(!sfx_playing(SFX_NONE)));
    TWICE(sfx_stop(SFX_NONE));
    TWICE(sfx_set_volume(0));
    TWICE(sfx_stop_all()); // stops no PSG sound
    CHECK(sfx_play(0) == SFX_NONE && !sfx_playing(1));
    check_untouched(u);
}

TEST_SUITE(planned_audio_tests, "planned_audio",
           {"sound_bank_and_tracker_music_do_nothing", sound_bank_and_tracker_music_do_nothing},
           {"sampled_effects_do_nothing", sampled_effects_do_nothing});
#else
// The host build links no stub: every planned name of audio.h is in src/gba/.
TEST_SUITE(planned_audio_tests, "planned_audio");
#endif
