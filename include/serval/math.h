#ifndef SERVAL_MATH_H
#define SERVAL_MATH_H

// Small integer and fixed-point helpers. Prefixed (int_, fx_) so they never
// clash with libtonc's clamp/min/max macros and functions.

#include "serval/fixed.h"
#include "serval/platform.h"

static inline int int_min(int a, int b) {
    return a < b ? a : b;
}

static inline int int_max(int a, int b) {
    return a > b ? a : b;
}

static inline int int_abs(int v) {
    return v < 0 ? -v : v;
}

// Limits v to [lo, hi], both inclusive.
static inline int int_clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

// a * b in fixed point, e.g. fx_mul(speed, FX(7) / 8) for 7/8 of a speed.
static inline FIXED fx_mul(FIXED a, FIXED b) {
    return (FIXED)(((int64_t)a * b) >> FX_SHIFT);
}

// a / b in fixed point. b must not be 0. Division is slow on the GBA (no
// hardware divider); multiply by a constant reciprocal where you can.
static inline FIXED fx_div(FIXED a, FIXED b) {
    return (FIXED)(((int64_t)a * FX_ONE) / b);
}

#endif // SERVAL_MATH_H
