// lz77: the loaders' check of LZ77 data before the BIOS unpacks it
// (lz77_internal.h). The format: a header word, 0x10 in its low byte and the
// unpacked size in bits 8-31; then blocks of a flag byte and up to eight
// items, one per flag bit from the highest: 0 a literal byte, 1 a match of
// two bytes, the length less 3 in the first's high nibble and the distance
// less 1 in its low nibble and the second byte. tools/lz77.py writes it.

#include "lz77_internal.h"

#include "serval/text.h"

#include <stdint.h>

#define LZ77_TYPE 0x10
#define MIN_MATCH 3

u32 serval_lz77_check(const void* data, u32 size, u32* at) {
    *at = 0;
    if ((uintptr_t)data & 3)
        return LZ77_MISALIGNED;
    u32 header = *(const u32*)data;
    if ((header & 0xFF) != LZ77_TYPE) {
        *at = header;
        return LZ77_NOT_LZ77;
    }
    if (header >> 8 != size) {
        *at = header >> 8;
        return LZ77_WRONG_SIZE;
    }
    const u8* in = (const u8*)data + 4;
    u32 out = 0; // bytes unpacked so far
    while (out < size) {
        u32 flags = *in++;
        for (u32 bit = 0x80; bit && out < size; bit >>= 1) {
            if (!(flags & bit)) {
                in++;
                out++;
                continue;
            }
            u32 b0 = in[0], b1 = in[1];
            in += 2;
            u32 length = (b0 >> 4) + MIN_MATCH;
            u32 distance = ((b0 & 0x0F) << 8 | b1) + 1;
            if (distance < 2 || distance > out || length > size - out) {
                *at = out;
                return distance < 2     ? LZ77_NOT_VRAM_SAFE
                       : distance > out ? LZ77_BEFORE_START
                                        : LZ77_PAST_END;
            }
            out += length;
        }
    }
    return LZ77_OK;
}

#ifdef SERVAL_DEBUG
const char* serval_lz77_problem(u32 problem, u32 size, u32 at) {
    switch (problem) {
    case LZ77_MISALIGNED:
        return "isn't word-aligned (the BIOS reads it by words)";
    case LZ77_NOT_LZ77:
        return text_format("starts 0x%x, not LZ77 (a header word with 0x10 in its low byte)", at);
    case LZ77_WRONG_SIZE:
        return text_format("unpacks to %u bytes (its header says), not the %u its tiles take", at,
                           size);
    case LZ77_NOT_VRAM_SAFE:
        return text_format("isn't VRAM-safe: the match at byte %u copies from the byte just "
                           "before it",
                           at);
    case LZ77_BEFORE_START:
        return text_format("has a match at byte %u copying from before the first byte", at);
    case LZ77_PAST_END:
        return text_format("has a match at byte %u running past the end, byte %u", at, size);
    default:
        return "is fine";
    }
}
#endif
