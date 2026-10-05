// Runs the save suites on the GBA for one save type: built once per type
// other than SRAM (which serval_tests covers) as serval_tests_<type>
// (tests/CMakeLists.txt) and run headless with mgba-rom-test, like
// tests/rom/main.c. The shared suite checks the slot logic on simulated
// memories; the hardware suite the ROM's real Flash or EEPROM in mGBA.

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"

extern const TestSuite gba_save_tests;

void test_output(const char* line) {
    debug_log(line);
}

int main(void) {
    serval_init();
    static const TestSuite* const suites[] = {&save_tests, &gba_save_tests};
    debug_exit(test_run(suites, sizeof(suites) / sizeof(suites[0])) ? 1 : 0);
}
