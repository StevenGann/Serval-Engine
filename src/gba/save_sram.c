// Save memory on the GBA for SAVE SRAM (the default): the cartridge's
// battery-backed SRAM (32 KiB at 0x0E000000), for src/core/save.c.
//
// SRAM sits on an 8-bit bus: only byte accesses work (a 16- or 32-bit read
// returns the byte repeated, and wider writes store only one byte), hence the
// volatile u8 loops, which GCC can't turn into memcpy or word copies.
// serval_init() sets its wait states (WAITCNT: 8 cycles per access).
//
// Each save type's backend is its own object (serval_save_<type> in
// CMakeLists.txt); serval_add_rom() links the game's one.

#include "../core/save_internal.h"

#define SRAM ((volatile u8*)0x0E000000)

// The ID string emulators and flash carts look for in the ROM to give the game
// SRAM (Nintendo's library put "SRAM_V" and its version there; scanners match
// it on a 4-byte boundary). It must be in every ROM that saves, so it shares
// one section with the device below: --gc-sections keeps or drops a section
// as a whole, and save.c references the device. tools/check-rom.py looks for
// this section in the link map.
#define SAVE_DEVICE_SECTION __attribute__((section(".rodata.serval_save_device")))

SAVE_DEVICE_SECTION __attribute__((aligned(4))) static const char sram_id[12] = "SRAM_V113";

static void sram_read(u32 offset, u8* dst, u32 count) {
    const volatile u8* src = SRAM + offset;
    for (u32 i = 0; i < count; i++)
        dst[i] = src[i];
}

static bool sram_write(u32 offset, const u8* src, u32 count) {
    volatile u8* dst = SRAM + offset;
    for (u32 i = 0; i < count; i++)
        dst[i] = src[i];
    return true;
}

SAVE_DEVICE_SECTION const SaveDevice serval_platform_save_device = {
    .read = sram_read, .write = sram_write, .id = sram_id, SAVE_LAYOUT_SRAM};
