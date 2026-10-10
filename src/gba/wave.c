#include "serval/audio.h"

#include <tonc.h>

#include "../core/psg_wave.h"
#include "../core/warn.h"
#include "internal.h"

// wave channel: PSG tone channel 3 (PSG_WAVE, docs/audio.md#wave-channel). It
// plays 32 4-bit steps from wave RAM, which has two 16-byte banks: the one
// selected for playback (SOUND3CNT_L bit 6) and the one the CPU reads and
// writes at 0x4000090. A note with another waveform than the one playing
// writes it into the idle bank and selects that bank; a note with the same
// waveform only restarts the channel. Volume has four levels and no envelope,
// so serval_wave_update() steps .fade once a frame (psg_wave.h). psg.c and
// music.c reach the channel through serval_psg_tone() and serval_psg_quiet(),
// as the others; sound effects, priorities and music work as on them.
//
// The state is in EWRAM: games that never play the wave channel keep the
// IWRAM they had before it, but for psg.c's voice for channel 3 and
// serval_wave_fading, which serval_psg_update() tests every frame.

typedef struct {
    const u32* table;         // psg_waves_set()'s waveforms; NULL: the triangle
    const u32* playing;       // the waveform in the bank playing; NULL: none yet
    PsgWaveEnvelope envelope; // the note's volume and fade
    u8 count;                 // waveforms in table
    u8 bank;                  // the bank selected for playback (0 or 1)
} Wave;

static SERVAL_EWRAM_BSS Wave wave;

u8 serval_wave_fading;

#define DAC_ON 0x0080 // SOUND3CNT_L bit 7: the channel plays
#define BANK_SHIFT 6  // SOUND3CNT_L bit 6: the bank playing (32 steps, one bank)
#define RESTART 0x8000

void serval_wave_tone(u16 control, u16 rate) {
    const u32* w = serval_psg_wave_pick(wave.table, wave.count, control & 0xFFu);
    if (w != wave.playing) {
        // The CPU reaches the bank that isn't playing: write the waveform
        // there, then play that bank.
        vu32* ram = REG_WAVE_RAM;
        ram[0] = w[0];
        ram[1] = w[1];
        ram[2] = w[2];
        ram[3] = w[3];
        wave.bank ^= 1;
        wave.playing = w;
    }
    serval_psg_wave_envelope_start(&wave.envelope, control);
    serval_wave_fading = wave.envelope.step != 0;
    REG_SND3SEL = (u16)(DAC_ON | wave.bank << BANK_SHIFT);
    REG_SND3CNT = serval_psg_wave_volume(wave.envelope.volume);
    REG_SND3FREQ = (u16)(RESTART | rate);
}

void serval_wave_quiet(void) {
    // Turning the DAC off stops the channel at once; the bank stays selected.
    REG_SND3SEL = (u16)(wave.bank << BANK_SHIFT);
    serval_wave_fading = 0;
}

void serval_wave_update(void) {
    if (serval_psg_wave_envelope_step(&wave.envelope))
        REG_SND3CNT = serval_psg_wave_volume(wave.envelope.volume);
    serval_wave_fading = wave.envelope.step != 0; // 0 once at 0 or 15
}

void psg_waves_set(const u32* waves, u8 count) {
    serval_psg_wave_reset_warnings();
    if (count && (!serval_plausible_pointer(waves) || ((uintptr_t)waves & 3))) {
        // Called rarely (at startup): reported every time.
        SERVAL_WARN("psg_waves_set: the waveform table is NULL or not a valid (word-aligned) "
                    "pointer; it is ignored, and the waveforms registered before play on");
        return;
    }
    wave.table = count ? waves : NULL;
    wave.count = count;
    // The next note copies its waveform afresh, even one of the same table.
    wave.playing = NULL;
}

u32 serval_wave_bank(void) {
    return wave.bank;
}
