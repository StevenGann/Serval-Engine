// The debug link's agent, the GBA's end of it (docs/debug-link.md#finding-
// the-endpoint): the presence register and the doorbell at 0x04FFF800-
// 0x04FFF80F (reserved since 1.0, core-api.md), the wait while parked and
// the sound muted meanwhile, the ROM header's title and game code; and, for
// the GBA and the web alike, what STATS reads from frame_end(). In debug
// builds with the agent only.
//
// Only the debugger's mGBA fork answers the presence register. Hardware, stock
// mGBA and other emulators don't, and the agent stays dormant: serval_init()
// writes the register once and reads it back, and nothing else is written.

#include "../core/link_internal.h"
#include "internal.h"
#include "serval/core.h"

#ifndef SERVAL_WEB
#include <tonc.h>
#endif

#ifdef SERVAL_LINK_AGENT

// frame_end()'s Maxmod mixer: its cycles in the last frame (core.c).
SERVAL_EWRAM_BSS u32 serval_link_mixer_cycles;

void serval_link_platform_stats(ServalLinkFrameStats* s) {
    s->frame = frame_count();
    s->cycles = frame_cpu_cycles();
    s->drawn = serval_sprite_stats.drawn;
    s->matrices = serval_sprite_stats.matrices;
    s->dropped = serval_sprite_stats.dropped;
    s->mixer_cycles = serval_mixer_hook ? serval_link_mixer_cycles : 0;
}

// debug link (E6): the cycle counter frame_cpu_cycles() counts with (core.c):
// timers 2 and 3 on the GBA, the browser's clock on the web.
u32 serval_link_platform_cycles(void) {
#ifdef SERVAL_WEB
    return serval_web_cycles();
#else
    u32 hi, lo;
    do { // read again if the low half wrapped between the two reads
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
#endif
}

#ifndef SERVAL_WEB // the web's end: src/web/link_web.c

#define REG_LINK_PRESENCE (*(volatile u16*)0x04FFF800)
#define REG_LINK_DOORBELL (*(volatile u32*)0x04FFF804)
#define LINK_PRESENCE_ASK 0x5E41
#define LINK_PRESENCE_ANSWER 0x1A5E

// The sound outputs: SOUNDCNT_L's tone generator enables (bits 8-15) and
// SOUNDCNT_H's Direct Sound A and B enables (bits 8, 9, 12, 13).
#define TONES_ON 0xFF00u
#define DIRECT_SOUND_ON 0x3300u

typedef struct {
    u16 tones, direct_sound; // the bits muting cleared, put back on resuming
} Muted;
SERVAL_EWRAM_BSS static Muted muted;

bool serval_link_platform_present(void) {
    REG_LINK_PRESENCE = LINK_PRESENCE_ASK;
    return REG_LINK_PRESENCE == LINK_PRESENCE_ANSWER;
}

// The fork handles the ring during the write: the compiler must assume the
// mailbox changed.
void serval_link_platform_ring(ServalLinkMailbox* mailbox) {
    __asm__ volatile("" ::: "memory");
    REG_LINK_DOORBELL = (u32)(uintptr_t)mailbox;
    __asm__ volatile("" ::: "memory");
}

// The fork pauses the machine while the agent is parked, so this runs only
// with an endpoint that doesn't (hardware, later): a frame's wait between
// polls.
void serval_link_platform_wait(void) {
    VBlankIntrWait();
    __asm__ volatile("" ::: "memory");
}

// Parked, the game plays nothing: the tone generators and Direct Sound are
// cut from the output (their state kept), and put back on resuming.
void serval_link_platform_park(bool parked) {
    if (parked) {
        muted.tones = REG_SNDDMGCNT & TONES_ON;
        muted.direct_sound = REG_SNDDSCNT & DIRECT_SOUND_ON;
        REG_SNDDMGCNT &= (u16)~TONES_ON;
        REG_SNDDSCNT &= (u16)~DIRECT_SOUND_ON;
    } else {
        REG_SNDDMGCNT |= muted.tones;
        REG_SNDDSCNT |= muted.direct_sound;
    }
}

// The cartridge header: the title at 0xA0 (12 bytes, NUL-padded) and the
// game code at 0xAC (4).
void serval_link_platform_rom(char title[13], char game_code[5]) {
    const volatile u8* header = (const volatile u8*)0x080000A0;
    for (u32 i = 0; i < 12; i++)
        title[i] = (char)header[i];
    title[12] = '\0';
    for (u32 i = 0; i < 4; i++)
        game_code[i] = (char)header[12 + i];
    game_code[4] = '\0';
}

#endif // !SERVAL_WEB
#endif // SERVAL_LINK_AGENT
