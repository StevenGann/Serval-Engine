#ifndef SERVAL_CORE_WARN_H
#define SERVAL_CORE_WARN_H

// Engine-internal: reports API misuse in debug builds (SERVAL_DEBUG). Not part
// of the public API.

#include "serval/platform.h"

#include <stddef.h>

// Writes "serval: <message>" to the debug log at warning level and counts it
// (debug_warning_count). Implemented per platform: src/gba/debug.c,
// src/web/platform.c, src/host/platform.c.
void serval_warn(const char* message);

// The size of serval_warnf's buffer: the longest warning, 247 characters, and
// its terminating NUL. Every platform's log shows that much whole: mGBA's debug
// string holds 255 characters, "serval: " included (src/gba/debug.c); the
// web's console line holds 279 after the prefix (src/web/platform.c; both
// files check this at compile time), and the host's stderr has no limit.
// Longer warnings are cut off, so keep each one within 247 characters,
// counting its numbers at their widest.
#define SERVAL_WARN_MAX 248

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
// Formats a warning as text_format does (the same conversions, text.h) into a
// buffer of its own, which holds SERVAL_WARN_MAX - 1 characters rather than
// text_format's 127, and reports it with serval_warn. What SERVAL_WARN calls;
// defined in text_format.c, in debug builds only.
void serval_warnf(const char* fmt, ...);

// For tests: the text of the last warning serval_warnf formatted (not counting
// one text_format.c reported while formatting it, about the format itself).
const char* serval_warn_text(void);

#define SERVAL_WARN(...) serval_warnf(__VA_ARGS__)
#else
#define SERVAL_WARN(...) ((void)0)
#endif

#endif // SERVAL_CORE_WARN_H
