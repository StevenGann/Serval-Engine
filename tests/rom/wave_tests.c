// Tests for the PSG wave channel (PSG_WAVE, psg_waves_set; src/gba/wave.c and
// psg.c), checked against the registers and wave RAM in mGBA: the banks,
// pitch, the volume levels and the fade the engine steps, priorities and
// music. The portable parts (rates, levels, the envelope) are
// tests/psg_wave_tests.c.

#include "../test.h"
#include "serval/audio.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/core/psg_sequencer.h"
#include "../../src/core/psg_wave.h"
#include "../../src/gba/internal.h"

#ifdef SERVAL_DEBUG
#define WARNED 1u
#else
#define WARNED 0u
#endif
#define REPORTED_ONCE(n) CHECK((n) == WARNED)

#define DAC_ON 0x0080
#define LEVEL (REG_SND3CNT & 0xE000) // SOUND3CNT_H's volume bits
#define LEVEL_100 0x2000
#define LEVEL_75 0x8000
#define LEVEL_50 0x4000
#define LEVEL_25 0x6000

static void frames(int n) {
    for (int i = 0; i < n; i++) {
        frame_begin();
        frame_end();
    }
}

// Playing: the channel's status bit (SOUNDCNT_X) and its DAC (SOUND3CNT_L).
static bool wave_on(void) {
    return (REG_SNDSTAT & SSTAT_WAVE) && (REG_SND3SEL & DAC_ON);
}

// The bank selected for playback agrees with the engine's.
static bool bank_selected(void) {
    return (REG_SND3SEL >> 6 & 1) == serval_wave_bank();
}

// Step i of a waveform in the order it plays.
static u32 step_of(const u32* wave, u32 i) {
    u32 byte = wave[i / 8] >> (i % 8 / 2 * 8) & 0xFF;
    return i & 1 ? byte & 15 : byte >> 4;
}

// The hardware plays a bank by rotating it in place (a shift register), so a
// bank that has played holds its waveform from wherever it got to: compare
// up to a rotation.
static bool same_wave(const u32* got, const u32* want) {
    for (u32 shift = 0; shift < 32; shift++) {
        u32 i = 0;
        while (i < 32 && step_of(got, (i + shift) & 31) == step_of(want, i))
            i++;
        if (i == 32)
            return true;
    }
    return false;
}

// What the CPU sees of wave RAM: the bank that isn't playing.
static bool cpu_bank_holds(const u32* want) {
    u32 ram[4];
    for (u32 i = 0; i < 4; i++)
        ram[i] = (REG_WAVE_RAM)[i];
    return same_wave(ram, want);
}

// The bank playing holds `want`. Stops the channel (DAC off), selects the
// other bank to read it, and selects the playing one again.
static bool playing_bank_holds(const u32* want) {
    u16 bank = REG_SND3SEL & 0x40;
    REG_SND3SEL = (u16)(bank ^ 0x40);
    bool same = cpu_bank_holds(want);
    REG_SND3SEL = bank;
    return same;
}

// Three waveforms no rotation turns into one another: a saw (0 to 15 twice),
// a square and a narrow pulse.
static const u32 saw[4] = {0x67452301, 0xEFCDAB89, 0x67452301, 0xEFCDAB89};
static const u32 waves[12] = {
    0x67452301, 0xEFCDAB89, 0x67452301, 0xEFCDAB89, // saw
    0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, // square
    0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, // pulse
};
#define SQUARE_WAVE (waves + 4)
#define PULSE (waves + 8)

enum {
    SND_A220,
    SND_SAW,
    SND_SQUARE,
    SND_PULSE,
    SND_PAST,
    SND_FADE,
    SND_FADE_IN,
    SND_SLIDE,
    SND_LOW,
    SND_LOW_NOTE,
    SND_HIGH,
    SND_LOW_PRIORITY,
    SND_SQUARE2,
    SOUND_COUNT
};
static const u16 low_notes[] = {110, 0, 25};
static const PsgSound* const sounds[SOUND_COUNT] = {
    [SND_A220] = &(const PsgSound){.channel = PSG_WAVE, .frequency = 220, .frames = 10},
    [SND_SAW] = &(const PsgSound){.channel = PSG_WAVE, .frequency = 110, .frames = 4},
    [SND_SQUARE] = &(const PsgSound){.channel = PSG_WAVE, .duty = 1, .frequency = 110, .frames = 4},
    [SND_PULSE] = &(const PsgSound){.channel = PSG_WAVE, .duty = 2, .frequency = 110, .frames = 4},
    [SND_PAST] = &(const PsgSound){.channel = PSG_WAVE, .duty = 5, .frequency = 110, .frames = 4},
    [SND_FADE] = &(const PsgSound){.channel = PSG_WAVE, .frequency = 440, .fade = -2},
    [SND_FADE_IN] = &(const PsgSound){.channel = PSG_WAVE, .frequency = 440, .fade = 1},
    [SND_SLIDE] =
        &(const PsgSound){
            .channel = PSG_WAVE, .frequency = 300, .frames = 4, .slide = 3, .slide_size = 2},
    [SND_LOW] = &(const PsgSound){.channel = PSG_WAVE, .frequency = 20, .frames = 4},
    [SND_LOW_NOTE] =
        &(const PsgSound){.channel = PSG_WAVE, .frames = 2, .notes = low_notes, .note_count = 3},
    [SND_HIGH] =
        &(const PsgSound){.channel = PSG_WAVE, .frequency = 600, .frames = 4, .priority = 2},
    [SND_LOW_PRIORITY] = &(const PsgSound){.channel = PSG_WAVE, .frequency = 300, .frames = 20},
    [SND_SQUARE2] = &(const PsgSound){.channel = PSG_SQUARE2, .frequency = 440, .frames = 20},
};

#define WAVE_RATE(hz) (2048 - 65536 / (hz))

static void reset(void) {
    psg_stop_all();
    psg_music_set_volume(15);
    psg_table_set(sounds, SOUND_COUNT); // makes sound problems reportable again
    psg_waves_set(waves, 3);
}

// --- The channel ---------------------------------------------------------------

static void init_sends_the_wave_channel_to_the_speakers(void) {
    CHECK((REG_SNDDMGCNT & (SDMG_LWAVE | SDMG_RWAVE)) == (SDMG_LWAVE | SDMG_RWAVE));
    // and the other three channels, as before
    CHECK((REG_SNDDMGCNT & (SDMG_SQR1 | SDMG_SQR2 | SDMG_NOISE) << 8) ==
          (SDMG_SQR1 | SDMG_SQR2 | SDMG_NOISE) << 8);
    psg_stop_all();
    CHECK(!wave_on() && bank_selected());
}

static void sounds_program_the_wave_channel(void) {
    reset();
    psg_play(SND_A220);
    CHECK(wave_on() && bank_selected());
    CHECK(serval_psg_rate(PSG_WAVE) == WAVE_RATE(220));
    CHECK(LEVEL == LEVEL_100);               // .volume 0: 15
    CHECK((REG_SND3SEL & 0x20) == 0);        // 32 steps, one bank
    CHECK((REG_SND3FREQ & 0x4000) == 0);     // no hardware length
    CHECK((REG_SNDDSCNT & 3) == SDS_DMG100); // the PSG's share untouched
    frames(9);
    CHECK(wave_on());
    frames(1); // 10 frames
    CHECK(!wave_on() && !(REG_SND3SEL & DAC_ON));
    // The other channels are as they were: a square plays beside it.
    psg_play(SND_SQUARE2);
    psg_play(SND_A220);
    CHECK(wave_on() && (REG_SND2CNT >> 12) == 15);
    psg_stop_all();
    CHECK(!wave_on() && (REG_SND2CNT >> 12) == 0);
}

static void the_triangle_plays_without_a_table(void) {
    reset();
    psg_waves_set(NULL, 0);
    u32 before = debug_warning_count();
    psg_play(SND_SAW); // .duty 0: the triangle
    CHECK(debug_warning_count() == before);
    CHECK(playing_bank_holds(serval_psg_triangle));
    psg_play(SND_SQUARE); // .duty 1: the triangle too, and a warning
    REPORTED_ONCE(debug_warning_count() - before);
    CHECK(playing_bank_holds(serval_psg_triangle));
    psg_stop_all();
}

static void waveforms_go_into_the_idle_bank(void) {
    reset();
    psg_play(SND_SAW);
    u32 bank = serval_wave_bank();
    CHECK(bank_selected() && wave_on());
    // The CPU sees the other bank. Mark it: the same waveform again is not
    // copied, nor does the bank change.
    static const u32 mark[4] = {0x5A5A5A5A, 0xA5A5A5A5, 0x0F0F0F0F, 0xF0F0F0F0};
    for (u32 i = 0; i < 4; i++)
        (REG_WAVE_RAM)[i] = mark[i];
    frames(1);
    psg_play(SND_SAW);
    CHECK(serval_wave_bank() == bank && bank_selected() && wave_on());
    CHECK(cpu_bank_holds(mark));
    // Another waveform goes into the idle bank, which then plays.
    psg_play(SND_SQUARE);
    CHECK(serval_wave_bank() == (bank ^ 1) && bank_selected() && wave_on());
    CHECK(cpu_bank_holds(saw)); // the old bank, the CPU's now
    CHECK(playing_bank_holds(SQUARE_WAVE));
    psg_play(SND_PULSE);
    CHECK(serval_wave_bank() == bank && bank_selected());
    CHECK(cpu_bank_holds(SQUARE_WAVE));
    CHECK(playing_bank_holds(PULSE));
    // A table registered again is copied afresh, even the same waveform.
    psg_waves_set(waves, 3);
    psg_play(SND_PULSE);
    CHECK(serval_wave_bank() == (bank ^ 1) && playing_bank_holds(PULSE));
    psg_stop_all();
}

static void duty_past_the_table_plays_waveform_0(void) {
    reset();
    psg_waves_set(waves, 2);
    u32 before = debug_warning_count();
    psg_play(SND_PAST);
    CHECK(wave_on() && playing_bank_holds(saw));
    psg_play(SND_PAST);
    REPORTED_ONCE(debug_warning_count() - before);
    // The table changes from the channel's next note.
    psg_waves_set(waves + 4, 1); // the square alone
    psg_play(SND_SAW);
    CHECK(playing_bank_holds(SQUARE_WAVE));
    psg_stop_all();
}

static void invalid_tables_are_ignored(void) {
    reset();
    u32 before = debug_warning_count();
    psg_waves_set(NULL, 2);
    psg_waves_set((const u32*)0x1234, 1);
    psg_waves_set((const u32*)((const u8*)waves + 2), 1); // not word-aligned
    // Each refused call warns (games call it rarely, at startup).
    CHECK(debug_warning_count() - before == 3 * WARNED);
    psg_play(SND_PULSE); // the table registered before plays on
    CHECK(playing_bank_holds(PULSE));
    psg_stop_all();
}

static void notes_play_at_their_rates(void) {
    reset();
    psg_play(SND_SAW);
    CHECK(serval_psg_rate(PSG_WAVE) == WAVE_RATE(110));
    static const u16 hz[] = {32, 33, 55, 1000, 4186, 65535};
    for (u32 i = 0; i < sizeof hz / sizeof hz[0]; i++) {
        PsgSound s = {.channel = PSG_WAVE, .frequency = hz[i], .frames = 2};
        serval_psg_play_sound(&s);
        CHECK(serval_psg_rate(PSG_WAVE) == WAVE_RATE(hz[i]) && wave_on());
    }
    psg_stop_all();
}

static void low_frequencies_play_at_32_hz(void) {
    reset();
    u32 before = debug_warning_count();
    psg_play(SND_LOW); // 20 Hz
    CHECK(serval_psg_rate(PSG_WAVE) == 0 && wave_on());
    psg_play(SND_LOW);
    REPORTED_ONCE(debug_warning_count() - before);
    psg_table_set(sounds, SOUND_COUNT); // reportable again, for a melody's note
    before = debug_warning_count();
    psg_play(SND_LOW_NOTE); // 110 Hz, a rest, 25 Hz
    CHECK(serval_psg_rate(PSG_WAVE) == WAVE_RATE(110));
    REPORTED_ONCE(debug_warning_count() - before);
    frames(2);
    CHECK(!wave_on()); // the rest
    frames(2);
    CHECK(serval_psg_rate(PSG_WAVE) == 0 && wave_on());
    // 40 Hz plays as itself on the wave channel (a square would warn).
    before = debug_warning_count();
    static const PsgSound forty = {.channel = PSG_WAVE, .frequency = 40, .frames = 2};
    static const PsgSound* const table[1] = {&forty};
    psg_table_set(table, 1);
    psg_play(0);
    CHECK(serval_psg_rate(PSG_WAVE) == WAVE_RATE(40));
    CHECK(debug_warning_count() == before);
    psg_stop_all();
}

static void slides_are_ignored(void) {
    reset();
    u32 before = debug_warning_count();
    psg_play(SND_SLIDE);
    psg_play(SND_SLIDE);
    REPORTED_ONCE(debug_warning_count() - before);
    CHECK(wave_on() && serval_psg_rate(PSG_WAVE) == WAVE_RATE(300));
    frames(3);
    CHECK(wave_on());
    psg_stop_all();
}

// --- Volume and fades --------------------------------------------------------

static void volumes_play_as_four_levels(void) {
    reset();
    static const u16 expected[16] = {LEVEL_100, LEVEL_25, LEVEL_25,  LEVEL_25, LEVEL_25, LEVEL_25,
                                     LEVEL_50,  LEVEL_50, LEVEL_50,  LEVEL_50, LEVEL_75, LEVEL_75,
                                     LEVEL_75,  LEVEL_75, LEVEL_100, LEVEL_100}; // 0: 15
    for (u32 v = 0; v < 16; v++) {
        PsgSound s = {.channel = PSG_WAVE, .frequency = 220, .volume = (u8)v};
        serval_psg_play_sound(&s);
        CHECK(LEVEL == expected[v] && wave_on());
    }
    psg_play(SND_FADE_IN); // .volume 0 fading in: from silence
    CHECK(LEVEL == 0 && wave_on());
    psg_stop_all();
}

static void fades_step_between_the_levels(void) {
    // From 15, a step every 2/64 s: the hardware level follows the envelope
    // frame by frame, silent at frame 28 (27.99), and the sound, which has no
    // .frames, holds its channel until psg.c's fade_frames(), 30.
    reset();
    psg_play(SND_FADE);
    PsgWaveEnvelope model;
    serval_psg_wave_envelope_start(&model, 15u << 12 | 2u << 8);
    CHECK(LEVEL == LEVEL_100);
    u32 levels_seen = 1, last = LEVEL;
    for (u32 f = 1; f <= 30; f++) {
        frames(1);
        serval_psg_wave_envelope_step(&model);
        if (f < 30) {
            CHECK(wave_on());
            CHECK(LEVEL == serval_psg_wave_volume(model.volume));
        }
        if (f == 27)
            CHECK(LEVEL == LEVEL_25);
        if (f == 28)
            CHECK(LEVEL == 0);
        if (LEVEL != last) {
            levels_seen++;
            last = LEVEL;
        }
    }
    CHECK(levels_seen == 5); // 100, 75, 50, 25, 0%
    CHECK(!wave_on());       // the sound ended, silent
    // Fading in (a step a 64th of a second) rises to full at frame 14
    // (13.99) and holds.
    psg_play(SND_FADE_IN);
    frames(13);
    CHECK(LEVEL == LEVEL_75); // 13
    frames(1);
    CHECK(LEVEL == LEVEL_100);
    frames(30);
    CHECK(LEVEL == LEVEL_100 && wave_on());
    psg_stop_all();
}

static void a_new_note_restarts_the_fade(void) {
    reset();
    psg_play(SND_FADE); // a step every 2/64 s: 1.87 frames
    frames(20);
    CHECK(LEVEL == LEVEL_25); // 10 steps: 5
    psg_play(SND_FADE);
    CHECK(LEVEL == LEVEL_100);
    frames(2);
    CHECK(LEVEL == LEVEL_100); // 1 step: 14
    frames(2);
    CHECK(LEVEL == LEVEL_75); // 2 steps: 13
    psg_stop_all();
    frames(10); // stopped: nothing steps the level any more
    CHECK(!wave_on());
}

// --- Priorities ----------------------------------------------------------------

static void priorities_hold_on_the_wave_channel(void) {
    reset();
    psg_play(SND_HIGH);
    psg_play(SND_LOW_PRIORITY); // lower: ignored
    CHECK(serval_psg_rate(PSG_WAVE) == WAVE_RATE(600));
    frames(4); // ended
    CHECK(!wave_on());
    psg_play(SND_LOW_PRIORITY);
    CHECK(serval_psg_rate(PSG_WAVE) == WAVE_RATE(300));
    psg_play(SND_HIGH); // higher: replaces
    CHECK(serval_psg_rate(PSG_WAVE) == WAVE_RATE(600) && wave_on());
    psg_stop_all();
}

// --- Music ---------------------------------------------------------------------

#define TICK_PER_FRAME .tempo = 3583, .ticks_per_beat = 1

static const PsgNote bass[] = {{PSG_C2, 2}, {PSG_REST, 1}, {PSG_G2, 3}};
static const PsgNote lead[] = {{PSG_E5, 6}};
static const PsgNote drums[] = {{PSG_C3, 3}, {PSG_C8, 3}};
static const PsgNote pluck[] = {{PSG_A1, 4}, {PSG_E2, 4}};
static const PsgTrack song_tracks[] = {
    {.channel = PSG_WAVE, .notes = bass, .note_count = 3, .duty = 1, .volume = 10},
    {.channel = PSG_SQUARE2, .notes = lead, .note_count = 1, .duty = PSG_DUTY_25},
    {.channel = PSG_NOISE, .notes = drums, .note_count = 2, .fade = -1},
};
static const PsgSong song = {TICK_PER_FRAME, .tracks = song_tracks, .track_count = 3};
static const PsgTrack pluck_track = {
    .channel = PSG_WAVE, .notes = pluck, .note_count = 2, .duty = 2, .fade = -1};
static const PsgSong plucked = {TICK_PER_FRAME, .tracks = &pluck_track, .track_count = 1};

static void music_plays_on_the_wave_channel(void) {
    reset();
    psg_music_play(&song);
    CHECK(psg_music_playing());
    CHECK(serval_psg_rate(PSG_WAVE) == serval_psg_wave_note_rate(PSG_C2) && wave_on());
    CHECK(serval_psg_rate(PSG_WAVE) == serval_psg_square_rates[PSG_C3]); // an octave up
    CHECK(LEVEL == LEVEL_75);                                            // volume 10
    CHECK(serval_psg_rate(PSG_SQUARE2) == serval_psg_square_rates[PSG_E5]);
    frames(1);
    CHECK(wave_on() && serval_psg_rate(PSG_WAVE) == serval_psg_wave_note_rate(PSG_C2));
    frames(1); // the rest
    CHECK(!wave_on());
    frames(1);
    CHECK(wave_on() && serval_psg_rate(PSG_WAVE) == serval_psg_wave_note_rate(PSG_G2));
    CHECK(LEVEL == LEVEL_75);
    frames(3); // loops: all three tracks are 6 ticks long
    CHECK(serval_psg_rate(PSG_WAVE) == serval_psg_wave_note_rate(PSG_C2));
    CHECK(playing_bank_holds(SQUARE_WAVE)); // .duty 1 (stops the channel)
    psg_music_stop();
    CHECK(!psg_music_playing() && !wave_on());
    frames(3);
    CHECK(!wave_on());
}

static void fading_wave_tracks_step_and_restart(void) {
    // Each note fades from 15 at a step a 64th of a second: 75% after 2
    // frames, then a new note starts at full volume.
    reset();
    psg_music_play(&plucked);
    CHECK(LEVEL == LEVEL_100 && serval_psg_rate(PSG_WAVE) == serval_psg_wave_note_rate(PSG_A1));
    frames(2);
    CHECK(LEVEL == LEVEL_75);
    frames(2);
    CHECK(LEVEL == LEVEL_100 && serval_psg_rate(PSG_WAVE) == serval_psg_wave_note_rate(PSG_E2));
    CHECK(playing_bank_holds(PULSE)); // .duty 2
    psg_stop_all();
}

static void sounds_take_over_and_the_bass_comes_back(void) {
    // A sound effect on the wave channel, with another waveform, plays over
    // the bass; the bass's held note comes back when it ends, with its own
    // waveform copied back.
    reset();
    psg_music_play(&song);
    psg_play(SND_SAW); // 4 frames, priority 0 >= the song's
    CHECK(serval_psg_rate(PSG_WAVE) == WAVE_RATE(110) && LEVEL == LEVEL_100);
    CHECK(serval_psg_rate(PSG_SQUARE2) == serval_psg_square_rates[PSG_E5]); // plays on
    frames(4); // the sound ends as the music is on G2 (from tick 3)
    CHECK(wave_on() && serval_psg_rate(PSG_WAVE) == serval_psg_wave_note_rate(PSG_G2));
    CHECK(LEVEL == LEVEL_75);
    CHECK(playing_bank_holds(SQUARE_WAVE));
    psg_stop_all();
    // The song's priority keeps lower sounds out.
    static const PsgSong important = {TICK_PER_FRAME, .priority = 1, .tracks = song_tracks,
                                      .track_count = 3};
    psg_music_play(&important);
    psg_play(SND_SAW); // priority 0 < 1
    CHECK(serval_psg_rate(PSG_WAVE) == serval_psg_wave_note_rate(PSG_C2));
    psg_play(SND_HIGH); // priority 2
    CHECK(serval_psg_rate(PSG_WAVE) == WAVE_RATE(600));
    psg_stop_all();
}

static void music_volume_scales_the_bass(void) {
    reset();
    psg_music_set_volume(8);
    psg_music_play(&song);
    CHECK(LEVEL == LEVEL_25); // 10 * 8 / 15 = 5
    psg_music_set_volume(0);  // from the next note
    frames(6);
    CHECK(LEVEL == 0);
    psg_music_set_volume(15);
    frames(6);
    CHECK(LEVEL == LEVEL_75);
    psg_stop_all();
}

static void pause_silences_the_bass(void) {
    reset();
    psg_music_play(&song);
    psg_music_pause();
    CHECK(!wave_on());
    frames(5);
    CHECK(!wave_on());
    psg_music_resume(); // a held note comes back at once
    CHECK(wave_on() && serval_psg_rate(PSG_WAVE) == serval_psg_wave_note_rate(PSG_C2));
    CHECK(LEVEL == LEVEL_75);
    psg_stop_all();
    // A fading track comes back with its next note.
    psg_music_play(&plucked);
    frames(1);
    psg_music_pause();
    psg_music_resume();
    CHECK(!wave_on());
    frames(3);
    CHECK(wave_on() && serval_psg_rate(PSG_WAVE) == serval_psg_wave_note_rate(PSG_E2));
    psg_stop_all();
}

// --- Costs ---------------------------------------------------------------------

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

#ifdef __OPTIMIZE__
#define CHECK_TIMING(cond) CHECK(cond)
#else
#define CHECK_TIMING(cond) ((void)(cond))
#endif

// The PSG step's average cost over n frames (serval_psg_update, frame_end).
static u32 update_cost(u32 n) {
    u32 total = 0;
    for (u32 i = 0; i < n; i++) {
        VBlankIntrWait();
        u32 t0 = cycles();
        serval_psg_update();
        total += cycles() - t0;
    }
    return total / n;
}

static const PsgTrack blackjack_like[] = {
    {.channel = PSG_SQUARE2, .notes = lead, .note_count = 1, .fade = -4},
    {.channel = PSG_SQUARE1, .notes = bass, .note_count = 3, .fade = -3},
    {.channel = PSG_NOISE, .notes = drums, .note_count = 2, .fade = -1},
    {.channel = PSG_WAVE, .notes = bass, .note_count = 3, .fade = -3},
};

static void costs(void) {
    reset();
    u32 idle = update_cost(60);
    // A three-track song at 150 BPM (a tick every 1.8 frames), then the same
    // with a fading bass on the wave channel too.
    PsgSong three = {.tempo = 150, .tracks = blackjack_like, .track_count = 3};
    psg_music_play(&three);
    u32 music3 = update_cost(240);
    PsgSong four = three;
    four.track_count = 4;
    psg_music_play(&four);
    u32 music4 = update_cost(240);
    psg_stop_all();
    // A note that copies its waveform, and one that doesn't.
    psg_play(SND_SAW);
    u32 t0 = cycles();
    psg_play(SND_SQUARE);
    u32 copy = cycles() - t0;
    t0 = cycles();
    psg_play(SND_SQUARE);
    u32 same = cycles() - t0;
    psg_play(SND_FADE);
    u32 fading = update_cost(20);
    psg_stop_all();
    debug_log(
        text_format("wave: PSG step %u cycles idle, %u fading on the wave channel", idle, fading));
    debug_log(text_format("wave: music %u cycles a frame with 3 tracks, %u with a wave track",
                          music3, music4));
    debug_log(text_format("wave: psg_play %u cycles with a waveform copy, %u without", copy, same));
    // Before the wave channel, the idle step was 148 cycles (RelWithDebInfo)
    // to 174 (Release), and this song 383 to 393: a channel more costs games
    // without it nothing (the step counts only the voices with frames left).
    CHECK_TIMING(idle < 150 && music3 < 400);
    CHECK_TIMING(fading < 700 && music4 < 900 && copy < 2200 && copy < same + 200);
}

TEST_SUITE(gba_wave_tests, "gba_wave",
           {"init_sends_the_wave_channel_to_the_speakers",
            init_sends_the_wave_channel_to_the_speakers},
           {"sounds_program_the_wave_channel", sounds_program_the_wave_channel},
           {"the_triangle_plays_without_a_table", the_triangle_plays_without_a_table},
           {"waveforms_go_into_the_idle_bank", waveforms_go_into_the_idle_bank},
           {"duty_past_the_table_plays_waveform_0", duty_past_the_table_plays_waveform_0},
           {"invalid_tables_are_ignored", invalid_tables_are_ignored},
           {"notes_play_at_their_rates", notes_play_at_their_rates},
           {"low_frequencies_play_at_32_hz", low_frequencies_play_at_32_hz},
           {"slides_are_ignored", slides_are_ignored},
           {"volumes_play_as_four_levels", volumes_play_as_four_levels},
           {"fades_step_between_the_levels", fades_step_between_the_levels},
           {"a_new_note_restarts_the_fade", a_new_note_restarts_the_fade},
           {"priorities_hold_on_the_wave_channel", priorities_hold_on_the_wave_channel},
           {"music_plays_on_the_wave_channel", music_plays_on_the_wave_channel},
           {"fading_wave_tracks_step_and_restart", fading_wave_tracks_step_and_restart},
           {"sounds_take_over_and_the_bass_comes_back", sounds_take_over_and_the_bass_comes_back},
           {"music_volume_scales_the_bass", music_volume_scales_the_bass},
           {"pause_silences_the_bass", pause_silences_the_bass}, {"costs", costs});
