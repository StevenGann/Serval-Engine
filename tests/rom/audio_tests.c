// Tests for PSG sound effects (src/gba/psg.c), checked against the sound
// registers they program.

#include "../test.h"
#include "serval/audio.h"
#include "serval/core.h"
#include "serval/debug.h"

#include <tonc.h>

#include "../../src/gba/internal.h"

#define VOLUME(cnt) ((cnt) >> 12)

enum { SND_BEEP, SND_MELODY, SND_SLIDE, SND_NOISE, SND_DEFAULTS, SOUND_COUNT };

static const u16 melody_notes[] = {262, 0, 392};
static const PsgSound beep = {.channel = PSG_SQUARE2,
                              .frequency = 440,
                              .frames = 3,
                              .volume = 12,
                              .fade = -2,
                              .duty = PSG_DUTY_25};
static const PsgSound melody = {
    .channel = PSG_SQUARE2, .frames = 2, .notes = melody_notes, .note_count = 3};
static const PsgSound slide = {
    .channel = PSG_SQUARE1, .frequency = 880, .frames = 10, .slide = -3, .slide_size = 2};
static const PsgSound noise = {.channel = PSG_NOISE, .frequency = 4096, .fade = -1};
static const PsgSound defaults = {.frequency = 1000, .frames = 1};

static const PsgSound* const sounds[SOUND_COUNT] = {[SND_BEEP] = &beep,
                                                    [SND_MELODY] = &melody,
                                                    [SND_SLIDE] = &slide,
                                                    [SND_NOISE] = &noise,
                                                    [SND_DEFAULTS] = &defaults};

static void frames(int n) {
    for (int i = 0; i < n; i++) {
        frame_begin();
        frame_end();
    }
}

static void init_turns_sound_on(void) {
    CHECK(REG_SNDSTAT & SSTAT_ENABLE);
    CHECK((REG_SNDDMGCNT & (SDMG_SQR1 | SDMG_SQR2 | SDMG_NOISE) << 8) ==
          (SDMG_SQR1 | SDMG_SQR2 | SDMG_NOISE) << 8);
}

static void play_programs_frequency_volume_and_envelope(void) {
    psg_table_set(sounds, SOUND_COUNT);
    psg_play(SND_BEEP);
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 440);
    CHECK(VOLUME(REG_SND2CNT) == 12);
    CHECK(((REG_SND2CNT >> 8) & 7) == 2 && !(REG_SND2CNT & (1 << 11))); // fade out, step 2
    CHECK(((REG_SND2CNT >> 6) & 3) == PSG_DUTY_25);
}

static void sounds_stop_after_their_frames(void) {
    psg_table_set(sounds, SOUND_COUNT);
    psg_play(SND_BEEP); // 3 frames
    frames(2);
    CHECK(VOLUME(REG_SND2CNT) == 12);
    frames(1);
    CHECK(VOLUME(REG_SND2CNT) == 0);
}

static void melodies_step_through_notes_and_rests(void) {
    psg_table_set(sounds, SOUND_COUNT);
    psg_play(SND_MELODY); // 262 Hz, rest, 392 Hz; 2 frames each
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 262);
    frames(2);
    CHECK(VOLUME(REG_SND2CNT) == 0); // rest
    frames(2);
    CHECK(serval_psg_rate(PSG_SQUARE2) == 2048 - 131072 / 392);
    CHECK(VOLUME(REG_SND2CNT) == 15); // default volume
    frames(2);
    CHECK(VOLUME(REG_SND2CNT) == 0); // finished
}

static void slide_uses_the_sweep_unit(void) {
    psg_table_set(sounds, SOUND_COUNT);
    psg_play(SND_SLIDE);
    CHECK(REG_SND1SWEEP == (3 << 4 | 0x0008 | 2)); // time 3, down, shift 2
    psg_play(SND_DEFAULTS);                        // square 1 without a slide
    CHECK(REG_SND1SWEEP & 0x0008);
    CHECK(((REG_SND1SWEEP >> 4) & 7) == 0);
    CHECK(((REG_SND1CNT >> 6) & 3) == PSG_DUTY_50); // default duty
}

static void noise_picks_a_close_rate(void) {
    psg_table_set(sounds, SOUND_COUNT);
    psg_play(SND_NOISE);
    u32 r = REG_SND4FREQ & 7, s = (REG_SND4FREQ >> 4) & 15; // readable on the noise channel
    CHECK(serval_psg_rate(PSG_NOISE) == (REG_SND4FREQ & 0xFF));
    u32 hz = r ? (524288u / r) >> (s + 1) : 1048576u >> (s + 1);
    CHECK(hz == 4096);
    CHECK(VOLUME(REG_SND4CNT) == 15);
}

static void bad_ids_are_ignored_and_reported(void) {
    psg_table_set(sounds, SOUND_COUNT);
    u32 warnings = debug_warning_count();
    psg_play(SOUND_COUNT);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1);
#else
    CHECK(debug_warning_count() == warnings);
#endif
    psg_stop_all();
    CHECK(VOLUME(REG_SND1CNT) == 0 && VOLUME(REG_SND2CNT) == 0 && VOLUME(REG_SND4CNT) == 0);
}

TEST_SUITE(audio_tests, "audio", {"init_turns_sound_on", init_turns_sound_on},
           {"play_programs_frequency_volume_and_envelope",
            play_programs_frequency_volume_and_envelope},
           {"sounds_stop_after_their_frames", sounds_stop_after_their_frames},
           {"melodies_step_through_notes_and_rests", melodies_step_through_notes_and_rests},
           {"slide_uses_the_sweep_unit", slide_uses_the_sweep_unit},
           {"noise_picks_a_close_rate", noise_picks_a_close_rate},
           {"bad_ids_are_ignored_and_reported", bad_ids_are_ignored_and_reported});
