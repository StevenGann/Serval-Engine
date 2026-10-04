#ifndef SERVAL_RANDOM_H
#define SERVAL_RANDOM_H

// Fast pseudo-random numbers (xorshift32). Deterministic: the same seed always
// gives the same sequence, which keeps benchmarks and replays reproducible.
// Not suitable for anything security-related.

#include "serval/platform.h"

// Restarts the sequence. A seed of 0 is replaced by a fixed non-zero value.
// serval_init() seeds with that same fixed value.
void random_seed(u32 seed);

// Next 32 random bits.
u32 random_u32(void);

// Random integer in [lo, hi], both inclusive. Returns lo if hi < lo.
int random_range(int lo, int hi);

#endif // SERVAL_RANDOM_H
