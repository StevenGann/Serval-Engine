// Runs the sampled audio suite on the GBA: tracker music and sampled sound
// effects (tests/rom/sampled_audio_tests.c), in a test ROM of their own,
// serval_tests_sampled_audio (tests/CMakeLists.txt), run headless with
// mgba-rom-test like tests/rom/main.c. Apart because Maxmod's IWRAM (about 5
// KB with the mixing buffer) doesn't fit beside every other suite's, and so
// that no other suite runs with the mixer on.

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"

extern const TestSuite gba_sampled_audio_tests;

void test_output(const char* line) {
    debug_log(line);
}

int main(void) {
    serval_init();
    static const TestSuite* const suites[] = {&gba_sampled_audio_tests};
    debug_exit(test_run(suites, sizeof(suites) / sizeof(suites[0])) ? 1 : 0);
}
