// Stage 1-4's music and sound effects (audio.h). The music (PsgSong) uses
// all three channels, as the other stages' does: melody on square 2, bass on
// square 1, drums on noise. When time runs low the game speeds it up from
// 120 to 150 beats per minute where it is (stage_castle.c's hurry_tempo).

#include "stage_castle.h"

// The stage's sound effects: IDs SND_STAGE(STAGE_CASTLE) + n
// (stage_castle.h). The dragon's on the noise channel and square 2, so the
// serval's own sounds (square 1) still play over them.
static const u16 sting_notes[] = {1047, 988, 0, 784, 740, 0, 523}; // C6 B5, G5 F#5, C5
static const u16 lever_notes[] = {196, 0, 147};                    // a clunk: G3, D3

const PsgSound castle_sounds[STAGE_SOUNDS] = {
    // SND_ROAR: a long, low rumble as the dragon wakes.
    [0] = {.channel = PSG_NOISE, .frequency = 160, .volume = 15, .fade = -6},
    // SND_STING: a few falling notes of alarm with it, which the short
    // effects don't cut.
    [1] = {.channel = PSG_SQUARE2,
           .duty = PSG_DUTY_25,
           .frames = 7,
           .notes = sting_notes,
           .note_count = sizeof sting_notes / sizeof sting_notes[0],
           .volume = 11,
           .priority = JINGLE_PRIORITY},
    // SND_BREATH: a fireball, a rushing hiss.
    [2] = {.channel = PSG_NOISE, .frequency = 1800, .volume = 13, .fade = -2},
    // SND_HOP: the dragon lands, a heavy thud.
    [3] = {.channel = PSG_NOISE, .frequency = 90, .volume = 15, .fade = -2},
    // SND_LEVER: the lever's clunk.
    [4] = {.channel = PSG_SQUARE2,
           .duty = PSG_DUTY_50,
           .frames = 4,
           .notes = lever_notes,
           .note_count = sizeof lever_notes / sizeof lever_notes[0],
           .volume = 13,
           .fade = -1},
    // SND_CRUMBLE: a piece of the bridge breaking away.
    [5] = {.channel = PSG_NOISE, .frequency = 420, .frames = 8, .volume = 14, .fade = -1},
    // SND_SPLASH: the dragon into the lava, a long, deep hiss.
    [6] = {.channel = PSG_NOISE,
           .frequency = 300,
           .volume = 15,
           .fade = -5,
           .priority = JINGLE_PRIORITY},
    // SND_LEAP: an ember out of the lava, a soft hiss.
    [7] = {.channel = PSG_NOISE, .frequency = 3200, .frames = 6, .volume = 6, .fade = -1},
};

// "Ember Halls", the stage's tune (written for this example): C minor, 120
// beats per minute, 16 bars that loop, 32 seconds. A tick is a 16th note,
// so a bar is 16 ticks. The bass pulses in eighth notes under it all. Phrase
// A creeps down from G, climbs to C and ends on G major, the minor key's
// dominant, whose B natural pulls back to C; A' starts high and falls, then
// climbs again over F minor; B falls in steps over A flat and B flat and
// leaps up over F minor; C hammers on C, holds a high A flat and goes
// through D flat, the darkest chord of the key, to G again.
static const PsgNote melody[] = {
    // A: Cm, Cm, Ab, G
    {PSG_G4, 4},
    {PSG_GS4, 2},
    {PSG_G4, 2},
    {PSG_F4, 2},
    {PSG_DS4, 2},
    {PSG_D4, 4},
    {PSG_DS4, 4},
    {PSG_F4, 2},
    {PSG_G4, 2},
    {PSG_C5, 8},
    {PSG_C5, 4},
    {PSG_DS5, 4},
    {PSG_D5, 2},
    {PSG_C5, 2},
    {PSG_GS4, 4},
    {PSG_B4, 6},
    {PSG_D5, 2},
    {PSG_F5, 4},
    {PSG_D5, 4},
    // A': Cm, Cm, Fm, G
    {PSG_G5, 4},
    {PSG_F5, 2},
    {PSG_DS5, 2},
    {PSG_D5, 2},
    {PSG_DS5, 2},
    {PSG_C5, 4},
    {PSG_G4, 4},
    {PSG_GS4, 2},
    {PSG_B4, 2},
    {PSG_C5, 8},
    {PSG_GS4, 4},
    {PSG_C5, 4},
    {PSG_F5, 4},
    {PSG_DS5, 2},
    {PSG_D5, 2},
    {PSG_D5, 4},
    {PSG_B4, 4},
    {PSG_G4, 8},
    // B: Ab, Bb, Fm, G
    {PSG_DS5, 6},
    {PSG_D5, 2},
    {PSG_C5, 4},
    {PSG_GS4, 4},
    {PSG_F5, 6},
    {PSG_DS5, 2},
    {PSG_D5, 4},
    {PSG_AS4, 4},
    {PSG_C5, 2},
    {PSG_GS4, 2},
    {PSG_C5, 2},
    {PSG_F5, 2},
    {PSG_GS5, 4},
    {PSG_G5, 4},
    {PSG_F5, 4},
    {PSG_DS5, 2},
    {PSG_D5, 2},
    {PSG_B4, 8},
    // C: Cm, Ab, Db, G
    {PSG_C5, 2},
    {PSG_REST, 2},
    {PSG_C5, 2},
    {PSG_DS5, 2},
    {PSG_G5, 4},
    {PSG_F5, 2},
    {PSG_DS5, 2},
    {PSG_GS5, 6},
    {PSG_G5, 2},
    {PSG_F5, 4},
    {PSG_DS5, 4},
    {PSG_CS5, 4},
    {PSG_F5, 4},
    {PSG_GS5, 4},
    {PSG_F5, 4},
    {PSG_G5, 4},
    {PSG_F5, 2},
    {PSG_D5, 2},
    {PSG_B4, 4},
    {PSG_D5, 2},
    {PSG_REST, 2},
};

// The bass: eighth notes on the chord's root, leaping to the octave, the
// seventh and the fifth.
#define BASS_BAR(root)                                                                             \
    {(root), 2}, {(root), 2}, {(root) + 12, 2}, {(root), 2}, {(root), 2}, {(root) + 10, 2},        \
        {(root) + 12, 2}, {                                                                        \
        (root) + 7, 2                                                                              \
    }

static const PsgNote bass[] = {
    BASS_BAR(PSG_C2),  BASS_BAR(PSG_C2),  BASS_BAR(PSG_GS2), BASS_BAR(PSG_G2),
    BASS_BAR(PSG_C2),  BASS_BAR(PSG_C2),  BASS_BAR(PSG_F2),  BASS_BAR(PSG_G2),
    BASS_BAR(PSG_GS2), BASS_BAR(PSG_AS2), BASS_BAR(PSG_F2),  BASS_BAR(PSG_G2),
    BASS_BAR(PSG_C2),  BASS_BAR(PSG_GS2), BASS_BAR(PSG_CS2), BASS_BAR(PSG_G2),
};

// The drums: a low thud on the beat and a double on the third, a hiss
// before the next bar.
static const PsgNote drums[] = {
    {PSG_C3, 4}, {PSG_REST, 4}, {PSG_C3, 2}, {PSG_C3, 2}, {PSG_REST, 2}, {PSG_A6, 2},
};

static const PsgTrack tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 10,
     .fade = -4,
     .notes = melody,
     .note_count = sizeof melody / sizeof melody[0]},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 9,
     .fade = -1,
     .notes = bass,
     .note_count = sizeof bass / sizeof bass[0]},
    {.channel = PSG_NOISE,
     .volume = 8,
     .fade = -1,
     .notes = drums,
     .note_count = sizeof drums / sizeof drums[0]},
};

const PsgSong castle_song = {.tempo = 120, .tracks = tracks, .track_count = 3};
