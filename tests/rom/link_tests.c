// The debug link's agent on the GBA with no endpoint (docs/debug-link.md):
// mgba-rom-test's stock mGBA doesn't answer the presence register, as
// hardware and other emulators don't, so serval_init() (rom/main.c) left the
// agent dormant: no frame hook, nothing written to the mailbox. And dormant,
// it costs frame_end() one load and one compare: timed here (about 30
// cycles, the hook being in EWRAM: 0.01% of a frame). debug_break()
// returns at once, warning nothing. In debug builds with the agent; release
// builds have none of it (the link_map tests check the ROMs' maps), and
// their debug_break() does nothing either.

#include "../../src/core/link_internal.h"
#include "../test.h"
#include "serval/debug.h"
#include "serval/text.h"

#include <tonc.h>

#ifdef SERVAL_LINK_AGENT

#define REG_LINK_PRESENCE (*(volatile u16*)0x04FFF800)

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

#ifdef __OPTIMIZE__
#define CHECK_TIMING(cond) CHECK(cond)
#else
#define CHECK_TIMING(cond) ((void)(cond))
#endif

static void dormant_without_an_endpoint(void) {
    CHECK(serval_link_hook == NULL);
    CHECK(!serval_link_started() && !serval_link_attached());
    const u8* m = (const u8*)&serval_link_mailbox;
    u32 written = 0;
    for (u32 i = 0; i < sizeof(serval_link_mailbox); i++)
        written += m[i] != 0;
    CHECK(written == 0);
    CHECK(REG_LINK_PRESENCE != 0x1A5E);
    // The mailbox and the hook are in EWRAM: the agent adds no IWRAM.
    CHECK(((u32)(uintptr_t)&serval_link_mailbox & 0xFF000000) == 0x02000000);
    CHECK(((u32)(uintptr_t)&serval_link_hook & 0xFF000000) == 0x02000000);
}

// frame_end()'s poll, as core.c has it, in a function of its own.
__attribute__((noinline)) static void frame_end_poll(void) {
    if (serval_link_hook)
        serval_link_hook();
}

__attribute__((noinline)) static void nothing(void) {
    __asm__ volatile("");
}

static void dormant_costs_a_load_and_a_compare(void) {
    enum { CALLS = 256 };
    u32 t0 = cycles();
    for (u32 i = 0; i < CALLS; i++)
        frame_end_poll();
    u32 t1 = cycles();
    for (u32 i = 0; i < CALLS; i++)
        nothing();
    u32 t2 = cycles();
    u32 poll = t1 - t0, empty = t2 - t1;
    u32 each = poll > empty ? (poll - empty) / CALLS : 0;
    debug_log(text_format("link: a dormant agent costs frame_end() %u cycles a frame "
                          "(of %u: %u.%03u%%)",
                          each, 280896u, each * 100 / 280896u, each * 100000 / 280896u % 1000));
    CHECK_TIMING(each <= 40); // about 30: a literal and an EWRAM load from ROM code
}

// debug link (E6): debug_break() with no debugger does nothing: it returns
// at once, writes nothing to the mailbox and warns nothing (it warned once
// while it was planned API).
static void debug_break_does_nothing_without_a_debugger(void) {
    u32 warnings = debug_warning_count();
    debug_break();
    debug_break();
    CHECK(debug_warning_count() == warnings);
    const u8* m = (const u8*)&serval_link_mailbox;
    u32 written = 0;
    for (u32 i = 0; i < sizeof(serval_link_mailbox); i++)
        written += m[i] != 0;
    CHECK(written == 0 && !serval_link_parked());
}

TEST_SUITE(gba_link_tests, "gba_link", {"dormant without an endpoint", dormant_without_an_endpoint},
           {"dormant costs a load and a compare", dormant_costs_a_load_and_a_compare},
           {"debug_break() without a debugger", debug_break_does_nothing_without_a_debugger});

#else

static void no_agent(void) {
    CHECK(true);
}

// debug link (E6): debug_break() in a build without the agent (release, or
// SERVAL_LINK off) does nothing and warns nothing.
static void debug_break_does_nothing(void) {
    u32 warnings = debug_warning_count();
    debug_break();
    debug_break();
    CHECK(debug_warning_count() == warnings);
}

TEST_SUITE(gba_link_tests, "gba_link", {"no agent in this build", no_agent},
           {"debug_break() does nothing", debug_break_does_nothing});

#endif // SERVAL_LINK_AGENT
