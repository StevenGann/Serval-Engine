// Sound effects and the warren's tune, on the tone generators (audio.h).
//
// The tune is slow and sparse: a bass on the wave channel (the built-in
// triangle) and a melody on square 2. Effects play on square 1 and the noise
// channel, so most leave the tune alone; who wins a channel when two effects
// want it is decided by priority:
//   0  the wand's spark and its dry click, menu blips
//   1  wisps: waking, striking, hurt; doors
//   2  a wisp gone, a hex cast, pickups, the locked door
//   3  the serval hurt, the key, the unlocked door, pause, the end jingles
// The tune has priority 0, so every effect plays over it.

#include "game.h"

// Note frequencies in Hz, for effects (the tune uses PSG_* note numbers).
#define A3 220
#define D4 294
#define F4 349
#define A4 440
#define C5 523
#define E5 659
#define G5 784
#define C6 1047
#define E6 1319
#define G6 1568
#define C7 2093

static const u16 wisp_hurt_notes[] = {760, 520};
static const u16 wisp_gone_notes[] = {980, 740, 560, 420, 310, 230};
static const u16 wisp_wakes_notes[] = {330, 0, 440, 660};
static const u16 hex_notes[] = {392, 494, 622, 784, 988, 1245, 1568};
static const u16 pickup_notes[] = {C6, E6, G6};
static const u16 key_notes[] = {G5, C6, E6, G6, C7};
static const u16 locked_notes[] = {196, 0, 196};
static const u16 unlock_notes[] = {E5, G5, C6, G6};
static const u16 confirm_notes[] = {G5, C6};
static const u16 pause_notes[] = {C6, G5};
static const u16 won_notes[] = {C5, E5, G5, C6, 0, G5, C6, E6, G6, C7, C7};
static const u16 lost_notes[] = {A4, 0, F4, 0, D4, A3, A3};

#define NOTES(n) .notes = n, .note_count = sizeof n / sizeof n[0]

static const PsgSound sounds[SOUND_COUNT] = {
    [SND_SPARK] = {.frequency = 1500,
                   .frames = 7,
                   .volume = 11,
                   .fade = -1,
                   .slide = -2,
                   .slide_size = 2,
                   .duty = PSG_DUTY_25},
    [SND_DRY] = {.channel = PSG_NOISE, .frequency = 3000, .frames = 2, .volume = 6},
    [SND_WISP_HURT] =
        {.frames = 3, .volume = 10, .duty = PSG_DUTY_12, .priority = 1, NOTES(wisp_hurt_notes)},
    [SND_WISP_GONE] =
        {.frames = 3, .volume = 12, .duty = PSG_DUTY_12, .priority = 2, NOTES(wisp_gone_notes)},
    [SND_WISP_WAKES] =
        {.frames = 4, .volume = 9, .duty = PSG_DUTY_25, .priority = 1, NOTES(wisp_wakes_notes)},
    [SND_STRIKE] = {.channel = PSG_NOISE,
                    .frequency = 900,
                    .frames = 8,
                    .volume = 12,
                    .fade = -2,
                    .priority = 1},
    [SND_HEX] = {.frames = 2, .volume = 10, .duty = PSG_DUTY_25, .priority = 2, NOTES(hex_notes)},
    [SND_HURT] = {.channel = PSG_NOISE,
                  .frequency = 260,
                  .frames = 12,
                  .volume = 15,
                  .fade = -2,
                  .priority = 3},
    [SND_PICKUP] = {.frames = 3, .volume = 10, .priority = 2, NOTES(pickup_notes)},
    [SND_KEY] = {.frames = 4, .volume = 12, .priority = 3, NOTES(key_notes)},
    [SND_DOOR] = {.channel = PSG_NOISE,
                  .frequency = 90,
                  .frames = 18,
                  .volume = 9,
                  .fade = -4,
                  .priority = 1},
    [SND_LOCKED] =
        {.frames = 4, .volume = 11, .duty = PSG_DUTY_25, .priority = 2, NOTES(locked_notes)},
    [SND_UNLOCK] = {.frames = 4, .volume = 12, .priority = 3, NOTES(unlock_notes)},
    [SND_MOVE] = {.frequency = 880, .frames = 2, .volume = 8},
    [SND_CONFIRM] = {.frames = 4, .volume = 10, NOTES(confirm_notes)},
    [SND_PAUSE] = {.frames = 4, .volume = 10, .priority = 3, NOTES(pause_notes)},
    [SND_WON] = {.frames = 6, .volume = 12, .priority = 3, NOTES(won_notes)},
    [SND_LOST] = {.frames = 9, .volume = 12, .duty = PSG_DUTY_25, .priority = 3, NOTES(lost_notes)},
};

static const PsgSound* const sound_table[SOUND_COUNT] = {
    [SND_SPARK] = &sounds[SND_SPARK],
    [SND_DRY] = &sounds[SND_DRY],
    [SND_WISP_HURT] = &sounds[SND_WISP_HURT],
    [SND_WISP_GONE] = &sounds[SND_WISP_GONE],
    [SND_WISP_WAKES] = &sounds[SND_WISP_WAKES],
    [SND_STRIKE] = &sounds[SND_STRIKE],
    [SND_HEX] = &sounds[SND_HEX],
    [SND_HURT] = &sounds[SND_HURT],
    [SND_PICKUP] = &sounds[SND_PICKUP],
    [SND_KEY] = &sounds[SND_KEY],
    [SND_DOOR] = &sounds[SND_DOOR],
    [SND_LOCKED] = &sounds[SND_LOCKED],
    [SND_UNLOCK] = &sounds[SND_UNLOCK],
    [SND_MOVE] = &sounds[SND_MOVE],
    [SND_CONFIRM] = &sounds[SND_CONFIRM],
    [SND_PAUSE] = &sounds[SND_PAUSE],
    [SND_WON] = &sounds[SND_WON],
    [SND_LOST] = &sounds[SND_LOST],
};

// The warren's tune: eight bars in A minor (Am, F, G, E, twice), 16 ticks to
// the bar. The bass holds each chord's root for half a bar.
static const PsgNote melody[] = {
    {PSG_E4, 4},  {PSG_A4, 4},   {PSG_C5, 4},  {PSG_B4, 4}, // Am
    {PSG_A4, 6},  {PSG_REST, 2}, {PSG_F4, 4},  {PSG_A4, 4}, // F
    {PSG_G4, 4},  {PSG_B4, 4},   {PSG_D5, 4},  {PSG_B4, 4}, // G
    {PSG_E4, 6},  {PSG_REST, 2}, {PSG_GS4, 4}, {PSG_B4, 4}, // E
    {PSG_C5, 6},  {PSG_B4, 2},   {PSG_A4, 8},               // Am
    {PSG_F4, 4},  {PSG_A4, 4},   {PSG_C5, 8},               // F
    {PSG_B4, 6},  {PSG_A4, 2},   {PSG_G4, 8},               // G
    {PSG_E4, 12}, {PSG_REST, 4},                            // E
};

static const PsgNote bass[] = {
    {PSG_A2, 0}, {PSG_A2, 0}, {PSG_F2, 0}, {PSG_F2, 0},
    {PSG_G2, 0}, {PSG_G2, 0}, {PSG_E2, 0}, {PSG_E2, 0},
};

static const PsgTrack tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_12,
     .volume = 6,
     .fade = -5,
     .notes = melody,
     .note_count = sizeof melody / sizeof melody[0]},
    {.channel = PSG_WAVE,
     .volume = 8,
     .length = 8,
     .notes = bass,
     .note_count = sizeof bass / sizeof bass[0]},
};

static const PsgSong tune = {.tempo = 76, .tracks = tracks, .track_count = 2};

void sound_init(void) {
    psg_table_set(sound_table, SOUND_COUNT);
}

void sound_music(bool on) {
    if (on)
        psg_music_play(&tune);
    else
        psg_music_stop();
}
