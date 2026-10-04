#include "test.h"

static unsigned failures;
static const char* current_suite;
static const char* current_case;

// Tiny line builder: the ROM runner has no snprintf.
typedef struct {
    char text[256];
    unsigned len;
} Line;

static void append(Line* line, const char* s) {
    while (*s && line->len < sizeof(line->text) - 1)
        line->text[line->len++] = *s++;
    line->text[line->len] = '\0';
}

static void append_uint(Line* line, unsigned value) {
    char digits[12];
    unsigned n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    char s[2] = {0, 0};
    while (n) {
        s[0] = digits[--n];
        append(line, s);
    }
}

void test_fail(const char* file, int line_number, const char* expr) {
    failures++;
    Line line = {0};
    append(&line, "FAIL ");
    append(&line, current_suite);
    append(&line, ".");
    append(&line, current_case);
    append(&line, " at ");
    append(&line, file);
    append(&line, ":");
    append_uint(&line, (unsigned)line_number);
    append(&line, ": CHECK(");
    append(&line, expr);
    append(&line, ")");
    test_output(line.text);
}

unsigned test_run(const TestSuite* const* suites, unsigned count) {
    unsigned cases = 0;
    for (unsigned s = 0; s < count; s++) {
        current_suite = suites[s]->name;
        for (unsigned c = 0; c < suites[s]->count; c++) {
            current_case = suites[s]->cases[c].name;
            suites[s]->cases[c].run();
            cases++;
        }
    }

    Line line = {0};
    append_uint(&line, cases);
    append(&line, " test cases, ");
    append_uint(&line, failures);
    append(&line, failures == 1 ? " failed check" : " failed checks");
    test_output(line.text);
    return failures;
}
