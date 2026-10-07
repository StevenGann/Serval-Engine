// The planned API of audio.h at run time (docs/development.md#planned-api): each
// planned function, called twice, does nothing harmful (returns 0, false or
// its type's "none", changes nothing; loaders refuse data needing a planned
// feature) and in debug builds warns on the first call only. Run natively and
// in the test ROM; cases for stubs in src/gba/, which the host build doesn't
// link, go inside #ifdef SERVAL_GBA.
//
// Here: the PSG wave channel's refusals (PSG_WAVE: psg_play() skips the sound,
// the music sequencer leaves the track out, each warning that it is planned)
// and psg_waves_set(); the sound bank, tracker music and sampled effects
// (src/gba/sampled_audio.c). The stubs must leave the PSG playing and the
// hardware the planned features will claim (Direct Sound, timer 0, DMA 1 and
// 2, the wave channel) untouched.

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

// --- The wave channel in PSG music (portable: the sequencer) ----------------

static const PsgNote wave_notes[] = {{PSG_C3, 4}};
static const PsgNote square_notes[] = {{PSG_E5, 4}};
static const PsgTrack tracks_with_wave[] = {
    {.channel = PSG_WAVE, .notes = wave_notes, .note_count = 1},
    {.channel = PSG_SQUARE1, .notes = square_notes, .note_count = 1},
    {.channel = 7, .notes = square_notes, .note_count = 1}, // invalid: its own warning
};
static const PsgSong song_with_wave = {.tracks = tracks_with_wave, .track_count = 3};
static const PsgSong wave_only_song = {.tracks = tracks_with_wave, .track_count = 1};

static void wave_tracks_are_left_out(void) {
    serval_psg_seq_reset_warnings();
    PsgSequencer seq;
    u32 before = debug_warning_count();
    CHECK(serval_psg_seq_start(&seq, &song_with_wave) == 1u << PSG_SQUARE1);
    // The wave track and the invalid one are different problems, each
    // reported once.
    CHECK(debug_warning_count() - before == 2 * WARNED);
    before = debug_warning_count();
    CHECK(serval_psg_seq_start(&seq, &song_with_wave) == 1u << PSG_SQUARE1);
    CHECK(serval_psg_seq_start(&seq, &wave_only_song) == 0);
    CHECK(debug_warning_count() == before);
    CHECK(seq.tracks[PSG_SQUARE1].track == NULL); // the wave-only song plays nothing
    CHECK(serval_psg_seq_channels(&seq) == 0);
}

#ifdef SERVAL_GBA
// --- Stubs and refusals in src/gba/ (the test ROM only) ---------------------

#include <tonc.h>

#include "../src/gba/internal.h"

#define VOLUME(cnt) ((cnt) >> 12)
#define WAVE_ENABLED (SDMG_LWAVE | SDMG_RWAVE) // SOUNDCNT_L: channel 3 to either speaker

// The wave channel stays off: not playing, not sent to the speakers.
static bool wave_channel_off(void) {
    return !(REG_SNDSTAT & SSTAT_WAVE) && !(REG_SNDDMGCNT & WAVE_ENABLED) &&
           !(REG_SND3SEL & 0x80); // SOUND3CNT_L bit 7: playback on
}

enum { SND_WAVE, SND_SQUARE, SND_BAD_CHANNEL, SOUND_COUNT };
static const PsgSound wave_sound = {.channel = PSG_WAVE, .frequency = 220, .frames = 10};
static const PsgSound square_sound = {.channel = PSG_SQUARE2, .frequency = 440}; // until replaced
static const PsgSound bad_channel_sound = {.channel = 7, .frequency = 440, .frames = 10};
static const PsgSound* const sounds[SOUND_COUNT] = {
    [SND_WAVE] = &wave_sound, [SND_SQUARE] = &square_sound, [SND_BAD_CHANNEL] = &bad_channel_sound};

static void wave_sounds_are_skipped(void) {
    psg_table_set(sounds, SOUND_COUNT); // makes sound problems reportable again
    psg_play(SND_SQUARE);
    u16 rate = serval_psg_rate(PSG_SQUARE2);
    TWICE(psg_play(SND_WAVE));
    CHECK(wave_channel_off());
    CHECK(VOLUME(REG_SND2CNT) == 15 && serval_psg_rate(PSG_SQUARE2) == rate); // plays on
    // An invalid channel is a different problem, reported on its own.
    TWICE(psg_play(SND_BAD_CHANNEL));
    psg_stop_all();
}

static void wave_tracks_are_left_out_of_psg_music(void) {
    psg_table_set(sounds, SOUND_COUNT); // makes song problems reportable again
    psg_music_play(&song_with_wave);
    CHECK(psg_music_playing());
    CHECK(serval_psg_rate(PSG_SQUARE1) == serval_psg_square_rates[PSG_E5]);
    CHECK(wave_channel_off());
    psg_music_play(&wave_only_song);
    CHECK(!psg_music_playing());
    CHECK(wave_channel_off());
    psg_stop_all();
}

static void psg_waves_set_ignores_the_table(void) {
    static const u32 waves[8] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476,
                                 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000};
    u32 wave_ram[4];
    for (u32 i = 0; i < 4; i++)
        wave_ram[i] = (REG_WAVE_RAM)[i];
    TWICE(psg_waves_set(waves, 2));
    u32 before = debug_warning_count();
    psg_waves_set(NULL, 0); // the stub reports once in all, not once per table
    CHECK(debug_warning_count() == before);
    for (u32 i = 0; i < 4; i++)
        CHECK((REG_WAVE_RAM)[i] == wave_ram[i]);
    CHECK(wave_channel_off());
}

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
           {"wave_tracks_are_left_out", wave_tracks_are_left_out},
           {"wave_sounds_are_skipped", wave_sounds_are_skipped},
           {"wave_tracks_are_left_out_of_psg_music", wave_tracks_are_left_out_of_psg_music},
           {"psg_waves_set_ignores_the_table", psg_waves_set_ignores_the_table},
           {"sound_bank_and_tracker_music_do_nothing", sound_bank_and_tracker_music_do_nothing},
           {"sampled_effects_do_nothing", sampled_effects_do_nothing});
#else
TEST_SUITE(planned_audio_tests, "planned_audio",
           {"wave_tracks_are_left_out", wave_tracks_are_left_out});
#endif
