#include "serval/debug.h"

#include "../core/warn.h"

// mGBA's debug output registers. Writing 0xC0DE to the enable register makes
// mGBA answer 0x1DEA; hardware and other emulators ignore these addresses.
#define REG_MGBA_DEBUG_ENABLE (*(volatile u16*)0x04FFF780)
#define REG_MGBA_DEBUG_FLAGS (*(volatile u16*)0x04FFF700)
#define MGBA_DEBUG_STRING ((volatile char*)0x04FFF600)
#define MGBA_DEBUG_MAX 255
#define MGBA_LOG_WARN 2
#define MGBA_LOG_INFO 3
#define MGBA_LOG_SEND 0x100

static u32 warnings;

static void mgba_log(u32 level, const char* prefix, const char* message) {
    REG_MGBA_DEBUG_ENABLE = 0xC0DE;
    if (REG_MGBA_DEBUG_ENABLE != 0x1DEA)
        return;
    u32 i = 0;
    for (; *prefix && i < MGBA_DEBUG_MAX; i++)
        MGBA_DEBUG_STRING[i] = *prefix++;
    for (; *message && i < MGBA_DEBUG_MAX; i++)
        MGBA_DEBUG_STRING[i] = *message++;
    MGBA_DEBUG_STRING[i] = '\0';
    REG_MGBA_DEBUG_FLAGS = (u16)(level | MGBA_LOG_SEND);
}

void debug_log(const char* message) {
    mgba_log(MGBA_LOG_INFO, "", message);
}

void serval_warn(const char* message) {
    warnings++;
    mgba_log(MGBA_LOG_WARN, "serval: ", message);
}

u32 debug_warning_count(void) {
    return warnings;
}

void debug_exit(int code) {
    // BIOS call 3 (Stop). mgba-rom-test intercepts it and exits with r0.
    register int r0 __asm__("r0") = code;
    __asm__ volatile("swi 0x03" : : "r"(r0));
    for (;;) {
    }
}
