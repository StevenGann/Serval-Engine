#ifndef SERVAL_RANDOM_H
#define SERVAL_RANDOM_H

// Fast pseudo-random numbers (xorshift32). Deterministic: the same seed always
// gives the same sequence, which keeps benchmarks and replays reproducible.
// Not suitable for anything security-related.

#include "serval/platform.h"

// Restarts the sequence. A seed of 0 is replaced by a fixed non-zero value.
// serval_init() seeds with that same fixed value, so a game that never seeds
// plays out the same way on every boot.
void random_seed(u32 seed);

// A value that differs from run to run, for seeding: it comes from the CPU
// cycle counter, so it depends on real-world timing. Call it after waiting for
// the player, e.g. once START is pressed on a title screen:
//
//     random_seed(random_entropy());
//
// Called at a fixed point with no input before it (such as right at boot), it
// returns the same value every time on emulators.
u32 random_entropy(void);

// Next 32 random bits.
u32 random_u32(void);

// Random integer in [lo, hi], both inclusive. Returns lo if hi < lo.
int random_range(int lo, int hi);

#endif // SERVAL_RANDOM_H
