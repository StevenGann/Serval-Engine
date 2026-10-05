// Save memory on the GBA: the cartridge's battery-backed SRAM (32 KiB at
// 0x0E000000), for src/core/save.c.
//
// SRAM sits on an 8-bit bus: only byte accesses work (a 16- or 32-bit read
// returns the byte repeated, and wider writes store only one byte), hence the
// volatile u8 loops, which GCC can't turn into memcpy or word copies.
// serval_init() sets its wait states (WAITCNT: 8 cycles per access).

#include "../core/save_internal.h"

#define SRAM ((volatile u8*)0x0E000000)

// The ID string emulators and flash carts look for in the ROM to give the game
// SRAM (Nintendo's library put "SRAM_V" and its version there; scanners match
// it on a 4-byte boundary). It must be in every ROM that saves, so it shares
// one section with the device below: --gc-sections keeps or drops a section
// as a whole, and save.c references the device.
#define SAVE_SRAM_SECTION __attribute__((section(".rodata.serval_save_sram")))

SAVE_SRAM_SECTION __attribute__((aligned(4))) const char serval_sram_id[12] = "SRAM_V113";

static void sram_read(u32 offset, u8* dst, u32 count) {
    const volatile u8* src = SRAM + offset;
    for (u32 i = 0; i < count; i++)
        dst[i] = src[i];
}

static void sram_write(u32 offset, const u8* src, u32 count) {
    volatile u8* dst = SRAM + offset;
    for (u32 i = 0; i < count; i++)
        dst[i] = src[i];
}

SAVE_SRAM_SECTION const SaveDevice serval_platform_save_device = {.read = sram_read,
                                                                  .write = sram_write};
