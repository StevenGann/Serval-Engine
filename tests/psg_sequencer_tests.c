// Tests for the PSG music sequencer (src/core/psg_sequencer.c): note tables,
// tempo and the frames notes start on, loops, and song checks.

#include "../src/core/psg_sequencer.h"
#include "serval/debug.h"
#include "test.h"

#include <stddef.h>

#define COUNT(a) ((u16)(sizeof(a) / sizeof((a)[0])))

#ifdef SERVAL_DEBUG
#define REPORTED_ONCE(n) CHECK((n) == 1)
#else
#define REPORTED_ONCE(n) CHECK((n) == 0)
#endif

// Octave 4 in millihertz (C4 to B4, A4 = 440 Hz).
static const u32 octave4_mhz[12] = {261626, 277183, 293665, 311127, 329628, 349228,
                                    369994, 391995, 415305, 440000, 466164, 493883};

// A note's frequency in millihertz (notes from C2).
static u32 note_mhz(u32 note) {
    u32 octave = note / 12 - 1, f = octave4_mhz[note % 12];
    return octave >= 4 ? f << (octave - 4) : f >> (4 - octave);
}

static void square_rates_follow_the_scale(void) {
    // Period 2048 - n is 131072 / f, rounded: within half a period step.
    for (u32 note = PSG_C2; note <= SERVAL_PSG_NOTE_MAX; note++) {
        uint64_t period = 2048u - serval_psg_square_rates[note];
        uint64_t f = note_mhz(note);
        uint64_t exact = 131072000ull; // period * f in mHz, if exact
        uint64_t got = period * f;
        uint64_t error = got > exact ? got - exact : exact - got;
        CHECK(error * 100 <= f * 51); // half a step, plus rounding in the mHz table
    }
    CHECK(serval_psg_square_rates[PSG_A4] == 2048 - 298); // 131072 / 440 = 297.9
    CHECK(serval_psg_square_rates[PSG_C2 - 1] == 0);      // below C2: the lowest, 64 Hz
}

// The noise rate in Hz for a register value s << 4 | r.
static u32 noise_hz(u32 rate) {
    u32 r = rate & 7, s = rate >> 4;
    return r ? (524288u / r) >> (s + 1) : 1048576u >> (s + 1);
}

static void noise_rates_are_the_closest(void) {
    for (u32 note = PSG_C2; note <= SERVAL_PSG_NOTE_MAX; note += 5) {
        u32 hz = (note_mhz(note) + 500) / 1000;
        u32 got = noise_hz(serval_psg_noise_rates[note]);
        u32 got_error = got > hz ? got - hz : hz - got;
        for (u32 s = 0; s < 14; s++) {
            for (u32 r = 0; r < 8; r++) {
                u32 f = noise_hz(s << 4 | r);
                CHECK(got_error <= (f > hz ? f - hz : hz - f));
            }
        }
    }
    // Higher notes, higher noise.
    CHECK(noise_hz(serval_psg_noise_rates[PSG_C7]) > noise_hz(serval_psg_noise_rates[PSG_C3]));
}

static void tempo_sets_ticks_per_frame(void) {
    // 120 BPM, 4 ticks a beat: 8 ticks a second at 59.7275 frames a second.
    CHECK(serval_psg_tick_step(0, 0) == 8778);
    CHECK(serval_psg_tick_step(120, 4) == 8778);
    CHECK(serval_psg_tick_step(60, 8) == 8778);
    CHECK(serval_psg_tick_step(3583, 1) <= SERVAL_PSG_TICK); // just under a tick per frame
    u32 before = debug_warning_count();
    serval_psg_seq_reset_warnings();
    CHECK(serval_psg_tick_step(240, 16) == SERVAL_PSG_TICK); // too fast: clamped
    CHECK(serval_psg_tick_step(240, 16) == SERVAL_PSG_TICK);
    REPORTED_ONCE(debug_warning_count() - before);
}

// Runs frames until a channel changes note; returns how many it took.
static u32 frames_until_change(PsgSequencer* seq, u32 channel) {
    for (u32 f = 1; f < 10000; f++)
        if (serval_psg_seq_advance(seq) & 1u << channel)
            return f;
    return 0;
}

static const PsgNote melody[] = {{PSG_C4, 1}, {PSG_E4, 2}, {PSG_REST, 1}, {PSG_G4, 0}};
static const PsgNote beat[] = {{PSG_C3, 0}, {PSG_C7, 0}};
static const PsgTrack tracks[] = {
    {.channel = PSG_SQUARE2, .notes = melody, .note_count = COUNT(melody), .length = 3},
    {.channel = PSG_NOISE, .notes = beat, .note_count = COUNT(beat)}, // a beat (2 ticks) each
};
// 3583 ticks a minute: a tick per frame.
static const PsgSong song = {
    .tempo = 3583, .ticks_per_beat = 1, .tracks = tracks, .track_count = COUNT(tracks)};
static const PsgSong two_tick_beats = {
    .tempo = 1791, .ticks_per_beat = 2, .tracks = tracks, .track_count = COUNT(tracks)};

static void notes_start_on_their_ticks(void) {
    PsgSequencer seq;
    CHECK(serval_psg_seq_start(&seq, &song) == (1u << PSG_SQUARE2 | 1u << PSG_NOISE));
    CHECK(seq.tracks[PSG_SQUARE2].note == PSG_C4 && seq.tracks[PSG_NOISE].note == PSG_C3);
    CHECK(seq.tracks[PSG_SQUARE1].track == NULL);
    CHECK(frames_until_change(&seq, PSG_SQUARE2) == 1); // C4: 1 tick
    CHECK(seq.tracks[PSG_SQUARE2].note == PSG_E4);
    CHECK(frames_until_change(&seq, PSG_SQUARE2) == 2); // E4: 2 ticks
    CHECK(seq.tracks[PSG_SQUARE2].note == PSG_REST);
    CHECK(frames_until_change(&seq, PSG_SQUARE2) == 1);
    CHECK(seq.tracks[PSG_SQUARE2].note == PSG_G4);
    CHECK(frames_until_change(&seq, PSG_SQUARE2) == 3); // length 0: the track's .length
    CHECK(seq.tracks[PSG_SQUARE2].note == PSG_C4);      // looped
    CHECK(serval_psg_seq_channels(&seq) == (1u << PSG_SQUARE2 | 1u << PSG_NOISE));
}

static void default_length_is_a_beat(void) {
    PsgSequencer seq;
    serval_psg_seq_start(&seq, &two_tick_beats); // still a tick per frame, 2 ticks a beat
    CHECK(frames_until_change(&seq, PSG_NOISE) == 2);
    CHECK(seq.tracks[PSG_NOISE].note == PSG_C7);
    CHECK(frames_until_change(&seq, PSG_NOISE) == 2);
    CHECK(seq.tracks[PSG_NOISE].note == PSG_C3);
}

static void tracks_loop_independently(void) {
    // The melody loops every 7 ticks, the beat every 2: after 28 frames both
    // are back on their first notes, having changed on their own ticks.
    PsgSequencer seq;
    serval_psg_seq_start(&seq, &two_tick_beats);
    u32 melody_changes = 0, beat_changes = 0;
    for (u32 f = 0; f < 28; f++) {
        u32 changed = serval_psg_seq_advance(&seq);
        melody_changes += (changed >> PSG_SQUARE2) & 1;
        beat_changes += (changed >> PSG_NOISE) & 1;
    }
    CHECK(melody_changes == 16 && beat_changes == 14);
    CHECK(seq.tracks[PSG_SQUARE2].note == PSG_C4 && seq.tracks[PSG_SQUARE2].index == 0);
    CHECK(seq.tracks[PSG_NOISE].note == PSG_C3);
}

static void real_tempo_rounds_to_the_closest_frame(void) {
    // 120 BPM, 16th notes: a tick every 7.466 frames. Tick k starts on the
    // frame closest to k * 7.466, and stays in time over a long song.
    static const PsgNote ticks[] = {{PSG_A4, 1}};
    static const PsgTrack one = {.notes = ticks, .note_count = 1};
    static const PsgSong slow = {.tracks = &one, .track_count = 1};
    PsgSequencer seq;
    serval_psg_seq_start(&seq, &slow);
    u32 frame = 0;
    for (u32 k = 1; k <= 2000; k++) {
        frame += frames_until_change(&seq, PSG_SQUARE1);
        // Exactly k * 2^24 / 8 / 280896 = k * 32768 / 4389 frames (a frame is
        // 280896 cycles at 2^24 Hz): within half a frame.
        u32 got = frame * 4389, exact = k * 32768;
        CHECK((got > exact ? got - exact : exact - got) <= 4389 / 2 + 2);
        if (k == 1)
            CHECK(frame == 7);
        if (k == 2)
            CHECK(frame == 15);
        if (k == 3)
            CHECK(frame == 22);
    }
}

// A song of one-tick notes at 120 BPM changes tempo at frame change_at: it
// keeps its place, and each later tick falls on the frame closest to its time
// at the new tempo (in 16.16 ticks, as the sequencer counts them).
static void check_tempo_change(u32 change_at, u16 new_tempo) {
    static const PsgNote ticks[] = {{PSG_A4, 1}};
    static const PsgTrack one = {.notes = ticks, .note_count = 1};
    static const PsgSong slow = {.tracks = &one, .track_count = 1};
    PsgSequencer seq;
    serval_psg_seq_start(&seq, &slow);
    u32 done = 0;
    for (u32 f = 1; f <= change_at; f++)
        done += (serval_psg_seq_advance(&seq) >> PSG_SQUARE1) & 1;
    uint64_t old_step = seq.step;
    serval_psg_seq_set_tempo(&seq, new_tempo);
    uint64_t step = seq.step;
    CHECK(step == serval_psg_tick_step(new_tempo, 0));
    // Where the song is, exactly: change_at frames at the old tempo.
    uint64_t position = change_at * old_step;
    u32 frame = change_at;
    for (u32 k = done + 1; k <= done + 300; k++) {
        frame += frames_until_change(&seq, PSG_SQUARE1);
        // Where tick k is, against where the frame it fell on is.
        uint64_t exact = (uint64_t)k * SERVAL_PSG_TICK;
        uint64_t got = position + (frame - change_at) * step;
        // Within half a frame; the first tick may come up to a frame late (a
        // tick due within half a frame of the change plays in the next one).
        uint64_t error = got > exact ? got - exact : exact - got;
        bool in_time = error <= (k == done + 1 ? step : step / 2 + 1);
        CHECK(in_time);
        if (!in_time)
            return; // one failure per tempo change is enough
    }
}

static void tempo_changes_keep_the_place(void) {
    for (u32 at = 1; at <= 16; at++) { // every phase within a tick or two
        check_tempo_change(at, 240);
        check_tempo_change(at, 150);
        check_tempo_change(at, 77);
    }
}

static void tempo_change_takes_effect_at_once(void) {
    // A tick per frame, halved: the note playing (C4, 1 tick, just started)
    // lasts 2 frames, then 2 ticks of E4 take 4.
    PsgSequencer seq;
    serval_psg_seq_start(&seq, &song);
    serval_psg_seq_set_tempo(&seq, 1791);
    CHECK(frames_until_change(&seq, PSG_SQUARE2) == 2);
    CHECK(seq.tracks[PSG_SQUARE2].note == PSG_E4);
    CHECK(frames_until_change(&seq, PSG_SQUARE2) == 4);
    // 0: the song's own tempo again, a tick per frame.
    serval_psg_seq_set_tempo(&seq, 0);
    CHECK(frames_until_change(&seq, PSG_SQUARE2) == 1);
    CHECK(frames_until_change(&seq, PSG_SQUARE2) == 3);
    // Starting a song resets it.
    serval_psg_seq_set_tempo(&seq, 600);
    serval_psg_seq_start(&seq, &song);
    CHECK(seq.step == serval_psg_tick_step(3583, 1));
    CHECK(frames_until_change(&seq, PSG_SQUARE2) == 1);
    // Too fast: a tick per frame, reported once.
    serval_psg_seq_reset_warnings();
    u32 before = debug_warning_count();
    serval_psg_seq_set_tempo(&seq, 4000);
    serval_psg_seq_set_tempo(&seq, 4000);
    REPORTED_ONCE(debug_warning_count() - before);
    CHECK(seq.step == SERVAL_PSG_TICK);
    CHECK(frames_until_change(&seq, PSG_SQUARE2) == 2);
}

static void songs_that_dont_loop_end(void) {
    static const PsgNote once[] = {{PSG_C5, 2}, {PSG_D5, 1}};
    static const PsgTrack track = {
        .channel = PSG_SQUARE1, .notes = once, .note_count = 2, .loop = PSG_NO_LOOP};
    static const PsgSong jingle = {
        .tempo = 3583, .ticks_per_beat = 1, .tracks = &track, .track_count = 1};
    PsgSequencer seq;
    serval_psg_seq_start(&seq, &jingle);
    CHECK(frames_until_change(&seq, PSG_SQUARE1) == 2);
    CHECK(frames_until_change(&seq, PSG_SQUARE1) == 1);
    CHECK(seq.tracks[PSG_SQUARE1].note == PSG_REST && seq.tracks[PSG_SQUARE1].track == NULL);
    CHECK(serval_psg_seq_channels(&seq) == 0);
    CHECK(serval_psg_seq_advance(&seq) == 0);
}

static void loop_point_skips_the_intro(void) {
    static const PsgNote notes[] = {{PSG_C5, 5}, {PSG_E5, 1}, {PSG_G5, 1}};
    static const PsgTrack track = {.notes = notes, .note_count = 3, .loop = 1};
    static const PsgSong song_with_intro = {
        .tempo = 3583, .ticks_per_beat = 1, .tracks = &track, .track_count = 1};
    PsgSequencer seq;
    serval_psg_seq_start(&seq, &song_with_intro);
    CHECK(frames_until_change(&seq, PSG_SQUARE1) == 5);
    CHECK(frames_until_change(&seq, PSG_SQUARE1) == 1);
    CHECK(frames_until_change(&seq, PSG_SQUARE1) == 1);
    CHECK(seq.tracks[PSG_SQUARE1].note == PSG_E5); // back to note 1, not the intro
}

// Songs with mistakes: each reported once (debug builds), and played safely.
static const PsgNote low[] = {{PSG_C1, 1}};
static const PsgNote high[] = {{200, 1}};
static const PsgTrack bad_tracks[] = {
    {.channel = 7, .notes = low, .note_count = 1},                       // invalid channel
    {.channel = PSG_SQUARE2, .notes = low, .note_count = 1},             // below C2
    {.channel = PSG_SQUARE2, .notes = high, .note_count = 1},            // same channel
    {.channel = PSG_NOISE, .note_count = 3},                             // no notes
    {.channel = PSG_SQUARE1, .notes = high, .note_count = 1, .loop = 4}, // loop, high note
};

static void song_mistakes_are_reported_and_safe(void) {
    static const PsgSong bad = {.tracks = bad_tracks, .track_count = COUNT(bad_tracks)};
    serval_psg_seq_reset_warnings();
    PsgSequencer seq;
    u32 before = debug_warning_count();
    CHECK(serval_psg_seq_start(&seq, &bad) == (1u << PSG_SQUARE1 | 1u << PSG_SQUARE2));
#ifdef SERVAL_DEBUG
    // Invalid channel, low note, duplicate channel, no notes, loop, high note.
    CHECK(debug_warning_count() - before == 6);
#endif
    before = debug_warning_count();
    serval_psg_seq_start(&seq, &bad); // each problem only once
    CHECK(debug_warning_count() == before);
    CHECK(seq.tracks[PSG_SQUARE2].note == PSG_C1);              // plays as the lowest square
    CHECK(seq.tracks[PSG_SQUARE1].note == SERVAL_PSG_NOTE_MAX); // clamped
    CHECK(seq.tracks[PSG_SQUARE1].loop == 0);
    for (u32 f = 0; f < 10; f++)
        serval_psg_seq_advance(&seq);
    CHECK(seq.tracks[PSG_SQUARE1].index == 0); // looped from the start

    before = debug_warning_count();
    CHECK(serval_psg_seq_start(&seq, NULL) == 0);
    REPORTED_ONCE(debug_warning_count() - before);
    CHECK(serval_psg_seq_channels(&seq) == 0);
    static const PsgSong no_tracks = {.track_count = 2};
    before = debug_warning_count();
    CHECK(serval_psg_seq_start(&seq, &no_tracks) == 0);
    REPORTED_ONCE(debug_warning_count() - before);
    static const PsgSong empty = {.tempo = 100, .tracks = bad_tracks}; // .track_count 0
    before = debug_warning_count();
    CHECK(serval_psg_seq_start(&seq, &empty) == 0);
    CHECK(serval_psg_seq_start(&seq, &empty) == 0);
    REPORTED_ONCE(debug_warning_count() - before);
}

TEST_SUITE(psg_sequencer_tests, "psg_sequencer",
           {"square_rates_follow_the_scale", square_rates_follow_the_scale},
           {"noise_rates_are_the_closest", noise_rates_are_the_closest},
           {"tempo_sets_ticks_per_frame", tempo_sets_ticks_per_frame},
           {"notes_start_on_their_ticks", notes_start_on_their_ticks},
           {"default_length_is_a_beat", default_length_is_a_beat},
           {"tracks_loop_independently", tracks_loop_independently},
           {"real_tempo_rounds_to_the_closest_frame", real_tempo_rounds_to_the_closest_frame},
           {"tempo_changes_keep_the_place", tempo_changes_keep_the_place},
           {"tempo_change_takes_effect_at_once", tempo_change_takes_effect_at_once},
           {"songs_that_dont_loop_end", songs_that_dont_loop_end},
           {"loop_point_skips_the_intro", loop_point_skips_the_intro},
           {"song_mistakes_are_reported_and_safe", song_mistakes_are_reported_and_safe});
