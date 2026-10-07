// Stage 1-1's music and sound effects (audio.h): its tune, and none of its
// own effects yet (its block of sound IDs, SND_STAGE(STAGE_OVERWORLD), is
// empty).
//
// The music (PsgSong) uses all three channels: melody on square 2, bass on
// square 1, drums on noise. When time runs low the game speeds it up from 150
// to 180 beats per minute where it is (stage_overworld.c's hurry_tempo).

#include "stage_overworld.h"

const PsgSound overworld_sounds[STAGE_SOUNDS];

// "Savanna Stroll", the stage's tune (written for this example): F major, 150
// beats per minute, 16 bars that loop, about 26 seconds. A tick is a 16th
// note, so a bar is 16 ticks. The melody's two A phrases (bars 1-4 and 5-8)
// answer each other, the B phrase (bars 9-12) climbs higher over Bb, Am, Gm
// and C, and bars 13-16 bring the opening back with a cadence that leads into
// the loop.

static const PsgNote melody[] = {
    // A: F, Bb, Gm-C, F
    {PSG_F4, 3},
    {PSG_A4, 3},
    {PSG_C5, 2},
    {PSG_E5, 4},
    {PSG_D5, 2},
    {PSG_C5, 2},
    {PSG_D5, 3},
    {PSG_AS4, 3},
    {PSG_F4, 2},
    {PSG_G4, 2},
    {PSG_A4, 2},
    {PSG_AS4, 4},
    {PSG_G4, 3},
    {PSG_AS4, 3},
    {PSG_D5, 2},
    {PSG_C5, 3},
    {PSG_AS4, 3},
    {PSG_G4, 2},
    {PSG_A4, 6},
    {PSG_REST, 2},
    {PSG_C5, 2},
    {PSG_D5, 2},
    {PSG_E5, 2},
    {PSG_G5, 2},
    // A': F, Dm, Bb-C, F
    {PSG_F5, 3},
    {PSG_E5, 3},
    {PSG_C5, 2},
    {PSG_A4, 4},
    {PSG_C5, 2},
    {PSG_F5, 2},
    {PSG_A5, 3},
    {PSG_F5, 3},
    {PSG_D5, 2},
    {PSG_E5, 2},
    {PSG_F5, 2},
    {PSG_A5, 4},
    {PSG_G5, 3},
    {PSG_F5, 3},
    {PSG_D5, 2},
    {PSG_E5, 3},
    {PSG_D5, 3},
    {PSG_C5, 2},
    {PSG_F5, 8},
    {PSG_REST, 4},
    {PSG_C5, 2},
    {PSG_D5, 2},
    // B: Bb, Am, Gm, C
    {PSG_F5, 4},
    {PSG_D5, 2},
    {PSG_F5, 2},
    {PSG_AS5, 6},
    {PSG_A5, 2},
    {PSG_A5, 4},
    {PSG_E5, 2},
    {PSG_A5, 2},
    {PSG_C6, 6},
    {PSG_G5, 2},
    {PSG_G5, 4},
    {PSG_D5, 2},
    {PSG_G5, 2},
    {PSG_AS5, 4},
    {PSG_A5, 2},
    {PSG_G5, 2},
    {PSG_E5, 3},
    {PSG_F5, 3},
    {PSG_G5, 2},
    {PSG_C6, 4},
    {PSG_AS5, 2},
    {PSG_G5, 2},
    // A'': F, Bb, Gm-C, F
    {PSG_F4, 3},
    {PSG_A4, 3},
    {PSG_C5, 2},
    {PSG_E5, 4},
    {PSG_D5, 2},
    {PSG_C5, 2},
    {PSG_D5, 3},
    {PSG_AS4, 3},
    {PSG_F4, 2},
    {PSG_G4, 2},
    {PSG_A4, 2},
    {PSG_AS4, 4},
    {PSG_G4, 3},
    {PSG_AS4, 3},
    {PSG_D5, 2},
    {PSG_E5, 3},
    {PSG_G5, 3},
    {PSG_E5, 2},
    {PSG_F5, 6},
    {PSG_C5, 2},
    {PSG_A4, 2},
    {PSG_C5, 2},
    {PSG_F4, 4},
};

// The bass walks root, fifth, octave, fifth in 8th notes (the track's
// .length of 2 ticks), a bar or half a bar per chord.
#define BASS_HALF(root)                                                                            \
    {(root), 0}, {(root) + 7, 0}, {(root) + 12, 0}, {                                              \
        (root) + 7, 0                                                                              \
    }
#define BASS_BAR(root) BASS_HALF(root), BASS_HALF(root)

static const PsgNote bass[] = {
    BASS_BAR(PSG_F2),  BASS_BAR(PSG_AS2), BASS_HALF(PSG_G2),  BASS_HALF(PSG_C3), BASS_BAR(PSG_F2),
    BASS_BAR(PSG_F2),  BASS_BAR(PSG_D3),  BASS_HALF(PSG_AS2), BASS_HALF(PSG_C3), BASS_BAR(PSG_F2),
    BASS_BAR(PSG_AS2), BASS_BAR(PSG_A2),  BASS_BAR(PSG_G2),   BASS_BAR(PSG_C3),  BASS_BAR(PSG_F2),
    BASS_BAR(PSG_AS2), BASS_HALF(PSG_G2), BASS_HALF(PSG_C3),  BASS_BAR(PSG_F2),
};

// One bar of drums, repeated under everything: a low thump (kick), a
// mid-pitched burst (snare) and a short hiss (hi-hat).
#define KICK PSG_A2
#define SNARE PSG_E5
#define HAT PSG_C8
static const PsgNote drums[] = {
    {KICK, 2}, {HAT, 2}, {SNARE, 2}, {HAT, 2}, {KICK, 2}, {KICK, 2}, {SNARE, 2}, {HAT, 2},
};

static const PsgTrack level_tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 9,
     .fade = -5,
     .notes = melody,
     .note_count = sizeof melody / sizeof melody[0]},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 8,
     .fade = -2,
     .length = 2,
     .notes = bass,
     .note_count = sizeof bass / sizeof bass[0]},
    {.channel = PSG_NOISE,
     .volume = 6,
     .fade = -1,
     .notes = drums,
     .note_count = sizeof drums / sizeof drums[0]},
};

const PsgSong overworld_song = {.tempo = 150, .tracks = level_tracks, .track_count = 3};
