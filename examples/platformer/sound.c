// Sound effects every stage shares, the sound table and the goal's fanfare,
// on the tone generators (audio.h). Each stage's music and its own sound
// effects are in its sound_<name>.c.
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
// the music comes back on that channel when they end. When time runs low the
// game speeds it up where it is (psg_music_set_tempo), and pausing the game
// pauses it (psg_music_pause).

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

static const PsgSound sounds[SOUND_SHARED_COUNT] = {
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

// One table for every stage: the shared sounds, then each stage's block of
// STAGE_SOUNDS (game.h).
// clang-format off
#define STAGE_SOUND(stage, array, n) [SND_STAGE(stage) + (n)] = &(array)[n]
#define STAGE_SOUND_IDS(stage, a)                                                                  \
    STAGE_SOUND(stage, a, 0), STAGE_SOUND(stage, a, 1), STAGE_SOUND(stage, a, 2),                  \
    STAGE_SOUND(stage, a, 3), STAGE_SOUND(stage, a, 4), STAGE_SOUND(stage, a, 5),                  \
    STAGE_SOUND(stage, a, 6), STAGE_SOUND(stage, a, 7)
// clang-format on

const PsgSound* const sound_table[SOUND_COUNT] = {
    [SND_JUMP] = &sounds[SND_JUMP],
    [SND_JUMP_BIG] = &sounds[SND_JUMP_BIG],
    [SND_GEM] = &sounds[SND_GEM],
    [SND_BUMP] = &sounds[SND_BUMP],
    [SND_BREAK] = &sounds[SND_BREAK],
    [SND_POWER_APPEARS] = &sounds[SND_POWER_APPEARS],
    [SND_POWER_UP] = &sounds[SND_POWER_UP],
    [SND_SHRINK] = &sounds[SND_SHRINK],
    [SND_STOMP] = &sounds[SND_STOMP],
    [SND_KICK] = &sounds[SND_KICK],
    [SND_DEATH] = &sounds[SND_DEATH],
    [SND_ONE_UP] = &sounds[SND_ONE_UP],
    [SND_POLE] = &sounds[SND_POLE],
    [SND_TALLY] = &sounds[SND_TALLY],
    [SND_HURRY] = &sounds[SND_HURRY],
    [SND_PAUSE] = &sounds[SND_PAUSE],
    [SND_START] = &sounds[SND_START],
    [SND_GAME_OVER] = &sounds[SND_GAME_OVER],
    STAGE_SOUND_IDS(STAGE_OVERWORLD, overworld_sounds),
    STAGE_SOUND_IDS(STAGE_UNDERGROUND, underground_sounds),
    STAGE_SOUND_IDS(STAGE_TREETOPS, treetops_sounds),
    STAGE_SOUND_IDS(STAGE_CASTLE, castle_sounds),
};

// The fanfare as the serval hops off the goal pole, or walks into a stage's
// exit: played once (PSG_NO_LOOP).
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
