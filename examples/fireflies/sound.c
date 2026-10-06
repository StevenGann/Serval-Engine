// Sound effects and music on the tone generators (audio.h). The scripts play
// them by number (SYS psg_play, SYS music_play through vm_bind's songs).
//
// Channels: the music's melody is on square 2, its bass on square 1 and
// crickets on noise. A catch's chime and the last seconds' ticks are on
// square 1, so the melody plays on through them (the bass drops out for a
// moment); the start chime, the jingle for every tenth catch and the time-up
// phrase are on square 2 with a higher priority, so a chime can't cut them
// short.

#include "game.h"

// Note frequencies in Hz.
#define G4 392
#define C5 523
#define E5 659
#define G5 784
#define C6 1047
#define E6 1319
#define G6 1568
#define B6 1976
#define C7 2093
#define E7 2637

static const u16 start_notes[] = {C5, E5, G5, C6};
static const u16 chime_notes[] = {E6, B6};
static const u16 jingle_notes[] = {C6, E6, G6, C7, E7};
static const u16 time_up_notes[] = {G5, E5, C5, 0, G4};

#define JINGLE_PRIORITY 1

static const PsgSound sounds[SOUND_COUNT] = {
    [SND_START] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_25,
                   .frames = 5,
                   .notes = start_notes,
                   .note_count = 4,
                   .volume = 10,
                   .priority = JINGLE_PRIORITY},
    [SND_CHIME] = {.duty = PSG_DUTY_12,
                   .frames = 4,
                   .notes = chime_notes,
                   .note_count = 2,
                   .volume = 12,
                   .fade = -2},
    [SND_JINGLE] = {.channel = PSG_SQUARE2,
                    .duty = PSG_DUTY_12,
                    .frames = 5,
                    .notes = jingle_notes,
                    .note_count = 5,
                    .volume = 11,
                    .priority = JINGLE_PRIORITY},
    [SND_TICK] = {.duty = PSG_DUTY_12, .frequency = 1760, .frames = 3, .volume = 8},
    [SND_TIME_UP] = {.channel = PSG_SQUARE2,
                     .duty = PSG_DUTY_25,
                     .frames = 12,
                     .notes = time_up_notes,
                     .note_count = 5,
                     .volume = 12,
                     .fade = -7,
                     .priority = JINGLE_PRIORITY},
};

const PsgSound* const sound_table[SOUND_COUNT] = {
    [SND_START] = &sounds[SND_START],     [SND_CHIME] = &sounds[SND_CHIME],
    [SND_JINGLE] = &sounds[SND_JINGLE],   [SND_TICK] = &sounds[SND_TICK],
    [SND_TIME_UP] = &sounds[SND_TIME_UP],
};

// --- Music -------------------------------------------------------------------
//
// "Dusk Meadow" (written for this example): A minor into C major, 88 beats
// per minute, 16 bars of 4/4 that loop, about 44 seconds. A tick is a 16th
// note, so a bar is 16 ticks. A soft melody over a bass of half notes, and
// crickets: a pair of short high hisses once a bar. Bars 1-8 rise and settle
// (Am F C G, Am F G C); bars 9-16 climb higher (F C Dm G, Am F G C).

static const PsgNote melody[] = {
    // Am, F, C, G
    {PSG_E5, 4},
    {PSG_A5, 4},
    {PSG_G5, 2},
    {PSG_E5, 2},
    {PSG_D5, 4},
    {PSG_C5, 6},
    {PSG_D5, 2},
    {PSG_E5, 4},
    {PSG_C5, 4},
    {PSG_G4, 4},
    {PSG_C5, 4},
    {PSG_E5, 4},
    {PSG_G5, 4},
    {PSG_D5, 8},
    {PSG_B4, 4},
    {PSG_REST, 4},
    // Am, F, G, C
    {PSG_E5, 4},
    {PSG_A5, 4},
    {PSG_B5, 2},
    {PSG_A5, 2},
    {PSG_G5, 4},
    {PSG_A5, 6},
    {PSG_G5, 2},
    {PSG_F5, 4},
    {PSG_E5, 4},
    {PSG_D5, 4},
    {PSG_E5, 2},
    {PSG_D5, 2},
    {PSG_B4, 4},
    {PSG_G4, 4},
    {PSG_C5, 12},
    {PSG_REST, 4},
    // F, C, Dm, G
    {PSG_A5, 4},
    {PSG_C6, 4},
    {PSG_A5, 4},
    {PSG_F5, 4},
    {PSG_G5, 6},
    {PSG_E5, 2},
    {PSG_C5, 8},
    {PSG_D5, 4},
    {PSG_F5, 4},
    {PSG_A5, 4},
    {PSG_D6, 4},
    {PSG_B5, 8},
    {PSG_G5, 4},
    {PSG_REST, 4},
    // Am, F, G, C
    {PSG_C6, 4},
    {PSG_B5, 2},
    {PSG_A5, 2},
    {PSG_E5, 8},
    {PSG_F5, 4},
    {PSG_A5, 4},
    {PSG_C6, 4},
    {PSG_A5, 4},
    {PSG_G5, 4},
    {PSG_B5, 4},
    {PSG_D6, 4},
    {PSG_B5, 2},
    {PSG_G5, 2},
    {PSG_C6, 12},
    {PSG_REST, 4},
};

// Root then fifth, a half note each (the track's .length of 8 ticks).
#define BASS_BAR(root, fifth)                                                                      \
    {(root), 0}, {                                                                                 \
        (fifth), 0                                                                                 \
    }

static const PsgNote bass[] = {
    BASS_BAR(PSG_A2, PSG_E3), BASS_BAR(PSG_F2, PSG_C3), BASS_BAR(PSG_C3, PSG_G2),
    BASS_BAR(PSG_G2, PSG_D3), BASS_BAR(PSG_A2, PSG_E3), BASS_BAR(PSG_F2, PSG_C3),
    BASS_BAR(PSG_G2, PSG_D3), BASS_BAR(PSG_C3, PSG_G2), BASS_BAR(PSG_F2, PSG_C3),
    BASS_BAR(PSG_C3, PSG_G2), BASS_BAR(PSG_D3, PSG_A2), BASS_BAR(PSG_G2, PSG_D3),
    BASS_BAR(PSG_A2, PSG_E3), BASS_BAR(PSG_F2, PSG_C3), BASS_BAR(PSG_G2, PSG_D3),
    BASS_BAR(PSG_C3, PSG_G2),
};

// A bar of crickets: two short chirps of hiss, then quiet.
static const PsgNote crickets[] = {
    {PSG_REST, 6}, {PSG_C8, 1}, {PSG_REST, 1}, {PSG_C8, 1}, {PSG_REST, 7},
};

static const PsgTrack dusk_tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 9,
     .fade = -5,
     .notes = melody,
     .note_count = sizeof melody / sizeof melody[0]},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 6,
     .fade = -6,
     .length = 8,
     .notes = bass,
     .note_count = sizeof bass / sizeof bass[0]},
    {.channel = PSG_NOISE,
     .volume = 3,
     .fade = -1,
     .notes = crickets,
     .note_count = sizeof crickets / sizeof crickets[0]},
};

static const PsgSong dusk_song = {.tempo = 88, .tracks = dusk_tracks, .track_count = 3};

const PsgSong* const songs[SONG_COUNT] = {[SONG_DUSK] = &dusk_song};
