// Serval Den's sounds: see sound.h.
//
// Channels: the purr, the crunches and the sweep on the noise channel; the
// serval's voice on square 1 (it slides); chimes, clicks and the world
// outside on square 2, quiet, at the lowest priority so that care sounds
// always win.

#include "sound.h"

#include "art.h"

enum {
    SND_PURR,
    SND_EAT,
    SND_MEW,
    SND_SAD,
    SND_CLEAN,
    SND_MEDICINE,
    SND_LIGHTS_ON,
    SND_LIGHTS_OFF,
    SND_NO,
    SND_BIRD,
    SND_CRICKET,
    SND_BAT,
    SND_TAP,
    SND_MISS,
    SND_COUNT,
};

#define G3 196
#define AS3 233
#define E6 1319
#define G6 1568
#define C7 2093

// A purr: a low rumble in pulses, breathing in and out.
static const u16 purr_notes[] = {160, 120, 0, 160, 120, 0, 150, 110, 0, 160, 120, 0, 150, 110};
static const u16 eat_notes[] = {2600, 0, 3200, 0, 2400, 0, 3000, 0, 2800};
static const u16 clean_notes[] = {6000, 9000, 14000, 9000, 6000};
static const u16 medicine_notes[] = {E6, G6, C7};
static const u16 no_notes[] = {AS3, G3};
static const u16 bird_notes[] = {3100, 3600, 0, 3300, 3900};
static const u16 cricket_notes[] = {4400, 0, 4400, 0, 4400};

static const u16 miss_notes[] = {523, 440, 349, 262};

static const PsgSound sounds[SND_COUNT] = {
    [SND_PURR] = {.channel = PSG_NOISE,
                  .notes = purr_notes,
                  .note_count = 14,
                  .frames = 5,
                  .volume = 8,
                  .priority = 2},
    [SND_EAT] = {.channel = PSG_NOISE,
                 .notes = eat_notes,
                 .note_count = 9,
                 .frames = 5,
                 .volume = 9,
                 .fade = -1,
                 .priority = 2},
    [SND_MEW] = {.channel = PSG_SQUARE1,
                 .duty = PSG_DUTY_25,
                 .frequency = 880,
                 .slide = 2,
                 .slide_size = 5,
                 .frames = 10,
                 .volume = 9,
                 .fade = -3},
    [SND_SAD] = {.channel = PSG_SQUARE1,
                 .duty = PSG_DUTY_25,
                 .frequency = 740,
                 .slide = -4,
                 .slide_size = 5,
                 .frames = 22,
                 .volume = 8,
                 .fade = -5},
    [SND_CLEAN] = {.channel = PSG_NOISE,
                   .notes = clean_notes,
                   .note_count = 5,
                   .frames = 3,
                   .volume = 8,
                   .priority = 2},
    [SND_MEDICINE] = {.channel = PSG_SQUARE2,
                      .duty = PSG_DUTY_12,
                      .notes = medicine_notes,
                      .note_count = 3,
                      .frames = 6,
                      .volume = 9,
                      .priority = 2},
    [SND_LIGHTS_ON] = {.channel = PSG_SQUARE2,
                       .duty = PSG_DUTY_12,
                       .frequency = 1400,
                       .frames = 2,
                       .volume = 7,
                       .priority = 2},
    [SND_LIGHTS_OFF] = {.channel = PSG_SQUARE2,
                        .duty = PSG_DUTY_12,
                        .frequency = 700,
                        .frames = 2,
                        .volume = 7,
                        .priority = 2},
    [SND_NO] = {.channel = PSG_SQUARE2,
                .duty = PSG_DUTY_50,
                .notes = no_notes,
                .note_count = 2,
                .frames = 4,
                .volume = 7,
                .priority = 2},
    [SND_BIRD] = {.channel = PSG_SQUARE2,
                  .duty = PSG_DUTY_12,
                  .notes = bird_notes,
                  .note_count = 5,
                  .frames = 3,
                  .volume = 3},
    [SND_CRICKET] = {.channel = PSG_SQUARE2,
                     .duty = PSG_DUTY_12,
                     .notes = cricket_notes,
                     .note_count = 5,
                     .frames = 2,
                     .volume = 2},
    // Yarn Bat: a thump of the paw, a tap on the wall, a falling mew.
    [SND_BAT] = {.channel = PSG_SQUARE1,
                 .duty = PSG_DUTY_50,
                 .frequency = 330,
                 .slide = -2,
                 .slide_size = 3,
                 .frames = 6,
                 .volume = 12,
                 .fade = -3,
                 .priority = 2},
    [SND_TAP] = {.channel = PSG_SQUARE2,
                 .duty = PSG_DUTY_12,
                 .frequency = 1100,
                 .frames = 2,
                 .volume = 7,
                 .priority = 2},
    [SND_MISS] = {.channel = PSG_SQUARE1,
                  .duty = PSG_DUTY_25,
                  .notes = miss_notes,
                  .note_count = 4,
                  .frames = 6,
                  .volume = 9,
                  .priority = 3},
};

static const PsgSound* sound_table[SND_COUNT];

void sound_init(void) {
    for (int i = 0; i < SND_COUNT; i++)
        sound_table[i] = &sounds[i];
    psg_table_set(sound_table, SND_COUNT);
}

void sound_purr(void) {
    psg_play(SND_PURR);
}

void sound_eat(void) {
    psg_play(SND_EAT);
}

void sound_mew(void) {
    psg_play(SND_MEW);
}

void sound_sad(void) {
    psg_play(SND_SAD);
}

void sound_clean(void) {
    psg_play(SND_CLEAN);
}

void sound_medicine(void) {
    psg_play(SND_MEDICINE);
}

void sound_lights(bool off) {
    psg_play(off ? SND_LIGHTS_OFF : SND_LIGHTS_ON);
}

void sound_no(void) {
    psg_play(SND_NO);
}

void sound_ambient(int tod) {
    // About one call in 400 by day (every 7 s or so); crickets more often.
    int chance = tod == TOD_NIGHT ? 150 : tod == TOD_DAY ? 400 : 0;
    if (chance && random_range(0, chance - 1) == 0)
        psg_play(tod == TOD_NIGHT ? SND_CRICKET : SND_BIRD);
}

void sound_bat(void) {
    psg_play(SND_BAT);
}

void sound_tap(void) {
    psg_play(SND_TAP);
}

void sound_miss(void) {
    psg_play(SND_MISS);
}
