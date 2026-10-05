// Sound for the web backend: emulates the GBA's PSG channels (the four Game
// Boy tone generators) from the sound registers the GBA code writes.
//
// Semantics follow GBATEK. The hardware reacts to each register write; here
// the registers are only seen once per call (once per video frame), so every
// write made during a frame takes effect at the start of the next call:
//
// - A trigger bit (bit 15 of SOUND1CNT_X, SOUND2CNT_H, SOUND3CNT_X,
//   SOUND4CNT_H) set in io means "written with trigger since the last call":
//   the channel restarts and the bit is cleared.
// - Frequency, duty, length enable, panning and volumes are read every call.
// - The envelope (initial volume, direction, step time) is latched at trigger,
//   as on hardware. Turning the DAC off (initial volume 0, decreasing; for the
//   wave channel, SOUND3CNT_L bit 7 clear) stops the channel at once.
// - The length fields are write-only on hardware, so a write is detected as a
//   change of value. A trigger with an expired length counter reloads it from
//   the register (hardware reloads the maximum unless the field was rewritten,
//   and rewriting the same value can't be seen here; games that use lengths
//   rewrite them before triggering).
// - Wave RAM: io holds the bank the CPU can access (the one not selected for
//   playback); both banks are kept here. Writes go to the bank the CPU saw at
//   the last call, except that writes with no net bank switch but a channel 3
//   restart go to the playing bank (see apply_wave). Loading both banks in
//   one frame (64-sample waves) keeps only the last bank written: load them
//   on different frames.
//
// Output is box-filtered (each sample is the exact average of the channel
// levels over its interval), which suppresses most aliasing of high square
// waves and fast noise, then AC-coupled by a DC-blocking high-pass filter as
// the hardware's output is.

#include "web.h"

// I/O register indices (byte offset / 2).
#define SOUND1CNT_L (0x60 / 2) // sweep
#define SOUND1CNT_H (0x62 / 2) // duty, length, envelope
#define SOUND1CNT_X (0x64 / 2) // frequency, length enable, trigger
#define SOUND2CNT_L (0x68 / 2) // duty, length, envelope
#define SOUND2CNT_H (0x6C / 2) // frequency, length enable, trigger
#define SOUND3CNT_L (0x70 / 2) // dimension, bank, DAC enable
#define SOUND3CNT_H (0x72 / 2) // length, volume
#define SOUND3CNT_X (0x74 / 2) // frequency, length enable, trigger
#define SOUND4CNT_L (0x78 / 2) // length, envelope
#define SOUND4CNT_H (0x7C / 2) // divider, width, shift, length enable, trigger
#define SOUNDCNT_L (0x80 / 2)  // PSG master volume and panning
#define SOUNDCNT_H (0x82 / 2)  // PSG volume ratio, Direct Sound control
#define SOUNDCNT_X (0x84 / 2)  // master enable, channel status
#define WAVE_RAM (0x90 / 2)    // 16 bytes

#define TRIGGER 0x8000
#define LENGTH_ENABLE 0x4000
#define MASTER_ENABLE 0x0080

// Times are in cycles of the 4.19 MHz Game Boy sound clock (a quarter of the
// GBA's CPU clock), which every PSG period is a whole multiple of.
#define CLOCK 4194304.0
#define SEQUENCER_PERIOD 8192.0 // frame sequencer: 512 Hz

// One channel at full volume, master volume 7 and PSG ratio 100% swings
// +-7.5 (half of 15) before scaling: this makes it +-0.25, so all four
// channels at full volume reach +-1.
#define OUTPUT_SCALE (0.25f / 7.5f)

// High-pass corner of the DC blocker (Hz).
#define DC_CORNER 20.0f

enum { CH_SQUARE1, CH_SQUARE2, CH_WAVE, CH_NOISE, CH_COUNT };

typedef struct {
    bool active;     // playing (the status bit in SOUNDCNT_X)
    u32 length;      // length counter: clocks left at 256 Hz
    u32 length_bits; // length field last seen in io (-1: never)
    u32 freq;        // 11-bit frequency register value (squares, wave)
    u32 volume;      // current envelope volume 0-15
    u32 env_period;  // envelope step time latched at trigger (0: off)
    u32 env_timer;   // 64 Hz clocks to the next envelope step
    bool env_up;     // envelope direction latched at trigger
    double timer;    // cycles to the next waveform step
    u32 pos;         // duty step 0-7, or wave sample position 0-63
    u32 shadow;      // channel 1: sweep shadow frequency
    u32 sweep_timer; // channel 1: 128 Hz clocks to the next sweep step
    bool sweep_on;   // channel 1
} Channel;

static struct {
    double cycles_per_sample; // 0 until web_audio_init
    double sequencer_timer;   // cycles to the next frame sequencer step
    u32 sequencer_step;       // 0-7
    bool enabled;             // master enable seen at the last call
    Channel ch[CH_COUNT];
    u16 lfsr;
    u8 wave[2][16];  // both wave RAM banks
    u32 cpu_bank;    // the bank io's wave RAM shows
    float dc_coeff;  // DC blocker pole
    float dc_in[2];  // last input, left and right
    float dc_out[2]; // last output, left and right
} apu;

// Duty cycle waveforms (12.5%, 25%, 50%, 75%), one bit per step.
static const u8 duty_patterns[4] = {0x80, 0x81, 0xE1, 0x7E};

static void reset_channels(void) {
    for (u32 i = 0; i < CH_COUNT; i++) {
        apu.ch[i] = (Channel){0};
        apu.ch[i].length_bits = 0xFFFFFFFFu;
    }
    apu.lfsr = 0x7FFF;
}

void web_audio_init(u32 sample_rate) {
    apu.cycles_per_sample = sample_rate ? CLOCK / sample_rate : 0;
    apu.sequencer_timer = SEQUENCER_PERIOD;
    apu.sequencer_step = 0;
    apu.enabled = false;
    reset_channels();
    for (u32 b = 0; b < 2; b++)
        for (u32 i = 0; i < 16; i++)
            apu.wave[b][i] = 0;
    apu.cpu_bank = 1; // bank select 0 at power-on: the CPU sees bank 1
    // One-pole high-pass: y = x - x1 + r * y1, r = 1 - 2 pi fc / fs (close
    // enough to exp(-2 pi fc / fs) for fc << fs, without needing libm).
    float r = sample_rate ? 1.0f - 6.2831853f * DC_CORNER / (float)sample_rate : 0.0f;
    apu.dc_coeff = r < 0 ? 0 : r;
    apu.dc_in[0] = apu.dc_in[1] = 0;
    apu.dc_out[0] = apu.dc_out[1] = 0;
}

// --- Register handling -------------------------------------------------------

// The DAC is on unless the initial volume is 0 and the envelope decreases.
static bool envelope_dac_on(u16 env) {
    return (env & 0xF800) != 0;
}

// Detects writes to a length field (write-only on hardware).
static void update_length(Channel* c, u32 bits, u32 max) {
    if (bits != c->length_bits) {
        c->length_bits = bits;
        c->length = max - bits;
    }
}

static void restart_envelope(Channel* c, u16 env) {
    c->volume = env >> 12;
    c->env_up = (env & 0x0800) != 0;
    c->env_period = (env >> 8) & 7;
    c->env_timer = c->env_period;
}

// Restarts the length counter on trigger (see the header comment).
static void restart_length(Channel* c, u32 max) {
    if (c->length == 0)
        c->length = max - c->length_bits;
}

// Channel 1's next sweep frequency.
static u32 sweep_next(const Channel* c, u16 sweep) {
    u32 delta = c->shadow >> (sweep & 7);
    return (sweep & 0x0008) ? c->shadow - delta : c->shadow + delta;
}

static void apply_square(u16* io, u32 index, u32 env_reg, u32 freq_reg) {
    Channel* c = &apu.ch[index];
    u16 env = io[env_reg];
    u16 freq = io[freq_reg];
    update_length(c, env & 63u, 64);
    c->freq = freq & 0x7FFu;
    if (freq & TRIGGER) {
        io[freq_reg] = (u16)(freq & ~TRIGGER);
        restart_length(c, 64);
        restart_envelope(c, env);
        c->timer = 4.0 * (2048 - c->freq);
        c->active = true;
        if (index == CH_SQUARE1) {
            u16 sweep = io[SOUND1CNT_L];
            u32 time = (sweep >> 4) & 7;
            c->shadow = c->freq;
            c->sweep_timer = time ? time : 8;
            c->sweep_on = time != 0 || (sweep & 7) != 0;
            if ((sweep & 7) && sweep_next(c, sweep) > 2047)
                c->active = false;
        }
    }
    if (!envelope_dac_on(env))
        c->active = false;
}

static void apply_wave(u16* io) {
    Channel* c = &apu.ch[CH_WAVE];
    u16 bank = io[SOUND3CNT_L];

    // Wave RAM. The CPU can only write the bank that isn't playing, so a
    // game loads a wave by selecting the other bank, writing, and selecting
    // the bank back, usually all in one frame, then restarting the channel.
    // Seen once per frame, that looks like writes to the CPU's bank with no
    // switch; a restart in the same frame tells it apart (the wave was meant
    // for the playing bank).
    u32 cpu_bank = ((bank >> 6) & 1) ^ 1;
    bool written = false;
    for (u32 i = 0; i < 8; i++)
        written |= io[WAVE_RAM + i] !=
                   (u16)(apu.wave[apu.cpu_bank][2 * i] | apu.wave[apu.cpu_bank][2 * i + 1] << 8);
    if (written) {
        u32 target = apu.cpu_bank;
        if (cpu_bank == apu.cpu_bank && (io[SOUND3CNT_X] & TRIGGER))
            target ^= 1;
        for (u32 i = 0; i < 8; i++) {
            apu.wave[target][2 * i] = (u8)io[WAVE_RAM + i];
            apu.wave[target][2 * i + 1] = (u8)(io[WAVE_RAM + i] >> 8);
        }
    }
    if (written || cpu_bank != apu.cpu_bank) {
        apu.cpu_bank = cpu_bank;
        for (u32 i = 0; i < 8; i++)
            io[WAVE_RAM + i] =
                (u16)(apu.wave[cpu_bank][2 * i] | apu.wave[cpu_bank][2 * i + 1] << 8);
    }

    u16 freq = io[SOUND3CNT_X];
    update_length(c, io[SOUND3CNT_H] & 0xFFu, 256);
    c->freq = freq & 0x7FFu;
    if (freq & TRIGGER) {
        io[SOUND3CNT_X] = (u16)(freq & ~TRIGGER);
        restart_length(c, 256);
        c->timer = 2.0 * (2048 - c->freq);
        c->pos = 0;
        c->active = true;
    }
    if (!(bank & 0x0080))
        c->active = false;
}

static void apply_noise(u16* io) {
    Channel* c = &apu.ch[CH_NOISE];
    u16 env = io[SOUND4CNT_L];
    u16 control = io[SOUND4CNT_H];
    update_length(c, env & 63u, 64);
    if (control & TRIGGER) {
        io[SOUND4CNT_H] = (u16)(control & ~TRIGGER);
        restart_length(c, 64);
        restart_envelope(c, env);
        apu.lfsr = (control & 0x0008) ? 0x7F : 0x7FFF;
        c->timer = 0;
        c->active = true;
    }
    if (!envelope_dac_on(env))
        c->active = false;
}

// Master disable: the PSG registers read as zero and can't be written (writes
// made while disabled are discarded here), the channels stop.
static void power_off(u16* io) {
    for (u32 i = SOUND1CNT_L; i <= SOUNDCNT_L; i++)
        io[i] = 0;
    io[SOUNDCNT_H] &= 0xFF00; // the Direct Sound bits survive
    reset_channels();
}

static void apply_registers(u16* io) {
    if (!(io[SOUNDCNT_X] & MASTER_ENABLE)) {
        power_off(io);
        apu.enabled = false;
        return;
    }
    if (!apu.enabled) {
        // Powering on restarts the frame sequencer.
        apu.enabled = true;
        apu.sequencer_step = 0;
        apu.sequencer_timer = SEQUENCER_PERIOD;
    }
    apply_square(io, CH_SQUARE1, SOUND1CNT_H, SOUND1CNT_X);
    apply_square(io, CH_SQUARE2, SOUND2CNT_L, SOUND2CNT_H);
    apply_wave(io);
    apply_noise(io);
}

// --- Frame sequencer -------------------------------------------------------

static void clock_length(u16* io, u32 index, u32 reg) {
    Channel* c = &apu.ch[index];
    if ((io[reg] & LENGTH_ENABLE) && c->length > 0 && --c->length == 0)
        c->active = false;
}

static void clock_sweep(u16* io) {
    Channel* c = &apu.ch[CH_SQUARE1];
    if (!c->active || !c->sweep_on || --c->sweep_timer > 0)
        return;
    u16 sweep = io[SOUND1CNT_L];
    u32 time = (sweep >> 4) & 7;
    c->sweep_timer = time ? time : 8;
    if (!time)
        return;
    u32 next = sweep_next(c, sweep);
    if (next > 2047) {
        c->active = false;
    } else if (sweep & 7) {
        c->shadow = next;
        c->freq = next;
        // The hardware writes the new frequency back to the register.
        io[SOUND1CNT_X] = (u16)((io[SOUND1CNT_X] & ~0x7FFu) | next);
        if (sweep_next(c, sweep) > 2047)
            c->active = false;
    }
}

static void clock_envelope(Channel* c) {
    if (!c->active || c->env_period == 0 || --c->env_timer > 0)
        return;
    c->env_timer = c->env_period;
    if (c->env_up && c->volume < 15)
        c->volume++;
    else if (!c->env_up && c->volume > 0)
        c->volume--;
}

static void sequencer_step(u16* io) {
    u32 step = apu.sequencer_step;
    apu.sequencer_step = (step + 1) & 7;
    if ((step & 1) == 0) { // 256 Hz
        clock_length(io, CH_SQUARE1, SOUND1CNT_X);
        clock_length(io, CH_SQUARE2, SOUND2CNT_H);
        clock_length(io, CH_WAVE, SOUND3CNT_X);
        clock_length(io, CH_NOISE, SOUND4CNT_H);
    }
    if (step == 2 || step == 6) // 128 Hz
        clock_sweep(io);
    if (step == 7) { // 64 Hz
        clock_envelope(&apu.ch[CH_SQUARE1]);
        clock_envelope(&apu.ch[CH_SQUARE2]);
        clock_envelope(&apu.ch[CH_NOISE]);
    }
}

// --- Waveforms -------------------------------------------------------------
//
// Each returns the channel's average level over the next dt cycles, centered
// on zero: a channel at volume v outputs +v/2 when its waveform is high and
// -v/2 when low (the hardware's 0..v, minus the DC that the output's AC
// coupling removes anyway; centering avoids a pop when a channel starts).

static double square_level(const Channel* c, u32 duty) {
    double half = c->volume * 0.5;
    return (duty_patterns[duty] >> (7 - c->pos)) & 1 ? half : -half;
}

static float square_average(Channel* c, u32 duty, double dt) {
    double period = 4.0 * (2048 - c->freq);
    double sum = 0, left = dt;
    while (c->timer <= left) {
        sum += square_level(c, duty) * c->timer;
        left -= c->timer;
        c->pos = (c->pos + 1) & 7;
        c->timer = period;
    }
    sum += square_level(c, duty) * left;
    c->timer -= left;
    return (float)(sum / dt);
}

// SOUND3CNT_H volume: 0%, 100%, 50%, 25%; bit 15 forces 75%.
static double wave_level(const Channel* c, u16 bank, u16 volume) {
    u32 play_bank = (bank >> 6) & 1;
    if (bank & 0x0020) // two banks: 64 samples, starting with the selected one
        play_bank ^= c->pos >> 5;
    u32 index = c->pos & 31;
    u8 byte = apu.wave[play_bank][index >> 1];
    u32 sample = index & 1 ? byte & 15u : byte >> 4u;
    static const double gain[4] = {0.0, 1.0, 0.5, 0.25};
    double g = (volume & 0x8000) ? 0.75 : gain[(volume >> 13) & 3];
    return (sample - 7.5) * g;
}

static float wave_average(Channel* c, u16 bank, u16 volume, double dt) {
    double period = 2.0 * (2048 - c->freq);
    u32 mask = (bank & 0x0020) ? 63 : 31;
    double sum = 0, left = dt;
    while (c->timer <= left) {
        sum += wave_level(c, bank, volume) * c->timer;
        left -= c->timer;
        c->pos = (c->pos + 1) & mask;
        c->timer = period;
    }
    sum += wave_level(c, bank, volume) * left;
    c->timer -= left;
    return (float)(sum / dt);
}

static double noise_level(const Channel* c) {
    double half = c->volume * 0.5;
    return (apu.lfsr & 1) ? half : -half;
}

static void noise_clock(bool narrow) {
    u32 bit = apu.lfsr & 1u;
    apu.lfsr >>= 1;
    if (bit)
        apu.lfsr ^= narrow ? 0x60 : 0x6000;
}

// Noise clock: 524288 Hz / r / 2^(s+1), r = 0 meaning 0.5; s = 14 and 15
// stop the clock.
static float noise_average(Channel* c, u16 control, double dt) {
    u32 shift = (control >> 4) & 15;
    if (shift >= 14)
        return (float)noise_level(c);
    u32 divider = control & 7;
    double period = (double)((divider ? 16u * divider : 8u) << shift);
    bool narrow = (control & 0x0008) != 0;
    double sum = 0, left = dt;
    while (c->timer <= left) {
        sum += noise_level(c) * c->timer;
        left -= c->timer;
        noise_clock(narrow);
        c->timer = period;
    }
    sum += noise_level(c) * left;
    c->timer -= left;
    return (float)(sum / dt);
}

// --- Mixing ----------------------------------------------------------------

static float dc_block(u32 side, float x) {
    float y = x - apu.dc_in[side] + apu.dc_coeff * apu.dc_out[side];
    apu.dc_in[side] = x;
    apu.dc_out[side] = y;
    if (y > 1.0f)
        return 1.0f;
    if (y < -1.0f)
        return -1.0f;
    return y;
}

void web_audio_generate(u16* io, float* out, u32 count) {
    if (apu.cycles_per_sample <= 0) {
        for (u32 i = 0; i < count * 2; i++)
            out[i] = 0;
        return;
    }
    apply_registers(io);
    double dt = apu.cycles_per_sample;

    for (u32 i = 0; i < count; i++) {
        float levels[CH_COUNT] = {0};
        if (apu.enabled) {
            apu.sequencer_timer -= dt;
            while (apu.sequencer_timer <= 0) {
                sequencer_step(io);
                apu.sequencer_timer += SEQUENCER_PERIOD;
            }
            // Duty, frequency and volume can change between steps.
            if (apu.ch[CH_SQUARE1].active)
                levels[CH_SQUARE1] =
                    square_average(&apu.ch[CH_SQUARE1], (io[SOUND1CNT_H] >> 6) & 3u, dt);
            if (apu.ch[CH_SQUARE2].active)
                levels[CH_SQUARE2] =
                    square_average(&apu.ch[CH_SQUARE2], (io[SOUND2CNT_L] >> 6) & 3u, dt);
            if (apu.ch[CH_WAVE].active)
                levels[CH_WAVE] =
                    wave_average(&apu.ch[CH_WAVE], io[SOUND3CNT_L], io[SOUND3CNT_H], dt);
            if (apu.ch[CH_NOISE].active)
                levels[CH_NOISE] = noise_average(&apu.ch[CH_NOISE], io[SOUND4CNT_H], dt);
        }

        // SOUNDCNT_L: master volume (0-7 means 1/8 to 8/8) and per-channel
        // enables, right in the low bits, left in the high ones. SOUNDCNT_H
        // bits 0-1: PSG at 25%, 50% or 100% (3 is prohibited; taken as 100%).
        u16 mix = io[SOUNDCNT_L];
        static const float ratio[4] = {0.25f, 0.5f, 1.0f, 1.0f};
        float gain = ratio[io[SOUNDCNT_H] & 3] * OUTPUT_SCALE / 8.0f;
        float right = 0, left = 0;
        for (u32 c = 0; c < CH_COUNT; c++) {
            if (mix & (0x0100u << c))
                right += levels[c];
            if (mix & (0x1000u << c))
                left += levels[c];
        }
        left *= gain * (float)(((mix >> 4) & 7) + 1);
        right *= gain * (float)((mix & 7) + 1);
        // TODO: Direct Sound A and B (DMA-fed sample FIFOs, SOUNDCNT_H bits
        // 2-3 and 8-15) mix in here once the web backend emulates DMA audio.
        out[2 * i] = dc_block(0, left);
        out[2 * i + 1] = dc_block(1, right);
    }

    // SOUNDCNT_X bits 0-3 report which channels are playing.
    u16 status = 0;
    for (u32 c = 0; c < CH_COUNT; c++)
        if (apu.ch[c].active)
            status |= (u16)(1u << c);
    io[SOUNDCNT_X] = (u16)((io[SOUNDCNT_X] & ~0x000Fu) | status);
}
