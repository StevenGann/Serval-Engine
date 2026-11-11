// Life's sound, on the tone generators. The world plays a gentle tune of its
// own: while it runs, a soft pluck on the wave channel, its pitch the
// population on a minor pentatonic scale over two octaves (any two notes of
// which sound well together), louder the more cells were born; at most about
// eight a second, and none when nothing changed. A still life is silent; an
// oscillator plays a little loop. The edits click and thump on the squares.

#include "life.h"

// A round waveform, a sine with a little of its octave: a soft, plucked
// voice where a square would buzz.
static const u32 waves[4] = {0xEEDDBC89, 0x98BACBDD, 0x32446587, 0x46231221};

// A minor pentatonic from A3 to A5, at three volumes: the wave channel's
// 25%, 50% and 75%.
#define SCALE 11
#define LEVELS 3

#define PLUCK(f, v) {.channel = PSG_WAVE, .volume = (v), .fade = -2, .frequency = (f), .frames = 0}
#define PLUCKS(v)                                                                                  \
    PLUCK(220, v), PLUCK(262, v), PLUCK(294, v), PLUCK(330, v), PLUCK(392, v), PLUCK(440, v),      \
        PLUCK(523, v), PLUCK(587, v), PLUCK(659, v), PLUCK(784, v), PLUCK(880, v)
static const PsgSound plucks[LEVELS * SCALE] = {PLUCKS(4), PLUCKS(8), PLUCKS(11)};

static const u16 open_notes[] = {523, 784};
static const u16 close_notes[] = {784, 523};
static const u16 start_notes[] = {440, 523, 659, 880};
static const u16 run_notes[] = {587, 880};
static const u16 pause_notes[] = {880, 587};

static const PsgSound effects[SFX_COUNT] = {
    [SFX_ON] = {.channel = PSG_SQUARE2,
                .duty = PSG_DUTY_12,
                .volume = 7,
                .fade = -1,
                .frequency = 1568,
                .frames = 3},
    [SFX_OFF] = {.channel = PSG_SQUARE2,
                 .duty = PSG_DUTY_12,
                 .volume = 6,
                 .fade = -1,
                 .frequency = 1046,
                 .frames = 3},
    [SFX_STAMP] = {.channel = PSG_SQUARE1,
                   .duty = PSG_DUTY_50,
                   .volume = 12,
                   .fade = -2,
                   .slide = -2,
                   .slide_size = 3,
                   .frequency = 330,
                   .frames = 14},
    [SFX_TURN] = {.channel = PSG_SQUARE2,
                  .duty = PSG_DUTY_25,
                  .volume = 6,
                  .fade = -1,
                  .frequency = 1175,
                  .frames = 2},
    [SFX_CLEAR] = {.channel = PSG_SQUARE1,
                   .duty = PSG_DUTY_25,
                   .volume = 10,
                   .fade = -4,
                   .slide = -3,
                   .slide_size = 2,
                   .frequency = 1046,
                   .frames = 30},
    [SFX_RANDOM] = {.channel = PSG_SQUARE1,
                    .duty = PSG_DUTY_25,
                    .volume = 10,
                    .fade = -4,
                    .slide = 3,
                    .slide_size = 2,
                    .frequency = 262,
                    .frames = 30},
    [SFX_MOVE] = {.channel = PSG_SQUARE2,
                  .duty = PSG_DUTY_12,
                  .volume = 5,
                  .fade = -1,
                  .frequency = 660,
                  .frames = 2},
    [SFX_CHANGE] = {.channel = PSG_SQUARE2,
                    .duty = PSG_DUTY_25,
                    .volume = 6,
                    .fade = -1,
                    .frequency = 990,
                    .frames = 2},
    [SFX_OPEN] = {.channel = PSG_SQUARE2,
                  .duty = PSG_DUTY_25,
                  .volume = 7,
                  .frames = 3,
                  .notes = open_notes,
                  .note_count = 2},
    [SFX_CLOSE] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_25,
                   .volume = 7,
                   .frames = 3,
                   .notes = close_notes,
                   .note_count = 2},
    [SFX_START] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_25,
                   .volume = 9,
                   .fade = -6,
                   .frames = 4,
                   .notes = start_notes,
                   .note_count = 4},
    [SFX_RUN] = {.channel = PSG_SQUARE2,
                 .duty = PSG_DUTY_25,
                 .volume = 7,
                 .frames = 3,
                 .notes = run_notes,
                 .note_count = 2},
    [SFX_PAUSE] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_25,
                   .volume = 7,
                   .frames = 3,
                   .notes = pause_notes,
                   .note_count = 2},
    [SFX_STEP] = {.channel = PSG_SQUARE2,
                  .duty = PSG_DUTY_12,
                  .volume = 5,
                  .fade = -1,
                  .frequency = 1318,
                  .frames = 2},
    [SFX_ZOOM_IN] = {.channel = PSG_SQUARE1,
                     .duty = PSG_DUTY_12,
                     .volume = 6,
                     .fade = -2,
                     .slide = 4,
                     .slide_size = 6,
                     .frequency = 440,
                     .frames = 10},
    [SFX_ZOOM_OUT] = {.channel = PSG_SQUARE1,
                      .duty = PSG_DUTY_12,
                      .volume = 6,
                      .fade = -2,
                      .slide = -2,
                      .slide_size = 3,
                      .frequency = 660,
                      .frames = 10},
};

// The sound table: the effects, then the plucks, level after level.
#define PLUCK_ID(level, note) (SFX_COUNT + (level) * SCALE + (note))
static const PsgSound* table[SFX_COUNT + LEVELS * SCALE];

static bool sound_on = true;
static u32 since_note;

void sound_init(void) {
    for (int i = 0; i < SFX_COUNT; i++)
        table[i] = &effects[i];
    for (int i = 0; i < LEVELS * SCALE; i++)
        table[SFX_COUNT + i] = &plucks[i];
    psg_waves_set(waves, 1);
    psg_table_set(table, SFX_COUNT + LEVELS * SCALE);
}

void sound_set(bool on) {
    sound_on = on;
    if (!on)
        psg_stop_all();
}

void sound_play(u32 sfx) {
    if (sound_on && sfx < SFX_COUNT)
        psg_play((u16)sfx);
}

void sound_generation(const LifeStats* st, u32 births, u32 deaths, u32 speed) {
    if (births == 0 && deaths == 0) {
        since_note = 0;
        return;
    }
    // About eight notes a second at most: every generation at 8 a second
    // and slower, every second at 15, and so on.
    u32 every = (speed + 7) / 8;
    if (++since_note < every)
        return;
    since_note = 0;
    if (!sound_on)
        return;
    u32 level = births > 40 ? 2 : births > 4 ? 1 : 0;
    psg_play((u16)PLUCK_ID(level, st->population % SCALE));
}
