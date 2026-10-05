// Sound effects and music on the tone generators (audio.h).
//
// The music uses all three channels: melody on square 2, bass on square 1,
// drums on noise. Effects take a channel over for a moment and the music comes
// back on it when they end. Who wins a channel is decided by priority:
//   0  the ship's gunfire (a short hiss on the noise channel, renewed by
//      autofire: while A is held the drums give way to it)
//   1  small explosions: never cut short by gunfire
//   2  big explosions, power-ups, the extra ship, the start jingle
//   3  the ship exploding, bombs, the boss warning, game over, pause
// Songs have priority 0, so every effect plays over them.

#include "game.h"

// Note frequencies in Hz, for effects (songs use PSG_* note numbers).
#define C5 523
#define E5 659
#define G5 784
#define A5 880
#define C6 1047
#define D6 1175
#define E6 1319
#define G6 1568
#define A6 1760
#define C7 2093
#define A4 440
#define F4 349
#define D4 294
#define A3 220

static const u16 item_notes[] = {C6, E6, G6, C7};
static const u16 power_max_notes[] = {G6, 0, G6, C7};
static const u16 extend_notes[] = {E6, G6, A6, 0, A6, C7};
static const u16 warning_notes[] = {A5, E5, A5, E5, A5, E5, A5, E5};
static const u16 start_notes[] = {A4, D6, A5, D6};
static const u16 game_over_notes[] = {A4, 0, F4, 0, D4, A3};
static const u16 confirm_notes[] = {G5, C6};

static const PsgSound sounds[SOUND_COUNT] = {
    [SND_SHOT] = {.channel = PSG_NOISE, .frequency = 12000, .frames = 3, .volume = 5, .fade = -1},
    [SND_POP] = {.channel = PSG_NOISE, .frequency = 5000, .volume = 12, .fade = -1, .priority = 1},
    [SND_BOOM] = {.channel = PSG_NOISE, .frequency = 1600, .volume = 15, .fade = -3, .priority = 2},
    [SND_PLAYER_DIE] =
        {.channel = PSG_NOISE, .frequency = 700, .volume = 15, .fade = -6, .priority = 3},
    [SND_BOMB] = {.channel = PSG_NOISE, .frequency = 400, .volume = 15, .fade = -7, .priority = 3},
    [SND_ITEM] = {.channel = PSG_SQUARE2,
                  .duty = PSG_DUTY_25,
                  .frames = 3,
                  .notes = item_notes,
                  .note_count = 4,
                  .volume = 12,
                  .priority = 2},
    [SND_POWER_MAX] = {.channel = PSG_SQUARE2,
                       .duty = PSG_DUTY_12,
                       .frames = 4,
                       .notes = power_max_notes,
                       .note_count = 4,
                       .priority = 2},
    [SND_EXTEND] = {.channel = PSG_SQUARE2,
                    .duty = PSG_DUTY_12,
                    .frames = 5,
                    .notes = extend_notes,
                    .note_count = 6,
                    .priority = 2},
    [SND_WARNING] = {.channel = PSG_SQUARE1,
                     .duty = PSG_DUTY_50,
                     .frames = 12,
                     .notes = warning_notes,
                     .note_count = 8,
                     .volume = 11,
                     .priority = 3},
    [SND_PAUSE] =
        {.channel = PSG_SQUARE2, .duty = PSG_DUTY_12, .frequency = G6, .frames = 3, .priority = 3},
    [SND_START] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_25,
                   .frames = 6,
                   .notes = start_notes,
                   .note_count = 4,
                   .priority = 2},
    [SND_GAME_OVER] = {.channel = PSG_SQUARE2,
                       .duty = PSG_DUTY_25,
                       .frames = 12,
                       .notes = game_over_notes,
                       .note_count = 6,
                       .priority = 3},
    [SND_LETTER] = {.channel = PSG_SQUARE2, .duty = PSG_DUTY_25, .frequency = E6, .frames = 2},
    [SND_MOVE] = {.channel = PSG_SQUARE2, .duty = PSG_DUTY_25, .frequency = A5, .frames = 4},
    [SND_CONFIRM] = {.channel = PSG_SQUARE2,
                     .duty = PSG_DUTY_25,
                     .frames = 5,
                     .notes = confirm_notes,
                     .note_count = 2,
                     .priority = 3},
    [SND_TALLY] =
        {.channel = PSG_SQUARE2, .duty = PSG_DUTY_12, .frequency = D6, .frames = 2, .volume = 8},
};

const PsgSound* const sound_table[SOUND_COUNT] = {
    [SND_SHOT] = &sounds[SND_SHOT],           [SND_POP] = &sounds[SND_POP],
    [SND_BOOM] = &sounds[SND_BOOM],           [SND_PLAYER_DIE] = &sounds[SND_PLAYER_DIE],
    [SND_BOMB] = &sounds[SND_BOMB],           [SND_ITEM] = &sounds[SND_ITEM],
    [SND_POWER_MAX] = &sounds[SND_POWER_MAX], [SND_EXTEND] = &sounds[SND_EXTEND],
    [SND_WARNING] = &sounds[SND_WARNING],     [SND_PAUSE] = &sounds[SND_PAUSE],
    [SND_START] = &sounds[SND_START],         [SND_GAME_OVER] = &sounds[SND_GAME_OVER],
    [SND_LETTER] = &sounds[SND_LETTER],       [SND_MOVE] = &sounds[SND_MOVE],
    [SND_CONFIRM] = &sounds[SND_CONFIRM],     [SND_TALLY] = &sounds[SND_TALLY],
};

// --- "Veldt Run", the stage's tune ---------------------------------------------
//
// Written for this example: D minor, 152 beats per minute, 16 bars that loop
// (about 25 seconds). A tick is a 16th note, a bar 16 ticks. Bars 1-8 state
// the theme over Dm, Bb, C and A; bars 9-16 climb over Gm and Dm and turn
// back on A. The bass drives in 8th notes, jumping octaves.

static const PsgNote stage_melody[] = {
    // 1 Dm
    {PSG_D5, 4},
    {PSG_A4, 2},
    {PSG_D5, 2},
    {PSG_F5, 4},
    {PSG_E5, 2},
    {PSG_D5, 2},
    // 2 Dm
    {PSG_C5, 2},
    {PSG_D5, 2},
    {PSG_E5, 4},
    {PSG_A4, 8},
    // 3 Bb
    {PSG_F5, 4},
    {PSG_D5, 2},
    {PSG_F5, 2},
    {PSG_AS5, 4},
    {PSG_A5, 2},
    {PSG_G5, 2},
    // 4 C
    {PSG_G5, 6},
    {PSG_E5, 2},
    {PSG_C5, 4},
    {PSG_E5, 4},
    // 5 Dm
    {PSG_D5, 4},
    {PSG_A4, 2},
    {PSG_D5, 2},
    {PSG_F5, 4},
    {PSG_G5, 2},
    {PSG_A5, 2},
    // 6 Dm
    {PSG_C6, 4},
    {PSG_A5, 2},
    {PSG_F5, 2},
    {PSG_D5, 8},
    // 7 Bb
    {PSG_D5, 2},
    {PSG_F5, 2},
    {PSG_AS5, 4},
    {PSG_A5, 2},
    {PSG_G5, 2},
    {PSG_F5, 4},
    // 8 A
    {PSG_E5, 4},
    {PSG_CS5, 4},
    {PSG_E5, 4},
    {PSG_A5, 4},
    // 9 Gm
    {PSG_G5, 6},
    {PSG_F5, 2},
    {PSG_D5, 4},
    {PSG_AS4, 4},
    // 10 Gm
    {PSG_C5, 2},
    {PSG_D5, 2},
    {PSG_G5, 4},
    {PSG_F5, 4},
    {PSG_D5, 4},
    // 11 Dm
    {PSG_F5, 6},
    {PSG_E5, 2},
    {PSG_D5, 4},
    {PSG_A4, 4},
    // 12 Dm
    {PSG_A4, 2},
    {PSG_D5, 2},
    {PSG_F5, 4},
    {PSG_A5, 8},
    // 13 Bb
    {PSG_AS5, 4},
    {PSG_A5, 2},
    {PSG_G5, 2},
    {PSG_F5, 4},
    {PSG_D5, 4},
    // 14 C
    {PSG_E5, 4},
    {PSG_G5, 4},
    {PSG_C6, 4},
    {PSG_AS5, 2},
    {PSG_G5, 2},
    // 15 A
    {PSG_A5, 8},
    {PSG_E5, 4},
    {PSG_CS5, 4},
    // 16 A
    {PSG_A5, 4},
    {PSG_G5, 2},
    {PSG_F5, 2},
    {PSG_E5, 4},
    {PSG_CS5, 4},
};

// A bar of driving bass on a chord's root: 8th notes (the track's .length).
// clang-format off
#define DRIVE(r) {(r), 0}, {(r), 0}, {(r) + 12, 0}, {(r), 0}, {(r), 0}, {(r) + 12, 0}, {(r) + 7, 0}, {(r) + 12, 0}
// clang-format on

static const PsgNote stage_bass[] = {
    DRIVE(PSG_D2),  DRIVE(PSG_D2), DRIVE(PSG_AS2), DRIVE(PSG_C3), // 1-4
    DRIVE(PSG_D2),  DRIVE(PSG_D2), DRIVE(PSG_AS2), DRIVE(PSG_A2), // 5-8
    DRIVE(PSG_G2),  DRIVE(PSG_G2), DRIVE(PSG_D2),  DRIVE(PSG_D2), // 9-12
    DRIVE(PSG_AS2), DRIVE(PSG_C3), DRIVE(PSG_A2),  DRIVE(PSG_A2), // 13-16
};

// One bar of drums: kick (a low thump), snare (a mid burst), hats (hiss).
#define KICK PSG_A2
#define SNARE PSG_E5
#define HAT PSG_C8
static const PsgNote stage_drums[] = {
    {KICK, 2}, {HAT, 2}, {SNARE, 2}, {HAT, 2}, {KICK, 2}, {HAT, 1}, {HAT, 1}, {SNARE, 2}, {HAT, 2},
};

static const PsgTrack stage_tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 10,
     .fade = -6,
     .notes = stage_melody,
     .note_count = sizeof stage_melody / sizeof stage_melody[0]},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 9,
     .fade = -1,
     .length = 2,
     .notes = stage_bass,
     .note_count = sizeof stage_bass / sizeof stage_bass[0]},
    {.channel = PSG_NOISE,
     .volume = 7,
     .fade = -1,
     .notes = stage_drums,
     .note_count = sizeof stage_drums / sizeof stage_drums[0]},
};

const PsgSong stage_song = {.tempo = 152, .tracks = stage_tracks, .track_count = 3};

// --- "Lantern Core", the boss's tune ---------------------------------------------
//
// E minor with a Phrygian F, 168 beats per minute (192 in the boss's last
// phase), 8 bars: stabbing figures over a bass that grinds between E and F,
// a lift to C and a turn on B major back to the top.

static const PsgNote boss_melody[] = {
    // 1-2 E
    {PSG_E5, 2},
    {PSG_REST, 2},
    {PSG_E5, 2},
    {PSG_F5, 2},
    {PSG_G5, 4},
    {PSG_F5, 2},
    {PSG_E5, 2},
    {PSG_B5, 4},
    {PSG_A5, 2},
    {PSG_G5, 2},
    {PSG_F5, 4},
    {PSG_E5, 4},
    // 3-4 E
    {PSG_E5, 2},
    {PSG_REST, 2},
    {PSG_E5, 2},
    {PSG_F5, 2},
    {PSG_G5, 4},
    {PSG_A5, 2},
    {PSG_B5, 2},
    {PSG_C6, 4},
    {PSG_B5, 2},
    {PSG_A5, 2},
    {PSG_B5, 8},
    // 5-6 C
    {PSG_C6, 2},
    {PSG_B5, 2},
    {PSG_G5, 4},
    {PSG_E5, 4},
    {PSG_G5, 4},
    {PSG_C6, 4},
    {PSG_D6, 4},
    {PSG_E6, 8},
    // 7-8 B
    {PSG_DS6, 4},
    {PSG_B5, 4},
    {PSG_FS5, 4},
    {PSG_DS5, 4},
    {PSG_B4, 2},
    {PSG_DS5, 2},
    {PSG_FS5, 2},
    {PSG_A5, 2},
    {PSG_B5, 8},
};

// clang-format off
#define GRIND(r) {(r), 0}, {(r), 0}, {(r) + 1, 0}, {(r), 0}, {(r), 0}, {(r) + 3, 0}, {(r) + 1, 0}, {(r), 0}
#define LIFT(r) {(r), 0}, {(r), 0}, {(r) + 2, 0}, {(r), 0}, {(r), 0}, {(r) + 4, 0}, {(r) + 2, 0}, {(r), 0}
// clang-format on

static const PsgNote boss_bass[] = {
    GRIND(PSG_E2), GRIND(PSG_E2), GRIND(PSG_E2), GRIND(PSG_E2),
    LIFT(PSG_C3),  LIFT(PSG_C3),  GRIND(PSG_B2), GRIND(PSG_B2),
};

static const PsgNote boss_drums[] = {
    {KICK, 2}, {HAT, 2}, {SNARE, 2}, {HAT, 2}, {KICK, 2}, {KICK, 2}, {SNARE, 2}, {HAT, 1}, {HAT, 1},
};

static const PsgTrack boss_tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_12,
     .volume = 10,
     .fade = -4,
     .notes = boss_melody,
     .note_count = sizeof boss_melody / sizeof boss_melody[0]},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_25,
     .volume = 9,
     .fade = -1,
     .length = 2,
     .notes = boss_bass,
     .note_count = sizeof boss_bass / sizeof boss_bass[0]},
    {.channel = PSG_NOISE,
     .volume = 7,
     .fade = -1,
     .notes = boss_drums,
     .note_count = sizeof boss_drums / sizeof boss_drums[0]},
};

const PsgSong boss_song = {.tempo = BOSS_TEMPO, .tracks = boss_tracks, .track_count = 3};

// --- The stage clear fanfare, played once (PSG_NO_LOOP) ----------------------------

static const PsgNote clear_melody[] = {
    {PSG_A4, 2}, {PSG_D5, 2}, {PSG_F5, 2},  {PSG_A5, 4}, {PSG_REST, 2},
    {PSG_G5, 2}, {PSG_A5, 2}, {PSG_AS5, 2}, {PSG_C6, 2}, {PSG_D6, 12},
};
static const PsgNote clear_bass[] = {
    {PSG_D3, 8},
    {PSG_AS2, 4},
    {PSG_C3, 4},
    {PSG_D3, 12},
};
static const PsgTrack clear_tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 12,
     .fade = -7,
     .notes = clear_melody,
     .note_count = sizeof clear_melody / sizeof clear_melody[0],
     .loop = PSG_NO_LOOP},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 10,
     .fade = -5,
     .notes = clear_bass,
     .note_count = sizeof clear_bass / sizeof clear_bass[0],
     .loop = PSG_NO_LOOP},
};
const PsgSong clear_song = {.tempo = 132, .tracks = clear_tracks, .track_count = 2};
