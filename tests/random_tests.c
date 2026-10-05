#include "serval/random.h"
#include "test.h"

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

static void degenerate_ranges(void) {
    CHECK(random_range(5, 5) == 5);
    CHECK(random_range(7, 2) == 7);
    int full = random_range(-2147483647 - 1, 2147483647); // must not divide by zero
    (void)full;
}

TEST_SUITE(random_tests, "random", {"same_seed_same_sequence", same_seed_same_sequence},
           {"zero_seed_still_produces_numbers", zero_seed_still_produces_numbers},
           {"range_stays_within_bounds", range_stays_within_bounds},
           {"degenerate_ranges", degenerate_ranges},
           {"entropy_seeds_without_breaking_the_sequence",
            entropy_seeds_without_breaking_the_sequence});
