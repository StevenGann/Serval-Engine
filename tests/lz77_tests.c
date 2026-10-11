// lz77: the loaders' check of LZ77 data (src/core/lz77.c) before the BIOS
// unpacks it: what it accepts, and each problem it refuses, with where. Data
// written by hand, from the format (tools/lz77.py's docstring); the encoder's
// own output is checked by tests/web_lz77_tests.c (host) and, through the
// loaders, by tests/rom/lz77_tests.c. Run natively and in the test ROM.

#include "../src/core/lz77_internal.h"
#include "test.h"

// The vectors as bytes, copied to a word-aligned buffer (as the data must
// be: the BIOS reads its header as a word).
static union {
    u32 words[16];
    u8 bytes[64];
} buffer;

static const void* aligned(const u8* bytes, u32 count) {
    for (u32 k = 0; k < sizeof(buffer.bytes); k++)
        buffer.bytes[k] = k < count ? bytes[k] : 0;
    return buffer.bytes;
}

#define VECTOR(...)                                                                                \
    (__extension__({                                                                               \
        static const u8 v_[] = {__VA_ARGS__};                                                      \
        aligned(v_, sizeof(v_));                                                                   \
    }))

// Header bytes for an unpacked size below 256.
#define HEADER(size) 0x10, (size), 0, 0

static void accepts_what_unpacks_to_its_size(void) {
    u32 at = 99;
    // 'a', 'b', then 6 bytes from 2 back: "abababab".
    CHECK(serval_lz77_check(VECTOR(HEADER(8), 0x20, 'a', 'b', 0x30, 0x01), 8, &at) == LZ77_OK);
    CHECK(at == 0);
    // Nine literals: two blocks.
    CHECK(serval_lz77_check(
              VECTOR(HEADER(9), 0x00, '1', '2', '3', '4', '5', '6', '7', '8', 0x00, '9'), 9, &at) ==
          LZ77_OK);
    // The longest match, 18 bytes, ending at the size; a distance of 4
    // (overlapping) and of 3.
    CHECK(serval_lz77_check(VECTOR(HEADER(25), 0x0C, 'a', 'b', 'c', 'd', 0xF0, 0x03, 0x00, 0x02),
                            25, &at) == LZ77_OK);
    // Flag bits past the size are never read as items.
    CHECK(serval_lz77_check(VECTOR(HEADER(2), 0x3F, 'x', 'y'), 2, &at) == LZ77_OK);
    // Size 0: nothing to unpack.
    CHECK(serval_lz77_check(VECTOR(HEADER(0)), 0, &at) == LZ77_OK);
}

static void refuses_a_bad_header_or_size(void) {
    u32 at = 0;
    CHECK(serval_lz77_check(buffer.bytes + 2, 8, &at) == LZ77_MISALIGNED);
    CHECK(serval_lz77_check(VECTOR(0x11, 8, 0, 0, 0x00, 'a'), 8, &at) == LZ77_NOT_LZ77);
    CHECK(at == 0x811);
    // Huffman (0x2x), and LZ77 with its reserved low nibble set.
    CHECK(serval_lz77_check(VECTOR(0x24, 8, 0, 0), 8, &at) == LZ77_NOT_LZ77);
    CHECK(serval_lz77_check(VECTOR(0x18, 8, 0, 0), 8, &at) == LZ77_NOT_LZ77);
    CHECK(serval_lz77_check(VECTOR(HEADER(8), 0x20, 'a', 'b', 0x30, 0x01), 16, &at) ==
          LZ77_WRONG_SIZE);
    CHECK(at == 8);
    // The size is the header's three high bytes.
    static const u32 big[] = {0x00012010};
    CHECK(serval_lz77_check(big, 0x100, &at) == LZ77_WRONG_SIZE);
    CHECK(at == 0x120);
    static const u32 bigger[] = {0x01000010};
    CHECK(serval_lz77_check(bigger, 32, &at) == LZ77_WRONG_SIZE);
    CHECK(at == 0x10000);
}

static void refuses_matches_that_would_unpack_wrong(void) {
    u32 at = 0;
    // 'a', then 3 from 1 back: the 16-bit decoder hasn't written it yet.
    CHECK(serval_lz77_check(VECTOR(HEADER(4), 0x40, 'a', 0x00, 0x00), 4, &at) ==
          LZ77_NOT_VRAM_SAFE);
    CHECK(at == 1);
    // Later on, after a VRAM-safe match.
    CHECK(serval_lz77_check(VECTOR(HEADER(9), 0x30, 'a', 'b', 0x00, 0x01, 0x00, 0x00), 9, &at) ==
          LZ77_NOT_VRAM_SAFE);
    CHECK(at == 5);
    // A match before anything is unpacked, or reaching back past the start.
    CHECK(serval_lz77_check(VECTOR(HEADER(6), 0x80, 0x00, 0x01), 6, &at) == LZ77_BEFORE_START);
    CHECK(at == 0);
    CHECK(serval_lz77_check(VECTOR(HEADER(6), 0x20, 'a', 'b', 0x00, 0x02), 6, &at) ==
          LZ77_BEFORE_START);
    CHECK(at == 2);
    // 'a', 'b', then 3 from 2 back: 5 bytes into 4. The BIOS would write
    // past the end.
    CHECK(serval_lz77_check(VECTOR(HEADER(4), 0x20, 'a', 'b', 0x00, 0x01), 4, &at) ==
          LZ77_PAST_END);
    CHECK(at == 2);
}

#ifdef SERVAL_DEBUG
static bool contains(const char* text, const char* part) {
    for (; *text; text++) {
        u32 k = 0;
        while (part[k] && text[k] == part[k])
            k++;
        if (!part[k])
            return true;
    }
    return false;
}
#endif

// What the loaders' warnings say about each problem (debug builds).
static void names_each_problem(void) {
#ifdef SERVAL_DEBUG
    CHECK(contains(serval_lz77_problem(LZ77_MISALIGNED, 0, 0), "word-aligned"));
    CHECK(contains(serval_lz77_problem(LZ77_NOT_LZ77, 32, 0x2024), "starts 0x2024, not LZ77"));
    CHECK(contains(serval_lz77_problem(LZ77_WRONG_SIZE, 64, 32), "unpacks to 32 bytes"));
    CHECK(contains(serval_lz77_problem(LZ77_WRONG_SIZE, 64, 32), "not the 64"));
    CHECK(contains(serval_lz77_problem(LZ77_NOT_VRAM_SAFE, 64, 7),
                   "isn't VRAM-safe: the match at byte 7"));
    CHECK(contains(serval_lz77_problem(LZ77_BEFORE_START, 64, 0), "before the first byte"));
    CHECK(contains(serval_lz77_problem(LZ77_PAST_END, 64, 60), "past the end, byte 64"));
#endif
}

TEST_SUITE(lz77_tests, "lz77",
           {"accepts_what_unpacks_to_its_size", accepts_what_unpacks_to_its_size},
           {"refuses_a_bad_header_or_size", refuses_a_bad_header_or_size},
           {"refuses_matches_that_would_unpack_wrong", refuses_matches_that_would_unpack_wrong},
           {"names_each_problem", names_each_problem});
