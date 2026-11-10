// The board games' sounds: see sound.h.
//
// Channels: the knocks are on the noise channel (a short burst, fading at
// once, low for a quiet move, lower and louder for a capture), the chimes
// and the "no" on square 2, the end-of-game jingles on square 1. So a
// capture's knock and a check's chime, which come together, both sound.

#include "sound.h"

#include <serval/serval.h>

enum {
    SND_PICK,
    SND_MOVE,
    SND_CAPTURE,
    SND_CHECK,
    SND_CASTLE,
    SND_PROMOTE,
    SND_WIN,
    SND_LOSE,
    SND_DRAW,
    SND_NO,
    SND_COUNT,
};

// Note frequencies in Hz.
#define G3 196
#define AS3 233
#define C4 262
#define E4 330
#define G4 392
#define C5 523
#define D5 587
#define E5 659
#define G5 784
#define B5 988
#define C6 1047
#define E6 1319
#define G6 1568
#define C7 2093

// Castling: knock, a breath, knock (0 is a rest).
static const u16 castle_notes[] = {1800, 0, 1500};
static const u16 check_notes[] = {B5, E6};
static const u16 promote_notes[] = {C6, E6, G6, C7};
static const u16 win_notes[] = {C5, E5, G5, C6, 0, G5, C6};
static const u16 lose_notes[] = {G4, E4, C4, G3};
static const u16 draw_notes[] = {E5, D5, C5, D5};
static const u16 no_notes[] = {AS3, G3};

static const PsgSound sounds[SND_COUNT] = {
    [SND_PICK] =
        {.channel = PSG_SQUARE2, .duty = PSG_DUTY_12, .frequency = 1760, .frames = 2, .volume = 7},
    [SND_MOVE] = {.channel = PSG_NOISE, .frequency = 1800, .volume = 11, .fade = -1},
    [SND_CAPTURE] = {.channel = PSG_NOISE, .frequency = 700, .volume = 15, .fade = -2},
    [SND_CHECK] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_25,
                   .notes = check_notes,
                   .note_count = 2,
                   .frames = 6,
                   .volume = 10},
    [SND_CASTLE] =
        {.channel = PSG_NOISE, .notes = castle_notes, .note_count = 3, .frames = 4, .volume = 11},
    [SND_PROMOTE] = {.channel = PSG_SQUARE2,
                     .duty = PSG_DUTY_25,
                     .notes = promote_notes,
                     .note_count = 4,
                     .frames = 4,
                     .volume = 10},
    [SND_WIN] = {.channel = PSG_SQUARE1,
                 .duty = PSG_DUTY_50,
                 .notes = win_notes,
                 .note_count = 7,
                 .frames = 7,
                 .volume = 11},
    [SND_LOSE] = {.channel = PSG_SQUARE1,
                  .duty = PSG_DUTY_50,
                  .notes = lose_notes,
                  .note_count = 4,
                  .frames = 10,
                  .volume = 10},
    [SND_DRAW] = {.channel = PSG_SQUARE1,
                  .duty = PSG_DUTY_50,
                  .notes = draw_notes,
                  .note_count = 4,
                  .frames = 8,
                  .volume = 10},
    [SND_NO] = {.channel = PSG_SQUARE2,
                .duty = PSG_DUTY_50,
                .notes = no_notes,
                .note_count = 2,
                .frames = 4,
                .volume = 7},
};

static const PsgSound* sound_table[SND_COUNT];

void sound_init(void) {
    for (int i = 0; i < SND_COUNT; i++)
        sound_table[i] = &sounds[i];
    psg_table_set(sound_table, SND_COUNT);
}

void sound_pick(void) {
    psg_play(SND_PICK);
}

void sound_move(void) {
    psg_play(SND_MOVE);
}

void sound_capture(void) {
    psg_play(SND_CAPTURE);
}

void sound_check(void) {
    psg_play(SND_CHECK);
}

void sound_castle(void) {
    psg_play(SND_CASTLE);
}

void sound_promote(void) {
    psg_play(SND_PROMOTE);
}

void sound_win(void) {
    psg_play(SND_WIN);
}

void sound_lose(void) {
    psg_play(SND_LOSE);
}

void sound_draw(void) {
    psg_play(SND_DRAW);
}

void sound_no(void) {
    psg_play(SND_NO);
}
