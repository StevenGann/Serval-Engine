// The debug link's game side (docs/debug-link.md): specified, not implemented.
// The agent will live here (debug builds only): the mailbox, the framing and
// the queries; the VM's part stays in vm.c, behind a hook. Portable: built for
// the GBA, the web and the host.
//
// Planned (docs/releases.md#planned-api): debug_break() is declared so that
// its name is reserved in scripts from 1.0, and is a stub. A stub does nothing
// harmful: it doesn't stop the game, keeps no state, and in debug builds warns
// on its first call, saying what happens instead.

#include "serval/debug.h"

#include "warn.h"

void debug_break(void) {
#ifdef SERVAL_DEBUG
    static bool warned;
    if (!warned) {
        warned = true;
        SERVAL_WARN("debug_break: script breakpoints are planned, not implemented in this engine "
                    "version; the game runs on");
    }
#endif
}
