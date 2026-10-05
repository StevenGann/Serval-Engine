#include "serval/math.h"
#include "test.h"

#include <stdint.h>

static void int_helpers(void) {
    CHECK(int_min(3, -2) == -2 && int_max(3, -2) == 3);
    CHECK(int_abs(-7) == 7 && int_abs(7) == 7);
    CHECK(int_clamp(5, 0, 10) == 5);
    CHECK(int_clamp(-1, 0, 10) == 0);
    CHECK(int_clamp(11, 0, 10) == 10);
}

static void fixed_multiply_and_divide(void) {
    CHECK(fx_mul(FX(3), FX(4)) == FX(12));
    CHECK(fx_mul(FX(10), FX(7) / 8) == FX(70) / 8);
    CHECK(fx_mul(-FX(2), FX(1) / 2) == -FX(1));
    CHECK(fx_mul(FX(200), FX(200)) == FX(40000)); // no 32-bit overflow
    CHECK(fx_div(FX(3), FX(4)) == FX(3) / 4);
    CHECK(fx_div(-FX(9), FX(3)) == -FX(3));
}

static void sine_and_cosine(void) {
    CHECK(fx_sin(0) == 0 && fx_cos(0) == FX(1));
    CHECK(fx_sin(ANGLE_DEG(90)) == FX(1) && fx_cos(ANGLE_DEG(90)) == 0);
    CHECK(fx_sin(ANGLE_DEG(180)) == 0 && fx_cos(ANGLE_DEG(180)) == -FX(1));
    CHECK(fx_sin(ANGLE_DEG(270)) == -FX(1) && fx_cos(ANGLE_DEG(270)) == 0);
    CHECK(fx_sin(ANGLE_DEG(30)) == FX(1) / 2); // 0.5 exactly at this precision
    CHECK(fx_sin(ANGLE_DEG(-30)) == -FX(1) / 2);
    CHECK(int_abs(fx_cos(ANGLE_DEG(60)) - FX(1) / 2) <= 1);
    CHECK(ANGLE_DEG(360) == 0);              // wraps
    for (u32 a = 0; a < 0x10000; a += 0x123) // odd values: symmetric everywhere
        CHECK(fx_sin((u16)(0x10000 - a)) == -fx_sin((u16)a));
    // sin^2 + cos^2 stays close to 1 all the way round.
    for (u32 a = 0; a < 0x10000; a += 0x400) {
        FIXED s = fx_sin((u16)a), c = fx_cos((u16)a);
        CHECK(int_abs(fx_mul(s, s) + fx_mul(c, c) - FX(1)) <= 3);
    }
}

// --- angle_of and fx_length against a double-precision reference -----------
// No libm in the test ROM: atan and sqrt are computed here (soft float on the
// GBA, so the point counts stay modest).

static const double kPi = 3.14159265358979323846;

// atan(x) for 0 <= x <= 1: reduced below tan(15 degrees), then the series.
static double ref_atan01(double x) {
    const double sqrt3 = 1.73205080756887729353;
    double base = 0;
    if (x > 0.26794919243112270) { // atan(x) = 30 degrees + atan(reduced)
        x = (x * sqrt3 - 1) / (sqrt3 + x);
        base = kPi / 6;
    }
    double x2 = x * x, term = x, sum = 0;
    for (int k = 1; k < 30; k += 2) {
        sum += term / k;
        term *= -x2;
    }
    return base + sum;
}

// atan2 in u16 angle units (0 to 65536, not wrapped), clockwise from right.
static double ref_angle(s32 dx, s32 dy) {
    double ax = dx < 0 ? -(double)dx : dx, ay = dy < 0 ? -(double)dy : dy;
    double a = ax >= ay ? ref_atan01(ay / ax) : kPi / 2 - ref_atan01(ax / ay);
    if (dx < 0)
        a = kPi - a;
    if (dy < 0)
        a = 2 * kPi - a;
    return a * 65536 / (2 * kPi);
}

static double ref_sqrt(double v) {
    if (v <= 0)
        return 0;
    double r = v > 1 ? v : 1;
    for (int k = 0; k < 80; k++)
        r = (r + v / r) / 2;
    return r;
}

static double ref_length(s32 dx, s32 dy) {
    return ref_sqrt((double)dx * dx + (double)dy * dy);
}

// angle_of's error in u16 angle units (65536 per turn), wrapped to +-32768.
static double angle_error(s32 dx, s32 dy) {
    double err = angle_of(dx, dy) - ref_angle(dx, dy);
    while (err > 32768)
        err -= 65536;
    while (err < -32768)
        err += 65536;
    return err < 0 ? -err : err;
}

// angle_of promises 0.1 degree (0.07 measured over 20 million random vectors).
#define MAX_ANGLE_ERROR (0.1 * 65536 / 360)

static u32 lcg_state;
static u32 lcg_next(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

// A random vector of random size: 1 to 31 significant bits per component.
static void random_vector(s32* dx, s32* dy) {
    u32 bits = lcg_next() % 31 + 1;
    u32 mask = bits >= 31 ? 0x7FFFFFFFu : (1u << bits) - 1;
    *dx = (s32)(lcg_next() & mask) * ((lcg_next() & 1) ? -1 : 1);
    *dy = (s32)(lcg_next() & mask) * ((lcg_next() & 1) ? -1 : 1);
}

static void angle_of_axes_and_diagonals(void) {
    CHECK(angle_of(0, 0) == 0);
    static const s32 sizes[] = {1, 2, 3, 255, FX(1), FX(100), 0x1234567, 0x7FFFFFFF};
    for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        s32 v = sizes[k];
        CHECK(angle_of(v, 0) == 0);
        CHECK(angle_of(0, v) == ANGLE_DEG(90)); // down
        CHECK(angle_of(-v, 0) == ANGLE_DEG(180));
        CHECK(angle_of(0, -v) == ANGLE_DEG(270)); // up
        CHECK(angle_of(v, v) == ANGLE_DEG(45));
        CHECK(angle_of(-v, v) == ANGLE_DEG(135));
        CHECK(angle_of(-v, -v) == ANGLE_DEG(225));
        CHECK(angle_of(v, -v) == ANGLE_DEG(315));
    }
    // The most negative FIXED has no positive counterpart.
    CHECK(angle_of(INT32_MIN, 0) == ANGLE_DEG(180));
    CHECK(angle_of(0, INT32_MIN) == ANGLE_DEG(270));
    CHECK(angle_of(INT32_MIN, INT32_MIN) == ANGLE_DEG(225));
    CHECK(angle_error(INT32_MIN, INT32_MAX) <= MAX_ANGLE_ERROR);
}

static void angle_of_matches_atan2(void) {
    double worst = 0;
    // Every direction to a point on a square ring, so every octant and
    // ratio, at a few sizes from tiny to huge.
    static const s32 rings[] = {3, 40, 300, 0x7FFF, 0x7FFFFFF};
    for (unsigned r = 0; r < sizeof(rings) / sizeof(rings[0]); r++) {
        s32 n = rings[r];
        s32 stride = n / 40 + 1;
        for (s32 t = -n; t <= n; t += stride) {
            static const s32 sides[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (int s = 0; s < 4; s++) {
                s32 dx = sides[s][0] ? sides[s][0] * n : t;
                s32 dy = sides[s][1] ? sides[s][1] * n : t;
                double err = angle_error(dx, dy);
                worst = err > worst ? err : worst;
            }
        }
    }
    lcg_state = 1;
    for (int k = 0; k < 1500; k++) {
        s32 dx, dy;
        random_vector(&dx, &dy);
        if (dx == 0 && dy == 0)
            continue;
        double err = angle_error(dx, dy);
        worst = err > worst ? err : worst;
    }
    CHECK(worst <= MAX_ANGLE_ERROR);
}

static void angle_of_inverts_sin_cos(void) {
    // Feeding a heading's direction back gives the heading (to fx_sin's
    // 1/256 precision: within a degree).
    for (u32 a = 0; a < 0x10000; a += 0x0F1) {
        s32 err = (s16)(angle_of(fx_cos((u16)a), fx_sin((u16)a)) - (u16)a);
        CHECK(int_abs(err) <= ANGLE_DEG(1));
    }
}

static void fx_length_matches_hypot(void) {
    CHECK(fx_length(0, 0) == 0);
    CHECK(fx_length(FX(3), FX(4)) == FX(5));
    CHECK(fx_length(-FX(3), -FX(4)) == FX(5));
    CHECK(fx_length(FX(7), 0) == FX(7) && fx_length(0, -FX(7)) == FX(7));
    CHECK(fx_length(1, 0) == 1 && fx_length(1, 1) == 1);
    // Saturates instead of overflowing.
    CHECK(fx_length(INT32_MAX, INT32_MAX) == INT32_MAX);
    CHECK(fx_length(INT32_MIN, 0) == INT32_MAX);
    CHECK(fx_length(INT32_MAX, 0) == INT32_MAX);
    lcg_state = 7;
    double worst = 0;
    for (int k = 0; k < 1500; k++) {
        s32 dx, dy;
        random_vector(&dx, &dy);
        double ref = ref_length(dx, dy);
        if (ref >= 2147483647.0)
            continue; // saturated
        // Within 0.1%, plus one unit of rounding.
        double err = fx_length(dx, dy) - ref;
        err = (err < 0 ? -err : err) - 1;
        worst = err / ref > worst ? err / ref : worst;
    }
    CHECK(worst <= 0.001);
    // Tiny vectors: within one unit (1/256 pixel).
    for (s32 dx = -20; dx <= 20; dx++) {
        for (s32 dy = -20; dy <= 20; dy++) {
            double err = fx_length(dx, dy) - ref_length(dx, dy);
            CHECK(err > -1.0 && err < 1.0);
        }
    }
}

TEST_SUITE(math_tests, "math", {"int_helpers", int_helpers},
           {"fixed_multiply_and_divide", fixed_multiply_and_divide},
           {"sine_and_cosine", sine_and_cosine},
           {"angle_of_axes_and_diagonals", angle_of_axes_and_diagonals},
           {"angle_of_matches_atan2", angle_of_matches_atan2},
           {"angle_of_inverts_sin_cos", angle_of_inverts_sin_cos},
           {"fx_length_matches_hypot", fx_length_matches_hypot});
