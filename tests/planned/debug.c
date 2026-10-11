// Every planned name in debug.h (SERVAL_PLANNED), used once, each on its own line
// ending "// planned". tools/check-planned.py (CTest planned_api; part of the
// build in web builds) compiles this file and fails unless each marked line
// warns as planned, no other line warns, and every planned name in the public
// headers is used in one of these files. When a name is implemented and its
// SERVAL_PLANNED goes, its line goes too. This file is in no build target: it
// warns on purpose. See docs/development.md#planned-api.

//
// debug.h has no planned names in this version: debug_break(), the debug
// link's script breakpoint, is implemented (docs/debug-link.md#script-
// breakpoints). A planned name added later gets its line here.

#include "serval/debug.h"

void planned_debug(void);
void planned_debug(void) {}
