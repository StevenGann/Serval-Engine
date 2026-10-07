// Stage 1-3's music and sound effects (audio.h). The music (PsgSong) uses
// all three channels, as the other stages' do: melody on square 2, bass on
// square 1, drums on noise. When time runs low the game speeds it up from 100
// to 125 beats per minute where it is (stage_treetops.c's hurry_tempo).

#include "stage_treetops.h"

// Note frequencies in Hz, for the effects.
#define G7 3136
#define B7 3951

static const u16 chirp_notes[] = {G7, B7, 0, G7, B7};

// The stage's sound effects: IDs SND_STAGE(STAGE_TREETOPS) + n.
const PsgSound treetops_sounds[STAGE_SOUNDS] = {
    // SND_BIRD: a bird comes into view: two quick high chirps.
    [0] = {.channel = PSG_SQUARE2,
           .duty = PSG_DUTY_12,
           .frames = 2,
           .notes = chirp_notes,
           .note_count = 5,
           .volume = 8},
    // SND_BURR: a burr drops out of the leaves: a short rustle.
    [1] = {.channel = PSG_NOISE, .frequency = 9000, .frames = 6, .volume = 6, .fade = -1},
};

// "High Branches", the stage's tune (written for this example): A major, in
// a lilting 6/8, 100 beats (dotted quarters) per minute, 24 bars that loop,
// about 29 seconds; brighter and airier than the cave's. A tick is an eighth
// note (3 ticks a beat), so a bar is 6 ticks. Phrase A leaps up the chord and
// floats back down; A' climbs higher, to F sharp; B drifts down in steps
// over D, A, Bm and E; C reaches the top of the tune and turns home; D
// settles lower in F sharp minor and C sharp minor before climbing back into
// the opening, which comes round once more to end on A.
static const PsgNote melody[] = {
    // A: A, D, E, A
    {PSG_E5, 2},
    {PSG_A5, 1},
    {PSG_CS6, 3},
    {PSG_B5, 1},
    {PSG_A5, 1},
    {PSG_FS5, 1},
    {PSG_A5, 3},
    {PSG_GS5, 2},
    {PSG_B5, 1},
    {PSG_E6, 3},
    {PSG_CS6, 1},
    {PSG_B5, 1},
    {PSG_A5, 1},
    {PSG_E5, 3},
    // A': F#m, D, Bm, E
    {PSG_FS5, 2},
    {PSG_A5, 1},
    {PSG_CS6, 3},
    {PSG_D6, 1},
    {PSG_CS6, 1},
    {PSG_B5, 1},
    {PSG_A5, 3},
    {PSG_B5, 2},
    {PSG_D6, 1},
    {PSG_FS6, 2},
    {PSG_E6, 1},
    {PSG_E6, 3},
    {PSG_REST, 1},
    {PSG_B5, 1},
    {PSG_GS5, 1},
    // B: D, A, Bm, E
    {PSG_A5, 1},
    {PSG_B5, 1},
    {PSG_CS6, 1},
    {PSG_D6, 3},
    {PSG_CS6, 1},
    {PSG_B5, 1},
    {PSG_A5, 1},
    {PSG_E5, 3},
    {PSG_D5, 1},
    {PSG_E5, 1},
    {PSG_FS5, 1},
    {PSG_B5, 2},
    {PSG_A5, 1},
    {PSG_GS5, 3},
    {PSG_E5, 3},
    // C: D, C#m, Bm-E, A
    {PSG_FS5, 1},
    {PSG_A5, 1},
    {PSG_D6, 1},
    {PSG_FS6, 3},
    {PSG_E6, 1},
    {PSG_CS6, 1},
    {PSG_GS5, 1},
    {PSG_E6, 3},
    {PSG_D6, 2},
    {PSG_CS6, 1},
    {PSG_B5, 2},
    {PSG_GS5, 1},
    {PSG_A5, 3},
    {PSG_REST, 3},
    // D: F#m, C#m, D, E
    {PSG_CS6, 2},
    {PSG_A5, 1},
    {PSG_FS5, 2},
    {PSG_A5, 1},
    {PSG_GS5, 2},
    {PSG_E5, 1},
    {PSG_CS5, 2},
    {PSG_E5, 1},
    {PSG_FS5, 1},
    {PSG_E5, 1},
    {PSG_D5, 1},
    {PSG_FS5, 2},
    {PSG_A5, 1},
    {PSG_GS5, 2},
    {PSG_B5, 1},
    {PSG_E6, 2},
    {PSG_D6, 1},
    // A'': A, D, E, A
    {PSG_CS6, 3},
    {PSG_E6, 3},
    {PSG_D6, 1},
    {PSG_CS6, 1},
    {PSG_B5, 1},
    {PSG_A5, 2},
    {PSG_FS5, 1},
    {PSG_E5, 2},
    {PSG_GS5, 1},
    {PSG_B5, 2},
    {PSG_D6, 1},
    {PSG_A5, 3},
    {PSG_REST, 3},
};

// The bass, a bar per chord: root, fifth, octave, fifth, rocking like a
// branch in the wind. (Bar 15 has two chords, a half bar each.)
#define BASS_BAR(root)                                                                             \
    {(root), 2}, {(root) + 7, 1}, {(root) + 12, 2}, {                                              \
        (root) + 7, 1                                                                              \
    }

static const PsgNote bass[] = {
    BASS_BAR(PSG_A2),  BASS_BAR(PSG_D3), BASS_BAR(PSG_E3), BASS_BAR(PSG_A2),  BASS_BAR(PSG_FS2),
    BASS_BAR(PSG_D3),  BASS_BAR(PSG_B2), BASS_BAR(PSG_E3), BASS_BAR(PSG_D3),  BASS_BAR(PSG_A2),
    BASS_BAR(PSG_B2),  BASS_BAR(PSG_E3), BASS_BAR(PSG_D3), BASS_BAR(PSG_CS3), {PSG_B2, 2},
    {PSG_FS3, 1},      {PSG_E3, 2},      {PSG_B3, 1},      BASS_BAR(PSG_A2),  BASS_BAR(PSG_FS2),
    BASS_BAR(PSG_CS3), BASS_BAR(PSG_D3), BASS_BAR(PSG_E3), BASS_BAR(PSG_A2),  BASS_BAR(PSG_D3),
    BASS_BAR(PSG_E3),  BASS_BAR(PSG_A2),
};

// One bar of drums, repeated: a soft thump, two ticks, a light rap, two ticks.
#define KICK PSG_A2
#define SNARE PSG_E5
#define HAT PSG_A7
static const PsgNote drums[] = {
    {KICK, 1}, {HAT, 1}, {HAT, 1}, {SNARE, 1}, {HAT, 1}, {HAT, 1},
};

static const PsgTrack tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 9,
     .fade = -3,
     .notes = melody,
     .note_count = sizeof melody / sizeof melody[0]},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 7,
     .fade = -2,
     .notes = bass,
     .note_count = sizeof bass / sizeof bass[0]},
    {.channel = PSG_NOISE,
     .volume = 4,
     .fade = -1,
     .notes = drums,
     .note_count = sizeof drums / sizeof drums[0]},
};

const PsgSong treetops_song = {
    .tempo = 100, .ticks_per_beat = 3, .tracks = tracks, .track_count = 3};
