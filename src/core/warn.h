#ifndef SERVAL_CORE_WARN_H
#define SERVAL_CORE_WARN_H

// Engine-internal: reports API misuse in debug builds (SERVAL_DEBUG). Not part
// of the public API.

#include "serval/text.h"

#include <stddef.h>

// Writes "serval: <message>" to the debug log at warning level and counts it
// (debug_warning_count). Implemented per platform: src/gba/debug.c, src/host/platform.c.
void serval_warn(const char* message);

// True if p could point at readable data (RAM or ROM). Debug checks use it to
// catch numbers passed where pointers belong.
static inline bool serval_plausible_pointer(const void* p) {
#ifdef SERVAL_GBA
    u32 a = (u32)(uintptr_t)p;
    return (a >= 0x02000000 && a < 0x02040000) || // EWRAM
           (a >= 0x03000000 && a < 0x03008000) || // IWRAM
           (a >= 0x08000000 && a < 0x0A000000);   // ROM
#else
    return p != NULL;
#endif
}

#ifdef SERVAL_DEBUG
#define SERVAL_WARN(...) serval_warn(text_format(__VA_ARGS__))
#else
#define SERVAL_WARN(...) ((void)0)
#endif

#endif // SERVAL_CORE_WARN_H
