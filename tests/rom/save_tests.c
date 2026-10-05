// Save slots on the ROM's real save memory, in mGBA: SRAM in serval_tests,
// Flash and EEPROM in serval_tests_<type> (tests/rom/save_main.c). The ID
// string that makes emulators and flash carts provide the memory, both copies
// of a slot where the layout puts them, every slot full at once (on 128 KiB
// Flash, slots 4-7 are in bank 1; on 8 KiB EEPROM, past the first 512
// bytes: a memory that ignored the bank or the address bits would mix them
// up), and the cost of a full-slot write and read.

#include "../test.h"
#include "serval/debug.h"
#include "serval/save.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/core/save_internal.h"

#define SRAM ((volatile u8*)0x0E000000)

static const SaveDevice* device(void) {
    return &serval_platform_save_device;
}

// Save type ID string prefixes, each character plus one, so this test puts
// none of them in the ROM itself.
static const char* const shifted_ids[] = {"TSBN`W",     "TSBN`G`W",  "GMBTI`W",
                                          "GMBTI623`W", "GMBTI2N`W", "FFQSPN`W"};

static bool id_at(const u8* rom, const char* shifted) {
    for (u32 i = 0; shifted[i]; i++)
        if (rom[i] != (u8)(shifted[i] - 1))
            return false;
    return true;
}

// What emulators and flash carts do: look for the save type ID strings on
// 4-byte boundaries in the ROM (here up to the 512 KiB the ROM is padded to).
// Exactly one is there: the engine's for this ROM's save type, kept because
// save code is linked.
static void rom_contains_one_save_id(void) {
    const u8* rom = (const u8*)0x08000000;
    u32 found = 0;
    const u8* at = NULL;
    for (u32 i = 0; i < 512 * 1024 - 16; i += 4)
        for (u32 k = 0; k < sizeof shifted_ids / sizeof shifted_ids[0]; k++)
            if (id_at(rom + i, shifted_ids[k])) {
                found++;
                at = rom + i;
            }
    CHECK(found == 1);
    CHECK(device()->id != NULL && at == (const u8*)device()->id);
}

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

static u32 copy_offset(u32 copy) {
    return copy * device()->copy_size;
}

static bool has_magic(u32 copy) {
    u8 p[4];
    device()->read(copy_offset(copy), p, sizeof p);
    return p[0] == 'S' && p[1] == 'V' && p[2] == 'S' && p[3] == '1';
}

static u8 byte_at(u32 offset) {
    u8 b;
    device()->read(offset, &b, 1);
    return b;
}

// Starts from known slots (mgba-rom-test starts with blank save memory, but
// a flash cart or a .sav may not).
static void write_read_erase(void) {
    CHECK(serval_save_device == device());
    u32 last = save_slot_count() - 1;
    save_erase(0);
    save_erase(last);
    u8 data[100], loaded[100];
    u32 size = save_slot_capacity() < sizeof data ? save_slot_capacity() : sizeof data;
    for (u32 i = 0; i < size; i++)
        data[i] = (u8)(i * 37u + 1u);
    CHECK(save_read(0, loaded, size, 1) == SAVE_EMPTY);
    CHECK(save_write(0, data, size, 1));
    CHECK(has_magic(0) && !has_magic(1));
    // The bytes really are in the memory, where the layout puts them.
    CHECK(byte_at(SAVE_HEADER_SIZE) == data[0] &&
          byte_at(SAVE_HEADER_SIZE + size - 1) == data[size - 1]);
    if (device()->block_size == 1 && !device()->erase) // SRAM: one byte per address
        CHECK(SRAM[SAVE_HEADER_SIZE] == data[0] &&
              SRAM[SAVE_HEADER_SIZE + size - 1] == data[size - 1]);
    for (u32 i = 0; i < size; i++)
        loaded[i] = 0;
    CHECK(save_read(0, loaded, size, 1) == SAVE_OK);
    bool same = true;
    for (u32 i = 0; i < size; i++)
        same &= loaded[i] == data[i];
    CHECK(same);

    // The second write goes to copy B; the third back to A.
    data[0] = 0xEE;
    CHECK(save_write(0, data, size, 1));
    CHECK(has_magic(0) && has_magic(1));
    CHECK(byte_at(copy_offset(1) + SAVE_HEADER_SIZE) == 0xEE);
    CHECK(save_read(0, loaded, size, 1) == SAVE_OK && loaded[0] == 0xEE);
    data[0] = 0x11;
    CHECK(save_write(0, data, size, 1));
    CHECK(byte_at(SAVE_HEADER_SIZE) == 0x11);
    CHECK(save_read(0, loaded, size, 1) == SAVE_OK && loaded[0] == 0x11);

    // The last slot ends at the end of the layout.
    CHECK(save_write(last, data, size, 2));
    CHECK(save_slot_version(last) == 2 && save_slot_size(last) == size);
    CHECK(has_magic(2 * last) || has_magic(2 * last + 1));

    save_erase(0);
    CHECK(!has_magic(0) && !has_magic(1));
    CHECK(save_read(0, loaded, size, 1) == SAVE_EMPTY);
    CHECK(save_slot_size(last) == size);
    save_erase(last);
    CHECK(save_slot_size(last) == 0);
}

// Every slot full, both copies, then all read back: slots never overlap in
// the real memory (banks, address bits, sectors).
static void full_slots_stay_separate(void) {
    static SERVAL_EWRAM_BSS u8 data[SAVE_SLOT_MAX];
    u32 capacity = save_slot_capacity(), slots = save_slot_count();
    for (u32 round = 0; round < 2; round++)
        for (u32 slot = 0; slot < slots; slot++) {
            for (u32 i = 0; i < capacity; i++)
                data[i] = (u8)(i * 13u + slot * 71u + round * 5u);
            CHECK(save_write(slot, data, capacity, (u16)(10 + slot)));
        }
    for (u32 slot = 0; slot < slots; slot++) {
        CHECK(save_read(slot, data, capacity, (u16)(10 + slot)) == SAVE_OK);
        bool ok = true;
        for (u32 i = 0; i < capacity; i++)
            ok &= data[i] == (u8)(i * 13u + slot * 71u + 5u);
        CHECK(ok);
    }
    for (u32 slot = 0; slot < slots; slot++)
        save_erase(slot);
    CHECK(save_slot_size(0) == 0 && save_slot_size(slots - 1) == 0);
}

// What saves cost for a full slot and a small one (a high score table, up to
// the capacity). Logged, for docs/runtime-systems.md#save-data.
// Every write's data differs from what the memory holds (EEPROM skips blocks
// that already hold what is written).
static void save_cost(u32 size, u32 seed, u32* write_empty, u32* write, u32* read, u32* erase) {
    static SERVAL_EWRAM_BSS u8 data[SAVE_SLOT_MAX];
    for (u32 i = 0; i < size; i++)
        data[i] = (u8)(i * 3u + seed);
    save_erase(1);
    u32 t0 = cycles();
    CHECK(save_write(1, data, size, 1)); // empty slot
    u32 t1 = cycles();
    for (u32 i = 0; i < size; i++)
        data[i] ^= 0x5A;
    u32 t1b = cycles();
    CHECK(save_write(1, data, size, 1)); // a slot holding a save
    u32 t2 = cycles();
    CHECK(save_read(1, data, size, 1) == SAVE_OK);
    u32 t3 = cycles();
    save_erase(1);
    u32 t4 = cycles();
    *write_empty = t1 - t0;
    *write = t2 - t1b;
    *read = t3 - t2;
    *erase = t4 - t3;
}

static void save_costs(void) {
    u32 sizes[] = {save_slot_capacity(), 100};
    if (sizes[1] > sizes[0])
        sizes[1] = sizes[0];
    for (u32 i = 0; i < 2; i++) {
        u32 write_empty, write, read, erase;
        save_cost(sizes[i], 17u * i + 1u, &write_empty, &write, &read, &erase);
        test_output(text_format("  save (%s): %u bytes: write %u cycles (empty slot %u), read %u, "
                                "erase %u",
                                device()->name, sizes[i], write, write_empty, read, erase));
#ifdef __OPTIMIZE__
        if (device()->block_size == 1 && !device()->erase) // SRAM
            CHECK(write < 280896u * 2); // under two frames (Debug builds are unoptimized)
#endif
    }
}

TEST_SUITE(gba_save_tests, "gba save", {"ROM contains one save ID", rom_contains_one_save_id},
           {"write, read and erase", write_read_erase},
           {"full slots stay separate", full_slots_stay_separate}, {"save costs", save_costs}, );
