#ifndef SERVAL_DEBUG_H
#define SERVAL_DEBUG_H

// Development diagnostics.

#include "serval/platform.h"

// Writes a line to the emulator's debug log (mGBA: Tools > View Logs, or the
// console of mgba-rom-test). Does nothing on hardware and other emulators.
void debug_log(const char* message);

// Ends a headless run with an exit code: mgba-rom-test, run with
// "-S 3 -R r0", exits with `code`. Elsewhere, stops the program.
void debug_exit(int code) __attribute__((noreturn));

#endif // SERVAL_DEBUG_H
