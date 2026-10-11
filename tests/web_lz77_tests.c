// lz77: the web's stand-in for the BIOS's LZ77UnCompVram (src/web/lz77.c),
// natively, into arrays standing in for VRAM: BIOS-format vectors written by
// hand, with what the 16-bit decoder leaves where data isn't VRAM-safe or
// runs past its size (as mGBA's BIOS does: tests/rom/lz77_tests.c checks the
// same vector there), and tools/lz77.py's output for the tests' and the
// platformer's tiles (built by serval_add_lz77(), tests/CMakeLists.txt),
// which must unpack to the tiles and pass the loaders' check.

#include "../src/core/lz77_internal.h"
#include "../src/web/web.h"
#include "test.h"

#include "lz77_tiles.h" // padded_lz77, castle_tiles_lz77, dragon_tiles_lz77

#include <string.h>

static const u32 padded[] = {
#include "lz77/padded.inc"
};
static const u32 castle_tiles[] = {
#include "../examples/platformer/castle_tiles.inc"
};
static const u32 dragon_tiles[] = {
#include "../examples/platformer/dragon_tiles.inc"
};

// Unpacks `count` bytes of data (a header and items, written as bytes) into
// memory that holds `before`, and checks the result against `after`.
static bool unpacks(const u8* data, u32 count, const char* before, const char* after) {
    static union {
        u32 words[16];
        u8 bytes[64];
    } in;
    static u8 memory[64];
    memset(in.bytes, 0, sizeof(in.bytes));
    memcpy(in.bytes, data, count);
    u32 n = (u32)strlen(before);
    memcpy(memory, before, n);
    web_lz77_unpack_vram(in.bytes, memory);
    return memcmp(memory, after, n) == 0;
}

#define UNPACKS(before, after, ...)                                                                \
    (__extension__({                                                                               \
        static const u8 v_[] = {__VA_ARGS__};                                                      \
        unpacks(v_, sizeof(v_), before, after);                                                    \
    }))

#define HEADER(size) 0x10, (size), 0, 0

static void unpacks_the_format(void) {
    // Literals, and 6 bytes from 2 back.
    CHECK(UNPACKS("........", "abababab", HEADER(8), 0x20, 'a', 'b', 0x30, 0x01));
    // Two blocks: the second's flag byte after eight items.
    CHECK(UNPACKS("..........", "12345678..", HEADER(8), 0x00, '1', '2', '3', '4', '5', '6', '7',
                  '8', 0x00, '9'));
    CHECK(UNPACKS("..........", "1234567890", HEADER(10), 0x00, '1', '2', '3', '4', '5', '6', '7',
                  '8', 0x00, '9', '0'));
    // The longest match (18), a distance of 4, then 3 from 3 back; the 25th
    // byte, at an even offset, waits for a 26th and is never written.
    CHECK(UNPACKS("..........................", "abcdabcdabcdabcdabcdabda..", HEADER(25), 0x0C, 'a',
                  'b', 'c', 'd', 0xF0, 0x03, 0x00, 0x02));
    // The header's low byte isn't checked; nor are flag bits past the size.
    CHECK(UNPACKS("....", "xy..", 0x00, 2, 0, 0, 0x3F, 'x', 'y'));
    CHECK(UNPACKS("....", "....", HEADER(0), 0xFF));
}

// The 16-bit decoder writes a halfword at a time: a byte at an even address
// waits for the next.
static void writes_halfwords_as_the_bios_does(void) {
    // A last byte at an even address is never written.
    CHECK(UNPACKS("..........", "12345678..", HEADER(9), 0x00, '1', '2', '3', '4', '5', '6', '7',
                  '8', 0x00, '9'));
    // 'a', then 3 from 1 back, over "0123": byte 1 copies the old '0' (byte
    // 0 waits), byte 2 byte 1 (written), byte 3 the old '2' (byte 2 waits).
    // A plain decoder would give "aaaa"; mGBA's BIOS gives this
    // (tests/rom/lz77_tests.c).
    CHECK(UNPACKS("0123", "a002", HEADER(4), 0x40, 'a', 0x00, 0x00));
    // A match running past the size is finished: its fifth byte waits,
    // unwritten, but a sixth would be written.
    CHECK(UNPACKS("......", "abab..", HEADER(4), 0x20, 'a', 'b', 0x00, 0x01));
    CHECK(UNPACKS("......", "ababab", HEADER(4), 0x20, 'a', 'b', 0x10, 0x01));
}

// tools/lz77.py's output: the loaders' check accepts it, and it unpacks to
// the tiles over memory full of garbage.
static void unpacks_the_encoders_tiles(void) {
    static const struct {
        const u32* packed;
        const u32* plain;
        u32 bytes;
    } cases[] = {
        {padded_lz77, padded, sizeof(padded)},
        {castle_tiles_lz77, castle_tiles, sizeof(castle_tiles)},
        {dragon_tiles_lz77, dragon_tiles, sizeof(dragon_tiles)},
    };
    static u8 vram[16 * 1024];
    for (u32 k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        u32 at = 1;
        CHECK(serval_lz77_check(cases[k].packed, cases[k].bytes, &at) == LZ77_OK && at == 0);
        CHECK(cases[k].bytes + 2 <= sizeof(vram));
        memset(vram, 0xEE, sizeof(vram));
        web_lz77_unpack_vram(cases[k].packed, vram);
        CHECK(memcmp(vram, cases[k].plain, cases[k].bytes) == 0);
        CHECK(vram[cases[k].bytes] == 0xEE && vram[cases[k].bytes + 1] == 0xEE); // no further
        // Smaller than the tiles: the point of it.
        CHECK(cases[k].packed[0] >> 8 == cases[k].bytes);
    }
    CHECK(sizeof(castle_tiles_lz77) * 3 < sizeof(castle_tiles));
    CHECK(sizeof(dragon_tiles_lz77) * 3 < sizeof(dragon_tiles));
}

TEST_SUITE(web_lz77_tests, "web_lz77", {"unpacks_the_format", unpacks_the_format},
           {"writes_halfwords_as_the_bios_does", writes_halfwords_as_the_bios_does},
           {"unpacks_the_encoders_tiles", unpacks_the_encoders_tiles});
