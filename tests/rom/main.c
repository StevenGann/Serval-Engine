// Runs the test suites on the GBA. Built as a ROM and run headless with
// mgba-rom-test: output goes to mGBA's debug log (debug_log), and the number
// of failed checks becomes mgba-rom-test's exit code (debug_exit).

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"

extern const TestSuite core_tests;
extern const TestSuite libc_tests;
extern const TestSuite sprite_tests;
extern const TestSuite gba_text_tests;
extern const TestSuite audio_tests;

void test_output(const char* line) {
    debug_log(line);
}

int main(void) {
    serval_init();

    static const TestSuite* const suites[] = {
        &ecs_tests,  &math_tests, &physics_tests, &random_tests,   &text_format_tests,
        &core_tests, &libc_tests, &sprite_tests,  &gba_text_tests, &audio_tests,
    };
    debug_exit((int)test_run(suites, sizeof(suites) / sizeof(suites[0])));
}
