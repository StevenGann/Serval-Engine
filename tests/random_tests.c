#include "serval/debug.h"
#include "serval/random.h"
#include "test.h"

#include "../src/core/random_internal.h"

static void same_seed_same_sequence(void) {
    random_seed(1234);
    u32 a1 = random_u32(), a2 = random_u32();
    random_seed(1234);
    CHECK(random_u32() == a1);
    CHECK(random_u32() == a2);
    CHECK(a1 != a2);
}

static void zero_seed_still_produces_numbers(void) {
    random_seed(0);
    CHECK(random_u32() != 0);
}

static void range_stays_within_bounds(void) {
    random_seed(99);
    bool seen_lo = false, seen_hi = false;
    for (int i = 0; i < 2000; i++) {
        int v = random_range(-3, 3);
        CHECK(v >= -3 && v <= 3);
        seen_lo |= v == -3;
        seen_hi |= v == 3;
    }
    CHECK(seen_lo && seen_hi); // both ends are reachable
}

static void entropy_seeds_without_breaking_the_sequence(void) {
    random_seed(random_entropy());
    u32 v = random_u32();
    (void)v; // the value varies; seeding with it must just work
    random_seed(1234);
    u32 a = random_u32();
    random_seed(1234);
    CHECK(random_u32() == a); // explicit seeds stay deterministic
}

// Plays `frames` frames of scripted input: A held from frame `press` to
// frame `release` (exclusive), B from frame 20 on. Returns random_entropy().
static u32 entropy_after(u32 frames, u32 press, u32 release) {
    serval_entropy_reset();
    for (u32 f = 0; f < frames; f++)
        serval_entropy_frame(f, (f >= press && f < release ? 0x1u : 0u) | (f >= 20 ? 0x2u : 0u));
    return random_entropy();
}

// random_entropy() hashes the input history and the frame count: the same
// input gives the same value (on any platform: nothing else goes in), and a
// press or release one frame earlier or later, a different button, or
// calling it on another frame gives another.
static void entropy_comes_from_the_input_history(void) {
    u32 v = entropy_after(30, 5, 9);
    CHECK(entropy_after(30, 5, 9) == v);
    CHECK(random_entropy() == v); // stable within a frame
    CHECK(entropy_after(30, 6, 9) != v);
    CHECK(entropy_after(30, 5, 10) != v);
    CHECK(entropy_after(31, 5, 9) != v);
    CHECK(entropy_after(30, 31, 31) != v); // never pressed
    // B instead of A on the same frames.
    serval_entropy_reset();
    for (u32 f = 0; f < 30; f++)
        serval_entropy_frame(f, (f >= 5 && f < 9 ? 0x2u : 0u) | (f >= 20 ? 0x2u : 0u));
    CHECK(random_entropy() != v);
    // Without input it still changes from frame to frame.
    u32 idle = entropy_after(1, 99, 99);
    CHECK(entropy_after(2, 99, 99) != idle);
    // A run of different values, not a few repeating ones.
    u32 seen[16];
    for (u32 i = 0; i < 16; i++) {
        seen[i] = entropy_after(25, i, 21);
        for (u32 j = 0; j < i; j++)
            CHECK(seen[j] != seen[i]);
    }
}

static void degenerate_ranges(void) {
    u32 warnings = debug_warning_count();
    CHECK(random_range(5, 5) == 5); // fine: no warning
    CHECK(random_range(7, 2) == 7);
    CHECK(random_range(7, 2) == 7);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1); // hi < lo, reported once
#else
    CHECK(debug_warning_count() == warnings);
#endif
    int full = random_range(-2147483647 - 1, 2147483647); // must not divide by zero
    (void)full;
}

// The span doesn't fit an int: computing lo + offset in int overflowed.
static void wide_ranges(void) {
    random_seed(7);
    bool seen_negative = false, seen_positive = false;
    for (int i = 0; i < 200; i++) {
        int v = random_range(-2000000000, 2000000000);
        CHECK(v >= -2000000000 && v <= 2000000000);
        seen_negative |= v < -1000000000;
        seen_positive |= v > 1000000000;
    }
    CHECK(seen_negative && seen_positive);
}

TEST_SUITE(random_tests, "random", {"same_seed_same_sequence", same_seed_same_sequence},
           {"zero_seed_still_produces_numbers", zero_seed_still_produces_numbers},
           {"range_stays_within_bounds", range_stays_within_bounds},
           {"degenerate_ranges", degenerate_ranges}, {"wide_ranges", wide_ranges},
           {"entropy_seeds_without_breaking_the_sequence",
            entropy_seeds_without_breaking_the_sequence},
           {"entropy_comes_from_the_input_history", entropy_comes_from_the_input_history});
