#include "serval/audio.h"

#include <tonc.h>

#include "../core/warn.h"
#include "internal.h"

// PSG sound effects on tone channels 1 (square with sweep), 2 (square) and 4
// (noise). The hardware plays each tone and its volume envelope by itself;
// the engine only times notes and lengths, once per frame (serval_psg_update,
// called by frame_end).

#define CHANNELS 3

typedef struct {
    const PsgSound* sound; // NULL: idle
    u32 note;              // index into sound->notes
    u32 frames_left;       // of the current note; 0 with sound set: no time limit
} Voice;

static const PsgSound* const* psg_table;
static u16 psg_count;
static Voice voices[CHANNELS];

// Last frequency register value written per channel. The hardware's square
// frequency bits are write-only, so this is the only way to read them back.
static u16 rates[CHANNELS];

u16 serval_psg_rate(u32 channel) {
    return channel < CHANNELS ? rates[channel] : 0;
}

// Control register (SOUNDxCNT_H for squares, SOUND4CNT_L for noise): duty,
// envelope and starting volume.
static u16 control_bits(const PsgSound* s) {
    u32 volume = s->volume ? s->volume : 15;
    u32 duty = s->duty ? s->duty : PSG_DUTY_50;
    u32 bits = (volume & 15) << 12;
    if (s->fade > 0)
        bits |= 1u << 11 | (u32)(s->fade & 7) << 8; // fade in
    else if (s->fade < 0)
        bits |= (u32)(-s->fade & 7) << 8; // fade out
    if (s->channel != PSG_NOISE)
        bits |= duty << 6;
    return (u16)bits;
}

// Frequency register value for a square wave: f = 131072 / (2048 - n).
static u16 square_rate(u32 hz) {
    if (hz < 64)
        hz = 64;
    if (hz > 131072)
        hz = 131072;
    return (u16)(2048 - 131072 / hz);
}

// Noise frequency register: f = 524288 / r / 2^(s+1), with r = 0 meaning 0.5.
// Picks the r and s that come closest to hz.
static u16 noise_rate(u32 hz) {
    u32 best = 0, best_error = 0xFFFFFFFF;
    for (u32 s = 0; s < 14; s++) {
        for (u32 r = 0; r < 8; r++) {
            u32 f = r ? (524288u / r) >> (s + 1) : (1048576u >> (s + 1));
            u32 error = f > hz ? f - hz : hz - f;
            if (error < best_error) {
                best_error = error;
                best = s << 4 | r;
            }
        }
    }
    return (u16)best;
}

static void silence(u32 channel) {
    // Volume 0 with a restart makes the channel go quiet immediately.
    switch (channel) {
    case PSG_SQUARE1:
        REG_SND1SWEEP = 0x0008; // sweep off
        REG_SND1CNT = 0;
        REG_SND1FREQ = 0x8000;
        break;
    case PSG_SQUARE2:
        REG_SND2CNT = 0;
        REG_SND2FREQ = 0x8000;
        break;
    case PSG_NOISE:
        REG_SND4CNT = 0;
        REG_SND4FREQ = 0x8000;
        break;
    }
    voices[channel].sound = NULL;
}

// Starts a tone at hz on the sound's channel (0 Hz: a rest, silent).
static void start_tone(const PsgSound* s, u32 hz) {
    if (hz == 0) {
        switch (s->channel) {
        case PSG_SQUARE1:
            REG_SND1CNT = 0;
            REG_SND1FREQ = 0x8000;
            break;
        case PSG_SQUARE2:
            REG_SND2CNT = 0;
            REG_SND2FREQ = 0x8000;
            break;
        default:
            REG_SND4CNT = 0;
            REG_SND4FREQ = 0x8000;
            break;
        }
        return;
    }
    u16 control = control_bits(s);
    switch (s->channel) {
    case PSG_SQUARE1: {
        u32 sweep = 0x0008; // no sweep
        if (s->slide) {
            u32 time = (u32)(s->slide < 0 ? -s->slide : s->slide) & 7;
            u32 size = s->slide_size ? s->slide_size & 7 : 1;
            sweep = time << 4 | (s->slide < 0 ? 0x0008 : 0) | size;
        }
        REG_SND1SWEEP = (u16)sweep;
        REG_SND1CNT = control;
        rates[PSG_SQUARE1] = square_rate(hz);
        REG_SND1FREQ = (u16)(0x8000 | rates[PSG_SQUARE1]);
        break;
    }
    case PSG_SQUARE2:
        REG_SND2CNT = control;
        rates[PSG_SQUARE2] = square_rate(hz);
        REG_SND2FREQ = (u16)(0x8000 | rates[PSG_SQUARE2]);
        break;
    default:
        REG_SND4CNT = control;
        rates[PSG_NOISE] = noise_rate(hz);
        REG_SND4FREQ = (u16)(0x8000 | rates[PSG_NOISE]);
        break;
    }
}

void serval_psg_init(void) {
    REG_SNDSTAT = SSTAT_ENABLE; // must come before the other sound registers
    // Tone generators at full volume, channels 1, 2 and 4 on both speakers.
    REG_SNDDMGCNT = SDMG_BUILD_LR(SDMG_SQR1 | SDMG_SQR2 | SDMG_NOISE, 7);
    REG_SNDDSCNT = SDS_DMG100;
    psg_stop_all();
}

void psg_table_set(const PsgSound* const* table, u16 count) {
    psg_table = table;
    psg_count = count;
    psg_stop_all();
}

void psg_stop_all(void) {
    for (u32 c = 0; c < CHANNELS; c++)
        silence(c);
}

void psg_play(u16 sound_id) {
    if (sound_id >= psg_count) {
        SERVAL_WARN("psg_play: sound ID %u is not in the sound table (%u sounds)", sound_id,
                    psg_count);
        return;
    }
    const PsgSound* s = psg_table[sound_id];
    if (s->channel >= CHANNELS) {
        SERVAL_WARN("psg_play: sound %u has an invalid channel (%u)", sound_id, s->channel);
        return;
    }
    Voice* v = &voices[s->channel];
    v->sound = s;
    v->note = 0;
    v->frames_left = s->frames;
    start_tone(s, s->note_count ? s->notes[0] : s->frequency);
}

void serval_psg_update(void) {
    for (u32 c = 0; c < CHANNELS; c++) {
        Voice* v = &voices[c];
        if (!v->sound || v->frames_left == 0 || --v->frames_left > 0)
            continue;
        const PsgSound* s = v->sound;
        if (s->note_count && ++v->note < s->note_count) {
            v->frames_left = s->frames;
            start_tone(s, s->notes[v->note]);
        } else {
            silence(c);
        }
    }
}
