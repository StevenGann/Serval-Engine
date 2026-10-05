// Sound effects and music on the tone generators (audio.h).
//
// Channels: noise for the cards (the hiss of a deal, the click of a flip),
// square 1 for short UI blips and thumps (landing cards, the cursor, the
// bankroll's count), square 2 for chips and jingles. The music uses all
// three: a walking bass on square 1, the melody on square 2, a brushed ride
// on noise; effects play over it and the tune comes back on that channel
// when they end.
//
// Priorities: count ticks and cursor blips are 0, card sounds 1, chips 1;
// results (win, blackjack, lose, bust, push) are 2, so a chip clink or a
// landing card can't cut a result's jingle short; a reset is 3.

#include "game.h"

// Note frequencies in Hz, for the effects (F major around them).
#define D4 294
#define F4 349
#define A4 440
#define C5 523
#define E5 659
#define F5 698
#define G5 784
#define A5 880
#define B5 988
#define C6 1047
#define E6 1319
#define F6 1397
#define G6 1568
#define A6 1760
#define B6 1976
#define D7 2349
#define G7 3136

static const u16 chip_up_notes[] = {E6, B6};
static const u16 chip_down_notes[] = {B5, E5};
static const u16 chips_notes[] = {G6, D7, B6, G7, D7};
static const u16 shuffle_notes[] = {5000, 9000,  6000, 10000, 5000, 9000,
                                    6000, 10000, 5000, 9000,  6000, 12000};
static const u16 win_notes[] = {F5, A5, C6, F6};
static const u16 blackjack_notes[] = {F5, A5, C6, F6, 0, C6, F6, A6, A6};
static const u16 lose_notes[] = {A4, F4, D4};
static const u16 push_notes[] = {C5, C5};
static const u16 start_notes[] = {F5, A5, C6, F6};
static const u16 refill_notes[] = {C5, E5, G5, C6, E6, G6};

#define CARD_PRIORITY 1
#define RESULT_PRIORITY 2

static const PsgSound sounds[SOUND_COUNT] = {
    [SND_DEAL] = {.channel = PSG_NOISE,
                  .frequency = 7000,
                  .frames = 7,
                  .volume = 9,
                  .fade = -1,
                  .priority = CARD_PRIORITY},
    [SND_FLIP] = {.channel = PSG_NOISE,
                  .frequency = 24000,
                  .frames = 3,
                  .volume = 11,
                  .fade = -1,
                  .priority = CARD_PRIORITY},
    [SND_LAND] = {.duty = PSG_DUTY_50,
                  .frequency = 140,
                  .slide = -1,
                  .slide_size = 3,
                  .frames = 5,
                  .volume = 12,
                  .fade = -1,
                  .priority = CARD_PRIORITY},
    [SND_CHIP_UP] = {.channel = PSG_SQUARE2,
                     .duty = PSG_DUTY_12,
                     .frames = 3,
                     .notes = chip_up_notes,
                     .note_count = 2,
                     .volume = 11,
                     .priority = CARD_PRIORITY},
    [SND_CHIP_DOWN] = {.channel = PSG_SQUARE2,
                       .duty = PSG_DUTY_12,
                       .frames = 3,
                       .notes = chip_down_notes,
                       .note_count = 2,
                       .volume = 10,
                       .priority = CARD_PRIORITY},
    [SND_CHIPS] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_12,
                   .frames = 2,
                   .notes = chips_notes,
                   .note_count = 5,
                   .volume = 9,
                   .fade = -1,
                   .priority = CARD_PRIORITY},
    [SND_TICK] = {.duty = PSG_DUTY_12, .frequency = 1800, .frames = 1, .volume = 6},
    [SND_MOVE] = {.duty = PSG_DUTY_25, .frequency = 660, .frames = 3, .volume = 8, .fade = -1},
    [SND_PRESS] = {.duty = PSG_DUTY_25,
                   .frequency = 330,
                   .slide = 3,
                   .slide_size = 6,
                   .frames = 6,
                   .volume = 11,
                   .fade = -1},
    [SND_NOPE] = {.duty = PSG_DUTY_50, .frequency = 120, .frames = 9, .volume = 11, .fade = -2},
    [SND_SHUFFLE] = {.channel = PSG_NOISE,
                     .frames = 3,
                     .notes = shuffle_notes,
                     .note_count = 12,
                     .volume = 10,
                     .priority = RESULT_PRIORITY},
    [SND_WIN] = {.channel = PSG_SQUARE2,
                 .duty = PSG_DUTY_25,
                 .frames = 5,
                 .notes = win_notes,
                 .note_count = 4,
                 .priority = RESULT_PRIORITY},
    [SND_BLACKJACK] = {.channel = PSG_SQUARE2,
                       .duty = PSG_DUTY_25,
                       .frames = 5,
                       .notes = blackjack_notes,
                       .note_count = 9,
                       .priority = RESULT_PRIORITY},
    [SND_LOSE] = {.channel = PSG_SQUARE2,
                  .duty = PSG_DUTY_50,
                  .frames = 9,
                  .notes = lose_notes,
                  .note_count = 3,
                  .volume = 11,
                  .priority = RESULT_PRIORITY},
    [SND_BUST] = {.duty = PSG_DUTY_50,
                  .frequency = 420,
                  .slide = -2,
                  .slide_size = 3,
                  .frames = 28,
                  .volume = 13,
                  .fade = -4,
                  .priority = RESULT_PRIORITY},
    [SND_PUSH] = {.channel = PSG_SQUARE2,
                  .duty = PSG_DUTY_25,
                  .frames = 6,
                  .notes = push_notes,
                  .note_count = 2,
                  .volume = 10,
                  .priority = RESULT_PRIORITY},
    [SND_START] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_25,
                   .frames = 5,
                   .notes = start_notes,
                   .note_count = 4,
                   .priority = RESULT_PRIORITY},
    [SND_RESET] = {.duty = PSG_DUTY_50,
                   .frequency = 600,
                   .slide = -2,
                   .slide_size = 3,
                   .frames = 30,
                   .priority = 3},
    [SND_REFILL] = {.channel = PSG_SQUARE2,
                    .duty = PSG_DUTY_12,
                    .frames = 4,
                    .notes = refill_notes,
                    .note_count = 6,
                    .priority = RESULT_PRIORITY},
};

const PsgSound* const sound_table[SOUND_COUNT] = {
    [SND_DEAL] = &sounds[SND_DEAL],           [SND_FLIP] = &sounds[SND_FLIP],
    [SND_LAND] = &sounds[SND_LAND],           [SND_CHIP_UP] = &sounds[SND_CHIP_UP],
    [SND_CHIP_DOWN] = &sounds[SND_CHIP_DOWN], [SND_CHIPS] = &sounds[SND_CHIPS],
    [SND_TICK] = &sounds[SND_TICK],           [SND_MOVE] = &sounds[SND_MOVE],
    [SND_PRESS] = &sounds[SND_PRESS],         [SND_NOPE] = &sounds[SND_NOPE],
    [SND_SHUFFLE] = &sounds[SND_SHUFFLE],     [SND_WIN] = &sounds[SND_WIN],
    [SND_BLACKJACK] = &sounds[SND_BLACKJACK], [SND_LOSE] = &sounds[SND_LOSE],
    [SND_BUST] = &sounds[SND_BUST],           [SND_PUSH] = &sounds[SND_PUSH],
    [SND_START] = &sounds[SND_START],         [SND_RESET] = &sounds[SND_RESET],
    [SND_REFILL] = &sounds[SND_REFILL],
};

// --- Music -------------------------------------------------------------------
//
// "Velvet Felt" (written for this example): a lounge tune in F major, 100
// beats per minute with a swing feel, 16 bars that loop, about 38 seconds.
// Six ticks per beat, so a swung pair of eighths is 4 + 2 ticks and a bar is
// 24. The A section turns around F, Dm7, Gm7, C7; the B section climbs
// through Bbmaj7, Bbm7, Am7, D7 and comes back through Gm7, C7.

static const PsgNote melody[] = {
    // 1 Fmaj7
    {PSG_REST, 4},
    {PSG_A4, 2},
    {PSG_C5, 4},
    {PSG_E5, 2},
    {PSG_G5, 8},
    {PSG_E5, 4},
    // 2 Dm7
    {PSG_F5, 4},
    {PSG_E5, 2},
    {PSG_D5, 4},
    {PSG_C5, 2},
    {PSG_A4, 12},
    // 3 Gm7
    {PSG_REST, 4},
    {PSG_AS4, 2},
    {PSG_D5, 4},
    {PSG_F5, 2},
    {PSG_A5, 8},
    {PSG_G5, 4},
    // 4 C7
    {PSG_F5, 4},
    {PSG_E5, 2},
    {PSG_D5, 4},
    {PSG_AS4, 2},
    {PSG_G4, 12},
    // 5 Fmaj7
    {PSG_REST, 4},
    {PSG_A4, 2},
    {PSG_C5, 4},
    {PSG_F5, 2},
    {PSG_E5, 4},
    {PSG_F5, 2},
    {PSG_A5, 6},
    // 6 Dm7
    {PSG_G5, 4},
    {PSG_F5, 2},
    {PSG_D5, 6},
    {PSG_REST, 4},
    {PSG_D5, 2},
    {PSG_E5, 6},
    // 7 Gm7 C7
    {PSG_F5, 4},
    {PSG_D5, 2},
    {PSG_AS4, 4},
    {PSG_G4, 2},
    {PSG_E5, 4},
    {PSG_D5, 2},
    {PSG_C5, 6},
    // 8 F6
    {PSG_A4, 12},
    {PSG_REST, 12},
    // 9 Bbmaj7
    {PSG_REST, 4},
    {PSG_D5, 2},
    {PSG_F5, 4},
    {PSG_A5, 2},
    {PSG_C6, 8},
    {PSG_A5, 4},
    // 10 Bbm7
    {PSG_GS5, 4},
    {PSG_F5, 2},
    {PSG_CS5, 4},
    {PSG_F5, 2},
    {PSG_GS5, 12},
    // 11 Am7
    {PSG_G5, 4},
    {PSG_E5, 2},
    {PSG_C5, 4},
    {PSG_E5, 2},
    {PSG_G5, 8},
    {PSG_E5, 4},
    // 12 D7
    {PSG_FS5, 4},
    {PSG_A5, 2},
    {PSG_C6, 4},
    {PSG_A5, 2},
    {PSG_FS5, 12},
    // 13 Gm7
    {PSG_REST, 4},
    {PSG_G5, 2},
    {PSG_F5, 4},
    {PSG_D5, 2},
    {PSG_AS4, 8},
    {PSG_D5, 4},
    // 14 C7
    {PSG_E5, 4},
    {PSG_G5, 2},
    {PSG_AS5, 4},
    {PSG_G5, 2},
    {PSG_E5, 12},
    // 15 Fmaj7 D7
    {PSG_A5, 4},
    {PSG_G5, 2},
    {PSG_F5, 4},
    {PSG_C5, 2},
    {PSG_FS5, 4},
    {PSG_A5, 2},
    {PSG_D5, 6},
    // 16 Gm7 C7
    {PSG_G5, 4},
    {PSG_AS5, 2},
    {PSG_A5, 4},
    {PSG_G5, 2},
    {PSG_E5, 6},
    {PSG_REST, 6},
};

// Walking bass: a quarter note per beat (6 ticks, the track's .length).
static const PsgNote bass[] = {
    {PSG_F2, 0},  {PSG_A2, 0},  {PSG_C3, 0},  {PSG_E3, 0},  // Fmaj7
    {PSG_D3, 0},  {PSG_C3, 0},  {PSG_A2, 0},  {PSG_F2, 0},  // Dm7
    {PSG_G2, 0},  {PSG_A2, 0},  {PSG_AS2, 0}, {PSG_B2, 0},  // Gm7
    {PSG_C3, 0},  {PSG_G2, 0},  {PSG_A2, 0},  {PSG_E2, 0},  // C7
    {PSG_F2, 0},  {PSG_A2, 0},  {PSG_C3, 0},  {PSG_A2, 0},  // Fmaj7
    {PSG_D2, 0},  {PSG_F2, 0},  {PSG_A2, 0},  {PSG_D3, 0},  // Dm7
    {PSG_G2, 0},  {PSG_AS2, 0}, {PSG_C3, 0},  {PSG_E2, 0},  // Gm7 C7
    {PSG_F2, 0},  {PSG_D2, 0},  {PSG_E2, 0},  {PSG_A2, 0},  // F6
    {PSG_AS2, 0}, {PSG_D3, 0},  {PSG_F3, 0},  {PSG_D3, 0},  // Bbmaj7
    {PSG_AS2, 0}, {PSG_CS3, 0}, {PSG_F3, 0},  {PSG_DS3, 0}, // Bbm7
    {PSG_A2, 0},  {PSG_C3, 0},  {PSG_E3, 0},  {PSG_C3, 0},  // Am7
    {PSG_D3, 0},  {PSG_FS2, 0}, {PSG_A2, 0},  {PSG_C3, 0},  // D7
    {PSG_G2, 0},  {PSG_AS2, 0}, {PSG_D3, 0},  {PSG_AS2, 0}, // Gm7
    {PSG_C3, 0},  {PSG_E3, 0},  {PSG_G2, 0},  {PSG_AS2, 0}, // C7
    {PSG_F2, 0},  {PSG_A2, 0},  {PSG_D3, 0},  {PSG_FS2, 0}, // Fmaj7 D7
    {PSG_G2, 0},  {PSG_AS2, 0}, {PSG_C3, 0},  {PSG_E2, 0},  // Gm7 C7
};

// A brushed ride cymbal in swing: "ding, ding-a ding, ding-a" each bar.
static const PsgNote ride[] = {
    {PSG_A8, 6}, {PSG_A8, 4}, {PSG_F8, 2}, {PSG_A8, 6}, {PSG_A8, 4}, {PSG_F8, 2},
};

static const PsgTrack lounge_tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 10,
     .fade = -4,
     .notes = melody,
     .note_count = sizeof melody / sizeof melody[0]},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 11,
     .fade = -3,
     .length = 6,
     .notes = bass,
     .note_count = sizeof bass / sizeof bass[0]},
    {.channel = PSG_NOISE,
     .volume = 5,
     .fade = -1,
     .notes = ride,
     .note_count = sizeof ride / sizeof ride[0]},
};

const PsgSong lounge_song = {
    .tempo = 100, .ticks_per_beat = 6, .tracks = lounge_tracks, .track_count = 3};
