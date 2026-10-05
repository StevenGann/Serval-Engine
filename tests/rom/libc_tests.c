// Tests for the engine's own memcpy/memset/memmove/memcmp (src/gba/libc.c).
// Calls go through volatile function pointers so GCC cannot inline builtins.

#include "../test.h"

#include <stddef.h>

void* memcpy(void* restrict dst, const void* restrict src, size_t n);
void* memset(void* dst, int c, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);

static void* (*volatile p_memcpy)(void* restrict, const void* restrict, size_t) = memcpy;
static void* (*volatile p_memset)(void*, int, size_t) = memset;
static void* (*volatile p_memmove)(void*, const void*, size_t) = memmove;
static int (*volatile p_memcmp)(const void*, const void*, size_t) = memcmp;
static size_t (*volatile p_strlen)(const char*) = strlen;

static void fill_sequence(unsigned char* buf, unsigned n) {
    for (unsigned i = 0; i < n; i++)
        buf[i] = (unsigned char)i;
}

static void memcpy_copies_unaligned(void) {
    unsigned char src[40], dst[40] = {0};
    fill_sequence(src, sizeof src);
    p_memcpy(dst + 1, src + 3, 33);
    for (unsigned i = 0; i < 33; i++)
        CHECK(dst[1 + i] == src[3 + i]);
    CHECK(dst[0] == 0 && dst[34] == 0);
}

static void memset_fills_bytes(void) {
    unsigned char buf[37] = {0};
    p_memset(buf + 1, 0xAB, 35);
    CHECK(buf[0] == 0 && buf[36] == 0);
    for (unsigned i = 1; i <= 35; i++)
        CHECK(buf[i] == 0xAB);
}

static void memmove_handles_overlap(void) {
    unsigned char buf[32];
    fill_sequence(buf, sizeof buf);
    p_memmove(buf + 4, buf, 20); // forward overlap: copies backward
    for (unsigned i = 0; i < 20; i++)
        CHECK(buf[4 + i] == i);

    fill_sequence(buf, sizeof buf);
    p_memmove(buf, buf + 4, 20); // backward overlap
    for (unsigned i = 0; i < 20; i++)
        CHECK(buf[i] == 4 + i);
}

static void zero_length_calls_touch_nothing(void) {
    unsigned char src[4] = {1, 2, 3, 4}, dst[4] = {9, 9, 9, 9};
    CHECK(p_memcpy(dst, src, 0) == dst);
    CHECK(p_memset(dst, 0, 0) == dst);
    CHECK(p_memmove(dst, src, 0) == dst);
    for (unsigned i = 0; i < 4; i++)
        CHECK(dst[i] == 9);
}

static void memmove_copies_forward_without_overlap(void) {
    unsigned char buf[32] = {0};
    fill_sequence(buf, 8);
    p_memmove(buf + 20, buf, 8); // dst after src, no overlap
    for (unsigned i = 0; i < 8; i++)
        CHECK(buf[20 + i] == i && buf[i] == i);
    CHECK(buf[19] == 0 && buf[28] == 0);
}

static void aligned_copies_handle_odd_tails(void) {
    // Word-aligned buffers with a length that isn't a multiple of 4: the
    // word loop, then a byte tail.
    unsigned int src_words[8], dst_words[8];
    unsigned char* src = (unsigned char*)src_words;
    unsigned char* dst = (unsigned char*)dst_words;
    fill_sequence(src, sizeof src_words);
    p_memset(dst, 0xEE, sizeof dst_words);
    p_memcpy(dst, src, 23);
    for (unsigned i = 0; i < 23; i++)
        CHECK(dst[i] == i);
    CHECK(dst[23] == 0xEE);

    p_memset(dst, 0x5A, 21);
    for (unsigned i = 0; i < 21; i++)
        CHECK(dst[i] == 0x5A);
    CHECK(dst[21] == 21 && dst[22] == 22 && dst[23] == 0xEE);
}

static void memcmp_orders_bytes(void) {
    const unsigned char a[] = {1, 2, 3}, b[] = {1, 2, 4};
    CHECK(p_memcmp(a, a, 3) == 0);
    CHECK(p_memcmp(a, b, 3) < 0);
    CHECK(p_memcmp(b, a, 3) > 0);
}

static void strlen_counts_chars(void) {
    CHECK(p_strlen("") == 0);
    CHECK(p_strlen("serval") == 6);
}

TEST_SUITE(libc_tests, "libc", {"memcpy_copies_unaligned", memcpy_copies_unaligned},
           {"memset_fills_bytes", memset_fills_bytes},
           {"memmove_handles_overlap", memmove_handles_overlap},
           {"zero_length_calls_touch_nothing", zero_length_calls_touch_nothing},
           {"memmove_copies_forward_without_overlap", memmove_copies_forward_without_overlap},
           {"aligned_copies_handle_odd_tails", aligned_copies_handle_odd_tails},
           {"memcmp_orders_bytes", memcmp_orders_bytes},
           {"strlen_counts_chars", strlen_counts_chars});
