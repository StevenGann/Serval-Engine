// The planned API of debug.h at run time (docs/development.md#planned-api):
// each planned function, called twice, does nothing harmful (returns 0, false
// or its type's "none", changes nothing) and in debug builds warns on the
// first call only. Run natively and in the test ROM.
//
// debug.h has no planned names in this version: debug_break(), the debug
// link's script breakpoint, is implemented (with a debugger attached:
// tests/link_vm_tests.c; without one, it does nothing: tests/rom/
// link_tests.c). A planned function added later gets its cases here.

#include "serval/debug.h"
#include "test.h"

TEST_SUITE(planned_debug_tests, "planned_debug");
