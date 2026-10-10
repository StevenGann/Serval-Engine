// The wave channel's portable parts: pitch, volume levels, the fade the engine
// steps, and which waveform a note plays. See psg_wave.h; src/gba/wave.c
// programs the hardware.

#include "psg_wave.h"

#include "psg_sequencer.h"
#include "warn.h"

// 0, 1, ... 15 rising, then 15, 14, ... 0: the first word's lowest byte holds
// steps 0 (high nibble) and 1.
const u32 serval_psg_triangle[4] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476};

u16 serval_psg_wave_rate(u32 hz) {
    if (hz < 32)
        hz = 32;
    return (u16)(2048 - 65536 / hz);
}

u16 serval_psg_wave_note_rate(u32 note) {
    if (note > SERVAL_PSG_NOTE_MAX)
        note = SERVAL_PSG_NOTE_MAX;
    if (note + 12 <= SERVAL_PSG_NOTE_MAX)
        return serval_psg_square_rates[note + 12];
    // The top octave: half the square's period 2048 - n.
    return (u16)((2048u + serval_psg_square_rates[note]) / 2);
}

// SOUND3CNT_H bits 13-15 by volume: 0%, then 25% (3 << 13), 50% (2 << 13),
// 75% (bit 15, which forces 75%) and 100% (1 << 13), each for the volumes
// nearest it.
static const u8 levels[16] = {0x00, 0x60, 0x60, 0x60, 0x60, 0x60, 0x40, 0x40,
                              0x40, 0x40, 0x80, 0x80, 0x80, 0x80, 0x20, 0x20};

u16 serval_psg_wave_volume(u32 volume) {
    return (u16)(levels[volume & 15] << 8);
}

void serval_psg_wave_envelope_start(PsgWaveEnvelope* e, u32 control) {
    e->volume = (u8)(control >> 12 & 15);
    e->step = (u8)(control >> 8 & 7);
    e->up = (control & 1u << 11) != 0;
    e->timer = 0;
    // A fade with nowhere to go holds, as the hardware's envelope does.
    if ((e->up && e->volume == 15) || (!e->up && e->volume == 0))
        e->step = 0;
}

bool serval_psg_wave_envelope_step(PsgWaveEnvelope* e) {
    if (!e->step)
        return false;
    u32 period = e->step * SERVAL_PSG_WAVE_STEP_UNITS;
    u32 timer = e->timer + SERVAL_PSG_WAVE_FRAME_UNITS;
    u32 volume = e->volume;
    // At most two steps a frame (step 1: 4389 / 4096 of one).
    while (timer >= period) {
        timer -= period;
        volume = e->up ? volume + 1 : volume - 1;
        if (volume == 0 || volume == 15) {
            e->step = 0;
            break;
        }
    }
    e->timer = (u16)timer;
    if (volume == e->volume)
        return false;
    e->volume = (u8)volume;
    return true;
}

#ifdef SERVAL_DEBUG
static bool warned;
#endif

void serval_psg_wave_reset_warnings(void) {
#ifdef SERVAL_DEBUG
    warned = false;
#endif
}

const u32* serval_psg_wave_pick(const u32* waves, u32 count, u32 duty) {
    if (!count) {
        waves = serval_psg_triangle;
        count = 1;
#ifdef SERVAL_DEBUG
        if (duty && !warned) {
            warned = true;
            SERVAL_WARN("PSG_WAVE: .duty %u picks a waveform, but none are registered "
                        "(psg_waves_set); the built-in triangle plays",
                        duty);
        }
#endif
    } else if (duty >= count) {
#ifdef SERVAL_DEBUG
        if (!warned) {
            warned = true;
            SERVAL_WARN("PSG_WAVE: .duty %u is past the %u waveforms registered "
                        "(psg_waves_set); waveform 0 plays",
                        duty, count);
        }
#endif
    }
    return duty < count ? waves + 4 * duty : waves;
}
