#ifndef SERVAL_FIXED_H
#define SERVAL_FIXED_H

// 24.8 fixed-point numbers (FIXED, from platform.h). The GBA has no FPU, so
// positions, velocities and other fractional values use these.

#include "serval/platform.h"

#define FX_SHIFT 8
#define FX_ONE (1 << FX_SHIFT)

// Whole number to FIXED. Usable in constant expressions.
#define FX(n) ((FIXED)((n) * FX_ONE))

// FIXED to whole number, rounding toward negative infinity.
static inline int fx_to_int(FIXED f) {
    return f >> FX_SHIFT;
}

#endif // SERVAL_FIXED_H
