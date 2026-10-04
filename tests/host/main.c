// Runs the platform-neutral test suites natively.

#include "../test.h"

#include <stdio.h>

void test_output(const char* line) {
    puts(line);
}

int main(void) {
    static const TestSuite* const suites[] = {&ecs_tests};
    return test_run(suites, sizeof(suites) / sizeof(suites[0])) ? 1 : 0;
}
