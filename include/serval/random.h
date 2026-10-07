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

// A value that differs from game to game, for seeding: a hash of the frame
// count and of the button history since serval_init() (which buttons were
// held, and on exactly which frame each press or release happened). A player
// never repeats their timing to the frame, so it varies with them; identical
// input gives the identical value, on the GBA and the web alike and in every
// build, so recorded input replays the same game. Call it after waiting for
// the player, e.g. once START is pressed on a title screen:
//
//     random_seed(random_entropy());
//
// Called before any input (such as right at boot), it depends on the frame
// count alone: it differs from frame to frame but is the same on every boot,
// so it seeds the same game every time. It changes only from frame to frame
// (frame_begin() records the input), not between calls within a frame.
u32 random_entropy(void);

// Next 32 random bits.
u32 random_u32(void);

// Random integer in [lo, hi], both inclusive; any int range works. Returns lo
// if hi < lo (warning in debug builds).
int random_range(int lo, int hi);

#endif // SERVAL_RANDOM_H
