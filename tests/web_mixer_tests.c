// Host tests for the web's software mixer (src/web/mixer.c), which mixes
// tracker music and sampled effects as Maxmod's GBA mixer does: samples in
// plain arrays with the header mmutil writes for the GBA, channels set as
// Maxmod's player sets them. Levels are checked exactly against the
// assembly's arithmetic (worked out by hand below), pitch by counting
// crossings.

#include "../src/web/web.h"
#include "test.h"

#include <string.h>

#define RATE 15768  // Maxmod's 16 kHz, where a freq of 4096 plays a sample a step
#define SCALE 16384 // its mm_ratescale there
#define ONE 4096    // freq: one sample a step
#define MAX_STEPS 4096
#define NO_LOOP 0xFFFFFFFFu

// A sample as mmutil writes it for the GBA (mm_mas_gba_sample): length, loop
// length, format, reserved, default frequency, then the unsigned 8-bit data
// (and padding).
typedef struct {
    u32 length;
    u32 loop_length;
    u8 format;
    u8 reserved;
    u16 frequency;
    u8 data[4096 + 4];
} Sample;

static Sample samples[3];
static WebMixerChannel channels[4];
static u16 work[2 * MAX_STEPS];
static s8 left[MAX_STEPS];
static s8 right[MAX_STEPS];

static void fill(Sample* s, u32 length, u32 loop_length, u8 value) {
    s->length = length;
    s->loop_length = loop_length;
    memset(s->data, value, sizeof(s->data));
}

// A square wave: `period` samples a cycle, high half first, at full swing.
static void square(Sample* s, u32 length, u32 loop_length, u32 period) {
    fill(s, length, loop_length, 128);
    for (u32 i = 0; i < length; i++)
        s->data[i] = i % period < period / 2 ? 255 : 0;
}

static void reset(void) {
    memset(channels, 0, sizeof(channels));
    for (u32 i = 0; i < 4; i++)
        channels[i].src = WEB_MIXER_STOPPED;
}

static void play(u32 c, Sample* s, u32 freq, u8 vol, u8 pan) {
    channels[c] = (WebMixerChannel){
        .src = (uintptr_t)s->data, .read = 0, .vol = vol, .pan = pan, .freq = freq};
}

static void mix(u32 steps) {
    web_mixer_mix(channels, 4, SCALE, work, left, right, steps);
}

// Rising crossings of the left side over n steps, per second at RATE.
static u32 pitch_hz(u32 n) {
    u32 rises = 0;
    bool high = false;
    for (u32 i = 0; i < n; i++) {
        if (!high && left[i] > 8) {
            high = true;
            rises++;
        } else if (high && left[i] < -8) {
            high = false;
        }
    }
    return rises * RATE / n;
}

static bool near(u32 value, u32 expected, u32 slack) {
    return value + slack >= expected && value <= expected + slack;
}

static bool all(const s8* side, u32 from, u32 to, s8 value) {
    for (u32 i = from; i < to; i++)
        if (side[i] != value)
            return false;
    return true;
}

static void sample_rate_sets_the_pitch(void) {
    // 32 samples a cycle at one sample a step: 15768 / 32 = 492.75 Hz.
    reset();
    square(&samples[0], 4096, 4096, 32);
    play(0, &samples[0], ONE, 255, 0);
    mix(MAX_STEPS);
    CHECK(near(pitch_hz(MAX_STEPS), 493, 4));
    // An octave up (two samples a step), and a fifth down (2/3 of one).
    reset();
    play(0, &samples[0], 2 * ONE, 255, 0);
    mix(MAX_STEPS);
    CHECK(near(pitch_hz(MAX_STEPS), 985, 6));
    reset();
    play(0, &samples[0], ONE * 2 / 3, 255, 0);
    mix(MAX_STEPS);
    CHECK(near(pitch_hz(MAX_STEPS), 328, 4));
    // Maxmod's other rates scale the step: at 32 kHz (mm_ratescale 8192) the
    // same freq plays half as many samples a step, the same pitch in Hz.
    reset();
    play(0, &samples[0], ONE, 255, 0);
    web_mixer_mix(channels, 4, 8192, work, left, right, 2048);
    CHECK(channels[0].read == 1024u << 12);
}

static void volume_and_pan_as_maxmod_mixes_them(void) {
    // A held full-scale sample (255): left volume vol * (256 - pan) >> 8,
    // right vol * pan >> 8; each step adds sample * volume >> 5, the sum
    // less (volumes / 2) * 8, shifted down 3.
    static const struct {
        u8 vol, pan;
        s8 left, right;
    } cases[] = {
        {255, 0, 127, 0},   // (2032 - 1016) >> 3; nothing right
        {255, 255, 0, 126}, // right volume 254: (2024 - 1016) >> 3
        {255, 128, 63, 63}, // 127 a side: (1012 - 504) >> 3
        {128, 0, 63, 0},    // (1020 - 512) >> 3
        {0, 128, 0, 0},
    };
    fill(&samples[0], 4000, 4000, 255);
    for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset();
        play(0, &samples[0], ONE, cases[i].vol, cases[i].pan);
        mix(264);
        CHECK(all(left, 0, 264, cases[i].left));
        CHECK(all(right, 0, 264, cases[i].right));
    }
    // The lowest sample: (0 - 1016) >> 3. Silence (128) is exactly 0.
    reset();
    fill(&samples[1], 4000, 4000, 0);
    play(0, &samples[1], ONE, 255, 0);
    mix(264);
    CHECK(all(left, 0, 264, -127));
    reset();
    fill(&samples[1], 4000, 4000, 128);
    play(0, &samples[1], ONE, 255, 37);
    mix(264);
    CHECK(all(left, 0, 264, 0) && all(right, 0, 264, 0));
}

static void the_sum_clips(void) {
    // Two channels at full volume on the left: (4064 - 2040) >> 3 = 253,
    // clipped; the lowest sample, (0 - 2040) >> 3 = -255, clipped too.
    reset();
    fill(&samples[0], 4000, 4000, 255);
    fill(&samples[1], 4000, 4000, 0);
    play(0, &samples[0], ONE, 255, 0);
    play(1, &samples[0], ONE, 255, 0);
    mix(264);
    CHECK(all(left, 0, 264, 127));
    reset();
    play(0, &samples[1], ONE, 255, 0);
    play(1, &samples[1], ONE, 255, 0);
    mix(264);
    CHECK(all(left, 0, 264, -128));
    // One up, one down: they cancel, but for the last bit (255 is 127 above
    // silence, 0 is 128 below): (2032 - 2040) >> 3.
    reset();
    play(0, &samples[0], ONE, 255, 0);
    play(1, &samples[1], ONE, 255, 0);
    mix(264);
    CHECK(all(left, 0, 264, -1));
}

static void loops_play_on(void) {
    // 1000 samples, the last 400 looped: after the end it plays from 600.
    reset();
    fill(&samples[0], 1000, 400, 128);
    for (u32 i = 0; i < 1000; i++)
        samples[0].data[i] = (u8)(i < 600 ? 255 : (i % 2 ? 0 : 255));
    play(0, &samples[0], ONE, 255, 0);
    for (u32 f = 0; f < 8; f++) // 2112 steps, in Maxmod's frame-sized calls
        mix(264);
    CHECK(channels[0].src == (uintptr_t)samples[0].data);
    CHECK(channels[0].read == (600u + (2112u - 1000u) % 400u) << 12);
    // Still sounding: the loop's alternating samples.
    CHECK(left[263] == -127 || left[263] == 127);
    CHECK(left[262] == -left[263]);
    // At a fractional rate (1.5 samples a step) the position wraps by the
    // loop's length, keeping its fraction.
    reset();
    play(0, &samples[0], ONE * 3 / 2, 255, 0);
    mix(1000); // 1500 samples: 600 + 500 % 400
    CHECK(channels[0].read == 700u << 12);
}

static void one_shots_stop_at_their_end(void) {
    // 100 samples at one a step: they play, then the channel stops (its src
    // marked, read 0) and is silent, within the same call.
    reset();
    fill(&samples[0], 100, NO_LOOP, 255);
    play(0, &samples[0], ONE, 255, 0);
    mix(264);
    CHECK(all(left, 0, 100, 127));
    CHECK(all(left, 100, 264, 0));
    CHECK(channels[0].src & WEB_MIXER_STOPPED);
    CHECK(channels[0].read == 0);
    // Ending exactly with the call: stopped in that call, as the assembly
    // does (the player sees the channel free at its next tick).
    reset();
    play(0, &samples[0], ONE, 255, 0);
    mix(100);
    CHECK(all(left, 0, 100, 127));
    CHECK(channels[0].src & WEB_MIXER_STOPPED);
    // Half a sample a step: twice as long.
    reset();
    play(0, &samples[0], ONE / 2, 255, 0);
    mix(264);
    CHECK(all(left, 0, 200, 127) && all(left, 200, 264, 0));
    CHECK(channels[0].src & WEB_MIXER_STOPPED);
    // A start past the end (a pattern's sample offset beyond it): stopped,
    // silent, nothing read beyond the sample.
    reset();
    play(0, &samples[0], ONE, 255, 0);
    channels[0].read = 5000u << 12;
    mix(264);
    CHECK(all(left, 0, 264, 0) && (channels[0].src & WEB_MIXER_STOPPED));
}

static void stopped_and_paused_channels_are_silent(void) {
    reset();
    fill(&samples[0], 4000, 4000, 255);
    mix(264);
    CHECK(all(left, 0, 264, 0) && all(right, 0, 264, 0));
    // Paused (Maxmod sets freq 0): silent, holding its place.
    play(0, &samples[0], 0, 255, 128);
    channels[0].read = 123u << 12;
    mix(264);
    CHECK(all(left, 0, 264, 0) && all(right, 0, 264, 0));
    CHECK(channels[0].read == 123u << 12);
    // Stopped by the player: silent.
    play(0, &samples[0], ONE, 255, 128);
    channels[0].src |= WEB_MIXER_STOPPED;
    mix(264);
    CHECK(all(left, 0, 264, 0) && all(right, 0, 264, 0));
    // At volume 0: silent, but it moves on.
    play(0, &samples[0], ONE, 0, 128);
    mix(264);
    CHECK(all(left, 0, 264, 0) && channels[0].read == 264u << 12);
}

static void a_channel_taken_for_another_sound(void) {
    // Maxmod gives an effect a channel another sound holds (the engine's
    // priorities stop an effect for a new one; a module's background notes
    // give way): the player points the channel at the new sample, from its
    // start. The mixer keeps nothing else about a channel, so the new sample
    // plays at once, and the old one not at all.
    reset();
    fill(&samples[0], 4000, 4000, 255);
    fill(&samples[1], 4000, 4000, 0);
    play(0, &samples[0], ONE, 255, 0);
    mix(264);
    CHECK(all(left, 0, 264, 127));
    play(0, &samples[1], ONE / 2, 255, 255);
    mix(264);
    CHECK(all(left, 0, 264, 0) && all(right, 0, 264, -127));
    CHECK(channels[0].read == 132u << 12);
    // A channel whose one-shot ended plays the next sample given it.
    fill(&samples[2], 50, NO_LOOP, 255);
    play(1, &samples[2], ONE, 255, 0);
    mix(264);
    CHECK(channels[1].src & WEB_MIXER_STOPPED);
    play(1, &samples[2], ONE, 255, 0);
    channels[0].src = WEB_MIXER_STOPPED;
    mix(264);
    CHECK(all(left, 0, 50, 127) && all(left, 50, 264, 0));
}

static void every_channel_mixes(void) {
    // Four quiet channels, each panned differently, add up.
    reset();
    fill(&samples[0], 4000, 4000, 255);
    for (u32 c = 0; c < 4; c++)
        play(c, &samples[0], ONE, 64, 0);
    mix(264);
    // Each: 255 * 64 >> 5 = 510; sum 2040, less (256 / 2) * 8 = 1024: 127.
    CHECK(all(left, 0, 264, 127));
    channels[3].pan = 255;
    mix(264);
    // Three left: 1530 - (192 / 2) * 8 = 762, >> 3 = 95; one right (volume
    // 63): 255 * 63 >> 5 = 502, less 31 * 8: 31.
    CHECK(all(left, 0, 264, 95) && all(right, 0, 264, 31));
}

TEST_SUITE(web_mixer_tests, "web_mixer", {"sample rate sets the pitch", sample_rate_sets_the_pitch},
           {"volume and pan as Maxmod mixes them", volume_and_pan_as_maxmod_mixes_them},
           {"the sum clips", the_sum_clips}, {"loops play on", loops_play_on},
           {"one-shots stop at their end", one_shots_stop_at_their_end},
           {"stopped and paused channels are silent", stopped_and_paused_channels_are_silent},
           {"a channel taken for another sound", a_channel_taken_for_another_sound},
           {"every channel mixes", every_channel_mixes});
