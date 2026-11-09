// The sound, all on the tone generators (audio.h), all written for this
// example.
//
// Channels:
// - The wave channel is the engine's drone, and nothing else uses it. The
//   engine can't change a playing tone's pitch, so the drone is DRONE_STEPS
//   sounds at rising pitches, each holding until replaced (.frames 0, .fade
//   0), and a new one is played only when the car's speed reaches another
//   step: a few times a second while accelerating, never at a steady speed.
//   Each step comes in a semitone and three quarters above the one below,
//   about 1.75 octaves from idle to top speed. A little slack on the way down
//   keeps a speed sitting on a step's edge from flicking between two.
// - The race tune plays its melody on square 2, its bass on square 1 and
//   its drums on the noise channel. The effects are on square 1 (the bass
//   drops out under them for a moment) but for the crash and the off-road
//   rumble, on the noise channel (the drums drop out instead).
// - Priorities: the countdown, the checkpoint and lap jingles, the pause
//   blip, the record and erased tones are JINGLE_PRIORITY; a bump and the
//   crash's tone KNOCK_PRIORITY; the skid's squeal 0. So a squeal held through
//   a checkpoint can't cut its jingle, and a bump cuts the squeal. On the
//   noise channel the crash is above the rumble, which is 0.

#include "sound.h"

#define JINGLE_PRIORITY 2
#define KNOCK_PRIORITY 1

#define DRONE_STEPS 16
// A step is a third of a track unit a frame of speed (Race.speed is FIXED):
// speed * 3 >> 8, 0 at a standstill (idle) to 15 at top speed (5.25 units a
// frame make 15.75).
#define DRONE_STEP(speed) (((speed) * 3) >> 8)
// The slack on the way down, track units a frame (an eighth of a step).
#define DRONE_SLACK 10
// The drone's volume: the wave channel's 50%, of a waveform that peaks at 14
// of 15. Under the music and well under the effects.
#define DRONE_VOLUME 8

// The held effects are played again as each ends, while their event lasts.
#define SKID_FRAMES 8
#define RUMBLE_FRAMES 6

// The table's sounds past the effects game.c plays: the held ones and the
// drone's.
enum {
    SND_SKID = SND_EFFECTS,
    SND_RUMBLE,
    SND_DRONE_OFF,
    SND_DRONE,
    SOUND_COUNT = SND_DRONE + DRONE_STEPS,
};

// The engine's waveform: a falling saw with a kick at the start of each
// half, 32 steps (high nibble of each byte first, from the lowest byte up):
// 14 14 14 13 10 10 9 9 9 8 8 8 7 7 7 6 / 9 8 8 8 4 4 4 3 3 3 2 2 2 1 1 0.
// The two halves differ, so it growls rather than buzzes.
static const u32 drone_wave[4] = {0x99aaedee, 0x76778898, 0x43448898, 0x10212233};

// Note frequencies in Hz, for the effects.
#define A4 440
#define B5 988
#define E6 1319
#define GS6 1661
#define B6 1976
#define D7 2349
#define G6 1568
#define G7 3136

static const u16 checkpoint_notes[] = {B5, E6, GS6};
static const u16 lap_notes[] = {E6, B5, E6, GS6, B6};
static const u16 record_notes[] = {G6, B6, D7, G7};
static const u16 erased_notes[] = {880, 660, 440, 220};
// The squeal: a high, unsteady pitch.
static const u16 skid_notes[] = {1480, 1320, 1400, 1250};

#define DRONE(hz) {.channel = PSG_WAVE, .frequency = (hz), .volume = DRONE_VOLUME}

static const PsgSound sounds[SOUND_COUNT] = {
    [SND_COUNT] = {.duty = PSG_DUTY_25,
                   .frequency = A4,
                   .frames = 12,
                   .volume = 15,
                   .fade = -2,
                   .priority = JINGLE_PRIORITY},
    [SND_GO] = {.duty = PSG_DUTY_25,
                .frequency = 2 * A4,
                .frames = 36,
                .volume = 15,
                .fade = -4,
                .priority = JINGLE_PRIORITY},
    [SND_CHECKPOINT] = {.duty = PSG_DUTY_25,
                        .frames = 5,
                        .notes = checkpoint_notes,
                        .note_count = 3,
                        .priority = JINGLE_PRIORITY},
    [SND_LAP] = {.duty = PSG_DUTY_25,
                 .frames = 5,
                 .notes = lap_notes,
                 .note_count = 5,
                 .priority = JINGLE_PRIORITY},
    [SND_BUMP] = {.frequency = 220,
                  .slide = -1,
                  .slide_size = 3,
                  .frames = 10,
                  .volume = 14,
                  .fade = -1,
                  .priority = KNOCK_PRIORITY},
    [SND_CRASH] = {.channel = PSG_NOISE,
                   .frequency = 400,
                   .volume = 10,
                   .fade = -4,
                   .priority = JINGLE_PRIORITY},
    [SND_CRASH_TONE] = {.duty = PSG_DUTY_12,
                        .frequency = 600,
                        .slide = -2,
                        .slide_size = 2,
                        .frames = 30,
                        .volume = 10,
                        .fade = -3,
                        .priority = KNOCK_PRIORITY},
    [SND_PAUSE] = {.duty = PSG_DUTY_25,
                   .frequency = 1047,
                   .frames = 4,
                   .volume = 10,
                   .priority = JINGLE_PRIORITY},
    [SND_RECORD] = {.duty = PSG_DUTY_25,
                    .frames = 5,
                    .notes = record_notes,
                    .note_count = 4,
                    .priority = JINGLE_PRIORITY},
    [SND_ERASED] = {.duty = PSG_DUTY_50,
                    .frames = 6,
                    .notes = erased_notes,
                    .note_count = 4,
                    .volume = 12,
                    .priority = JINGLE_PRIORITY},
    [SND_SKID] = {.duty = PSG_DUTY_12,
                  .frames = SKID_FRAMES / 4,
                  .notes = skid_notes,
                  .note_count = 4,
                  .volume = 10},
    [SND_RUMBLE] = {.channel = PSG_NOISE, .frequency = 120, .frames = RUMBLE_FRAMES, .volume = 9},
    // The drone off: the lowest level, gone within a frame.
    [SND_DRONE_OFF] = {.channel = PSG_WAVE, .frequency = 50, .volume = 1, .fade = -1},
    // 50 Hz at idle, then each a semitone and three quarters up.
    [SND_DRONE + 0] = DRONE(50),
    [SND_DRONE + 1] = DRONE(54),
    [SND_DRONE + 2] = DRONE(59),
    [SND_DRONE + 3] = DRONE(64),
    [SND_DRONE + 4] = DRONE(69),
    [SND_DRONE + 5] = DRONE(75),
    [SND_DRONE + 6] = DRONE(81),
    [SND_DRONE + 7] = DRONE(88),
    [SND_DRONE + 8] = DRONE(95),
    [SND_DRONE + 9] = DRONE(104),
    [SND_DRONE + 10] = DRONE(112),
    [SND_DRONE + 11] = DRONE(122),
    [SND_DRONE + 12] = DRONE(132),
    [SND_DRONE + 13] = DRONE(143),
    [SND_DRONE + 14] = DRONE(155),
    [SND_DRONE + 15] = DRONE(168),
};

static const PsgSound* sound_table[SOUND_COUNT];

// --- Music ---------------------------------------------------------------------------------
//
// "Dust Road": E minor, 150 beats per minute, 8 bars of 4/4 that loop,
// about 13 seconds. A tick is a 16th note, so a bar is 16 ticks. A lead
// that climbs and runs over Em C D Em, then Em C D B; a bass pumping
// eighths between the root and its octave; drums in eighths.

static const PsgNote race_melody[] = {
    // Em
    {PSG_E5, 2},
    {PSG_G5, 2},
    {PSG_B5, 4},
    {PSG_A5, 2},
    {PSG_G5, 2},
    {PSG_E5, 4},
    // C
    {PSG_E5, 2},
    {PSG_G5, 2},
    {PSG_C6, 4},
    {PSG_B5, 2},
    {PSG_G5, 2},
    {PSG_E5, 2},
    {PSG_D5, 2},
    // D
    {PSG_D5, 2},
    {PSG_FS5, 2},
    {PSG_A5, 4},
    {PSG_G5, 2},
    {PSG_FS5, 2},
    {PSG_D5, 4},
    // Em
    {PSG_E5, 4},
    {PSG_B4, 2},
    {PSG_E5, 2},
    {PSG_G5, 8},
    // Em
    {PSG_B5, 2},
    {PSG_B5, 2},
    {PSG_A5, 2},
    {PSG_B5, 2},
    {PSG_G5, 4},
    {PSG_E5, 4},
    // C
    {PSG_C6, 2},
    {PSG_B5, 2},
    {PSG_A5, 2},
    {PSG_G5, 2},
    {PSG_E5, 4},
    {PSG_G5, 4},
    // D
    {PSG_A5, 2},
    {PSG_G5, 2},
    {PSG_FS5, 2},
    {PSG_A5, 2},
    {PSG_D6, 4},
    {PSG_A5, 4},
    // B
    {PSG_B5, 6},
    {PSG_A5, 2},
    {PSG_FS5, 2},
    {PSG_DS5, 2},
    {PSG_B4, 4},
};

// A bar of eighths: root, root, octave, root, root, octave, fifth, octave
// (each the track's .length of 2 ticks).
#define BASS_BAR(r)                                                                                \
    {(r), 0}, {(r), 0}, {(r) + 12, 0}, {(r), 0}, {(r), 0}, {(r) + 12, 0}, {(r) + 7, 0}, {          \
        (r) + 12, 0                                                                                \
    }

static const PsgNote race_bass[] = {
    BASS_BAR(PSG_E2), BASS_BAR(PSG_C2), BASS_BAR(PSG_D2), BASS_BAR(PSG_E2),
    BASS_BAR(PSG_E2), BASS_BAR(PSG_C2), BASS_BAR(PSG_D2), BASS_BAR(PSG_B2),
};

// Kick (a low rumble), snare and hi-hat (a short hiss), in 8th notes.
#define KICK PSG_A2
#define SNARE PSG_E5
#define HAT PSG_C8
static const PsgNote race_drums[] = {
    {KICK, 2}, {HAT, 2}, {SNARE, 2}, {HAT, 2}, {KICK, 2}, {KICK, 2}, {SNARE, 2}, {HAT, 2},
};

static const PsgTrack race_tracks[] = {
    {.channel = PSG_SQUARE2,
     .duty = PSG_DUTY_25,
     .volume = 7,
     .fade = -3,
     .notes = race_melody,
     .note_count = sizeof race_melody / sizeof race_melody[0]},
    {.channel = PSG_SQUARE1,
     .duty = PSG_DUTY_50,
     .volume = 6,
     .fade = -2,
     .length = 2,
     .notes = race_bass,
     .note_count = sizeof race_bass / sizeof race_bass[0]},
    {.channel = PSG_NOISE,
     .volume = 5,
     .fade = -1,
     .notes = race_drums,
     .note_count = sizeof race_drums / sizeof race_drums[0]},
};

// The title's jingle: the tune's opening run, up to a long E.
static const PsgNote title_melody[] = {
    {PSG_E5, 2}, {PSG_G5, 2}, {PSG_B5, 2}, {PSG_E6, 2}, {PSG_D6, 2}, {PSG_B5, 2}, {PSG_E6, 12},
};
static const PsgNote title_bass[] = {
    {PSG_E3, 4}, {PSG_D3, 4}, {PSG_C3, 4}, {PSG_B2, 4}, {PSG_E2, 8},
};

// The finish: up through E major to a held high E.
static const PsgNote finish_melody[] = {
    {PSG_B4, 2}, {PSG_E5, 2}, {PSG_GS5, 2}, {PSG_B5, 4},  {PSG_GS5, 2},
    {PSG_B5, 2}, {PSG_E6, 4}, {PSG_DS6, 2}, {PSG_E6, 12},
};
static const PsgNote finish_bass[] = {
    {PSG_E3, 4}, {PSG_B2, 4}, {PSG_E3, 2}, {PSG_GS3, 2}, {PSG_B3, 4}, {PSG_E2, 16},
};
static const PsgNote finish_drums[] = {
    {SNARE, 1}, {SNARE, 1},    {SNARE, 2}, {PSG_REST, 4}, {SNARE, 1}, {SNARE, 1},
    {SNARE, 2}, {PSG_REST, 4}, {KICK, 2},  {SNARE, 2},    {KICK, 4},
};

// Time up: falling, and ending low and unresolved.
static const PsgNote time_up_melody[] = {
    {PSG_B5, 4}, {PSG_G5, 4}, {PSG_E5, 4}, {PSG_DS5, 4}, {PSG_C5, 6}, {PSG_B4, 14},
};
static const PsgNote time_up_bass[] = {
    {PSG_E3, 8},
    {PSG_C3, 8},
    {PSG_A2, 6},
    {PSG_B2, 14},
};

#define ONCE(ch, d, vol, fd, n)                                                                    \
    {.channel = (ch),                                                                              \
     .duty = (d),                                                                                  \
     .volume = (vol),                                                                              \
     .fade = (fd),                                                                                 \
     .notes = (n),                                                                                 \
     .note_count = sizeof(n) / sizeof(n)[0],                                                       \
     .loop = PSG_NO_LOOP}

static const PsgTrack title_tracks[] = {
    ONCE(PSG_SQUARE2, PSG_DUTY_25, 10, -5, title_melody),
    ONCE(PSG_SQUARE1, PSG_DUTY_50, 8, -4, title_bass),
};
static const PsgTrack finish_tracks[] = {
    ONCE(PSG_SQUARE2, PSG_DUTY_25, 11, -6, finish_melody),
    ONCE(PSG_SQUARE1, PSG_DUTY_50, 9, -5, finish_bass),
    ONCE(PSG_NOISE, 0, 7, -1, finish_drums),
};
static const PsgTrack time_up_tracks[] = {
    ONCE(PSG_SQUARE2, PSG_DUTY_50, 10, -7, time_up_melody),
    ONCE(PSG_SQUARE1, PSG_DUTY_50, 8, -6, time_up_bass),
};

static const PsgSong tunes[TUNES] = {
    [TUNE_TITLE] = {.tempo = 150, .tracks = title_tracks, .track_count = 2},
    [TUNE_RACE] = {.tempo = 150, .tracks = race_tracks, .track_count = 3},
    [TUNE_FINISH] = {.tempo = 150, .tracks = finish_tracks, .track_count = 3},
    [TUNE_TIME_UP] = {.tempo = 120, .tracks = time_up_tracks, .track_count = 2},
};

// --- Playing ----------------------------------------------------------------------------------

static int drone = -1;  // the drone's step playing, -1 none
static int skid_left;   // frames until the squeal is played again, 0: at once
static int rumble_left; // ... and the rumble

void sound_init(void) {
    for (int i = 0; i < SOUND_COUNT; i++)
        sound_table[i] = &sounds[i];
    psg_table_set(sound_table, SOUND_COUNT);
    psg_waves_set(drone_wave, 1);
}

void sound_play(int effect) {
    psg_play((u16)effect);
}

void sound_tune(int tune) {
    psg_music_play(&tunes[tune]);
}

void sound_quiet(void) {
    psg_music_stop();
    sound_drone_off();
}

void sound_drone_off(void) {
    if (drone >= 0)
        psg_play(SND_DRONE_OFF);
    drone = -1;
    skid_left = rumble_left = 0;
}

// Plays the held effect `id` each `frames` while `held`; the next hold
// starts it at once.
static void hold(int* left, bool held, int id, int frames) {
    if (!held) {
        *left = 0;
    } else if (--*left <= 0) {
        psg_play((u16)id);
        *left = frames;
    }
}

void sound_drive(const Race* r, u32 ev) {
    int step = int_min(DRONE_STEP(r->speed), DRONE_STEPS - 1);
    if (drone >= 0 && step < drone && DRONE_STEP(r->speed + DRONE_SLACK) >= drone)
        step = drone; // not far enough below the step to leave it
    if (step != drone) {
        drone = step;
        psg_play((u16)(SND_DRONE + step));
    }
    hold(&skid_left, ev & RACE_EV_SKID, SND_SKID, SKID_FRAMES);
    hold(&rumble_left, ev & RACE_EV_OFFROAD, SND_RUMBLE, RUMBLE_FRAMES);
}

void sound_pause(bool paused) {
    if (paused) {
        psg_music_pause();
        sound_drone_off();
    } else {
        psg_music_resume();
    }
    psg_play(SND_PAUSE);
}
