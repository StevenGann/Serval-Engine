// Runs the test suites on the GBA. Built as a ROM and run headless with
// mgba-rom-test: output goes to mGBA's debug log (debug_log), and
// mgba-rom-test's exit code (debug_exit) is 1 if any check failed, else 0.

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"

extern const TestSuite core_tests;
extern const TestSuite libc_tests;
extern const TestSuite sprite_tests;
extern const TestSuite gba_text_tests;
extern const TestSuite audio_tests;
extern const TestSuite splash_tests;
extern const TestSuite gba_map_tests;
extern const TestSuite gba_present_tests;
extern const TestSuite gba_save_tests;
extern const TestSuite gba_ecs_cost_tests;
extern const TestSuite gba_vm_tests;

void test_output(const char* line) {
    debug_log(line);
}

int main(void) {
    serval_init();

    static const TestSuite* const suites[] = {
        &ecs_tests,          &math_tests,        &physics_tests,     &map_tests,
        &random_tests,       &text_format_tests, &core_tests,        &libc_tests,
        &sprite_tests,       &gba_text_tests,    &audio_tests,       &splash_tests,
        &gba_map_tests,      &anim_tests,        &gba_present_tests, &psg_sequencer_tests,
        &save_tests,         &gba_save_tests,    &input_tests,       &path_tests,
        &gba_ecs_cost_tests, &vm_tests,          &gba_vm_tests,      &splash_logic_tests,
    };
    // Not the failure count itself: exit codes wrap at 256, so 256 failures
    // would look like a pass.
    debug_exit(test_run(suites, sizeof(suites) / sizeof(suites[0])) ? 1 : 0);
}
