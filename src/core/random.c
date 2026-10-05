#include "serval/random.h"

#include "random_internal.h"
#include "warn.h"

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
    if (hi <= lo) {
#ifdef SERVAL_DEBUG
        static bool warned;
        if (hi < lo && !warned) {
            warned = true;
            SERVAL_WARN("random_range(%d, %d): hi is less than lo; returning lo", lo, hi);
        }
#endif
        return lo;
    }
    u32 span = (u32)hi - (u32)lo + 1u; // 0 means the full 32-bit range
    if (span == 0)
        return (int)random_u32();
    // Scale by multiplication instead of %: no division (slow without a
    // hardware divider) and no modulo bias toward small values.
    // Added in u32: lo + offset can't overflow there, and the result is in
    // [lo, hi], so it fits an int again.
    return (int)((u32)lo + (u32)(((uint64_t)random_u32() * span) >> 32));
}

// random_entropy: a hash of the player's input history. Every change of the
// button state mixes the new state and the frame it happened on into
// `history`, so the result depends on exactly when each button went down or
// up, which no player repeats to the frame, but not on CPU timing: identical
// input gives identical games on every platform and build (replays, tests,
// GBA vs web).
static u32 history;
static u32 history_frame;
static u32 history_buttons;

// A 32-bit integer hash with good avalanche (lowbias32, by Chris Wellons):
// shifts, XORs and two multiplies, no division.
static u32 mix32(u32 h) {
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

void serval_entropy_reset(void) {
    history = 0;
    history_frame = 0;
    history_buttons = 0;
}

void serval_entropy_frame(u32 frame, u32 buttons) {
    history_frame = frame;
    if (buttons != history_buttons) {
        history_buttons = buttons;
        history = mix32(history ^ mix32(frame) ^ (buttons * 0x9E3779B9u));
    }
}

u32 random_entropy(void) {
    return mix32(history ^ mix32(history_frame + 0x632BE5ABu));
}
