#include "serval/math.h"
#include "test.h"

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

TEST_SUITE(math_tests, "math", {"int_helpers", int_helpers},
           {"fixed_multiply_and_divide", fixed_multiply_and_divide},
           {"sine_and_cosine", sine_and_cosine});
