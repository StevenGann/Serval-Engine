// Platform functions for host builds (unit tests): debug.h output goes to
// stderr, and saves go to memory that lasts until the program exits.

#include "../core/map_internal.h"
#include "../core/save_internal.h"
#include "../core/warn.h"
#include "serval/debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// Save memory: SRAM's, starting like never-written SRAM (all 0xFF).
static u8* save_memory(void) {
    static u8 memory[SAVE_SIZE_SRAM];
    static bool ready;
    if (!ready) {
        memset(memory, 0xFF, sizeof memory);
        ready = true;
    }
    return memory;
}

static void host_save_read(u32 offset, u8* dst, u32 count) {
    memcpy(dst, save_memory() + offset, count);
}

static bool host_save_write(u32 offset, const u8* src, u32 count) {
    memcpy(save_memory() + offset, src, count);
    return true;
}

const SaveDevice serval_platform_save_device = {
    .read = host_save_read, .write = host_save_write, SAVE_LAYOUT_SRAM};
