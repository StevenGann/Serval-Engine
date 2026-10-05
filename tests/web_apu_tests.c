// Host tests for the web backend's sound (src/web/apu.c): synthetic register
// states in a plain array standing in for the GBA's I/O registers.

#include "../src/web/web.h"
#include "test.h"

#define RATE 48000
#define MAX_SAMPLES (2 * RATE)

// Register indices (byte offset / 2).
#define SOUND1CNT_L (0x60 / 2)
#define SOUND1CNT_H (0x62 / 2)
#define SOUND1CNT_X (0x64 / 2)
#define SOUND2CNT_L (0x68 / 2)
#define SOUND2CNT_H (0x6C / 2)
#define SOUND3CNT_L (0x70 / 2)
#define SOUND3CNT_H (0x72 / 2)
#define SOUND3CNT_X (0x74 / 2)
#define SOUND4CNT_L (0x78 / 2)
#define SOUND4CNT_H (0x7C / 2)
#define SOUNDCNT_L (0x80 / 2)
#define SOUNDCNT_H (0x82 / 2)
#define SOUNDCNT_X (0x84 / 2)
#define WAVE_RAM (0x90 / 2)

static u16 io[512];
static float out[2 * MAX_SAMPLES];
static u32 rate;

static float absf(float x) {
    return x < 0 ? -x : x;
}

// Power-on, then sound enabled: every channel on both sides at full volume.
static void setup_rate(u32 sample_rate) {
    for (u32 i = 0; i < 512; i++)
        io[i] = 0;
    rate = sample_rate;
    web_audio_init(sample_rate);
    io[SOUNDCNT_X] = 0x0080;
    io[SOUNDCNT_L] = 0xFF77;
    io[SOUNDCNT_H] = 0x0002;
}

static void setup(void) {
    setup_rate(RATE);
}

// Generates n sample frames at out[2 * start], in per-frame calls of varying
// (odd and even) sizes like the web glue's.
static void run(u32 start, u32 n) {
    static const u32 sizes[] = {803, 804, 803, 1, 799, 806};
    u32 k = 0;
    while (n > 0) {
        u32 c = sizes[k++ % 6];
        if (c > n)
            c = n;
        web_audio_generate(io, out + 2 * start, c);
        start += c;
        n -= c;
    }
}

// Frequency of the left channel over [a, b), from its rising zero crossings.
static float pitch(u32 a, u32 b) {
    u32 first = 0, last = 0, crossings = 0;
    for (u32 i = a + 1; i < b; i++) {
        if (out[2 * (i - 1)] <= 0 && out[2 * i] > 0) {
            if (crossings == 0)
                first = i;
            last = i;
            crossings++;
        }
    }
    if (crossings < 2)
        return 0;
    return (float)(crossings - 1) * (float)rate / (float)(last - first);
}

// Peak-to-peak of one side over [a, b).
static float swing(u32 side, u32 a, u32 b) {
    float lo = 0, hi = 0;
    for (u32 i = a; i < b; i++) {
        float v = out[2 * i + side];
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
    }
    return hi - lo;
}

// Fraction of the left channel's samples over [a, b) above zero.
static float high_fraction(u32 a, u32 b) {
    u32 high = 0;
    for (u32 i = a; i < b; i++)
        high += out[2 * i] > 0;
    return (float)high / (float)(b - a);
}

static bool near(float value, float expected, float tolerance) {
    return absf(value - expected) <= tolerance;
}

static u32 ms(u32 t) {
    return rate * t / 1000;
}

// Square channel 2 at 50% duty, full volume, no envelope.
static void play_square2(u32 n, u32 duty) {
    io[SOUND2CNT_L] = (u16)(0xF000 | duty << 6);
    io[SOUND2CNT_H] = (u16)(0x8000 | n);
}

static void square_pitch(void) {
    setup();
    play_square2(1750, 2); // 131072 / 298 = 439.8 Hz
    run(0, RATE);
    CHECK(near(pitch(ms(100), RATE), 439.84f, 0.5f));

    setup();
    play_square2(2000, 2); // 2730.7 Hz
    run(0, RATE / 2);
    CHECK(near(pitch(ms(100), RATE / 2), 2730.67f, 3.0f));
}

static void square_amplitude(void) {
    // Full volume, master 7, 100%: about +-0.25 (+-0.27 at 440 Hz: the DC
    // blocker's tilt adds a little to the peaks).
    setup();
    play_square2(1750, 2);
    run(0, ms(200));
    CHECK(near(swing(0, ms(100), ms(200)), 0.52f, 0.03f));
    CHECK(near(swing(1, ms(100), ms(200)), 0.52f, 0.03f));
}

static void square_duty(void) {
    static const float expected[4] = {0.125f, 0.25f, 0.5f, 0.75f};
    for (u32 duty = 0; duty < 4; duty++) {
        setup();
        play_square2(1536, duty); // 256 Hz
        run(0, ms(500));
        CHECK(near(high_fraction(ms(200), ms(500)), expected[duty], 0.02f));
    }
}

static void trigger_bit_cleared(void) {
    setup();
    play_square2(1750, 2);
    io[SOUND1CNT_H] = 0xF000;
    io[SOUND1CNT_X] = 0xC123;
    io[SOUND3CNT_X] = 0x8456;
    io[SOUND4CNT_L] = 0xF000;
    io[SOUND4CNT_H] = 0xC012;
    run(0, 10);
    CHECK(io[SOUND1CNT_X] == 0x4123);
    CHECK(io[SOUND2CNT_H] == 1750);
    CHECK(io[SOUND3CNT_X] == 0x0456);
    CHECK(io[SOUND4CNT_H] == 0x4012);
    // Channels 1, 2 and 4 play; 3 has its DAC off.
    CHECK((io[SOUNDCNT_X] & 0xF) == 0xB);
}

static void frequency_without_trigger(void) {
    setup();
    play_square2(1750, 2);
    run(0, ms(200));
    io[SOUND2CNT_H] = 1536; // no trigger: 256 Hz
    run(ms(200), ms(800));
    CHECK(near(pitch(ms(50), ms(200)), 439.84f, 1.0f));
    CHECK(near(pitch(ms(300), ms(1000)), 256.0f, 1.0f));
}

// Time of the last sample louder than a threshold.
static u32 last_sound(u32 n) {
    u32 last = 0;
    for (u32 i = 0; i < n; i++)
        if (absf(out[2 * i]) > 0.002f)
            last = i;
    return last;
}

static void envelope_decay(void) {
    // Volume 15 down one step per 1/64 s: silent after 15/64 s = 234 ms.
    setup();
    io[SOUND2CNT_L] = 0xF180;
    io[SOUND2CNT_H] = 0x8000 | 1750;
    run(0, RATE);
    u32 end = last_sound(RATE);
    CHECK(end > ms(220) && end < ms(250));
    // Halfway, about half as loud.
    float start = swing(0, ms(5), ms(15));
    CHECK(near(swing(0, ms(112), ms(122)) / start, 0.5f, 0.1f));

    // Step time 3: three times as long.
    setup();
    io[SOUND2CNT_L] = 0xF380;
    io[SOUND2CNT_H] = 0x8000 | 1750;
    run(0, RATE);
    end = last_sound(RATE);
    CHECK(end > ms(690) && end < ms(730));
}

static void envelope_rise(void) {
    // Volume 0 up, step time 1: full after 15/64 s.
    setup();
    io[SOUND2CNT_L] = 0x0980;
    io[SOUND2CNT_H] = 0x8000 | 1750;
    run(0, ms(400));
    CHECK(swing(0, ms(0), ms(10)) < 0.1f);
    CHECK(near(swing(0, ms(300), ms(400)), 0.52f, 0.03f));
}

static void length_stops_channel(void) {
    // Length enabled, field 0: 64/256 s.
    setup();
    io[SOUND2CNT_L] = 0xF080;
    io[SOUND2CNT_H] = 0xC000 | 1750;
    run(0, ms(500));
    u32 end = last_sound(ms(500));
    CHECK(end > ms(240) && end < ms(270));
    CHECK((io[SOUNDCNT_X] & 0x2) == 0);

    // Field 32: 32/256 s.
    setup();
    io[SOUND2CNT_L] = 0xF0A0;
    io[SOUND2CNT_H] = 0xC000 | 1750;
    run(0, ms(500));
    end = last_sound(ms(500));
    CHECK(end > ms(115) && end < ms(145));

    // Without length enable it keeps playing.
    setup();
    io[SOUND2CNT_L] = 0xF0A0;
    io[SOUND2CNT_H] = 0x8000 | 1750;
    run(0, ms(500));
    CHECK(last_sound(ms(500)) > ms(490));
}

static void sweep_changes_pitch(void) {
    // Up, time 7 (7/128 s), shift 2: 1024 -> 1280 -> 1600 -> 2000, then the
    // next step would pass 2047 and the channel stops.
    setup();
    io[SOUND1CNT_L] = 0x0072;
    io[SOUND1CNT_H] = 0xF080;
    io[SOUND1CNT_X] = 0x8000 | 1024;
    run(0, ms(80));
    CHECK(near(pitch(ms(10), ms(50)), 128.0f, 1.0f));
    CHECK((io[SOUND1CNT_X] & 0x7FF) == 1280); // written back
    run(ms(80), ms(30));
    CHECK(near(pitch(ms(60), ms(105)), 170.67f, 2.0f));
    run(ms(110), ms(190));
    CHECK((io[SOUNDCNT_X] & 0x1) == 0);
    u32 end = last_sound(ms(300));
    CHECK(end > ms(160) && end < ms(175));

    // Down, shift 1: 1024 -> 512 -> 256 ...
    setup();
    io[SOUND1CNT_L] = 0x0079;
    io[SOUND1CNT_H] = 0xF080;
    io[SOUND1CNT_X] = 0x8000 | 1024;
    run(0, ms(105));
    CHECK((io[SOUND1CNT_X] & 0x7FF) == 512);
    CHECK(near(pitch(ms(5), ms(50)), 128.0f, 2.0f));
    CHECK(near(pitch(ms(60), ms(105)), 85.33f, 3.0f));

    // The engine's "no sweep" (0x0008) leaves the pitch alone.
    setup();
    io[SOUND1CNT_L] = 0x0008;
    io[SOUND1CNT_H] = 0xF080;
    io[SOUND1CNT_X] = 0x8000 | 1750;
    run(0, RATE);
    CHECK((io[SOUND1CNT_X] & 0x7FF) == 1750);
    CHECK(near(pitch(ms(100), RATE), 439.84f, 0.5f));
}

// Mean absolute difference between the left channel and itself delayed by
// lag, over [a, b).
static float self_difference(u32 lag, u32 a, u32 b) {
    float sum = 0;
    for (u32 i = a; i < b; i++)
        sum += absf(out[2 * i] - out[2 * (i - lag)]);
    return sum / (float)(b - a);
}

static void noise_width(void) {
    // Divider 1, shift 3: 32768 Hz, one LFSR step per sample at this rate.
    // The 7-bit LFSR repeats every 127 steps; the 15-bit one doesn't.
    setup_rate(32768);
    io[SOUND4CNT_L] = 0xF000;
    io[SOUND4CNT_H] = 0x8000 | 0x0039;
    run(0, 32768);
    float narrow = self_difference(127, 16384, 32768);
    float narrow_swing = swing(0, 16384, 32768);

    setup_rate(32768);
    io[SOUND4CNT_L] = 0xF000;
    io[SOUND4CNT_H] = 0x8000 | 0x0031;
    run(0, 32768);
    float wide = self_difference(127, 16384, 32768);
    float wide_swing = swing(0, 16384, 32768);

    CHECK(narrow < 0.001f);
    CHECK(wide > 0.1f);
    CHECK(narrow_swing > 0.4f && wide_swing > 0.4f);
}

static void noise_rate(void) {
    // A slow clock (divider 7, shift 13: 4.6 Hz) holds its level between
    // steps; a fast one changes constantly.
    setup();
    io[SOUND4CNT_L] = 0xF000;
    io[SOUND4CNT_H] = 0x8000 | 0x00D7;
    run(0, ms(100));
    u32 changes = 0;
    for (u32 i = 1; i < ms(100); i++)
        changes += absf(out[2 * i] - out[2 * (i - 1)]) > 0.05f;
    CHECK(changes <= 1);

    setup();
    io[SOUND4CNT_L] = 0xF000;
    io[SOUND4CNT_H] = 0x8000 | 0x0021; // 65536 Hz
    run(0, ms(100));
    changes = 0;
    for (u32 i = 1; i < ms(100); i++)
        changes += absf(out[2 * i] - out[2 * (i - 1)]) > 0.05f;
    CHECK(changes > ms(30));
}

static void master_disable(void) {
    setup();
    play_square2(1750, 2);
    run(0, ms(100));
    io[SOUNDCNT_X] = 0;
    run(ms(100), ms(200));
    CHECK(swing(0, ms(200), ms(300)) < 0.001f);
    CHECK(io[SOUND2CNT_L] == 0 && io[SOUND2CNT_H] == 0 && io[SOUNDCNT_L] == 0);
    // Writes while disabled are discarded; re-enabling doesn't resume.
    io[SOUND2CNT_L] = 0xF080;
    run(ms(300), 10);
    CHECK(io[SOUND2CNT_L] == 0);
    io[SOUNDCNT_X] = 0x0080;
    io[SOUNDCNT_L] = 0xFF77;
    run(ms(300), ms(100));
    CHECK(swing(0, ms(350), ms(400)) < 0.001f);
}

static void dac_off(void) {
    // Volume 0, decreasing: the DAC turns off and the channel stops at once.
    setup();
    play_square2(1750, 2);
    run(0, ms(100));
    io[SOUND2CNT_L] = 0;
    run(ms(100), ms(200));
    CHECK(swing(0, ms(150), ms(300)) < 0.001f);
    CHECK((io[SOUNDCNT_X] & 0x2) == 0);

    // The engine's silence: control 0 with a trigger.
    setup();
    play_square2(1750, 2);
    run(0, ms(100));
    io[SOUND2CNT_L] = 0;
    io[SOUND2CNT_H] = 0x8000;
    run(ms(100), ms(200));
    CHECK(swing(0, ms(150), ms(300)) < 0.001f);

    // Volume 0 increasing keeps the DAC on.
    setup();
    io[SOUND2CNT_L] = 0x0800;
    io[SOUND2CNT_H] = 0x8000 | 1750;
    run(0, 10);
    CHECK((io[SOUNDCNT_X] & 0x2) != 0);
}

static void phase_continuity(void) {
    // One call, then many calls of odd sizes: the same samples.
    static float whole[2 * 4000];
    setup();
    io[SOUND1CNT_L] = 0x0011;
    io[SOUND1CNT_H] = 0xF180;
    io[SOUND1CNT_X] = 0x8000 | 1900;
    play_square2(1750, 1);
    io[SOUND4CNT_L] = 0xF000;
    io[SOUND4CNT_H] = 0x8000 | 0x0022;
    web_audio_generate(io, whole, 4000);

    setup();
    io[SOUND1CNT_L] = 0x0011;
    io[SOUND1CNT_H] = 0xF180;
    io[SOUND1CNT_X] = 0x8000 | 1900;
    play_square2(1750, 1);
    io[SOUND4CNT_L] = 0xF000;
    io[SOUND4CNT_H] = 0x8000 | 0x0022;
    static const u32 sizes[] = {1, 803, 3, 799, 805, 1, 1588};
    u32 at = 0;
    for (u32 k = 0; k < 7; k++) {
        web_audio_generate(io, out + 2 * at, sizes[k]);
        at += sizes[k];
    }
    CHECK(at == 4000);
    float worst = 0;
    for (u32 i = 0; i < 2 * 4000; i++) {
        float d = absf(out[i] - whole[i]);
        worst = d > worst ? d : worst;
    }
    CHECK(worst < 1e-6f);
}

static void mixing(void) {
    // Left only.
    setup();
    io[SOUNDCNT_L] = 0x2077;
    play_square2(1750, 2);
    run(0, ms(200));
    CHECK(near(swing(0, ms(100), ms(200)), 0.52f, 0.03f));
    CHECK(swing(1, 0, ms(200)) == 0);

    // Master volume: left 3 (4/8), right 7.
    setup();
    io[SOUNDCNT_L] = 0x2237;
    play_square2(1750, 2);
    run(0, ms(200));
    CHECK(near(swing(0, ms(100), ms(200)), 0.26f, 0.02f));
    CHECK(near(swing(1, ms(100), ms(200)), 0.52f, 0.03f));

    // PSG ratio 25%.
    setup();
    io[SOUNDCNT_H] = 0x0000;
    play_square2(1750, 2);
    run(0, ms(200));
    CHECK(near(swing(0, ms(100), ms(200)), 0.13f, 0.01f));

    // Two channels add up.
    setup();
    play_square2(1750, 2);
    io[SOUND1CNT_L] = 0x0008;
    io[SOUND1CNT_H] = 0xF080;
    io[SOUND1CNT_X] = 0x8000 | 1750;
    run(0, ms(200));
    CHECK(near(swing(0, ms(100), ms(200)), 1.04f, 0.06f));
}

// Wave RAM bank b gets a rising saw (0..15 twice), written through io the
// way a game does: select the other bank for playback first.
static void load_saw(u32 b) {
    io[SOUND3CNT_L] = (u16)((b ^ 1) << 6);
    run(0, 1);
    for (u32 i = 0; i < 8; i++) {
        u32 s = (i * 4) & 15;
        u32 b0 = s << 4 | (s + 1), b1 = (s + 2) << 4 | (s + 3);
        io[WAVE_RAM + i] = (u16)(b0 | b1 << 8);
    }
    run(0, 1);
}

static void wave_channel(void) {
    // 32 samples (bank 0), n = 1536: 65536 / 512 = 128 Hz per 32 samples,
    // and the saw repeats twice in them: 256 Hz.
    setup();
    load_saw(0);
    io[SOUND3CNT_L] = 0x0080; // DAC on, play bank 0
    io[SOUND3CNT_H] = 0x2000; // 100%
    io[SOUND3CNT_X] = 0x8000 | 1536;
    run(0, ms(500));
    CHECK(near(pitch(ms(100), ms(500)), 256.0f, 1.0f));
    float full = swing(0, ms(100), ms(500));
    CHECK(near(full, 0.5f, 0.05f));

    // Volume 50%, 25%, forced 75%.
    io[SOUND3CNT_H] = 0x4000;
    run(0, ms(300));
    CHECK(near(swing(0, ms(100), ms(300)) / full, 0.5f, 0.05f));
    io[SOUND3CNT_H] = 0x6000;
    run(0, ms(300));
    CHECK(near(swing(0, ms(100), ms(300)) / full, 0.25f, 0.05f));
    io[SOUND3CNT_H] = 0x8000;
    run(0, ms(300));
    CHECK(near(swing(0, ms(100), ms(300)) / full, 0.75f, 0.05f));

    // Bank 1 untouched (flat), two-bank mode: the saw plays for half of each
    // 64-sample cycle: rising crossings at 128 Hz pairs, the flat half silent.
    io[SOUND3CNT_H] = 0x2000;
    io[SOUND3CNT_L] = 0x00A0;
    io[SOUND3CNT_X] = 0x8000 | 1536;
    run(0, ms(500));
    // One period of the 64-sample cycle is 1/64 s; the saw half has two
    // crossings per cycle: 128 per second.
    CHECK(near(pitch(ms(100), ms(500)), 128.0f, 2.0f));
    // io now shows bank 1 (the CPU side), which is still empty.
    CHECK(io[WAVE_RAM] == 0);

    // Playing bank 1 (flat): no tone. io shows bank 0's saw.
    io[SOUND3CNT_L] = 0x00C0;
    io[SOUND3CNT_X] = 0x8000 | 1536;
    run(0, ms(300));
    CHECK(swing(0, ms(200), ms(300)) < 0.01f);
    CHECK(io[WAVE_RAM] == 0x2301);

    // DAC off silences.
    io[SOUND3CNT_L] = 0x0000;
    run(0, ms(300));
    CHECK(swing(0, ms(200), ms(300)) < 0.001f);
}

static void wave_same_frame_load(void) {
    // Select bank 1, write (to bank 0), select bank 0 and restart, all in one
    // frame: the saw plays from bank 0, and io shows bank 1 again.
    setup();
    io[SOUND3CNT_L] = 0x0040;
    for (u32 i = 0; i < 8; i++) {
        u32 s = (i * 4) & 15;
        io[WAVE_RAM + i] = (u16)((s << 4 | (s + 1)) | ((s + 2) << 4 | (s + 3)) << 8);
    }
    io[SOUND3CNT_L] = 0x0080;
    io[SOUND3CNT_H] = 0x2000;
    io[SOUND3CNT_X] = 0x8000 | 1536;
    run(0, ms(500));
    CHECK(near(pitch(ms(100), ms(500)), 256.0f, 1.0f));
    CHECK(io[WAVE_RAM] == 0);

    // While bank 0 plays, a write without restart goes to bank 1 (the
    // CPU's); switching to it next frame plays it: flat, so silence.
    for (u32 i = 0; i < 8; i++)
        io[WAVE_RAM + i] = 0x7777;
    run(0, ms(100));
    CHECK(near(pitch(ms(50), ms(100)), 256.0f, 2.0f));
    io[SOUND3CNT_L] = 0x00C0;
    run(0, ms(300));
    CHECK(swing(0, ms(200), ms(300)) < 0.01f);
    CHECK(io[WAVE_RAM] == 0x2301); // bank 0's saw
}

static void wave_length(void) {
    // Field 128: 128/256 s.
    setup();
    load_saw(0);
    io[SOUND3CNT_L] = 0x0080;
    io[SOUND3CNT_H] = 0x2080;
    io[SOUND3CNT_X] = 0xC000 | 1536;
    run(0, RATE);
    u32 end = last_sound(RATE);
    CHECK(end > ms(490) && end < ms(520));
}

static void uninitialized_is_silent(void) {
    web_audio_init(0);
    io[SOUNDCNT_X] = 0x80;
    io[SOUND2CNT_L] = 0xF080;
    io[SOUND2CNT_H] = 0x8000 | 1750;
    out[0] = 1;
    web_audio_generate(io, out, 100);
    CHECK(swing(0, 0, 100) == 0 && out[0] == 0);
}

TEST_SUITE(web_apu_tests, "web_apu", {"square pitch", square_pitch},
           {"square amplitude", square_amplitude}, {"square duty", square_duty},
           {"trigger bit cleared", trigger_bit_cleared},
           {"frequency without trigger", frequency_without_trigger},
           {"envelope decay", envelope_decay}, {"envelope rise", envelope_rise},
           {"length stops channel", length_stops_channel},
           {"sweep changes pitch", sweep_changes_pitch}, {"noise width", noise_width},
           {"noise rate", noise_rate}, {"master disable", master_disable}, {"dac off", dac_off},
           {"phase continuity", phase_continuity}, {"mixing", mixing},
           {"wave channel", wave_channel}, {"wave same-frame load", wave_same_frame_load},
           {"wave length", wave_length}, {"uninitialized is silent", uninitialized_is_silent});
