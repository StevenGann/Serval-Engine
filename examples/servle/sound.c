// Sound effects and music on the tone generators (audio.h). The script plays
// them by number (SYS psg_play, SYS psg_music_play through vm_bind's
// psg_songs).
//
// Channels: the tune's melody is on square 2, its bass on the wave channel
// (the built-in triangle, a soft bass) and a light hi-hat on noise. Every
// effect is on square 1, which the tune doesn't use, so it plays on whole
// under them. Priorities: the ticks of typing (spin, move, erase) are 0; the
// buzz and the marks 1; the jingles (start, win, lose) 2; a reset 3. So a
// tick can't cut a mark or a jingle short, and a mark can't cut a jingle.

#include "game.h"

// Note frequencies in Hz.
#define G3 196
#define C4 262
#define D4 294
#define E4 330
#define G4 392
#define C5 523
#define E5 659
#define G5 784
#define C6 1047

static const u16 start_notes[] = {G4, C5, E5};
static const u16 win_notes[] = {C5, E5, G5, C6};
static const u16 lose_notes[] = {E4, C4, G3};

#define MARK_PRIORITY 1
#define JINGLE_PRIORITY 2

static const PsgSound sounds[SOUND_COUNT] = {
    [SND_TICK] = {.duty = PSG_DUTY_12, .frequency = 880, .frames = 2, .volume = 7},
    [SND_MOVE] = {.duty = PSG_DUTY_25, .frequency = 440, .frames = 2, .volume = 8},
    [SND_BACK] = {.duty = PSG_DUTY_25,
                  .frequency = 330,
                  .slide = -1,
                  .slide_size = 4,
                  .frames = 3,
                  .volume = 8},
    [SND_BUZZ] = {.duty = PSG_DUTY_50,
                  .frequency = 110,
                  .frames = 11,
                  .volume = 11,
                  .fade = -2,
                  .priority = MARK_PRIORITY},
    // The marks rise with the mark: G3, D4, G4.
    [SND_MARK_ABSENT] = {.duty = PSG_DUTY_25,
                         .frequency = G3,
                         .frames = 6,
                         .volume = 11,
                         .fade = -2,
                         .priority = MARK_PRIORITY},
    [SND_MARK_PRESENT] = {.duty = PSG_DUTY_25,
                          .frequency = D4,
                          .frames = 6,
                          .volume = 11,
                          .fade = -2,
                          .priority = MARK_PRIORITY},
    [SND_MARK_CORRECT] = {.duty = PSG_DUTY_25,
                          .frequency = G4,
                          .frames = 6,
                          .volume = 12,
                          .fade = -2,
                          .priority = MARK_PRIORITY},
    [SND_START] = {.duty = PSG_DUTY_25,
                   .frames = 4,
                   .notes = start_notes,
                   .note_count = 3,
                   .volume = 11,
                   .priority = JINGLE_PRIORITY},
    [SND_WIN] = {.duty = PSG_DUTY_25,
                 .frames = 6,
                 .notes = win_notes,
                 .note_count = 4,
                 .volume = 12,
                 .priority = JINGLE_PRIORITY},
    [SND_LOSE] = {.duty = PSG_DUTY_50,
                  .frames = 10,
                  .notes = lose_notes,
                  .note_count = 3,
                  .volume = 11,
                  .priority = JINGLE_PRIORITY},
    [SND_RESET] = {.duty = PSG_DUTY_50,
                   .frequency = 600,
                   .slide = -2,
                   .slide_size = 3,
                   .frames = 30,
                   .priority = 3},
};

const PsgSound* const sound_table[SOUND_COUNT] = {
    [SND_TICK] = &sounds[SND_TICK],
    [SND_MOVE] = &sounds[SND_MOVE],
    [SND_BACK] = &sounds[SND_BACK],
    [SND_BUZZ] = &sounds[SND_BUZZ],
    [SND_MARK_ABSENT] = &sounds[SND_MARK_ABSENT],
    [SND_MARK_PRESENT] = &sounds[SND_MARK_PRESENT],
    [SND_MARK_CORRECT] = &sounds[SND_MARK_CORRECT],
    [SND_START] = &sounds[SND_START],
    [SND_WIN] = &sounds[SND_WIN],
    [SND_LOSE] = &sounds[SND_LOSE],
    [SND_RESET] = &sounds[SND_RESET],
};

// --- Music -------------------------------------------------------------------
//
// "Spot the Word" (written for this example): a light, bouncy tune in G
// major, 112 beats per minute, 16 bars of 4/4 that loop, about 34 seconds. A
// tick is a 16th note, so a bar is 16 ticks. Bars 1-8: G Em C D, G Em Am D;
// bars 9-16 lift: C D Bm Em, C D G D. Quiet, to think over: the effects are
// louder.

static const PsgNote melody[] = {
    // G, Em, C, D
    {PSG_B4, 2},
    {PSG_D5, 2},
    {PSG_G5, 4},
    {PSG_FS5, 2},
    {PSG_G5, 2},
    {PSG_D5, 4},
    {PSG_E5, 4},
    {PSG_G5, 2},
    {PSG_E5, 2},
    {PSG_B4, 4},
    {PSG_REST, 4},
    {PSG_C5, 2},
    {PSG_E5, 2},
    {PSG_G5, 4},
    {PSG_A5, 2},
    {PSG_G5, 2},
    {PSG_E5, 4},
    {PSG_FS5, 4},
    {PSG_E5, 2},
    {PSG_D5, 2},
    {PSG_A4, 8},
    // G, Em, Am, D
    {PSG_B4, 2},
    {PSG_D5, 2},
    {PSG_G5, 4},
    {PSG_A5, 2},
    {PSG_B5, 2},
    {PSG_G5, 4},
    {PSG_E5, 2},
    {PSG_FS5, 2},
    {PSG_G5, 4},
    {PSG_B5, 4},
    {PSG_G5, 4},
    {PSG_A5, 4},
    {PSG_G5, 2},
    {PSG_E5, 2},
    {PSG_C5, 4},
    {PSG_E5, 4},
    {PSG_D5, 6},
    {PSG_C5, 2},
    {PSG_B4, 4},
    {PSG_A4, 4},
    // C, D, Bm, Em
    {PSG_E5, 4},
    {PSG_E5, 2},
    {PSG_G5, 2},
    {PSG_C6, 4},
    {PSG_B5, 4},
    {PSG_A5, 4},
    {PSG_FS5, 2},
    {PSG_A5, 2},
    {PSG_D6, 8},
    {PSG_B5, 2},
    {PSG_A5, 2},
    {PSG_FS5, 4},
    {PSG_D5, 2},
    {PSG_FS5, 2},
    {PSG_B5, 4},
    {PSG_G5, 4},
    {PSG_FS5, 2},
    {PSG_E5, 2},
    {PSG_B4, 8},
    // C, D, G, D
    {PSG_C5, 2},
    {PSG_E5, 2},
    {PSG_G5, 2},
    {PSG_C6, 2},
    {PSG_B5, 4},
    {PSG_G5, 4},
    {PSG_A5, 2},
    {PSG_FS5, 2},
    {PSG_D5, 4},
    {PSG_FS5, 4},
    {PSG_A5, 4},
    {PSG_G5, 4},
    {PSG_D5, 2},
    {PSG_B4, 2},
    {PSG_G5, 4},
    {PSG_FS5, 4},
    {PSG_E5, 2},
    {PSG_FS5, 2},
    {PSG_A5, 4},
    {PSG_G5, 6},
    {PSG_REST, 2},
};

// Root, fifth, octave, fifth: a quarter note each (the track's .length).
#define BASS_BAR(root, fifth, octave)                                                              \
    {(root), 0}, {(fifth), 0}, {(octave), 0}, {                                                    \
        (fifth), 0                                                                                 \
    }

static const PsgNote bass[] = {
    BASS_BAR(PSG_G2, PSG_D3, PSG_G3),  BASS_BAR(PSG_E2, PSG_B2, PSG_E3),
    BASS_BAR(PSG_C2, PSG_G2, PSG_C3),  BASS_BAR(PSG_D2, PSG_A2, PSG_D3),
    BASS_BAR(PSG_G2, PSG_D3, PSG_G3),  BASS_BAR(PSG_E2, PSG_B2, PSG_E3),
    BASS_BAR(PSG_A2, PSG_E3, PSG_A3),  BASS_BAR(PSG_D2, PSG_A2, PSG_D3),
    BASS_BAR(PSG_C2, PSG_G2, PSG_C3),  BASS_BAR(PSG_D2, PSG_A2, PSG_D3),
    BASS_BAR(PSG_B2, PSG_FS3, PSG_B3), BASS_BAR(PSG_E2, PSG_B2, PSG_E3),
    BASS_BAR(PSG_C2, PSG_G2, PSG_C3),  BASS_BAR(PSG_D2, PSG_A2, PSG_D3),
    BASS_BAR(PSG_G2, PSG_D3, PSG_G3),  BASS_BAR(PSG_D2, PSG_A2, PSG_D3),
};

// A soft tick of hiss on each off-beat.
static const PsgNote hat[] = {{PSG_REST, 2}, {PSG_C9, 2}};

static const PsgTrack theme_tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 7,
     .fade = -3,
     .notes = melody,
     .note_count = sizeof melody / sizeof melody[0]},
    {.channel = PSG_WAVE,
     .volume = 8, // the wave channel's 50%
     .fade = -4,
     .length = 4,
     .notes = bass,
     .note_count = sizeof bass / sizeof bass[0]},
    {.channel = PSG_NOISE,
     .volume = 2,
     .fade = -1,
     .notes = hat,
     .note_count = sizeof hat / sizeof hat[0]},
};

static const PsgSong theme_song = {.tempo = 112, .tracks = theme_tracks, .track_count = 3};

const PsgSong* const songs[SONG_COUNT] = {[SONG_THEME] = &theme_song};
