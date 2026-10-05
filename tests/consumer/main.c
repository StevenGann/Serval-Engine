// consumer: a minimal game built from a Serval Engine release archive by
// tests/consumer/CMakeLists.txt, the way a game project uses the engine.
//
// Runs a few frames, prints text, does integer division (which needs libgcc)
// and exits through debug_exit with the number of failed checks, so
// mgba-rom-test reports whether the ROM booted and ran correctly.
//
// Uses only Serval Engine's API; no third-party headers.

#include "serval/serval.h"

// Never called: the link must drop it (check-gc-sections.cmake).
int consumer_unused(int x);
int consumer_unused(int x) {
    return x * 3 + 1;
}

static volatile int dividend = 1000;
static volatile int divisor = 7;

int main(void) {
    serval_init();
    text_print(1, 1, "consumer");

    int failures = 0;
    for (int frame = 0; frame < 3; frame++) {
        frame_begin();
        frame_end();
    }
    if (dividend / divisor != 142 || dividend % divisor != 6) {
        debug_log("consumer: integer division is wrong");
        failures++;
    }
    if (debug_warning_count() != 0) {
        debug_log("consumer: the engine reported warnings");
        failures++;
    }
    debug_log(failures == 0 ? "consumer: ok" : "consumer: FAILED");
    debug_exit(failures);
}
