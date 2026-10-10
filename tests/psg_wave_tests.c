// Tests for the wave channel's portable parts (src/core/psg_wave.c): pitch,
// volume levels, the fade the engine steps, which waveform a note plays, and
// wave tracks in the music sequencer. The hardware side is
// tests/rom/wave_tests.c.

#include "../src/core/psg_sequencer.h"
#include "../src/core/psg_wave.h"
#include "serval/debug.h"
#include "test.h"

#define COUNT(a) ((u16)(sizeof(a) / sizeof((a)[0])))

#ifdef SERVAL_DEBUG
#define REPORTED_ONCE(n) CHECK((n) == 1)
#else
#define REPORTED_ONCE(n) CHECK((n) == 0)
#endif

// The waveform's 32 steps in the order they play: from the first word's
// lowest byte up, the high nibble of each byte first.
static u32 step_of(const u32* wave, u32 i) {
    u32 byte = wave[i / 8] >> (i % 8 / 2 * 8) & 0xFF;
    return i & 1 ? byte & 15 : byte >> 4;
}

static void the_triangle_rises_and_falls(void) {
    for (u32 i = 0; i < 16; i++) {
        CHECK(step_of(serval_psg_triangle, i) == i);
        CHECK(step_of(serval_psg_triangle, 31 - i) == i);
    }
}

// A note's frequency in millihertz (equal temperament, A4 = 440 Hz).
static u32 note_mhz(u32 note) {
    static const u32 octave4[12] = {261626, 277183, 293665, 311127, 329628, 349228,
                                    369994, 391995, 415305, 440000, 466164, 493883};
    u32 octave = note / 12 - 1, f = octave4[note % 12];
    return octave >= 4 ? f << (octave - 4) : f >> (4 - octave);
}

static void notes_play_an_octave_below_the_square_table(void) {
    // Up to B9, the square table's value an octave up: 65536 / f is
    // 131072 / 2f.
    for (u32 note = PSG_C1; note + 12 <= SERVAL_PSG_NOTE_MAX; note++)
        CHECK(serval_psg_wave_note_rate(note) == serval_psg_square_rates[note + 12]);
    // Every note from C1 sounds within about half a register step of its
    // pitch, up to B9 (0.55: the square table's own rounding, 0.52 for C#2,
    // and the millihertz table's); the top octave's periods are 2 to 4,
    // within a step.
    for (u32 note = PSG_C1; note <= SERVAL_PSG_NOTE_MAX; note++) {
        uint64_t period = 2048u - serval_psg_wave_note_rate(note);
        uint64_t f = note_mhz(note), got = period * f, exact = 65536000ull;
        uint64_t error = got > exact ? got - exact : exact - got;
        if (note <= PSG_B9)
            CHECK(error * 100 <= f * 55);
        else
            CHECK(error <= f);
    }
    CHECK(serval_psg_wave_note_rate(PSG_A2) == 2048 - 596); // 65536 / 110 = 595.8
    CHECK(serval_psg_wave_note_rate(PSG_C1) == 44);         // 65536 / 2004 = 32.7 Hz
    CHECK(serval_psg_wave_note_rate(PSG_B0) == 0);          // below C1: 32 Hz
    CHECK(serval_psg_wave_note_rate(PSG_B10) == 2046);      // 32 kHz for 31.6
    CHECK(serval_psg_wave_note_rate(255) == serval_psg_wave_note_rate(PSG_B10));
}

static void frequencies_play_from_32_hz(void) {
    CHECK(serval_psg_wave_rate(220) == 2048 - 65536 / 220);
    CHECK(serval_psg_wave_rate(32) == 0);
    CHECK(serval_psg_wave_rate(20) == 0); // below: 32 Hz
    CHECK(serval_psg_wave_rate(0) == 0);
    CHECK(serval_psg_wave_rate(65535) == 2047);
    CHECK(serval_psg_wave_rate(1000) == 2048 - 65);
}

static void volumes_play_as_the_nearest_level(void) {
    // SOUND3CNT_H bits 13-15: 0 silent, 3 25%, 2 50%, 4 (bit 15) 75%, 1 100%.
    static const u32 percent[8] = {0, 100, 50, 25, 75, 75, 75, 75};
    CHECK(serval_psg_wave_volume(0) == 0);
    for (u32 v = 1; v <= 15; v++) {
        u32 bits = serval_psg_wave_volume(v);
        CHECK((bits & 0x1FFF) == 0);
        u32 level = percent[bits >> 13];
        CHECK(level != 0);
        // No other level is nearer to v / 15 (ties don't occur).
        u32 error = level * 15 > v * 100 ? level * 15 - v * 100 : v * 100 - level * 15;
        for (u32 other = 25; other <= 100; other += 25) {
            u32 e = other * 15 > v * 100 ? other * 15 - v * 100 : v * 100 - other * 15;
            CHECK(error <= e);
        }
    }
    CHECK(serval_psg_wave_volume(15) == 0x2000 && serval_psg_wave_volume(10) == 0x8000);
    CHECK(serval_psg_wave_volume(9) == 0x4000 && serval_psg_wave_volume(5) == 0x6000);
}

// serval_psg_control()'s bits (psg.c): volume 12-15, fading in 11, step 8-10.
#define CONTROL(volume, up, step) ((volume) << 12 | (up) << 11 | (step) << 8)

static void fades_step_as_the_hardware_envelope(void) {
    // From 15, a step every `step` 64ths of a second: silent at frame
    // ceil(15 * step * 4096 / 4389), each step on the frame its exact time
    // falls in, as psg.c's fade_frames() predicts within its added frame.
    for (u32 step = 1; step <= 7; step++) {
        PsgWaveEnvelope e;
        serval_psg_wave_envelope_start(&e, CONTROL(15u, 0u, step));
        CHECK(e.volume == 15 && e.step == step && !e.up);
        u32 frame = 0, volume = 15;
        while (e.volume > 0 && frame < 200) {
            frame++;
            bool changed = serval_psg_wave_envelope_step(&e);
            CHECK(changed == (e.volume != volume));
            // Steps so far: the 64ths of a second elapsed, over the step.
            u32 steps = frame * SERVAL_PSG_WAVE_FRAME_UNITS / (step * SERVAL_PSG_WAVE_STEP_UNITS);
            CHECK(e.volume == (steps >= 15 ? 0 : 15 - steps));
            volume = e.volume;
        }
        u32 silent = (15 * step * SERVAL_PSG_WAVE_STEP_UNITS + SERVAL_PSG_WAVE_FRAME_UNITS - 1) /
                     SERVAL_PSG_WAVE_FRAME_UNITS;
        CHECK(frame == silent);
        // psg.c holds the channel for fade_frames(): never less, at most 2
        // frames more (it rounds 4096 / 4389 up to 239 / 256, and adds one).
        u32 fade_frames = ((15 * step * 239 + 255) >> 8) + 1;
        CHECK(fade_frames >= frame && fade_frames <= frame + 2);
        CHECK(e.step == 0); // it stops at 0
        CHECK(!serval_psg_wave_envelope_step(&e) && e.volume == 0);
    }
}

static void fades_in_rise_to_15_and_hold(void) {
    PsgWaveEnvelope e;
    serval_psg_wave_envelope_start(&e, CONTROL(0u, 1u, 2u)); // from silence
    CHECK(e.volume == 0 && e.up && e.step == 2);
    u32 frames = 0;
    while (e.step && frames < 100) {
        serval_psg_wave_envelope_step(&e);
        frames++;
    }
    CHECK(e.volume == 15 && frames == 28); // 30 64ths of a second: 27.99 frames
    // Held volumes, and fades with nowhere to go, don't step.
    serval_psg_wave_envelope_start(&e, CONTROL(9u, 0u, 0u));
    for (u32 i = 0; i < 50; i++)
        CHECK(!serval_psg_wave_envelope_step(&e));
    CHECK(e.volume == 9);
    serval_psg_wave_envelope_start(&e, CONTROL(15u, 1u, 3u));
    CHECK(e.step == 0 && !serval_psg_wave_envelope_step(&e));
    serval_psg_wave_envelope_start(&e, CONTROL(0u, 0u, 3u));
    CHECK(e.step == 0 && !serval_psg_wave_envelope_step(&e) && e.volume == 0);
}

static void fades_never_drift(void) {
    // Step 7 from 15, restarted 50 times: each fade takes 15 * 7 / 64 s, 98
    // frames (97.99), and a restart starts the timer afresh.
    for (u32 n = 0; n < 50; n++) {
        PsgWaveEnvelope e;
        serval_psg_wave_envelope_start(&e, CONTROL(15u, 0u, 7u));
        u32 frames = 0;
        while (e.step) {
            serval_psg_wave_envelope_step(&e);
            frames++;
        }
        CHECK(frames == 98);
    }
}

static const u32 waves[8] = {0x11111111, 0x22222222, 0x33333333, 0x44444444,
                             0x55555555, 0x66666666, 0x77777777, 0x88888888};

static void duty_picks_the_waveform(void) {
    serval_psg_wave_reset_warnings();
    u32 before = debug_warning_count();
    CHECK(serval_psg_wave_pick(NULL, 0, 0) == serval_psg_triangle);
    CHECK(serval_psg_wave_pick(waves, 2, 0) == waves);
    CHECK(serval_psg_wave_pick(waves, 2, 1) == waves + 4);
    CHECK(debug_warning_count() == before);
    // Past the table: waveform 0, reported once.
    CHECK(serval_psg_wave_pick(waves, 2, 2) == waves);
    CHECK(serval_psg_wave_pick(waves, 2, 200) == waves);
    REPORTED_ONCE(debug_warning_count() - before);
    // With none registered, every .duty plays the triangle; past 0 it warns.
    serval_psg_wave_reset_warnings();
    before = debug_warning_count();
    CHECK(serval_psg_wave_pick(NULL, 0, 3) == serval_psg_triangle);
    CHECK(serval_psg_wave_pick(waves, 0, 1) == serval_psg_triangle);
    REPORTED_ONCE(debug_warning_count() - before);
}

// --- Wave tracks in the sequencer --------------------------------------------

static const PsgNote bass[] = {{PSG_C1, 2}, {PSG_G1, 1}, {PSG_REST, 1}};
static const PsgNote lead[] = {{PSG_C5, 4}};
static const PsgNote low[] = {{PSG_B0, 1}};
static const PsgTrack four_tracks[] = {
    {.channel = PSG_WAVE, .notes = bass, .note_count = COUNT(bass), .duty = 1},
    {.channel = PSG_SQUARE1, .notes = lead, .note_count = COUNT(lead)},
    {.channel = PSG_SQUARE2, .notes = lead, .note_count = COUNT(lead)},
    {.channel = PSG_NOISE, .notes = lead, .note_count = COUNT(lead)},
    {.channel = PSG_WAVE, .notes = lead, .note_count = COUNT(lead)}, // a second: left out
};
static const PsgSong four = {
    .tempo = 3583, .ticks_per_beat = 1, .tracks = four_tracks, .track_count = COUNT(four_tracks)};

static void wave_tracks_play_in_songs(void) {
    serval_psg_seq_reset_warnings();
    PsgSequencer seq;
    u32 before = debug_warning_count();
    CHECK(serval_psg_seq_start(&seq, &four) == 0xFu);
    REPORTED_ONCE(debug_warning_count() - before); // the second wave track
    CHECK(seq.tracks[PSG_WAVE].track == &four_tracks[0] && seq.tracks[PSG_WAVE].note == PSG_C1);
    // A tick per frame: C1 lasts 2, G1 and the rest 1 each, the others' C5 4.
    CHECK(serval_psg_seq_advance(&seq) == 0);
    CHECK(serval_psg_seq_advance(&seq) == 1u << PSG_WAVE);
    CHECK(seq.tracks[PSG_WAVE].note == PSG_G1);
    CHECK(serval_psg_seq_advance(&seq) == 1u << PSG_WAVE);
    CHECK(seq.tracks[PSG_WAVE].note == PSG_REST);
    CHECK(serval_psg_seq_advance(&seq) == 0xFu);
    CHECK(seq.tracks[PSG_WAVE].note == PSG_C1); // looped
    CHECK(serval_psg_seq_channels(&seq) == 0xFu);
}

static void low_wave_notes_are_reported(void) {
    // C1 is the wave channel's lowest note; below it plays as 32 Hz (warns).
    // A square track's C1 is below its C2 (warns too); the wave's C1 isn't.
    static const PsgTrack c1 = {.channel = PSG_WAVE, .notes = bass, .note_count = COUNT(bass)};
    static const PsgTrack b0 = {.channel = PSG_WAVE, .notes = low, .note_count = 1};
    static const PsgTrack square = {.channel = PSG_SQUARE1, .notes = bass, .note_count = 1};
    PsgSong song = {.tracks = &c1, .track_count = 1};
    PsgSequencer seq;
    serval_psg_seq_reset_warnings();
    u32 before = debug_warning_count();
    CHECK(serval_psg_seq_start(&seq, &song) == 1u << PSG_WAVE);
    CHECK(debug_warning_count() == before);
    song.tracks = &b0;
    CHECK(serval_psg_seq_start(&seq, &song) == 1u << PSG_WAVE);
    CHECK(serval_psg_seq_start(&seq, &song) == 1u << PSG_WAVE);
    REPORTED_ONCE(debug_warning_count() - before);
    serval_psg_seq_reset_warnings();
    before = debug_warning_count();
    song.tracks = &square;
    CHECK(serval_psg_seq_start(&seq, &song) == 1u << PSG_SQUARE1);
    REPORTED_ONCE(debug_warning_count() - before);
}

static void wave_tracks_take_any_duty(void) {
    // A wave track's .duty is a waveform's number, checked as it plays, not a
    // PSG_DUTY_* (which a square track past PSG_DUTY_75 is warned about).
    static const PsgTrack wave = {
        .channel = PSG_WAVE, .notes = bass, .note_count = COUNT(bass), .duty = 9};
    static const PsgTrack square = {
        .channel = PSG_SQUARE2, .notes = lead, .note_count = COUNT(lead), .duty = 9};
    PsgSong song = {.tracks = &wave, .track_count = 1};
    PsgSequencer seq;
    serval_psg_seq_reset_warnings();
    u32 before = debug_warning_count();
    CHECK(serval_psg_seq_start(&seq, &song) == 1u << PSG_WAVE);
    CHECK(debug_warning_count() == before);
    song.tracks = &square;
    CHECK(serval_psg_seq_start(&seq, &song) == 1u << PSG_SQUARE2);
    REPORTED_ONCE(debug_warning_count() - before);
}

TEST_SUITE(
    psg_wave_tests, "psg_wave", {"the_triangle_rises_and_falls", the_triangle_rises_and_falls},
    {"notes_play_an_octave_below_the_square_table", notes_play_an_octave_below_the_square_table},
    {"frequencies_play_from_32_hz", frequencies_play_from_32_hz},
    {"volumes_play_as_the_nearest_level", volumes_play_as_the_nearest_level},
    {"fades_step_as_the_hardware_envelope", fades_step_as_the_hardware_envelope},
    {"fades_in_rise_to_15_and_hold", fades_in_rise_to_15_and_hold},
    {"fades_never_drift", fades_never_drift}, {"duty_picks_the_waveform", duty_picks_the_waveform},
    {"wave_tracks_play_in_songs", wave_tracks_play_in_songs},
    {"low_wave_notes_are_reported", low_wave_notes_are_reported},
    {"wave_tracks_take_any_duty", wave_tracks_take_any_duty});
