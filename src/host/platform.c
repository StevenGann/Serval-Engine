// Platform functions for host builds (unit tests): debug.h output goes to
// stderr.

#include "../core/map_internal.h"
#include "../core/warn.h"
#include "serval/debug.h"

#include <stdio.h>
#include <stdlib.h>

static u32 warnings;

void debug_log(const char* message) {
    fprintf(stderr, "%s\n", message);
}

void serval_warn(const char* message) {
    warnings++;
    fprintf(stderr, "serval: %s\n", message);
}

u32 debug_warning_count(void) {
    return warnings;
}

void debug_exit(int code) {
    exit(code);
}

// Host builds draw nothing: map layers only matter for collision there.
void serval_map_attach(void) {}
