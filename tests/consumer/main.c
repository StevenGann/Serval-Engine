// consumer: a minimal game built from a Serval Engine release archive by
// tests/consumer/CMakeLists.txt, the way a game project uses the engine.
//
// Runs a few frames, prints text, does integer division (which needs libgcc),
// runs a script assembled at build time by serval_add_script() (consumer.svm:
// its Create handler stores 42 in a global) and exits through debug_exit with
// the number of failed checks, so mgba-rom-test reports whether the ROM
// booted and ran correctly.
//
// Uses only Serval Engine's API; no third-party headers.

#include "consumer_script.h"
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
    if (!vm_load(consumer_script, consumer_script_size)) {
        debug_log("consumer: the script blob did not load");
        failures++;
    }
    vm_start(OBJ_THING, VM_EV_CREATE); // runs in the first vm_step
    for (int frame = 0; frame < 3; frame++) {
        frame_begin();
        vm_step();
        frame_end();
    }
    if (vm_global(G_ANSWER) != 42) {
        debug_log("consumer: the script did not run");
        failures++;
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
