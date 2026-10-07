// Runs the platform-neutral test suites natively.

#include "../test.h"

#include <stdio.h>

void test_output(const char* line) {
    puts(line);
}

int main(void) {
    static const TestSuite* const suites[] = {
        &ecs_tests,           &random_tests,
        &text_format_tests,   &math_tests,
        &physics_tests,       &map_tests,
        &web_ppu_tests,       &web_apu_tests,
        &anim_tests,          &psg_sequencer_tests,
        &save_tests,          &input_tests,
        &path_tests,          &vm_tests,
        &splash_logic_tests,  &color_tests,
        &planned_audio_tests, &planned_sprites_tests,
        &planned_map_tests,   &planned_screen_tests,
    };
    return test_run(suites, sizeof(suites) / sizeof(suites[0])) ? 1 : 0;
}
