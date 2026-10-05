// Save slots (save.h) on a test save memory: an array that can lose power
// partway through a write, ignore writes, or have its bits flipped. Runs
// natively and in the test ROM; tests/rom/save_tests.c covers the real SRAM.

#include "serval/debug.h"
#include "serval/save.h"
#include "test.h"

#include "../src/core/save_internal.h"

#include <stddef.h>

static SERVAL_EWRAM_BSS u8 memory[SAVE_MEMORY_SIZE];
static u32 write_budget; // bytes the memory still stores; after that, power is lost
static bool ignore_writes;
static bool out_of_range;
static u32 bytes_written;
static u32 flushes;

static void test_read(u32 offset, u8* dst, u32 count) {
    if (offset > SAVE_MEMORY_SIZE || count > SAVE_MEMORY_SIZE - offset) {
        out_of_range = true;
        return;
    }
    for (u32 i = 0; i < count; i++)
        dst[i] = memory[offset + i];
}

static void test_write(u32 offset, const u8* src, u32 count) {
    if (offset > SAVE_MEMORY_SIZE || count > SAVE_MEMORY_SIZE - offset) {
        out_of_range = true;
        return;
    }
    for (u32 i = 0; i < count; i++) {
        if (ignore_writes || !write_budget)
            continue;
        write_budget--;
        bytes_written++;
        memory[offset + i] = src[i];
    }
}

static void test_flush(void) {
    flushes++;
}

static const SaveDevice test_device = {test_read, test_write, test_flush};

// Points the save code at the test memory, filled with `fill`.
static void begin(u8 fill) {
    for (u32 i = 0; i < SAVE_MEMORY_SIZE; i++)
        memory[i] = fill;
    write_budget = 0xFFFFFFFFu;
    ignore_writes = false;
    out_of_range = false;
    bytes_written = 0;
    flushes = 0;
    serval_save_device = &test_device;
}

static void end(void) {
    CHECK(!out_of_range);
    serval_save_device = &serval_platform_save_device;
}

static u32 copy_offset(u32 slot, u32 copy) {
    return (slot * 2 + copy) * SAVE_COPY_SIZE;
}

static bool copy_has_magic(u32 slot, u32 copy) {
    const u8* p = memory + copy_offset(slot, copy);
    return p[0] == 'S' && p[1] == 'V' && p[2] == 'S' && p[3] == '1';
}

typedef struct {
    u32 score;
    u16 level;
    u8 name[10];
} TestSave;

static TestSave make_save(u32 seed) {
    TestSave s = {seed * 1000u + 7u, (u16)seed, {0}};
    for (u32 i = 0; i < sizeof s.name; i++)
        s.name[i] = (u8)('A' + (seed + i) % 26);
    return s;
}

static bool same(const void* a, const void* b, u32 size) {
    const u8 *x = a, *y = b;
    for (u32 i = 0; i < size; i++)
        if (x[i] != y[i])
            return false;
    return true;
}

static void crc32_matches_the_standard(void) {
    static const u8 check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(serval_save_crc32(0, check, sizeof check) == 0xCBF43926u); // CRC-32 check value
    // In pieces, continuing from the previous result.
    CHECK(serval_save_crc32(serval_save_crc32(0, check, 4), check + 4, 5) == 0xCBF43926u);
    CHECK(serval_save_crc32(0, check, 0) == 0);
}

// Never-written SRAM (0xFF), zeros or garbage: nothing saved, not corrupt.
static void blank_memory_reads_empty(void) {
    static const u8 fills[] = {0xFF, 0x00, 0x5A};
    for (u32 f = 0; f < sizeof fills; f++) {
        begin(fills[f]);
        if (fills[f] == 0x5A) // garbage: a different byte everywhere
            for (u32 i = 0; i < SAVE_MEMORY_SIZE; i++)
                memory[i] = (u8)(i * 151u + (i >> 7) * 13u);
        for (u32 slot = 0; slot < SAVE_SLOTS; slot++) {
            TestSave s = make_save(1), before = s;
            CHECK(save_read(slot, &s, sizeof s, 1) == SAVE_EMPTY);
            CHECK(same(&s, &before, sizeof s)); // untouched
            CHECK(save_slot_version(slot) == 0);
            CHECK(save_slot_size(slot) == 0);
        }
        CHECK(bytes_written == 0); // reading never writes
        end();
    }
}

static void write_then_read(void) {
    begin(0xFF);
    TestSave saved = make_save(3), loaded = make_save(0);
    CHECK(save_write(2, &saved, sizeof saved, 5));
    CHECK(flushes == 1);
    CHECK(save_read(2, &loaded, sizeof loaded, 5) == SAVE_OK);
    CHECK(same(&saved, &loaded, sizeof saved));
    CHECK(save_slot_version(2) == 5);
    CHECK(save_slot_size(2) == sizeof saved);
    CHECK(save_read(1, &loaded, sizeof loaded, 5) == SAVE_EMPTY); // other slots
    CHECK(save_read(3, &loaded, sizeof loaded, 5) == SAVE_EMPTY);
    end();
}

// Each write goes to the copy that doesn't hold the save, alternating.
static void writes_alternate_between_copies(void) {
    begin(0xFF);
    TestSave s;
    for (u32 n = 1; n <= 4; n++) {
        s = make_save(n);
        CHECK(save_write(6, &s, sizeof s, 1));
        u32 copy = (n - 1) % 2;
        CHECK(copy_has_magic(6, copy));
        CHECK(copy_has_magic(6, copy ^ 1) == (n > 1));
        TestSave loaded;
        CHECK(save_read(6, &loaded, sizeof loaded, 1) == SAVE_OK);
        CHECK(same(&s, &loaded, sizeof s)); // always the latest
    }
    end();
}

static void other_version_or_size_is_reported(void) {
    begin(0xFF);
    TestSave s = make_save(4);
    CHECK(save_write(0, &s, sizeof s, 1));
    TestSave loaded = make_save(9), before = loaded;
    CHECK(save_read(0, &loaded, sizeof loaded, 2) == SAVE_OTHER_VERSION);
    CHECK(save_read(0, &loaded, sizeof loaded - 1, 1) == SAVE_OTHER_VERSION);
    CHECK(same(&loaded, &before, sizeof loaded)); // untouched
    CHECK(save_slot_version(0) == 1);
    CHECK(save_slot_size(0) == sizeof s);
    // The game reads it with the old version and size, then converts it.
    CHECK(save_read(0, &loaded, sizeof loaded, 1) == SAVE_OK);
    u8 bigger[40] = {0};
    CHECK(save_write(0, bigger, sizeof bigger, 2));
    CHECK(save_slot_version(0) == 2);
    CHECK(save_slot_size(0) == sizeof bigger);
    CHECK(save_read(0, &loaded, sizeof loaded, 1) == SAVE_OTHER_VERSION);
    // Version 0 is a version like any other.
    CHECK(save_write(1, &s, sizeof s, 0));
    CHECK(save_read(1, &loaded, sizeof loaded, 0) == SAVE_OK);
    end();
}

// A damaged save with no intact copy to fall back to reads as corrupt.
static void bit_flips_read_as_corrupt(void) {
    // Offsets into the copy: data, sequence number, version, size, CRC.
    static const u32 flips[] = {SAVE_HEADER_SIZE, SAVE_HEADER_SIZE + 13, 4, 7, 8, 10, 11, 12, 15};
    for (u32 f = 0; f < sizeof flips / sizeof flips[0]; f++) {
        begin(0xFF);
        TestSave s = make_save(5), loaded = make_save(1), before = loaded;
        CHECK(save_write(4, &s, sizeof s, 3));
        memory[copy_offset(4, 0) + flips[f]] ^= 0x10;
        CHECK(save_read(4, &loaded, sizeof loaded, 3) == SAVE_CORRUPT);
        CHECK(same(&loaded, &before, sizeof loaded));
        CHECK(save_slot_version(4) == 0);
        CHECK(save_slot_size(4) == 0);
        // Writing again replaces it.
        CHECK(save_write(4, &s, sizeof s, 3));
        CHECK(save_read(4, &loaded, sizeof loaded, 3) == SAVE_OK);
        end();
    }
    // Without the magic, a copy is just empty.
    begin(0xFF);
    TestSave s = make_save(5);
    CHECK(save_write(4, &s, sizeof s, 3));
    memory[copy_offset(4, 0) + 1] ^= 0x01;
    CHECK(save_read(4, &s, sizeof s, 3) == SAVE_EMPTY);
    end();
}

// If the newest copy is damaged, the slot falls back to the other one.
static void damaged_newest_copy_falls_back(void) {
    begin(0xFF);
    TestSave first = make_save(1), second = make_save(2), loaded;
    CHECK(save_write(7, &first, sizeof first, 1));
    CHECK(save_write(7, &second, sizeof second, 1));
    memory[copy_offset(7, 1) + SAVE_HEADER_SIZE + 2] ^= 0x80;
    CHECK(save_read(7, &loaded, sizeof loaded, 1) == SAVE_OK);
    CHECK(same(&loaded, &first, sizeof first));
    // The next write replaces the damaged copy, keeping the intact one.
    TestSave third = make_save(3);
    CHECK(save_write(7, &third, sizeof third, 1));
    CHECK(save_read(7, &loaded, sizeof loaded, 1) == SAVE_OK);
    CHECK(same(&loaded, &third, sizeof third));
    memory[copy_offset(7, 1) + SAVE_HEADER_SIZE] ^= 0x01;
    CHECK(save_read(7, &loaded, sizeof loaded, 1) == SAVE_OK);
    CHECK(same(&loaded, &first, sizeof first));
    end();
}

// Interrupts a write after every possible number of bytes, from an empty
// slot, a slot with one copy and a slot with both. Afterwards the slot holds
// either its old save or, only if the write completed, the new one.
static void power_loss_keeps_the_old_save(void) {
    static SERVAL_EWRAM_BSS u8 before[SAVE_MEMORY_SIZE];
    for (u32 saves = 0; saves <= 2; saves++) {
        TestSave old = make_save(10 + saves), next = make_save(20), loaded;
        // Measure the full write, from the same starting state.
        begin(0xFF);
        for (u32 n = 0; n < saves; n++)
            CHECK(save_write(3, &old, sizeof old, 1));
        for (u32 i = 0; i < SAVE_MEMORY_SIZE; i++)
            before[i] = memory[i];
        bytes_written = 0;
        CHECK(save_write(3, &next, sizeof next, 1));
        u32 total = bytes_written;
        CHECK(total >= sizeof next + SAVE_HEADER_SIZE);
        end();

        for (u32 cut = 0; cut <= total; cut++) {
            begin(0xFF);
            for (u32 i = 0; i < SAVE_MEMORY_SIZE; i++)
                memory[i] = before[i];
            write_budget = cut;
            bool ok = save_write(3, &next, sizeof next, 1);
            CHECK(ok == (cut == total));
            write_budget = 0xFFFFFFFFu; // power back on
            int result = save_read(3, &loaded, sizeof loaded, 1);
            if (cut == total) {
                CHECK(result == SAVE_OK && same(&loaded, &next, sizeof next));
            } else if (saves == 0) {
                CHECK(result == SAVE_EMPTY);
            } else {
                CHECK(result == SAVE_OK && same(&loaded, &old, sizeof old));
            }
            end();
        }
    }
}

// The same for erasing: the slot reads as before or as empty.
static void power_loss_during_erase(void) {
    TestSave first = make_save(1), second = make_save(2), loaded;
    for (u32 cut = 0; cut <= 8; cut++) {
        begin(0xFF);
        CHECK(save_write(5, &first, sizeof first, 1));
        CHECK(save_write(5, &second, sizeof second, 1));
        write_budget = cut;
        bytes_written = 0;
        save_erase(5);
        write_budget = 0xFFFFFFFFu;
        int result = save_read(5, &loaded, sizeof loaded, 1);
        // The older copy is cleared first (4 bytes of magic); once the newer
        // copy's magic is touched, the slot is empty.
        if (bytes_written > 4)
            CHECK(result == SAVE_EMPTY);
        else
            CHECK(result == SAVE_OK && same(&loaded, &second, sizeof second));
        end();
    }
}

static void erase_empties_both_copies(void) {
    begin(0xFF);
    TestSave s = make_save(6), loaded;
    CHECK(save_write(1, &s, sizeof s, 1));
    CHECK(save_write(1, &s, sizeof s, 1));
    CHECK(save_write(2, &s, sizeof s, 1));
    u32 flushed = flushes;
    save_erase(1);
    CHECK(flushes == flushed + 1);
    CHECK(!copy_has_magic(1, 0) && !copy_has_magic(1, 1));
    CHECK(save_read(1, &loaded, sizeof loaded, 1) == SAVE_EMPTY);
    CHECK(save_slot_size(1) == 0);
    CHECK(save_read(2, &loaded, sizeof loaded, 1) == SAVE_OK); // other slots stay
    // Erasing an empty slot writes nothing.
    bytes_written = 0;
    save_erase(1);
    save_erase(0);
    CHECK(bytes_written == 0);
    // And the slot works again.
    CHECK(save_write(1, &s, sizeof s, 1));
    CHECK(save_read(1, &loaded, sizeof loaded, 1) == SAVE_OK);
    end();
}

// Every slot holds SAVE_SLOT_MAX bytes without touching its neighbours, and
// everything fits the 32 KiB (the test memory flags accesses outside it).
static void full_slots_stay_separate(void) {
    static SERVAL_EWRAM_BSS u8 data[SAVE_SLOT_MAX];
    static SERVAL_EWRAM_BSS u8 loaded[SAVE_SLOT_MAX];
    begin(0xFF);
    for (u32 round = 0; round < 2; round++) // both copies of every slot
        for (u32 slot = 0; slot < SAVE_SLOTS; slot++) {
            for (u32 i = 0; i < SAVE_SLOT_MAX; i++)
                data[i] = (u8)(i * 7u + slot * 31u + round);
            CHECK(save_write(slot, data, SAVE_SLOT_MAX, (u16)slot));
        }
    for (u32 slot = 0; slot < SAVE_SLOTS; slot++) {
        CHECK(save_read(slot, loaded, SAVE_SLOT_MAX, (u16)slot) == SAVE_OK);
        bool ok = true;
        for (u32 i = 0; i < SAVE_SLOT_MAX; i++)
            ok &= loaded[i] == (u8)(i * 7u + slot * 31u + 1u);
        CHECK(ok);
    }
    save_erase(3);
    CHECK(save_read(2, loaded, SAVE_SLOT_MAX, 2) == SAVE_OK);
    CHECK(save_read(3, loaded, SAVE_SLOT_MAX, 3) == SAVE_EMPTY);
    CHECK(save_read(4, loaded, SAVE_SLOT_MAX, 4) == SAVE_OK);
    end();
}

// Sequence numbers are compared modulo 2^32, so wrapping keeps the order.
static void sequence_number_wraps(void) {
    begin(0xFF);
    TestSave first = make_save(1), second = make_save(2), third = make_save(3), loaded;
    CHECK(save_write(0, &first, sizeof first, 1));
    // Rewrite copy A's sequence number to 0xFFFFFFFF, with a matching CRC.
    u8* header = memory + copy_offset(0, 0);
    header[4] = header[5] = header[6] = header[7] = 0xFF;
    u32 crc = serval_save_crc32(serval_save_crc32(0, header + 4, 8), header + SAVE_HEADER_SIZE,
                                sizeof first);
    for (u32 i = 0; i < 4; i++)
        header[12 + i] = (u8)(crc >> (8 * i));
    CHECK(save_read(0, &loaded, sizeof loaded, 1) == SAVE_OK);
    CHECK(save_write(0, &second, sizeof second, 1)); // copy B, sequence number 0
    CHECK(memory[copy_offset(0, 1) + 4] == 0);
    CHECK(save_read(0, &loaded, sizeof loaded, 1) == SAVE_OK);
    CHECK(same(&loaded, &second, sizeof second));
    CHECK(save_write(0, &third, sizeof third, 1)); // back to copy A
    CHECK(save_read(0, &loaded, sizeof loaded, 1) == SAVE_OK);
    CHECK(same(&loaded, &third, sizeof third));
    end();
}

// Memory that doesn't store what is written (no save RAM): the write fails.
// Runs before the power-loss tests, whose failed writes give the same
// warning (once).
static void write_that_does_not_stick_fails(void) {
    begin(0xFF);
    TestSave s = make_save(8), loaded;
    CHECK(save_write(0, &s, sizeof s, 1));
    ignore_writes = true;
    u32 warnings = debug_warning_count();
    TestSave other = make_save(9);
    CHECK(!save_write(0, &other, sizeof other, 1));
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1);
#else
    CHECK(debug_warning_count() == warnings);
#endif
    ignore_writes = false;
    CHECK(save_read(0, &loaded, sizeof loaded, 1) == SAVE_OK);
    CHECK(same(&loaded, &s, sizeof s)); // the old save is kept
    end();
}

static void misuse_is_reported_and_harmless(void) {
    begin(0xFF);
    TestSave s = make_save(2), loaded = make_save(3), before = loaded;
    CHECK(save_write(0, &s, sizeof s, 1));
    u32 warnings = debug_warning_count();
    bytes_written = 0;
    CHECK(!save_write(SAVE_SLOTS, &s, sizeof s, 1));
    CHECK(!save_write(SAVE_SLOTS + 5, &s, sizeof s, 1)); // the same problem: no new warning
    CHECK(!save_write(0, &s, 0, 1));
    CHECK(!save_write(0, &s, SAVE_SLOT_MAX + 1, 1));
    CHECK(!save_write(0, NULL, sizeof s, 1));
    CHECK(bytes_written == 0);
    CHECK(save_read(SAVE_SLOTS, &loaded, sizeof loaded, 1) == SAVE_EMPTY);
    CHECK(save_read(0, NULL, sizeof loaded, 1) == SAVE_EMPTY);
    CHECK(save_read(0, &loaded, SAVE_SLOT_MAX + 1, 1) == SAVE_OTHER_VERSION);
    CHECK(same(&loaded, &before, sizeof loaded));
    CHECK(save_slot_version(SAVE_SLOTS) == 0);
    CHECK(save_slot_size(SAVE_SLOTS) == 0);
    save_erase(SAVE_SLOTS);
    CHECK(bytes_written == 0);
    CHECK(save_read(0, &loaded, sizeof loaded, 1) == SAVE_OK); // slot 0 untouched
#ifdef SERVAL_DEBUG
    // write: slot, size, data; read: slot, data, size; version, size, erase: slot.
    CHECK(debug_warning_count() == warnings + 9);
#else
    CHECK(debug_warning_count() == warnings);
#endif
    end();
}

TEST_SUITE(save_tests, "save", {"CRC-32 matches the standard", crc32_matches_the_standard},
           {"blank memory reads as empty", blank_memory_reads_empty},
           {"write then read", write_then_read},
           {"writes alternate between copies", writes_alternate_between_copies},
           {"other version or size is reported", other_version_or_size_is_reported},
           {"bit flips read as corrupt", bit_flips_read_as_corrupt},
           {"damaged newest copy falls back", damaged_newest_copy_falls_back},
           {"write that doesn't stick fails", write_that_does_not_stick_fails},
           {"power loss keeps the old save", power_loss_keeps_the_old_save},
           {"power loss during erase", power_loss_during_erase},
           {"erase empties both copies", erase_empties_both_copies},
           {"full slots stay separate", full_slots_stay_separate},
           {"sequence number wraps", sequence_number_wraps},
           {"misuse is reported and harmless", misuse_is_reported_and_harmless}, );
