#include "serval/random.h"

#define DEFAULT_SEED 0x2545F491u

static u32 state = DEFAULT_SEED;

void random_seed(u32 seed) {
    state = seed ? seed : DEFAULT_SEED;
}

u32 random_u32(void) {
    // xorshift32 (Marsaglia). Never yields 0 from a non-zero state.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

int random_range(int lo, int hi) {
    if (hi <= lo)
        return lo;
    u32 span = (u32)hi - (u32)lo + 1u; // 0 means the full 32-bit range
    if (span == 0)
        return (int)random_u32();
    // Scale by multiplication instead of %: no division (slow without a
    // hardware divider) and no modulo bias toward small values.
    return lo + (int)(((uint64_t)random_u32() * span) >> 32);
}
