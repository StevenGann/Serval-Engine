#ifndef SERVAL_CORE_PSG_WAVE_H
#define SERVAL_CORE_PSG_WAVE_H

// Engine-internal: the wave channel's portable parts (psg_wave.c), which
// src/gba/wave.c programs the hardware with: pitch, the volume levels, the
// fade the engine steps, and which waveform a note plays. Not part of the
// public API. See docs/audio.md#wave-channel.

#include "serval/audio.h"

// The waveform played with no table registered (psg_waves_set): a triangle,
// 0, 1, ... 15, 15, 14, ... 0, as wave RAM holds it.
extern const u32 serval_psg_triangle[4];

// Frequency register value for the wave channel at hz: its 32 steps play at
// 2097152 / (2048 - n) steps a second, so the waveform sounds at
// 65536 / (2048 - n) Hz. Below 32 Hz plays at 32 Hz.
u16 serval_psg_wave_rate(u32 hz);

// Frequency register value for a note (PSG_C0 ... PSG_B10, clamped there) on
// the wave channel: the square table's value for the note an octave up
// (65536 / f = 131072 / 2f), so notes below PSG_C1 play at 32 Hz. The top
// octave, past the square table, is halfway between its square value and
// 2048.
u16 serval_psg_wave_note_rate(u32 note);

// SOUND3CNT_H's volume bits for a volume 0-15: 0 is silent; 1-15 play as the
// nearest of the four levels, 25%, 50%, 75% and 100% of 15 (1-5, 6-9, 10-13,
// 14-15).
u16 serval_psg_wave_volume(u32 volume);

// The wave channel has no envelope, so the engine steps .fade once a frame,
// timed as the hardware steps the other channels': a volume step every
// `step` 64ths of a second, up or down, until 15 or 0. The timer counts CPU
// cycles in units of 64: a frame (280896 cycles) adds 4389, a 64th of a
// second (262144) is 4096, both exact, so the fade never drifts.
typedef struct {
    u16 timer; // units of 64 cycles since the last step
    u8 volume; // 0-15
    u8 step;   // 64ths of a second per volume step, 1-7; 0: holds
    bool up;   // fading in
} PsgWaveEnvelope;

#define SERVAL_PSG_WAVE_FRAME_UNITS 4389u // a frame: 280896 cycles / 64
#define SERVAL_PSG_WAVE_STEP_UNITS 4096u  // a 64th of a second: 262144 cycles / 64

// Starts the envelope of a note from its control bits (serval_psg_control:
// volume in bits 12-15, fading in at bit 11, step in bits 8-10).
void serval_psg_wave_envelope_start(PsgWaveEnvelope* e, u32 control);

// Steps the envelope by one frame. Returns true if the volume changed. At 15
// (fading in) or 0 (fading out) it stops: .step becomes 0.
bool serval_psg_wave_envelope_step(PsgWaveEnvelope* e);

// The waveform a note with .duty `duty` plays from the table psg_waves_set()
// registered (`count` waveforms; NULL and 0 for none): waves[duty], or
// waves[0] if duty is `count` or more (warns once); with no table, the
// triangle, which a duty past 0 also plays (warns once).
const u32* serval_psg_wave_pick(const u32* waves, u32 count, u32 duty);

// Makes serval_psg_wave_pick's problem reportable again (psg_table_set() and
// psg_waves_set() call it).
void serval_psg_wave_reset_warnings(void);

#endif // SERVAL_CORE_PSG_WAVE_H
