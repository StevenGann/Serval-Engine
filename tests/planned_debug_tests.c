// The planned API of debug.h at run time (docs/development.md#planned-api):
// debug_break(), called twice, does nothing harmful (it returns, and the game
// runs on: no debugger can be attached in this version) and in debug builds
// warns on the first call only, saying script breakpoints are planned. Run
// natively and in the test ROM.

// Calls planned API on purpose: without this, every call would warn.
#define SERVAL_NO_PLANNED_WARNINGS

#include "../src/core/warn.h"
#include "serval/debug.h"
#include "test.h"

#ifdef SERVAL_DEBUG
static bool equals(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
#endif

static void debug_break_runs_on_and_warns_once(void) {
    u32 before = debug_warning_count();
    debug_break();
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + 1);
    CHECK(equals(serval_warn_text(), "debug_break: script breakpoints are planned, not "
                                     "implemented in this engine version; the game runs on"));
#else
    CHECK(debug_warning_count() == before);
#endif
    debug_break();
    debug_break();
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + 1);
#else
    CHECK(debug_warning_count() == before);
#endif
}

TEST_SUITE(planned_debug_tests, "planned_debug",
           {"debug_break", debug_break_runs_on_and_warns_once});
