// Platform functions for host builds (unit tests): debug.h output goes to
// stderr, and random_entropy uses the clock.

#include "../core/warn.h"
#include "serval/debug.h"

#include "serval/random.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static u32 warnings;

void debug_log(const char* message) {
    fprintf(stderr, "%s\n", message);
}

void serval_warn(const char* message) {
    warnings++;
    fprintf(stderr, "serval: %s\n", message);
}

u32 debug_warning_count(void) {
    return warnings;
}

void debug_exit(int code) {
    exit(code);
}

// random.h's random_entropy for host builds.
u32 random_entropy(void) {
    return (u32)clock() ^ (u32)time(NULL);
}
