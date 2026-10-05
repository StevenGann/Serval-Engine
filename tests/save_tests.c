// Save slots (save.h) on simulated save memories, one per save type's layout:
// RAM (SRAM), Flash (erases sectors to 0xFF; programming can only clear bits)
// and EEPROM (whole 8-byte blocks only). Each can lose power after any step
// (a byte, a block, a quarter of a sector erase), ignore writes, or have its
// bits flipped, and flags any access the real memory wouldn't allow. Runs
// natively and in the test ROMs; tests/rom/save_tests.c covers the real
// memories in mGBA.

#include "serval/debug.h"
#include "serval/save.h"
#include "test.h"

#include "../src/core/save_internal.h"

#include <stddef.h>

enum { KIND_RAM, KIND_FLASH, KIND_EEPROM };

static SERVAL_EWRAM_BSS u8 memory[SAVE_SIZE_FLASH128K];
static u32 kind;
static u32 power; // steps the memory still takes; after that, power is lost
static bool ignore_writes;
static bool misuse; // an access the real memory wouldn't allow
static u32 steps;   // steps taken (bytes, blocks, erase quarters)
static u32 flushes;
static u32 erases; // sector erases

static const SaveDevice* sim(void) {
    return serval_save_device;
}

static bool in_range(u32 offset, u32 count) {
    if (offset > sim()->size || count > sim()->size - offset) {
        misuse = true;
        return false;
    }
    return true;
}

static void test_read(u32 offset, u8* dst, u32 count) {
    if (!in_range(offset, count))
        return;
    for (u32 i = 0; i < count; i++)
        dst[i] = memory[offset + i];
}

static bool test_write(u32 offset, const u8* src, u32 count) {
    if (!in_range(offset, count))
        return true;
    u32 unit = sim()->block_size;
    if (offset % unit || count % unit) {
        misuse = true; // EEPROM: whole blocks only
        return true;
    }
    for (u32 at = 0; at < count; at += unit) {
        if (ignore_writes || !power)
            continue;
        power--;
        steps++;
        for (u32 i = at; i < at + unit; i++) {
            u8* p = memory + offset + i;
            if (kind == KIND_FLASH && (*p & src[i]) != src[i])
                misuse = true; // programming can't set bits: erase first
            *p = kind == KIND_FLASH ? (u8)(*p & src[i]) : src[i];
        }
    }
    return true;
}

// Flash: erases whole sectors, a quarter at a time (a power loss can leave a
// sector partly erased). Each erase must stay within one copy.
static bool test_erase(u32 offset, u32 count) {
    u32 sector = sim()->erase_size, copy = sim()->copy_size;
    u32 end = (offset + count + sector - 1) / sector * sector;
    if (kind != KIND_FLASH || offset % sector || !count || !in_range(offset, end - offset) ||
        offset / copy != (end - 1) / copy) {
        misuse = true;
        return true;
    }
    for (u32 at = offset; at < end; at += sector / 4) {
        if (ignore_writes || !power)
            continue;
        power--;
        steps++;
        for (u32 i = at; i < at + sector / 4; i++)
            memory[i] = 0xFF;
    }
    erases += (end - offset) / sector;
    return true;
}

static void test_flush(void) {
    flushes++;
}

#define SIM(erase_fn, layout)                                                                      \
    {.read = test_read, .write = test_write, .erase = erase_fn, .flush = test_flush, layout}

static const SaveDevice layouts[] = {
    SIM(NULL, SAVE_LAYOUT_SRAM),
    SIM(test_erase, SAVE_LAYOUT_FLASH64K),
    SIM(test_erase, SAVE_LAYOUT_FLASH128K),
    SIM(NULL, SAVE_LAYOUT_EEPROM8K),
    SIM(NULL, SAVE_LAYOUT_EEPROM512),
};
static const u32 layout_kinds[] = {KIND_RAM, KIND_FLASH, KIND_FLASH, KIND_EEPROM, KIND_EEPROM};
#define LAYOUTS (sizeof layouts / sizeof layouts[0])

static u32 layout_index;

// Points the save code at the current layout's memory, filled with `fill`.
static void begin(u8 fill) {
    serval_save_device = &layouts[layout_index];
    kind = layout_kinds[layout_index];
    for (u32 i = 0; i < sim()->size; i++)
        memory[i] = fill;
    power = 0xFFFFFFFFu;
    ignore_writes = false;
    misuse = false;
    steps = 0;
    flushes = 0;
    erases = 0;
}

static void end(void) {
    CHECK(!misuse);
    serval_save_device = &serval_platform_save_device;
}

// Runs a test on every layout, naming the layout of any failure.
static void on_every_layout(void (*test)(void)) {
    for (layout_index = 0; layout_index < LAYOUTS; layout_index++) {
        unsigned failed = test_failures();
        serval_save_device = &layouts[layout_index]; // for slot_n() before begin()
        test();
        if (test_failures() != failed)
            test_output(layouts[layout_index].name);
    }
    serval_save_device = &serval_platform_save_device;
}

#define ON_EVERY_LAYOUT(name)                                                                      \
    static void name(void) {                                                                       \
        on_every_layout(name##_on);                                                                \
    }

// Slot n, wrapped to the layout's slots (EEPROM512 has 2).
static u32 slot_n(u32 n) {
    return n % save_slot_count();
}

static u32 copy_offset(u32 slot, u32 copy) {
    return (slot * 2 + copy) * sim()->copy_size;
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

// Every layout fits its memory, and copies own whole sectors (Flash) and
// start on blocks (EEPROM).
static void layouts_fit_their_memory_on(void) {
    begin(0xFF);
    const SaveDevice* d = sim();
    CHECK(d->slots >= 1 && d->slots <= SAVE_SLOTS);
    CHECK((u32)d->slots * 2u * d->copy_size <= d->size);
    CHECK(d->copy_size % d->block_size == 0 && SAVE_HEADER_SIZE % d->block_size == 0);
    CHECK(d->block_size <= 8);
    if (d->erase_size)
        CHECK(d->copy_size % d->erase_size == 0);
    CHECK(save_slot_count() == d->slots);
    CHECK(save_slot_capacity() == (d->copy_size - SAVE_HEADER_SIZE < SAVE_SLOT_MAX
                                       ? d->copy_size - SAVE_HEADER_SIZE
                                       : SAVE_SLOT_MAX));
    end();
}
ON_EVERY_LAYOUT(layouts_fit_their_memory)

// The slots and capacities each save type gives games (save.h).
static void slot_counts_and_capacities(void) {
    static const u32 expected[LAYOUTS][2] = {{8, 2000}, {8, 2000}, {8, 2000}, {8, 496}, {2, 112}};
    for (layout_index = 0; layout_index < LAYOUTS; layout_index++) {
        begin(0xFF);
        CHECK(save_slot_count() == expected[layout_index][0]);
        CHECK(save_slot_capacity() == expected[layout_index][1]);
        end();
    }
    // The platform's own memory (SRAM's on the host and in the SRAM ROMs).
    CHECK(save_slot_count() >= 2 && save_slot_count() <= SAVE_SLOTS);
    CHECK(save_slot_capacity() >= 112 && save_slot_capacity() <= SAVE_SLOT_MAX);
}

// Never-written memory (0xFF), zeros or garbage: nothing saved, not corrupt.
static void blank_memory_reads_empty_on(void) {
    static const u8 fills[] = {0xFF, 0x00, 0x5A};
    for (u32 f = 0; f < sizeof fills; f++) {
        begin(fills[f]);
        if (fills[f] == 0x5A) // garbage: a different byte everywhere
            for (u32 i = 0; i < sim()->size; i++)
                memory[i] = (u8)(i * 151u + (i >> 7) * 13u);
        for (u32 slot = 0; slot < save_slot_count(); slot++) {
            TestSave s = make_save(1), before = s;
            CHECK(save_read(slot, &s, sizeof s, 1) == SAVE_EMPTY);
            CHECK(same(&s, &before, sizeof s)); // untouched
            CHECK(save_slot_version(slot) == 0);
            CHECK(save_slot_size(slot) == 0);
        }
        CHECK(steps == 0); // reading never writes
        end();
    }
}
ON_EVERY_LAYOUT(blank_memory_reads_empty)

static void write_then_read_on(void) {
    begin(0xFF);
    TestSave saved = make_save(3), loaded = make_save(0);
    u32 slot = slot_n(2);
    CHECK(save_write(slot, &saved, sizeof saved, 5));
    CHECK(flushes == 1);
    CHECK(save_read(slot, &loaded, sizeof loaded, 5) == SAVE_OK);
    CHECK(same(&saved, &loaded, sizeof saved));
    CHECK(save_slot_version(slot) == 5);
    CHECK(save_slot_size(slot) == sizeof saved);
    for (u32 other = 0; other < save_slot_count(); other++)
        if (other != slot)
            CHECK(save_read(other, &loaded, sizeof loaded, 5) == SAVE_EMPTY);
    end();
}
ON_EVERY_LAYOUT(write_then_read)

// Each write goes to the copy that doesn't hold the save, alternating. On
// Flash, each write erases one sector: the target copy's first.
static void writes_alternate_between_copies_on(void) {
    begin(0xFF);
    TestSave s;
    u32 slot = slot_n(6);
    for (u32 n = 1; n <= 4; n++) {
        s = make_save(n);
        u32 erased = erases;
        CHECK(save_write(slot, &s, sizeof s, 1));
        CHECK(erases == erased + (kind == KIND_FLASH ? 1 : 0));
        u32 copy = (n - 1) % 2;
        CHECK(copy_has_magic(slot, copy));
        CHECK(copy_has_magic(slot, copy ^ 1) == (n > 1));
        TestSave loaded;
        CHECK(save_read(slot, &loaded, sizeof loaded, 1) == SAVE_OK);
        CHECK(same(&s, &loaded, sizeof s)); // always the latest
    }
    end();
}
ON_EVERY_LAYOUT(writes_alternate_between_copies)

static void other_version_or_size_is_reported_on(void) {
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
ON_EVERY_LAYOUT(other_version_or_size_is_reported)

// A damaged save with no intact copy to fall back to reads as corrupt.
static void bit_flips_read_as_corrupt_on(void) {
    // Offsets into the copy: data, sequence number, version, size, CRC.
    static const u32 flips[] = {SAVE_HEADER_SIZE, SAVE_HEADER_SIZE + 13, 4, 7, 8, 10, 11, 12, 15};
    u32 slot = slot_n(4);
    for (u32 f = 0; f < sizeof flips / sizeof flips[0]; f++) {
        begin(0xFF);
        TestSave s = make_save(5), loaded = make_save(1), before = loaded;
        CHECK(save_write(slot, &s, sizeof s, 3));
        memory[copy_offset(slot, 0) + flips[f]] ^= 0x10;
        CHECK(save_read(slot, &loaded, sizeof loaded, 3) == SAVE_CORRUPT);
        CHECK(same(&loaded, &before, sizeof loaded));
        CHECK(save_slot_version(slot) == 0);
        CHECK(save_slot_size(slot) == 0);
        // Writing again replaces it.
        CHECK(save_write(slot, &s, sizeof s, 3));
        CHECK(save_read(slot, &loaded, sizeof loaded, 3) == SAVE_OK);
        end();
    }
    // Without the magic, a copy is just empty.
    begin(0xFF);
    TestSave s = make_save(5);
    CHECK(save_write(slot, &s, sizeof s, 3));
    memory[copy_offset(slot, 0) + 1] ^= 0x01;
    CHECK(save_read(slot, &s, sizeof s, 3) == SAVE_EMPTY);
    end();
}
ON_EVERY_LAYOUT(bit_flips_read_as_corrupt)

// If the newest copy is damaged, the slot falls back to the other one.
static void damaged_newest_copy_falls_back_on(void) {
    begin(0xFF);
    TestSave first = make_save(1), second = make_save(2), loaded;
    u32 slot = slot_n(7);
    CHECK(save_write(slot, &first, sizeof first, 1));
    CHECK(save_write(slot, &second, sizeof second, 1));
    memory[copy_offset(slot, 1) + SAVE_HEADER_SIZE + 2] ^= 0x80;
    CHECK(save_read(slot, &loaded, sizeof loaded, 1) == SAVE_OK);
    CHECK(same(&loaded, &first, sizeof first));
    // The next write replaces the damaged copy, keeping the intact one.
    TestSave third = make_save(3);
    CHECK(save_write(slot, &third, sizeof third, 1));
    CHECK(save_read(slot, &loaded, sizeof loaded, 1) == SAVE_OK);
    CHECK(same(&loaded, &third, sizeof third));
    memory[copy_offset(slot, 1) + SAVE_HEADER_SIZE] ^= 0x01;
    CHECK(save_read(slot, &loaded, sizeof loaded, 1) == SAVE_OK);
    CHECK(same(&loaded, &first, sizeof first));
    end();
}
ON_EVERY_LAYOUT(damaged_newest_copy_falls_back)

// Interrupts a write after every possible number of steps, from an empty
// slot, a slot with one copy and a slot with both. Afterwards the slot holds
// either its old save or, only if the write completed, the new one.
static void power_loss_keeps_the_old_save_on(void) {
    static SERVAL_EWRAM_BSS u8 before[2 * 8192];
    u32 slot = slot_n(3);
    u32 start = copy_offset(slot, 0), length = 2 * sim()->copy_size;
    for (u32 saves = 0; saves <= 2; saves++) {
        TestSave old = make_save(10 + saves), next = make_save(20), loaded;
        // Measure the full write, from the same starting state.
        begin(0xFF);
        for (u32 n = 0; n < saves; n++)
            CHECK(save_write(slot, &old, sizeof old, 1));
        for (u32 i = 0; i < length; i++)
            before[i] = memory[start + i];
        steps = 0;
        CHECK(save_write(slot, &next, sizeof next, 1));
        u32 total = steps;
        CHECK(total >= (sizeof next + SAVE_HEADER_SIZE) / sim()->block_size);
        end();

        for (u32 cut = 0; cut <= total; cut++) {
            begin(0xFF);
            for (u32 i = 0; i < length; i++)
                memory[start + i] = before[i];
            power = cut;
            bool ok = save_write(slot, &next, sizeof next, 1);
            CHECK(ok == (cut == total));
            power = 0xFFFFFFFFu; // power back on
            int result = save_read(slot, &loaded, sizeof loaded, 1);
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
ON_EVERY_LAYOUT(power_loss_keeps_the_old_save)

// The same for erasing: the slot reads as before or as empty.
static void power_loss_during_erase_on(void) {
    TestSave first = make_save(1), second = make_save(2), loaded;
    u32 slot = slot_n(5);
    // Steps to clear one copy's magic: 4 bytes, or one EEPROM block.
    begin(0xFF);
    u32 per_copy = kind == KIND_EEPROM ? 1 : 4;
    end();
    for (u32 cut = 0; cut <= 2 * per_copy; cut++) {
        begin(0xFF);
        CHECK(save_write(slot, &first, sizeof first, 1));
        CHECK(save_write(slot, &second, sizeof second, 1));
        power = cut;
        steps = 0;
        save_erase(slot);
        power = 0xFFFFFFFFu;
        int result = save_read(slot, &loaded, sizeof loaded, 1);
        // The older copy is cleared first; once the newer copy's magic is
        // touched, the slot is empty.
        if (steps > per_copy)
            CHECK(result == SAVE_EMPTY);
        else
            CHECK(result == SAVE_OK && same(&loaded, &second, sizeof second));
        end();
    }
}
ON_EVERY_LAYOUT(power_loss_during_erase)

static void erase_empties_both_copies_on(void) {
    begin(0xFF);
    TestSave s = make_save(6), loaded;
    u32 slot = slot_n(1), other = slot_n(2);
    CHECK(save_write(slot, &s, sizeof s, 1));
    CHECK(save_write(slot, &s, sizeof s, 1));
    CHECK(save_write(other, &s, sizeof s, 1));
    u32 flushed = flushes, erased = erases;
    save_erase(slot);
    CHECK(flushes == flushed + 1);
    CHECK(erases == erased); // Flash: clearing the magic needs no sector erase
    CHECK(!copy_has_magic(slot, 0) && !copy_has_magic(slot, 1));
    CHECK(save_read(slot, &loaded, sizeof loaded, 1) == SAVE_EMPTY);
    CHECK(save_slot_size(slot) == 0);
    CHECK(save_read(other, &loaded, sizeof loaded, 1) == SAVE_OK); // other slots stay
    // Erasing an empty slot writes nothing.
    steps = 0;
    save_erase(slot);
    CHECK(steps == 0);
    // And the slot works again.
    CHECK(save_write(slot, &s, sizeof s, 1));
    CHECK(save_read(slot, &loaded, sizeof loaded, 1) == SAVE_OK);
    end();
}
ON_EVERY_LAYOUT(erase_empties_both_copies)

// Every slot holds save_slot_capacity() bytes without touching its
// neighbours, and everything fits the memory (the test memory flags accesses
// outside it).
static void full_slots_stay_separate_on(void) {
    static SERVAL_EWRAM_BSS u8 data[SAVE_SLOT_MAX];
    static SERVAL_EWRAM_BSS u8 loaded[SAVE_SLOT_MAX];
    begin(0xFF);
    u32 capacity = save_slot_capacity(), slots = save_slot_count();
    for (u32 round = 0; round < 2; round++) // both copies of every slot
        for (u32 slot = 0; slot < slots; slot++) {
            for (u32 i = 0; i < capacity; i++)
                data[i] = (u8)(i * 7u + slot * 31u + round);
            CHECK(save_write(slot, data, capacity, (u16)slot));
        }
    for (u32 slot = 0; slot < slots; slot++) {
        CHECK(save_read(slot, loaded, capacity, (u16)slot) == SAVE_OK);
        bool ok = true;
        for (u32 i = 0; i < capacity; i++)
            ok &= loaded[i] == (u8)(i * 7u + slot * 31u + 1u);
        CHECK(ok);
    }
    u32 middle = slots / 2;
    save_erase(middle);
    CHECK(save_read(middle - 1, loaded, capacity, (u16)(middle - 1)) == SAVE_OK);
    CHECK(save_read(middle, loaded, capacity, (u16)middle) == SAVE_EMPTY);
    if (middle + 1 < slots)
        CHECK(save_read(middle + 1, loaded, capacity, (u16)(middle + 1)) == SAVE_OK);
    end();
}
ON_EVERY_LAYOUT(full_slots_stay_separate)

// Sequence numbers are compared modulo 2^32, so wrapping keeps the order.
static void sequence_number_wraps_on(void) {
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
ON_EVERY_LAYOUT(sequence_number_wraps)

// Memory that doesn't store what is written (no save memory of this type):
// the write fails. Runs before the power-loss tests, whose failed writes give
// the same warning (once, on the first layout only).
static void write_that_does_not_stick_fails_on(void) {
    begin(0xFF);
    TestSave s = make_save(8), loaded;
    CHECK(save_write(0, &s, sizeof s, 1));
    ignore_writes = true;
    u32 warnings = debug_warning_count();
    TestSave other = make_save(9);
    CHECK(!save_write(0, &other, sizeof other, 1));
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + (layout_index == 0 ? 1 : 0));
#else
    CHECK(debug_warning_count() == warnings);
#endif
    ignore_writes = false;
    CHECK(save_read(0, &loaded, sizeof loaded, 1) == SAVE_OK);
    CHECK(same(&loaded, &s, sizeof s)); // the old save is kept
    end();
}
ON_EVERY_LAYOUT(write_that_does_not_stick_fails)

// Bad slots and sizes are the layout's: on EEPROM512, slot 2 and 113 bytes
// are already out of range. Each problem warns once (on the first layout).
static void misuse_is_reported_and_harmless_on(void) {
    begin(0xFF);
    TestSave s = make_save(2), loaded = make_save(3), before = loaded;
    static SERVAL_EWRAM_BSS u8 big[SAVE_SLOT_MAX + 1];
    u32 slots = save_slot_count(), capacity = save_slot_capacity();
    CHECK(save_write(0, &s, sizeof s, 1));
    u32 warnings = debug_warning_count();
    steps = 0;
    CHECK(!save_write(slots, &s, sizeof s, 1));
    CHECK(!save_write(slots + 5, &s, sizeof s, 1)); // the same problem: no new warning
    CHECK(!save_write(0, &s, 0, 1));
    CHECK(!save_write(0, big, capacity + 1, 1));
    CHECK(!save_write(0, NULL, sizeof s, 1));
    CHECK(steps == 0);
    CHECK(save_read(slots, &loaded, sizeof loaded, 1) == SAVE_EMPTY);
    CHECK(save_read(0, NULL, sizeof loaded, 1) == SAVE_EMPTY);
    CHECK(save_read(0, &loaded, capacity + 1, 1) == SAVE_OTHER_VERSION);
    CHECK(same(&loaded, &before, sizeof loaded));
    CHECK(save_slot_version(slots) == 0);
    CHECK(save_slot_size(slots) == 0);
    save_erase(slots);
    CHECK(steps == 0);
    CHECK(save_read(0, &loaded, sizeof loaded, 1) == SAVE_OK); // slot 0 untouched
#ifdef SERVAL_DEBUG
    // write: slot, size, data; read: slot, data, size; version, size, erase: slot.
    CHECK(debug_warning_count() == warnings + (layout_index == 0 ? 9 : 0));
#else
    CHECK(debug_warning_count() == warnings);
#endif
    end();
}
ON_EVERY_LAYOUT(misuse_is_reported_and_harmless)

TEST_SUITE(save_tests, "save", {"CRC-32 matches the standard", crc32_matches_the_standard},
           {"layouts fit their memory", layouts_fit_their_memory},
           {"slot counts and capacities", slot_counts_and_capacities},
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
