// The game's sounds, on the tone generators, so that a press is heard at
// once (a sampled effect would wait for the mixer's next frame or two).
//
// The four lanes' blips are short square-wave notes of a C major chord, low
// to high from LEFT to A: the left thumb's lanes on square 1, the right
// thumb's on square 2, so two lanes pressed together, one with each thumb,
// both sound. They are quiet beside the music (volume 8 of 15, fading in a
// few frames): a tick under the beat, not a melody over it.

#include "sound.h"

#define BLIP(ch, hz)                                                                               \
    {.channel = (ch), .duty = PSG_DUTY_25, .frequency = (hz), .frames = 4, .volume = 8, .fade = -1}

static const u16 select_notes[] = {1568, 2093};

static const PsgSound sounds[SND_EFFECTS] = {
    [SND_LANE + 0] = BLIP(PSG_SQUARE1, 1047), // C6
    [SND_LANE + 1] = BLIP(PSG_SQUARE1, 1319), // E6
    [SND_LANE + 2] = BLIP(PSG_SQUARE2, 1568), // G6
    [SND_LANE + 3] = BLIP(PSG_SQUARE2, 2093), // C7
    [SND_MOVE] =
        {.channel = PSG_SQUARE2, .duty = PSG_DUTY_12, .frequency = 1760, .frames = 2, .volume = 7},
    [SND_SELECT] = {.channel = PSG_SQUARE2,
                    .duty = PSG_DUTY_25,
                    .frames = 4,
                    .notes = select_notes,
                    .note_count = 2,
                    .volume = 9},
    [SND_PAUSE] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_50,
                   .frequency = 880,
                   .frames = 5,
                   .volume = 8,
                   .fade = -1},
};

static const PsgSound* sound_table[SND_EFFECTS];

void sound_init(void) {
    for (int i = 0; i < SND_EFFECTS; i++)
        sound_table[i] = &sounds[i];
    psg_table_set(sound_table, SND_EFFECTS);
}

void sound_play(int effect) {
    psg_play((u16)effect);
}
