// Sound effects and music on the tone generators (audio.h).
//
// Channels: square 1 for the ball (paddle, walls, bricks: each brick color
// has its own pitch, so clearing a row plays a little scale), square 2 for
// jingles (power-ups, an extra life, start, game over), noise for silver and
// gold bricks. The music uses all three: its melody on square 2, the bass on
// square 1 and drums on noise; effects play over it on their channel and the
// tune comes back there when they end.
//
// Priorities: the ball's blips are 0, so they never cut a jingle short.
// Power-up jingles are 1; an extra life, a lost ball and game over are 2:
// a power-up caught as the ball is lost can't drown the loss.

#include "game.h"

// Note frequencies in Hz, for the effects.
#define E4 330
#define G4 392
#define A4 440
#define B4 494
#define C5 523
#define D5 587
#define E5 659
#define G5 784
#define A5 880
#define B5 988
#define C6 1047
#define D6 1175
#define E6 1319
#define G6 1568
#define A6 1760

static const u16 power_notes[] = {E5, A5, C6, E6};
static const u16 life_notes[] = {A5, C6, E6, A6, 0, E6, A6};
static const u16 start_notes[] = {A4, C5, E5, A5};
static const u16 game_over_notes[] = {E5, D5, C5, B4, 0, A4, 0, E4};

#define POWER_PRIORITY 1
#define BIG_PRIORITY 2

// A brick: a short, bright blip on square 1 at the color's pitch (an A minor
// pentatonic scale, red lowest).
#define BRICK_SOUND(hz)                                                                            \
    {.duty = PSG_DUTY_25, .frequency = (hz), .frames = 6, .volume = 12, .fade = -1}

static const PsgSound sounds[SOUND_COUNT] = {
    [SND_PADDLE] = {.duty = PSG_DUTY_50, .frequency = 262, .frames = 6, .volume = 13, .fade = -1},
    [SND_WALL] = {.duty = PSG_DUTY_50, .frequency = 196, .frames = 4, .volume = 8, .fade = -1},
    [SND_BRICK_RED] = BRICK_SOUND(A5),
    [SND_BRICK_ORANGE] = BRICK_SOUND(C6),
    [SND_BRICK_YELLOW] = BRICK_SOUND(D6),
    [SND_BRICK_GREEN] = BRICK_SOUND(E6),
    [SND_BRICK_BLUE] = BRICK_SOUND(G6),
    [SND_BRICK_PURPLE] = BRICK_SOUND(A6),
    [SND_CRACK] = {.channel = PSG_NOISE, .frequency = 3000, .frames = 8, .volume = 12, .fade = -1},
    [SND_CLANK] = {.channel = PSG_NOISE, .frequency = 12000, .frames = 5, .volume = 10, .fade = -1},
    [SND_LAUNCH] = {.duty = PSG_DUTY_25,
                    .frequency = 330,
                    .slide = 2,
                    .slide_size = 6,
                    .frames = 8,
                    .volume = 11,
                    .fade = -2},
    [SND_CATCH] = {.duty = PSG_DUTY_50, .frequency = 165, .frames = 5, .volume = 12, .fade = -1},
    [SND_POWER_UP] = {.channel = PSG_SQUARE2,
                      .duty = PSG_DUTY_12,
                      .frames = 4,
                      .notes = power_notes,
                      .note_count = 4,
                      .volume = 11,
                      .priority = POWER_PRIORITY},
    [SND_LIFE_UP] = {.channel = PSG_SQUARE2,
                     .duty = PSG_DUTY_12,
                     .frames = 5,
                     .notes = life_notes,
                     .note_count = 7,
                     .priority = BIG_PRIORITY},
    [SND_BALL_LOST] = {.duty = PSG_DUTY_50,
                       .frequency = 440,
                       .slide = -3,
                       .slide_size = 4,
                       .frames = 45,
                       .volume = 13,
                       .fade = -6,
                       .priority = BIG_PRIORITY},
    [SND_START] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_25,
                   .frames = 6,
                   .notes = start_notes,
                   .note_count = 4,
                   .priority = POWER_PRIORITY},
    [SND_GAME_OVER] = {.channel = PSG_SQUARE2,
                       .duty = PSG_DUTY_25,
                       .frames = 12,
                       .notes = game_over_notes,
                       .note_count = 8,
                       .priority = BIG_PRIORITY},
    [SND_PAUSE] = {.duty = PSG_DUTY_12, .frequency = 784, .frames = 3},
    [SND_RESET] = {.duty = PSG_DUTY_50,
                   .frequency = 600,
                   .slide = -2,
                   .slide_size = 3,
                   .frames = 30,
                   .priority = BIG_PRIORITY},
};

const PsgSound* const sound_table[SOUND_COUNT] = {
    [SND_PADDLE] = &sounds[SND_PADDLE],
    [SND_WALL] = &sounds[SND_WALL],
    [SND_BRICK_RED] = &sounds[SND_BRICK_RED],
    [SND_BRICK_ORANGE] = &sounds[SND_BRICK_ORANGE],
    [SND_BRICK_YELLOW] = &sounds[SND_BRICK_YELLOW],
    [SND_BRICK_GREEN] = &sounds[SND_BRICK_GREEN],
    [SND_BRICK_BLUE] = &sounds[SND_BRICK_BLUE],
    [SND_BRICK_PURPLE] = &sounds[SND_BRICK_PURPLE],
    [SND_CRACK] = &sounds[SND_CRACK],
    [SND_CLANK] = &sounds[SND_CLANK],
    [SND_LAUNCH] = &sounds[SND_LAUNCH],
    [SND_CATCH] = &sounds[SND_CATCH],
    [SND_POWER_UP] = &sounds[SND_POWER_UP],
    [SND_LIFE_UP] = &sounds[SND_LIFE_UP],
    [SND_BALL_LOST] = &sounds[SND_BALL_LOST],
    [SND_START] = &sounds[SND_START],
    [SND_GAME_OVER] = &sounds[SND_GAME_OVER],
    [SND_PAUSE] = &sounds[SND_PAUSE],
    [SND_RESET] = &sounds[SND_RESET],
};

// --- Music -------------------------------------------------------------------
//
// "Pounce Patrol" (written for this example): A minor, 140 beats per minute,
// 16 bars that loop, about 27 seconds. A tick is a 16th note, so a bar is 16
// ticks. Bars 1-8 state the theme over Am F G Em | Am F Dm E; bars 9-16 climb
// over F G Em Am | F G E Am and end on an E chord that leads back in.

static const PsgNote melody[] = {
    // 1 Am
    {PSG_E5, 2},
    {PSG_A5, 2},
    {PSG_REST, 2},
    {PSG_A5, 2},
    {PSG_G5, 2},
    {PSG_E5, 2},
    {PSG_D5, 2},
    {PSG_E5, 2},
    // 2 F
    {PSG_F5, 4},
    {PSG_E5, 2},
    {PSG_C5, 2},
    {PSG_REST, 2},
    {PSG_C5, 2},
    {PSG_D5, 2},
    {PSG_E5, 2},
    // 3 G
    {PSG_D5, 2},
    {PSG_G5, 2},
    {PSG_REST, 2},
    {PSG_G5, 2},
    {PSG_F5, 2},
    {PSG_D5, 2},
    {PSG_B4, 2},
    {PSG_D5, 2},
    // 4 Em
    {PSG_E5, 6},
    {PSG_B4, 2},
    {PSG_G4, 4},
    {PSG_REST, 4},
    // 5 Am
    {PSG_A4, 2},
    {PSG_C5, 2},
    {PSG_E5, 2},
    {PSG_A5, 4},
    {PSG_G5, 2},
    {PSG_A5, 2},
    {PSG_B5, 2},
    // 6 F
    {PSG_C6, 4},
    {PSG_A5, 2},
    {PSG_F5, 2},
    {PSG_A5, 4},
    {PSG_REST, 2},
    {PSG_F5, 2},
    // 7 Dm
    {PSG_D5, 2},
    {PSG_F5, 2},
    {PSG_A5, 2},
    {PSG_D6, 4},
    {PSG_C6, 2},
    {PSG_A5, 2},
    {PSG_F5, 2},
    // 8 E
    {PSG_GS5, 4},
    {PSG_E5, 2},
    {PSG_B4, 2},
    {PSG_GS4, 4},
    {PSG_B4, 2},
    {PSG_D5, 2},
    // 9 F
    {PSG_A5, 3},
    {PSG_G5, 3},
    {PSG_F5, 2},
    {PSG_E5, 4},
    {PSG_C5, 4},
    // 10 G
    {PSG_B4, 3},
    {PSG_D5, 3},
    {PSG_G5, 2},
    {PSG_F5, 4},
    {PSG_D5, 4},
    // 11 Em
    {PSG_E5, 3},
    {PSG_G5, 3},
    {PSG_B5, 2},
    {PSG_A5, 2},
    {PSG_G5, 2},
    {PSG_E5, 4},
    // 12 Am
    {PSG_A5, 8},
    {PSG_REST, 4},
    {PSG_E5, 2},
    {PSG_G5, 2},
    // 13 F
    {PSG_A5, 2},
    {PSG_C6, 2},
    {PSG_A5, 2},
    {PSG_F5, 2},
    {PSG_G5, 2},
    {PSG_A5, 2},
    {PSG_F5, 2},
    {PSG_C5, 2},
    // 14 G
    {PSG_D5, 2},
    {PSG_G5, 2},
    {PSG_D5, 2},
    {PSG_B4, 2},
    {PSG_C5, 2},
    {PSG_D5, 2},
    {PSG_G5, 2},
    {PSG_B5, 2},
    // 15 E
    {PSG_GS5, 4},
    {PSG_B5, 4},
    {PSG_E6, 4},
    {PSG_D6, 2},
    {PSG_B5, 2},
    // 16 Am, then E leading back
    {PSG_A5, 6},
    {PSG_REST, 2},
    {PSG_E5, 2},
    {PSG_GS5, 2},
    {PSG_B5, 4},
};

// The bass in 8th notes (the track's .length of 2 ticks): a bouncing figure
// on each chord's root, octave and fifth.
#define BASS_BAR(root)                                                                             \
    {(root), 0}, {(root), 0}, {(root) + 12, 0}, {(root), 0}, {(root) + 7, 0}, {(root), 0},         \
        {(root) + 12, 0}, {                                                                        \
        (root) + 7, 0                                                                              \
    }

static const PsgNote bass[] = {
    BASS_BAR(PSG_A2), BASS_BAR(PSG_F2), BASS_BAR(PSG_G2), BASS_BAR(PSG_E2),
    BASS_BAR(PSG_A2), BASS_BAR(PSG_F2), BASS_BAR(PSG_D2), BASS_BAR(PSG_E2),
    BASS_BAR(PSG_F2), BASS_BAR(PSG_G2), BASS_BAR(PSG_E2), BASS_BAR(PSG_A2),
    BASS_BAR(PSG_F2), BASS_BAR(PSG_G2), BASS_BAR(PSG_E2), BASS_BAR(PSG_A2),
};

// One bar of drums under everything: kick (a low thump), snare (a mid burst)
// and hi-hat (a short hiss) in 8th notes.
#define KICK PSG_A2
#define SNARE PSG_E5
#define HAT PSG_C8
static const PsgNote drums[] = {
    {KICK, 2}, {HAT, 2}, {SNARE, 2}, {KICK, 2}, {HAT, 2}, {KICK, 2}, {SNARE, 2}, {HAT, 2},
};

static const PsgTrack level_tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 8,
     .fade = -4,
     .notes = melody,
     .note_count = sizeof melody / sizeof melody[0]},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 7,
     .fade = -2,
     .length = 2,
     .notes = bass,
     .note_count = sizeof bass / sizeof bass[0]},
    {.channel = PSG_NOISE,
     .volume = 5,
     .fade = -1,
     .notes = drums,
     .note_count = sizeof drums / sizeof drums[0]},
};

const PsgSong level_song = {.tempo = 140, .tracks = level_tracks, .track_count = 3};

// Level cleared: a short fanfare in A major, played once (PSG_NO_LOOP).
static const PsgNote clear_melody[] = {
    {PSG_E5, 2}, {PSG_A5, 2}, {PSG_CS6, 2}, {PSG_E6, 4}, {PSG_CS6, 2}, {PSG_E6, 2}, {PSG_A6, 8},
};
static const PsgNote clear_bass[] = {
    {PSG_A3, 4}, {PSG_E3, 4}, {PSG_A3, 2}, {PSG_E3, 2}, {PSG_A2, 10},
};
static const PsgTrack clear_tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 11,
     .fade = -6,
     .notes = clear_melody,
     .note_count = sizeof clear_melody / sizeof clear_melody[0],
     .loop = PSG_NO_LOOP},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 9,
     .fade = -5,
     .notes = clear_bass,
     .note_count = sizeof clear_bass / sizeof clear_bass[0],
     .loop = PSG_NO_LOOP},
};
const PsgSong clear_song = {.tempo = 140, .tracks = clear_tracks, .track_count = 2};
