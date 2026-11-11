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
    SND_JUMP,
    SND_SNAP,
    SND_KNOCK,
    SND_RUSTLE, // five loudnesses in a row
    SND_RUSTLE_END = SND_RUSTLE + 4,
    SND_FLUTTER,
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
static const u16 snap_notes[] = {1800, 4200, 900};
static const u16 knock_notes[] = {262, 196, 147};
static const u16 rustle_notes[] = {7200, 9800, 6400, 10400};
static const u16 flutter_notes[] = {2400, 3000, 2500, 3300, 2700, 3600};

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
    // Acacia Climb: a springing chirp, a crack of wood, a bonk off a nest.
    [SND_JUMP] = {.channel = PSG_SQUARE1,
                  .duty = PSG_DUTY_25,
                  .frequency = 500,
                  .slide = 3,
                  .slide_size = 3,
                  .frames = 7,
                  .volume = 8,
                  .fade = -2,
                  .priority = 1},
    [SND_SNAP] = {.channel = PSG_NOISE,
                  .notes = snap_notes,
                  .note_count = 3,
                  .frames = 3,
                  .volume = 12,
                  .fade = -2,
                  .priority = 3},
    [SND_KNOCK] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_50,
                   .notes = knock_notes,
                   .note_count = 3,
                   .frames = 4,
                   .volume = 9,
                   .priority = 3},
    // High Pounce: the grass rustling, quiet to loud (noise, hissy, low
    // priority so that the catches and misses win), and a bird's wings.
    [SND_RUSTLE] = {.channel = PSG_NOISE,
                    .notes = rustle_notes,
                    .note_count = 4,
                    .frames = 3,
                    .volume = 2,
                    .fade = -1,
                    .priority = 1},
    [SND_RUSTLE + 1] = {.channel = PSG_NOISE,
                        .notes = rustle_notes,
                        .note_count = 4,
                        .frames = 3,
                        .volume = 4,
                        .fade = -1,
                        .priority = 1},
    [SND_RUSTLE + 2] = {.channel = PSG_NOISE,
                        .notes = rustle_notes,
                        .note_count = 4,
                        .frames = 3,
                        .volume = 8,
                        .fade = -1,
                        .priority = 1},
    [SND_RUSTLE + 3] = {.channel = PSG_NOISE,
                        .notes = rustle_notes,
                        .note_count = 4,
                        .frames = 3,
                        .volume = 13,
                        .fade = -1,
                        .priority = 1},
    [SND_RUSTLE + 4] = {.channel = PSG_NOISE,
                        .notes = rustle_notes,
                        .note_count = 4,
                        .frames = 3,
                        .volume = 15,
                        .fade = -1,
                        .priority = 1},
    [SND_FLUTTER] = {.channel = PSG_SQUARE2,
                     .duty = PSG_DUTY_12,
                     .notes = flutter_notes,
                     .note_count = 6,
                     .frames = 2,
                     .volume = 6,
                     .priority = 2},
};

// --- Tunes (PsgSong, once each) ---------------------------------------------------------------

// Growing up: a rising call and its answer.
static const PsgNote grow_lead[] = {
    {PSG_C5, 2},   {PSG_E5, 2}, {PSG_G5, 2}, {PSG_C6, 4},
    {PSG_REST, 2}, {PSG_G5, 2}, {PSG_C6, 2}, {PSG_E6, 8},
};
static const PsgNote grow_bass[] = {
    {PSG_C4, 6}, {PSG_G4, 4}, {PSG_REST, 2}, {PSG_E4, 4}, {PSG_C5, 8},
};
static const PsgTrack grow_tracks[] = {
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_25,
     .volume = 11,
     .fade = -1,
     .notes = grow_lead,
     .note_count = 8,
     .loop = PSG_NO_LOOP},
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_50,
     .volume = 7,
     .fade = -1,
     .notes = grow_bass,
     .note_count = 5,
     .loop = PSG_NO_LOOP},
};
static const PsgSong grow_song = {.tempo = 150, .tracks = grow_tracks, .track_count = 2};

// The litter: a bouncing tune that settles home.
static const PsgNote litter_lead[] = {
    {PSG_E5, 2}, {PSG_G5, 2}, {PSG_A5, 4}, {PSG_G5, 2}, {PSG_E5, 2}, {PSG_D5, 4},
    {PSG_E5, 2}, {PSG_G5, 2}, {PSG_A5, 2}, {PSG_B5, 2}, {PSG_C6, 8},
};
static const PsgNote litter_bass[] = {
    {PSG_C4, 4}, {PSG_G4, 4}, {PSG_A3, 4}, {PSG_G3, 4}, {PSG_F4, 4}, {PSG_G4, 4}, {PSG_C4, 8},
};
static const PsgTrack litter_tracks[] = {
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_12,
     .volume = 11,
     .fade = -1,
     .notes = litter_lead,
     .note_count = 11,
     .loop = PSG_NO_LOOP},
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_50,
     .volume = 7,
     .fade = -1,
     .notes = litter_bass,
     .note_count = 7,
     .loop = PSG_NO_LOOP},
};
static const PsgSong litter_song = {.tempo = 132, .tracks = litter_tracks, .track_count = 2};

// The tombstone: slow and quiet, in A minor, ending on the low A.
static const PsgNote tomb_lead[] = {
    {PSG_A4, 4}, {PSG_C5, 4}, {PSG_E5, 8}, {PSG_D5, 4},  {PSG_C5, 4},
    {PSG_B4, 8}, {PSG_C5, 4}, {PSG_B4, 4}, {PSG_A4, 16},
};
static const PsgNote tomb_bass[] = {
    {PSG_A3, 16},
    {PSG_F3, 8},
    {PSG_G3, 8},
    {PSG_A3, 16},
};
static const PsgTrack tomb_tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_12,
     .volume = 8,
     .fade = -1,
     .notes = tomb_lead,
     .note_count = 9,
     .loop = PSG_NO_LOOP},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 5,
     .fade = -1,
     .notes = tomb_bass,
     .note_count = 4,
     .loop = PSG_NO_LOOP},
};
static const PsgSong tomb_song = {.tempo = 72, .tracks = tomb_tracks, .track_count = 2};

void sound_fanfare_grow(void) {
    psg_music_play(&grow_song);
}

void sound_fanfare_litter(void) {
    psg_music_play(&litter_song);
}

void sound_tune_tomb(void) {
    psg_music_play(&tomb_song);
}

void sound_tune_stop(void) {
    psg_music_stop();
}

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

void sound_jump(void) {
    psg_play(SND_JUMP);
}

void sound_snap(void) {
    psg_play(SND_SNAP);
}

void sound_knock(void) {
    psg_play(SND_KNOCK);
}

void sound_rustle(int loudness) {
    if (loudness >= 1 && loudness <= 5)
        psg_play((u16)(SND_RUSTLE + loudness - 1));
}

void sound_flutter(void) {
    psg_play(SND_FLUTTER);
}
