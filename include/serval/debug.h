#ifndef SERVAL_DEBUG_H
#define SERVAL_DEBUG_H

// Development diagnostics.

#include "serval/platform.h"

// Writes a line to the emulator's debug log (mGBA: Tools > View Logs, or the
// console of mgba-rom-test; cut at 255 characters), or on the web to the
// browser's console. Does nothing on hardware and other emulators.
void debug_log(const char* message);

// Number of warnings the engine has reported so far. Debug builds (SERVAL_DEBUG,
// on in Debug and RelWithDebInfo builds) report API misuse, such as drawing a
// sprite that isn't loaded, to the debug log as "serval: ..." warnings; release
// builds report nothing and this stays 0.
u32 debug_warning_count(void);

// Ends a headless run with an exit code: mgba-rom-test, run with
// "-S 3 -R r0", exits with `code`. Elsewhere, stops the program.
void debug_exit(int code) __attribute__((noreturn));

// debug link: a script breakpoint (docs/debug-link.md#script-breakpoints).
// Stops the whole game where it is called (every script, every system, the
// frame) while a debugger is attached over the debug link, until the
// debugger resumes it; the debugger is told the game stopped here. With no
// debugger attached, and in release builds, it does nothing. Scripts get a
// builtin of the same name, compiling to the VM's BRK opcode, in the version
// that implements it.
//
// Planned (docs/debug-link.md#script-breakpoints): in this engine version it
// does nothing and the game runs on (warns once, debug builds).
SERVAL_PLANNED("script breakpoints, docs/debug-link.md#script-breakpoints")
void debug_break(void);

#endif // SERVAL_DEBUG_H
