#ifndef SERVAL_CORE_LZ77_INTERNAL_H
#define SERVAL_CORE_LZ77_INTERNAL_H

// lz77: LZ77 data in the GBA BIOS's format (SPRITE_ASSET_LZ77, TILESET_LZ77;
// docs/sprites.md#lz77-compression), checked before the BIOS's 16-bit decoder
// (LZ77UnCompVram, SWI 0x12; on the web its stand-in, src/web/lz77.c) unpacks
// it into VRAM. The BIOS trusts the data: it writes as many bytes as the
// header says, finishes a match that runs past them (into the next sprite's
// tiles, or the map), and copies garbage for a match from before the start or
// from the byte just before it (VRAM-safe data has none: the 16-bit decoder
// hasn't written that byte yet). So the loaders check first and refuse what
// would unpack wrong. Portable; not part of the public API.

#include "serval/platform.h"

// What serval_lz77_check() found.
enum {
    LZ77_OK,
    LZ77_MISALIGNED,    // not on a word boundary (the BIOS reads the header as a word)
    LZ77_NOT_LZ77,      // the header word's low byte isn't 0x10
    LZ77_WRONG_SIZE,    // the header's size isn't the one expected
    LZ77_NOT_VRAM_SAFE, // a match copies from the byte just before it (distance 1)
    LZ77_BEFORE_START,  // a match copies from before the first byte
    LZ77_PAST_END,      // a match runs past the size
};

// Checks that `data` is LZ77 data the 16-bit decoder unpacks to exactly
// `size` bytes: the header, then every item up to that size (reading nothing
// past the last one; the data's own length is unknown, so data cut short
// can't be told). Returns LZ77_OK or the first problem, with *at the header
// word (LZ77_NOT_LZ77), the header's size (LZ77_WRONG_SIZE), or the offset in
// the unpacked data where the bad match starts. About 10,000 cycles per KB
// unpacked on the GBA (Thumb, from ROM), a third of loading LZ77 data, the
// rest the BIOS's (tests/rom/lz77_tests.c logs both).
u32 serval_lz77_check(const void* data, u32 size, u32* at);

#ifdef SERVAL_DEBUG
// A problem serval_lz77_check() found, as the loaders' warnings put it after
// "its ... data" (text_format's buffer), e.g. "isn't VRAM-safe: the match at
// byte 34 copies from the byte just before it".
const char* serval_lz77_problem(u32 problem, u32 size, u32 at);
#endif

#endif // SERVAL_CORE_LZ77_INTERNAL_H
