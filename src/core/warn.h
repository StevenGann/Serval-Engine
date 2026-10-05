#ifndef SERVAL_CORE_WARN_H
#define SERVAL_CORE_WARN_H

// Engine-internal: reports API misuse in debug builds (SERVAL_DEBUG). Not part
// of the public API.

#include "serval/text.h"

// Writes "serval: <message>" to the debug log at warning level and counts it
// (debug_warning_count). Implemented per platform: src/gba/debug.c, src/host/debug.c.
void serval_warn(const char* message);

#ifdef SERVAL_DEBUG
#define SERVAL_WARN(...) serval_warn(text_format(__VA_ARGS__))
#else
#define SERVAL_WARN(...) ((void)0)
#endif

#endif // SERVAL_CORE_WARN_H
