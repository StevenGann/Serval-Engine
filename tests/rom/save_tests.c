// Save slots on the cartridge's SRAM (src/gba/save_sram.c), in mGBA: the ID
// string that makes emulators and flash carts provide SRAM, byte access,
// both copies of a slot, and the cost of a full-slot write and read.

#include "../test.h"
#include "serval/debug.h"
#include "serval/save.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/core/save_internal.h"

#define SRAM ((volatile u8*)0x0E000000)

// What emulators and flash carts do: look for "SRAM_V" on a 4-byte boundary
// in the ROM (here up to the 512 KiB the ROM is padded to). Compared
// character by character, so this test puts no such string in the ROM
// itself: the one found is the engine's, kept because save code is linked.
static void rom_contains_the_sram_id(void) {
    const u8* rom = (const u8*)0x08000000;
    u32 found = 0, at = 0;
    for (u32 i = 0; i < 512 * 1024 - 12; i += 4)
        if (rom[i] == 'S' && rom[i + 1] == 'R' && rom[i + 2] == 'A' && rom[i + 3] == 'M' &&
            rom[i + 4] == '_' && rom[i + 5] == 'V') {
            found++;
            at = i;
        }
    CHECK(found == 1);
    CHECK(rom[at + 6] == '1' && rom[at + 7] == '1' && rom[at + 8] == '3');
}

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

static bool sram_has_magic(u32 copy) {
    volatile u8* p = SRAM + copy * SAVE_COPY_SIZE;
    return p[0] == 'S' && p[1] == 'V' && p[2] == 'S' && p[3] == '1';
}

// mgba-rom-test may keep SRAM between runs: start from known slots.
static void sram_write_read_erase(void) {
    CHECK(serval_save_device == &serval_platform_save_device);
    save_erase(0);
    save_erase(7);
    u8 data[100], loaded[100];
    for (u32 i = 0; i < sizeof data; i++)
        data[i] = (u8)(i * 37u + 1u);
    CHECK(save_read(0, loaded, sizeof loaded, 1) == SAVE_EMPTY);
    CHECK(save_write(0, data, sizeof data, 1));
    CHECK(sram_has_magic(0) && !sram_has_magic(1));
    // The bytes really are in SRAM, one per address (an 8-bit bus).
    CHECK(SRAM[SAVE_HEADER_SIZE] == data[0] && SRAM[SAVE_HEADER_SIZE + 99] == data[99]);
    for (u32 i = 0; i < sizeof loaded; i++)
        loaded[i] = 0;
    CHECK(save_read(0, loaded, sizeof loaded, 1) == SAVE_OK);
    bool same = true;
    for (u32 i = 0; i < sizeof data; i++)
        same &= loaded[i] == data[i];
    CHECK(same);

    // The second write goes to copy B; the third back to A.
    data[0] = 0xEE;
    CHECK(save_write(0, data, sizeof data, 1));
    CHECK(sram_has_magic(0) && sram_has_magic(1));
    CHECK(SRAM[SAVE_COPY_SIZE + SAVE_HEADER_SIZE] == 0xEE);
    CHECK(save_read(0, loaded, sizeof loaded, 1) == SAVE_OK && loaded[0] == 0xEE);
    data[0] = 0x11;
    CHECK(save_write(0, data, sizeof data, 1));
    CHECK(SRAM[SAVE_HEADER_SIZE] == 0x11);
    CHECK(save_read(0, loaded, sizeof loaded, 1) == SAVE_OK && loaded[0] == 0x11);

    // The last slot ends at the end of the 32 KiB.
    CHECK(save_write(7, data, sizeof data, 2));
    CHECK(save_slot_version(7) == 2 && save_slot_size(7) == sizeof data);

    save_erase(0);
    CHECK(!sram_has_magic(0) && !sram_has_magic(1));
    CHECK(save_read(0, loaded, sizeof loaded, 1) == SAVE_EMPTY);
    CHECK(save_slot_size(7) == sizeof data);
    save_erase(7);
}

// What saves cost with SRAM at 8 wait states (serval_init), for a full slot
// (SAVE_SLOT_MAX bytes) and a small one (a high score table). Logged, for
// docs/runtime-systems.md#save-data.
static void save_cost(u32 size, u32* write_empty, u32* write, u32* read, u32* erase) {
    static SERVAL_EWRAM_BSS u8 data[SAVE_SLOT_MAX];
    for (u32 i = 0; i < size; i++)
        data[i] = (u8)i;
    save_erase(1);
    u32 t0 = cycles();
    CHECK(save_write(1, data, size, 1)); // empty slot
    u32 t1 = cycles();
    CHECK(save_write(1, data, size, 1)); // a slot holding a save
    u32 t2 = cycles();
    CHECK(save_read(1, data, size, 1) == SAVE_OK);
    u32 t3 = cycles();
    save_erase(1);
    u32 t4 = cycles();
    *write_empty = t1 - t0;
    *write = t2 - t1;
    *read = t3 - t2;
    *erase = t4 - t3;
}

static void save_costs(void) {
    static const u32 sizes[] = {SAVE_SLOT_MAX, 100};
    for (u32 i = 0; i < 2; i++) {
        u32 write_empty, write, read, erase;
        save_cost(sizes[i], &write_empty, &write, &read, &erase);
        test_output(text_format("  save: %u bytes: write %u cycles (empty slot %u), read %u, "
                                "erase %u",
                                sizes[i], write, write_empty, read, erase));
#ifdef __OPTIMIZE__
        CHECK(write < 280896u * 2); // under two frames (Debug builds are unoptimized)
#endif
    }
}

TEST_SUITE(gba_save_tests, "gba save", {"ROM contains the SRAM ID", rom_contains_the_sram_id},
           {"SRAM write, read and erase", sram_write_read_erase}, {"save costs", save_costs}, );
