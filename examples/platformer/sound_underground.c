// Stage 1-2's music and sound effects (audio.h). The music (PsgSong) uses
// all three channels, as 1-1's does: melody on square 2, bass on square 1,
// drums on noise. When time runs low the game speeds it up from 120 to 150
// beats per minute where it is (stage_underground.c's hurry_tempo).

#include "stage_underground.h"

// The stage's sound effects: IDs SND_STAGE(STAGE_UNDERGROUND) + n.
const PsgSound underground_sounds[STAGE_SOUNDS] = {
    // SND_BAT: a squeak sliding down as a bat wakes and swoops (square 1, as
    // the serval's sounds: a slide needs it).
    [0] = {.duty = PSG_DUTY_12,
           .frequency = 2600,
           .slide = -1,
           .slide_size = 4,
           .frames = 9,
           .volume = 9,
           .fade = -1},
};

// "Lantern Burrow", the stage's tune (written for this example): E minor,
// 120 beats per minute, 16 bars that loop, 32 seconds; quieter and sparser
// than 1-1's. A tick is a 16th note, so a bar is 16 ticks. Phrase A climbs
// out of the dark in short, separated notes; A' answers it and lands on B
// major, the minor key's bright dominant; B rises over C and D and falls back
// to A minor; C walks up through the scale to a held B that leads into the
// loop.
static const PsgNote melody[] = {
    // A: Em, Em, C, D
    {PSG_E5, 2},
    {PSG_REST, 2},
    {PSG_B4, 2},
    {PSG_REST, 2},
    {PSG_G4, 2},
    {PSG_A4, 2},
    {PSG_B4, 4},
    {PSG_D5, 2},
    {PSG_REST, 2},
    {PSG_B4, 2},
    {PSG_REST, 2},
    {PSG_A4, 2},
    {PSG_G4, 2},
    {PSG_FS4, 4},
    {PSG_E4, 2},
    {PSG_G4, 2},
    {PSG_C5, 4},
    {PSG_B4, 2},
    {PSG_A4, 2},
    {PSG_G4, 4},
    {PSG_FS4, 2},
    {PSG_A4, 2},
    {PSG_D5, 6},
    {PSG_REST, 6},
    // A': Em, Em, Am, B
    {PSG_E5, 2},
    {PSG_REST, 2},
    {PSG_B4, 2},
    {PSG_REST, 2},
    {PSG_G4, 2},
    {PSG_A4, 2},
    {PSG_B4, 4},
    {PSG_G5, 2},
    {PSG_REST, 2},
    {PSG_FS5, 2},
    {PSG_E5, 2},
    {PSG_D5, 2},
    {PSG_B4, 2},
    {PSG_G4, 4},
    {PSG_A4, 2},
    {PSG_C5, 2},
    {PSG_E5, 4},
    {PSG_D5, 2},
    {PSG_C5, 2},
    {PSG_A4, 4},
    {PSG_B4, 2},
    {PSG_DS5, 2},
    {PSG_FS5, 6},
    {PSG_REST, 6},
    // B: C, D, Em, Am
    {PSG_G5, 4},
    {PSG_E5, 2},
    {PSG_C5, 2},
    {PSG_G4, 4},
    {PSG_C5, 4},
    {PSG_FS5, 4},
    {PSG_D5, 2},
    {PSG_A4, 2},
    {PSG_FS4, 4},
    {PSG_A4, 4},
    {PSG_G4, 2},
    {PSG_B4, 2},
    {PSG_E5, 2},
    {PSG_G5, 2},
    {PSG_FS5, 2},
    {PSG_E5, 2},
    {PSG_D5, 2},
    {PSG_B4, 2},
    {PSG_C5, 4},
    {PSG_A4, 2},
    {PSG_E4, 2},
    {PSG_A4, 8},
    // C: C, D, B, B
    {PSG_E5, 2},
    {PSG_D5, 2},
    {PSG_C5, 2},
    {PSG_B4, 2},
    {PSG_C5, 4},
    {PSG_E5, 4},
    {PSG_FS5, 2},
    {PSG_E5, 2},
    {PSG_D5, 2},
    {PSG_C5, 2},
    {PSG_D5, 4},
    {PSG_FS5, 4},
    {PSG_DS5, 4},
    {PSG_FS5, 4},
    {PSG_B5, 8},
    {PSG_A5, 2},
    {PSG_FS5, 2},
    {PSG_DS5, 2},
    {PSG_B4, 2},
    {PSG_REST, 8},
};

// The bass in short 8th notes with rests between: root, octave, fifth, octave,
// root, a bar per chord.
#define BASS_BAR(root)                                                                             \
    {(root), 2}, {PSG_REST, 2}, {(root) + 12, 2}, {PSG_REST, 2}, {(root) + 7, 2},                  \
        {(root) + 12, 2}, {(root), 2}, {                                                           \
        PSG_REST, 2                                                                                \
    }

static const PsgNote bass[] = {
    BASS_BAR(PSG_E2), BASS_BAR(PSG_E2), BASS_BAR(PSG_C3), BASS_BAR(PSG_D3),
    BASS_BAR(PSG_E2), BASS_BAR(PSG_E2), BASS_BAR(PSG_A2), BASS_BAR(PSG_B2),
    BASS_BAR(PSG_C3), BASS_BAR(PSG_D3), BASS_BAR(PSG_E2), BASS_BAR(PSG_A2),
    BASS_BAR(PSG_C3), BASS_BAR(PSG_D3), BASS_BAR(PSG_B2), BASS_BAR(PSG_B2),
};

// One bar of drums, repeated: a soft thump, a tick, a rap, two ticks.
#define KICK PSG_G2
#define SNARE PSG_D5
#define HAT PSG_A7
static const PsgNote drums[] = {
    {KICK, 4}, {HAT, 4}, {SNARE, 4}, {HAT, 2}, {HAT, 2},
};

static const PsgTrack tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_12,
     .volume = 9,
     .fade = -3,
     .notes = melody,
     .note_count = sizeof melody / sizeof melody[0]},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 8,
     .fade = -2,
     .notes = bass,
     .note_count = sizeof bass / sizeof bass[0]},
    {.channel = PSG_NOISE,
     .volume = 5,
     .fade = -1,
     .notes = drums,
     .note_count = sizeof drums / sizeof drums[0]},
};

const PsgSong underground_song = {.tempo = 120, .tracks = tracks, .track_count = 3};
