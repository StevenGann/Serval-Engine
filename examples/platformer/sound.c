// Sound effects and music on the tone generators (audio.h).
//
// Channels: square 1 for the serval's own sounds (jumps, growing, shrinking,
// the pole slide, stomps); square 2 for pickups and jingles (gems, 1-up,
// start, game over); noise for blocks and knocked enemies. Jingles have a
// higher .priority than the short effects, so a gem chime can't cut a 1-up
// jingle short. Upward slides use small steps (a large slide_size): a slide
// that climbs past the hardware's highest pitch silences the channel (debug
// builds warn about it).
//
// The music (PsgSong) uses all three channels: melody on square 2, bass on
// square 1, drums on noise. Sound effects play over it on their channel, and
// the music comes back on that channel when they end.

#include "game.h"

// Note frequencies in Hz.
#define G4 392
#define B4 494
#define C5 523
#define D5 587
#define E5 659
#define F5 698
#define G5 784
#define A5 880
#define B5 988
#define C6 1047
#define D6 1175
#define E6 1319
#define G6 1568
#define C7 2093
#define E7 2637

static const u16 gem_notes[] = {E6, G6, C7};
static const u16 power_appears_notes[] = {G4, B4, D5, G5, B5, D6};
static const u16 one_up_notes[] = {C6, E6, G6, C7, E7};
static const u16 death_notes[] = {B5, 0, B5, F5, 0, E5, D5, C5, 0, G4};
static const u16 hurry_notes[] = {A5, 0, A5, 0, A5, C6};
static const u16 start_notes[] = {G5, C6, E6, G6};
static const u16 game_over_notes[] = {G5, E5, C5, 0, D5, B4, G4};

// Jingles: sounds of lower priority (everything else) wait until they end.
#define JINGLE_PRIORITY 1

static const PsgSound sounds[SOUND_COUNT] = {
    [SND_JUMP] = {.duty = PSG_DUTY_25,
                  .frequency = 330,
                  .slide = 2,
                  .slide_size = 6,
                  .frames = 10,
                  .volume = 11,
                  .fade = -2},
    [SND_JUMP_BIG] = {.duty = PSG_DUTY_25,
                      .frequency = 220,
                      .slide = 2,
                      .slide_size = 6,
                      .frames = 12,
                      .volume = 12,
                      .fade = -2},
    [SND_GEM] = {.channel = PSG_SQUARE2,
                 .duty = PSG_DUTY_12,
                 .frames = 3,
                 .notes = gem_notes,
                 .note_count = 3,
                 .volume = 11},
    [SND_BUMP] = {.channel = PSG_NOISE, .frequency = 300, .frames = 6, .volume = 12, .fade = -1},
    [SND_BREAK] = {.channel = PSG_NOISE, .frequency = 2500, .volume = 15, .fade = -2},
    [SND_POWER_APPEARS] = {.channel = PSG_SQUARE2,
                           .duty = PSG_DUTY_25,
                           .frames = 3,
                           .notes = power_appears_notes,
                           .note_count = 6,
                           .volume = 10},
    [SND_POWER_UP] = {.duty = PSG_DUTY_50,
                      .frequency = 262,
                      .slide = 3,
                      .slide_size = 7,
                      .frames = 36,
                      .volume = 12},
    [SND_SHRINK] = {.duty = PSG_DUTY_50,
                    .frequency = 880,
                    .slide = -3,
                    .slide_size = 5,
                    .frames = 36,
                    .volume = 12},
    [SND_STOMP] = {.duty = PSG_DUTY_50,
                   .frequency = 700,
                   .slide = -1,
                   .slide_size = 2,
                   .frames = 6,
                   .volume = 13,
                   .fade = -1},
    [SND_KICK] = {.channel = PSG_NOISE, .frequency = 5000, .volume = 13, .fade = -1},
    [SND_DEATH] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_25,
                   .frames = 9,
                   .notes = death_notes,
                   .note_count = 10,
                   .priority = JINGLE_PRIORITY},
    [SND_ONE_UP] = {.channel = PSG_SQUARE2,
                    .duty = PSG_DUTY_12,
                    .frames = 5,
                    .notes = one_up_notes,
                    .note_count = 5,
                    .priority = JINGLE_PRIORITY},
    [SND_POLE] = {.duty = PSG_DUTY_12,
                  .frequency = 1400,
                  .slide = -6,
                  .slide_size = 5,
                  .frames = 50,
                  .volume = 9},
    [SND_TALLY] =
        {.channel = PSG_SQUARE2, .duty = PSG_DUTY_12, .frequency = 1760, .frames = 2, .volume = 7},
    [SND_HURRY] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_50,
                   .frames = 6,
                   .notes = hurry_notes,
                   .note_count = 6,
                   .volume = 10,
                   .priority = JINGLE_PRIORITY},
    [SND_PAUSE] = {.duty = PSG_DUTY_12, .frequency = 784, .frames = 3},
    [SND_START] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_25,
                   .frames = 6,
                   .notes = start_notes,
                   .note_count = 4,
                   .priority = JINGLE_PRIORITY},
    [SND_GAME_OVER] = {.channel = PSG_SQUARE2,
                       .duty = PSG_DUTY_25,
                       .frames = 12,
                       .notes = game_over_notes,
                       .note_count = 7,
                       .priority = JINGLE_PRIORITY},
};

const PsgSound* const sound_table[SOUND_COUNT] = {
    [SND_JUMP] = &sounds[SND_JUMP],         [SND_JUMP_BIG] = &sounds[SND_JUMP_BIG],
    [SND_GEM] = &sounds[SND_GEM],           [SND_BUMP] = &sounds[SND_BUMP],
    [SND_BREAK] = &sounds[SND_BREAK],       [SND_POWER_APPEARS] = &sounds[SND_POWER_APPEARS],
    [SND_POWER_UP] = &sounds[SND_POWER_UP], [SND_SHRINK] = &sounds[SND_SHRINK],
    [SND_STOMP] = &sounds[SND_STOMP],       [SND_KICK] = &sounds[SND_KICK],
    [SND_DEATH] = &sounds[SND_DEATH],       [SND_ONE_UP] = &sounds[SND_ONE_UP],
    [SND_POLE] = &sounds[SND_POLE],         [SND_TALLY] = &sounds[SND_TALLY],
    [SND_HURRY] = &sounds[SND_HURRY],       [SND_PAUSE] = &sounds[SND_PAUSE],
    [SND_START] = &sounds[SND_START],       [SND_GAME_OVER] = &sounds[SND_GAME_OVER],
};

// --- Music -------------------------------------------------------------------
//
// "Savanna Stroll", the level's tune (written for this example): F major, 150
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

const PsgSong level_song = {.tempo = 150, .tracks = level_tracks, .track_count = 3};
// The same tune, faster: once the time runs low.
const PsgSong hurry_song = {.tempo = 180, .tracks = level_tracks, .track_count = 3};

// The fanfare as the serval hops off the goal pole: played once (PSG_NO_LOOP).
static const PsgNote fanfare_melody[] = {
    {PSG_G5, 2}, {PSG_A5, 2}, {PSG_C6, 2}, {PSG_REST, 1},
    {PSG_A5, 1}, {PSG_C6, 3}, {PSG_D6, 1}, {PSG_F6, 8},
};
static const PsgNote fanfare_bass[] = {
    {PSG_F3, 6},
    {PSG_REST, 2},
    {PSG_C3, 4},
    {PSG_F3, 8},
};
static const PsgTrack fanfare_tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 11,
     .fade = -6,
     .notes = fanfare_melody,
     .note_count = sizeof fanfare_melody / sizeof fanfare_melody[0],
     .loop = PSG_NO_LOOP},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 9,
     .fade = -4,
     .notes = fanfare_bass,
     .note_count = sizeof fanfare_bass / sizeof fanfare_bass[0],
     .loop = PSG_NO_LOOP},
};
const PsgSong goal_song = {.tempo = 150, .tracks = fanfare_tracks, .track_count = 2};
