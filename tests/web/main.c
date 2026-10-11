// lz77: the web test page. Web builds run no test suites of their own (the
// host tests cover src/web/'s renderer, sound, mixer and LZ77 decoder on
// arrays); this page runs the suites that need the web's BIOS stand-ins in
// place, on its virtual hardware: the LZ77 loaders (tests/rom/lz77_tests.c,
// also in the GBA test ROM) and their check (tests/lz77_tests.c). Output goes
// to the browser's console. A failed check ends the page (debug_exit(1)), so
// it never finishes its frames: CI runs it with tools/web-shots.py, which
// then fails. Otherwise it shows "PASS" and the case count, and runs on.

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/screen.h"
#include "serval/text.h"

extern const TestSuite gba_lz77_tests;

void test_output(const char* line) {
    debug_log(line);
}

int main(void) {
    serval_init();
    static const TestSuite* const suites[] = {&lz77_tests, &gba_lz77_tests};
    unsigned failures = test_run(suites, sizeof(suites) / sizeof(suites[0]));
    if (failures)
        debug_exit(1);
    screen_set_backdrop(COLOR_RGB(0, 96, 32));
    text_print_centered(9, "WEB TESTS: PASS");
    for (;;) {
        frame_begin();
        frame_end();
    }
}
