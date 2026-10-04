// Runs the test suites on the GBA. Built as a ROM and run headless with
// mgba-rom-test: output goes to mGBA's debug log, and the number of failed
// checks is passed in r0 to SWI 3, which mgba-rom-test turns into its exit code.

#include "../test.h"
#include "serval/core.h"

extern const TestSuite core_tests;
extern const TestSuite libc_tests;
extern const TestSuite sprite_tests;

// mGBA debug output registers (ignored by hardware and other emulators).
#define REG_MGBA_DEBUG_ENABLE (*(volatile u16*)0x04FFF780)
#define REG_MGBA_DEBUG_FLAGS (*(volatile u16*)0x04FFF700)
#define MGBA_DEBUG_STRING ((volatile char*)0x04FFF600)
#define MGBA_LOG_INFO 3
#define MGBA_LOG_SEND 0x100

void test_output(const char* line) {
    unsigned i = 0;
    for (; line[i] && i < 255; i++)
        MGBA_DEBUG_STRING[i] = line[i];
    MGBA_DEBUG_STRING[i] = '\0';
    REG_MGBA_DEBUG_FLAGS = MGBA_LOG_INFO | MGBA_LOG_SEND;
}

static void __attribute__((noreturn)) exit_with(unsigned failures) {
    register unsigned r0 __asm__("r0") = failures;
    __asm__ volatile("swi 0x03" : : "r"(r0));
    for (;;) {
    }
}

int main(void) {
    REG_MGBA_DEBUG_ENABLE = 0xC0DE;
    serval_init();

    static const TestSuite* const suites[] = {&ecs_tests, &core_tests, &libc_tests, &sprite_tests};
    exit_with(test_run(suites, sizeof(suites) / sizeof(suites[0])));
}
