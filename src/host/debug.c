// debug.h for host builds (unit tests): output goes to stderr.

#include "serval/debug.h"
#include "../core/warn.h"

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
