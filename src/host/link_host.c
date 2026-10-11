// The debug link's agent on the host (unit tests): the platform's end is the
// tests' fake endpoint (tests/link_tests.c), through serval_host_link. In
// debug builds with the agent only.

#include "link_host.h"
#include "../core/link_internal.h"

#ifdef SERVAL_LINK_AGENT

ServalHostLink serval_host_link;

bool serval_link_platform_present(void) {
    return serval_host_link.present;
}

void serval_link_platform_ring(ServalLinkMailbox* mailbox) {
    serval_host_link.rings++;
    if (serval_host_link.ring)
        serval_host_link.ring(mailbox);
}

void serval_link_platform_wait(void) {
    serval_host_link.waits++;
    if (serval_host_link.wait)
        serval_host_link.wait();
}

void serval_link_platform_park(bool parked) {
    serval_host_link.muted = parked;
}

void serval_link_platform_rom(char title[13], char game_code[5]) {
    const char* t = serval_host_link.title ? serval_host_link.title : "";
    const char* c = serval_host_link.game_code ? serval_host_link.game_code : "";
    u32 i = 0;
    for (; i < 12 && t[i]; i++)
        title[i] = t[i];
    title[i] = '\0';
    for (i = 0; i < 4 && c[i]; i++)
        game_code[i] = c[i];
    game_code[i] = '\0';
}

void serval_link_platform_stats(ServalLinkFrameStats* s) {
    *s = serval_host_link.stats;
}

// debug link (E6): each read advances the tests' clock by cycles_step.
u32 serval_link_platform_cycles(void) {
    serval_host_link.cycles += serval_host_link.cycles_step;
    return serval_host_link.cycles;
}

#endif // SERVAL_LINK_AGENT
