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
extern const TestSuite gba_blend_tests;
extern const TestSuite gba_save_tests;
extern const TestSuite gba_ecs_cost_tests;
extern const TestSuite gba_vm_tests;
extern const TestSuite gba_sprite_tiles_tests; // sprite tiles: runtime sprite tiles
extern const TestSuite gba_palette_tests;      // palettes: palette writes
extern const TestSuite gba_stream_tests;
extern const TestSuite gba_raster_tests; // raster: raster effects
extern const TestSuite gba_wave_tests;   // wave channel: the PSG wave channel

void test_output(const char* line) {
    debug_log(line);
}

int main(void) {
    serval_init();

    static const TestSuite* const suites[] = {
        &ecs_tests,
        &math_tests,
        &physics_tests,
        &map_tests,
        &random_tests,
        &text_format_tests,
        &core_tests,
        &libc_tests,
        &sprite_tests,
        &gba_stream_tests,
        &gba_text_tests,
        &audio_tests,
        &splash_tests,
        &gba_map_tests,
        &anim_tests,
        &gba_present_tests,
        &gba_blend_tests,
        &psg_sequencer_tests,
        &psg_wave_tests, // wave channel
        &gba_wave_tests, // wave channel
        &save_tests,
        &gba_save_tests,
        &input_tests,
        &path_tests,
        &gba_ecs_cost_tests,
        &vm_tests,
        &gba_vm_tests,
        &gba_raster_tests,       // raster:
        &gba_sprite_tiles_tests, // sprite tiles: runtime sprite tiles
        &gba_palette_tests,      // palettes: palette writes
        &splash_logic_tests,
        &color_tests,
        &planned_audio_tests,
        &planned_sprites_tests,
        &planned_map_tests,
        &planned_screen_tests,
    };
    // Not the failure count itself: exit codes wrap at 256, so 256 failures
    // would look like a pass.
    debug_exit(test_run(suites, sizeof(suites) / sizeof(suites[0])) ? 1 : 0);
}
