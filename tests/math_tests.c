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

TEST_SUITE(math_tests, "math", {"int_helpers", int_helpers},
           {"fixed_multiply_and_divide", fixed_multiply_and_divide});
