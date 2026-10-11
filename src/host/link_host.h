#ifndef SERVAL_HOST_LINK_HOST_H
#define SERVAL_HOST_LINK_HOST_H

// Host build only (src/host/link_host.c): the debug link's platform end, for
// the tests, which play the endpoint. Not defined in GBA or web builds.

#include "../core/link_internal.h"

#ifdef SERVAL_LINK_AGENT
typedef struct {
    bool present;                             // what the presence check answers
    void (*ring)(ServalLinkMailbox* mailbox); // the endpoint's doorbell, if set
    void (*wait)(void);                       // a parked agent's wait, if set
    u32 rings, waits;                         // calls so far
    bool muted;                               // between park(true) and park(false)
    const char* title;                        // the ROM's title and game code
    const char* game_code;                    //
    ServalLinkFrameStats stats;               // what frame_end() would report
    u32 cycles, cycles_step;                  // the cycle counter, and what each read adds
} ServalHostLink;
extern ServalHostLink serval_host_link;
#endif

#endif // SERVAL_HOST_LINK_HOST_H
