// Stage 1-4's music and sound effects (audio.h).
//
// A placeholder: a short loop until the castle has its own tune (and the
// dragon's music and the ending's), and no sound effects of its own yet
// (its IDs, SND_STAGE(STAGE_CASTLE) + n, are reserved).

#include "stage_castle.h"

// The stage's sound effects: IDs SND_STAGE(STAGE_CASTLE) + n. None yet.
const PsgSound castle_sounds[STAGE_SOUNDS];

// A sketch of a tune (written for this example): D minor, 112 beats per
// minute, 8 bars that loop. Melody on square 2, bass on square 1, drums on
// noise, as in the other stages.
static const PsgNote melody[] = {
    // Dm, Bb, Gm, A
    {PSG_D5, 4},
    {PSG_F5, 4},
    {PSG_A5, 4},
    {PSG_F5, 4},
    {PSG_D5, 4},
    {PSG_F5, 4},
    {PSG_AS5, 8},
    {PSG_G5, 4},
    {PSG_F5, 4},
    {PSG_E5, 4},
    {PSG_D5, 4},
    {PSG_CS5, 4},
    {PSG_E5, 4},
    {PSG_A4, 8},
    // Dm, Bb, Gm, A
    {PSG_A5, 4},
    {PSG_G5, 4},
    {PSG_F5, 4},
    {PSG_E5, 4},
    {PSG_F5, 4},
    {PSG_D5, 4},
    {PSG_AS4, 8},
    {PSG_G4, 4},
    {PSG_AS4, 4},
    {PSG_D5, 4},
    {PSG_G5, 4},
    {PSG_E5, 4},
    {PSG_CS5, 4},
    {PSG_A4, 8},
};

// The bass: root, octave, root, fifth in quarter notes.
#define BASS_BAR(root)                                                                             \
    {(root), 4}, {(root) + 12, 4}, {(root), 4}, {                                                  \
        (root) + 7, 4                                                                              \
    }

static const PsgNote bass[] = {
    BASS_BAR(PSG_D2), BASS_BAR(PSG_AS2), BASS_BAR(PSG_G2), BASS_BAR(PSG_A2),
    BASS_BAR(PSG_D2), BASS_BAR(PSG_AS2), BASS_BAR(PSG_G2), BASS_BAR(PSG_A2),
};

static const PsgNote drums[] = {
    {PSG_F2, 8},
    {PSG_D5, 8},
};

static const PsgTrack tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_50,
     .volume = 9,
     .fade = -3,
     .notes = melody,
     .note_count = sizeof melody / sizeof melody[0]},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_25,
     .volume = 8,
     .fade = -2,
     .notes = bass,
     .note_count = sizeof bass / sizeof bass[0]},
    {.channel = PSG_NOISE,
     .volume = 6,
     .fade = -2,
     .notes = drums,
     .note_count = sizeof drums / sizeof drums[0]},
};

const PsgSong castle_song = {.tempo = 112, .tracks = tracks, .track_count = 3};
