// Tests for color math (src/core/color.c). Run natively and in the test ROM.

#include "serval/debug.h"
#include "serval/screen.h"
#include "test.h"

#ifdef SERVAL_DEBUG
#define WARNINGS(n) (n)
#else
#define WARNINGS(n) 0
#endif

// A Color from 5-bit channels (0-31).
#define RGB5(r, g, b) ((Color)((r) | ((g) << 5) | ((b) << 10)))

static u32 channel(Color c, u32 i) {
    return ((u32)c >> (5 * i)) & 31u;
}

// The reference, from screen.h's definition rather than color.c's arithmetic:
// m is (ca x (256 - amount) + cb x amount) / 256 rounded down exactly when
// m x 256 <= that sum < (m + 1) x 256.
static bool mixed_channel_ok(u32 m, u32 ca, u32 cb, u32 amount) {
    u32 sum = ca * (256 - amount) + cb * amount;
    return m * 256 <= sum && sum < m * 256 + 256;
}

static bool mix_ok(Color m, Color a, Color b, u32 amount) {
    if (m & 0x8000)
        return false;
    for (u32 i = 0; i < 3; i++)
        if (!mixed_channel_ok(channel(m, i), channel(a, i), channel(b, i), amount))
            return false;
    return true;
}

static const Color samples[] = {
    0,
    0x7FFF,
    RGB5(31, 0, 0),
    RGB5(0, 31, 0),
    RGB5(0, 0, 31),
    RGB5(1, 2, 3),
    RGB5(30, 15, 7),
    COLOR_RGB(16, 32, 80),
    COLOR_RGB(252, 206, 80),
};
#define SAMPLES (sizeof(samples) / sizeof(samples[0]))

// 0 gives a, 256 gives b, exactly; a mixed with itself is itself at any amount.
static void endpoints_are_exact(void) {
    for (u32 i = 0; i < SAMPLES; i++) {
        for (u32 j = 0; j < SAMPLES; j++) {
            CHECK(color_mix(samples[i], samples[j], 0) == samples[i]);
            CHECK(color_mix(samples[i], samples[j], 256) == samples[j]);
        }
        for (u32 t = 0; t <= 256; t += 15)
            CHECK(color_mix(samples[i], samples[i], t) == samples[i]);
    }
}

// Every pair of channel values, at every amount, in each channel: red mixes
// (i, j), green (j, i), blue (31 - i, 31 - j).
static void matches_the_reference_everywhere(void) {
    u32 wrong = 0;
    for (u32 t = 0; t <= 256; t++)
        for (u32 i = 0; i < 32; i++)
            for (u32 j = 0; j < 32; j++) {
                Color a = RGB5(i, j, 31 - i), b = RGB5(j, i, 31 - j);
                wrong += !mix_ok(color_mix(a, b, t), a, b, t);
            }
    CHECK(wrong == 0);
}

// Rounded down, not to the nearest value.
static void rounds_down(void) {
    CHECK(color_mix(0, 0x7FFF, 128) == RGB5(15, 15, 15));                        // 15.5
    CHECK(color_mix(0x7FFF, 0, 128) == RGB5(15, 15, 15));                        // 15.5
    CHECK(color_mix(0, RGB5(31, 0, 0), 255) == RGB5(30, 0, 0));                  // 30.88
    CHECK(color_mix(RGB5(0, 31, 0), 0, 1) == RGB5(0, 30, 0));                    // 30.88
    CHECK(color_mix(0, RGB5(0, 0, 1), 255) == 0);                                // 0.996
    CHECK(color_mix(RGB5(1, 1, 1), RGB5(2, 2, 2), 128) == RGB5(1, 1, 1));        // 1.5
    CHECK(color_mix(RGB5(10, 20, 30), RGB5(20, 10, 0), 64) == RGB5(12, 17, 22)); // 12.5 17.5 22.5
}

// Swapping the colors and the side of the amount gives the same color.
static void is_symmetric(void) {
    for (u32 i = 0; i < SAMPLES; i++)
        for (u32 j = 0; j < SAMPLES; j++)
            for (u32 t = 0; t <= 256; t += 7)
                CHECK(color_mix(samples[i], samples[j], t) ==
                      color_mix(samples[j], samples[i], 256 - t));
}

// Bit 15, unused by the hardware, is ignored and 0 in the result.
static void ignores_bit_15(void) {
    CHECK(color_mix(0xFFFF, 0xFFFF, 100) == 0x7FFF);
    CHECK(color_mix(0xFFFF, 0, 0) == 0x7FFF);
    CHECK(color_mix(0x8000, 0xFFFF, 256) == 0x7FFF);
    CHECK(color_mix(0x8000 | RGB5(4, 5, 6), RGB5(4, 5, 6), 77) == RGB5(4, 5, 6));
}

// At multiples of 16: the hardware's alpha blending (GBATEK: min(31,
// (top x EVA + bottom x EVB) / 16), rounded down) with weights 16 - k and k,
// and its brightness increase (v + (31 - v) x EVY / 16, rounded down) toward
// white, exactly, as screen.h says. Its brightness decrease (v - v x EVY /
// 16, rounded down) rounds the other way, so toward black color_mix() is
// the same or one step darker (docs/runtime-systems.md#color-mixing).
static void matches_the_hardware_effects(void) {
    u32 wrong = 0;
    for (u32 k = 0; k <= 16; k++)
        for (u32 i = 0; i < 32; i++)
            for (u32 j = 0; j < 32; j++) {
                u32 blend = (i * (16 - k) + j * k) >> 4;
                if (blend > 31)
                    blend = 31;
                wrong += color_mix(RGB5(i, j, 0), RGB5(j, i, 31), 16 * k) !=
                         RGB5(blend, (j * (16 - k) + i * k) >> 4, (31 * k) >> 4);
            }
    for (u32 k = 0; k <= 16; k++)
        for (u32 v = 0; v < 32; v++) {
            u32 bright = v + (((31 - v) * k) >> 4);
            wrong += color_mix(RGB5(v, v, v), 0x7FFF, 16 * k) != RGB5(bright, bright, bright);
            u32 dark = v - ((v * k) >> 4);
            u32 mixed = channel(color_mix(RGB5(v, v, v), 0, 16 * k), 0);
            wrong += mixed > dark || mixed + 1 < dark;
        }
    CHECK(wrong == 0);
}

// Past 256: all of b, reported once.
static void amount_past_256_is_clamped(void) {
    u32 before = debug_warning_count();
    CHECK(color_mix(RGB5(1, 2, 3), RGB5(30, 15, 7), 257) == RGB5(30, 15, 7));
    CHECK(debug_warning_count() == before + WARNINGS(1));
    CHECK(color_mix(RGB5(1, 2, 3), RGB5(30, 15, 7), 0xFFFFFFFFu) == RGB5(30, 15, 7)); // e.g. -1
    CHECK(color_mix(0x7FFF, 0, 1000) == 0);
    CHECK(debug_warning_count() == before + WARNINGS(1));
}

// The cost of mixing a 256-color palette on the GBA, logged, measured in CPU
// cycles with the cascaded timers serval_init() starts. This file is Thumb
// code in ROM, like a game's code; color_mix() is too. Nothing to measure on
// the host.
#ifdef SERVAL_GBA
#include "serval/text.h"

#include <tonc.h>

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

static volatile Color sink; // keeps the measured loops from being optimized away
#endif

static void palette_cost(void) {
#ifdef SERVAL_GBA
    static Color palette[256];
    for (u32 i = 0; i < 256; i++)
        palette[i] = (Color)(i * 129u);
    u32 t0 = cycles();
    for (u32 i = 0; i < 256; i++)
        sink = color_mix(palette[i], 0x7FFF, 100);
    u32 t1 = cycles();
    for (u32 i = 0; i < 256; i++)
        sink = palette[i];
    u32 t2 = cycles();
    u32 mixing = (t1 - t0) - (t2 - t1); // without the loop's own cost
    debug_log(text_format("color_mix: a 256-color palette in %u cycles", mixing));
#ifdef __OPTIMIZE__ // Debug builds (-O0) check correctness, not timing
    CHECK(mixing < 256 * 200);
#endif
#endif
}

TEST_SUITE(color_tests, "color", {"endpoints_are_exact", endpoints_are_exact},
           {"matches_the_reference_everywhere", matches_the_reference_everywhere},
           {"rounds_down", rounds_down}, {"is_symmetric", is_symmetric},
           {"ignores_bit_15", ignores_bit_15},
           {"matches_the_hardware_effects", matches_the_hardware_effects},
           {"amount_past_256_is_clamped", amount_past_256_is_clamped},
           {"palette_cost", palette_cost});
