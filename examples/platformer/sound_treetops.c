// Stage 1-3's music and sound effects (audio.h).
//
// A placeholder: a short loop until the treetops have their own tune, and no
// sound effects of their own yet (their IDs, SND_STAGE(STAGE_TREETOPS) + n,
// are reserved).

#include "stage_treetops.h"

// The stage's sound effects: IDs SND_STAGE(STAGE_TREETOPS) + n. None yet.
const PsgSound treetops_sounds[STAGE_SOUNDS];

// A sketch of a tune (written for this example): G major, 132 beats per
// minute, 8 bars that loop. Melody on square 2, bass on square 1, drums on
// noise, as in the other stages.
static const PsgNote melody[] = {
    // G, C, D, G
    {PSG_G5, 2},
    {PSG_D5, 2},
    {PSG_B4, 2},
    {PSG_D5, 2},
    {PSG_G5, 4},
    {PSG_A5, 4},
    {PSG_E5, 2},
    {PSG_G5, 2},
    {PSG_C6, 4},
    {PSG_B5, 2},
    {PSG_A5, 2},
    {PSG_G5, 4},
    {PSG_FS5, 2},
    {PSG_A5, 2},
    {PSG_D6, 4},
    {PSG_C6, 2},
    {PSG_A5, 2},
    {PSG_FS5, 4},
    {PSG_G5, 4},
    {PSG_D5, 4},
    {PSG_G4, 8},
    // Em, C, D, G
    {PSG_B4, 2},
    {PSG_E5, 2},
    {PSG_G5, 4},
    {PSG_FS5, 2},
    {PSG_E5, 2},
    {PSG_B4, 4},
    {PSG_C5, 2},
    {PSG_E5, 2},
    {PSG_G5, 4},
    {PSG_A5, 4},
    {PSG_G5, 4},
    {PSG_A5, 4},
    {PSG_FS5, 2},
    {PSG_D5, 2},
    {PSG_E5, 2},
    {PSG_FS5, 2},
    {PSG_A5, 4},
    {PSG_G5, 8},
    {PSG_REST, 8},
};

// The bass: root and fifth, a half bar each.
#define BASS_BAR(root)                                                                             \
    {(root), 8}, {                                                                                 \
        (root) + 7, 8                                                                              \
    }

static const PsgNote bass[] = {
    BASS_BAR(PSG_G2), BASS_BAR(PSG_C3), BASS_BAR(PSG_D3), BASS_BAR(PSG_G2),
    BASS_BAR(PSG_E2), BASS_BAR(PSG_C3), BASS_BAR(PSG_D3), BASS_BAR(PSG_G2),
};

static const PsgNote drums[] = {
    {PSG_A2, 4},
    {PSG_C8, 4},
    {PSG_E5, 4},
    {PSG_C8, 4},
};

static const PsgTrack tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 9,
     .fade = -4,
     .notes = melody,
     .note_count = sizeof melody / sizeof melody[0]},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 7,
     .fade = -3,
     .notes = bass,
     .note_count = sizeof bass / sizeof bass[0]},
    {.channel = PSG_NOISE,
     .volume = 5,
     .fade = -1,
     .notes = drums,
     .note_count = sizeof drums / sizeof drums[0]},
};

const PsgSong treetops_song = {.tempo = 132, .tracks = tracks, .track_count = 3};
