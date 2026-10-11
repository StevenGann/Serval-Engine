// lz77: the GBA BIOS's LZ77UnCompReadNormalWrite16bit (SWI 0x12, libtonc's
// LZ77UnCompVram) in C, for the web's stand-in (platform.c), which unpacks
// SPRITE_ASSET_LZ77 sprites and TILESET_LZ77 tilesets (sprites.c, map.c).
// Byte for byte the BIOS's, as mGBA's BIOS implements it (src/gba/bios.c,
// _unLz77), bad data included:
//
// - The header word's bits 8-31 are the size; its low byte isn't checked.
// - Bytes go out a halfword at a time, as VRAM needs: a byte at an even
//   address waits for the next one, and the two are written together. A
//   match reads what memory holds, so copying from the byte just before it
//   while that byte waits reads the old one (VRAM-safe data never does).
// - Unpacking stops at the size between items: a match running past it is
//   finished, and a last byte at an even address is never written.
//
// The loaders check the data first (src/core/lz77.c), so only VRAM-safe data
// ending at its size gets here, which every decoder unpacks alike; the rest is
// matched so that what the web shows never differs from the GBA. Host-tested
// (tests/web_lz77_tests.c), and against mGBA's BIOS in the test ROM
// (tests/rom/lz77_tests.c).

#include "web.h"

void web_lz77_unpack_vram(const void* src, void* dst) {
    const u8* in = src;
    u8* out = dst;
    long remaining = (long)((u32)in[1] | (u32)in[2] << 8 | (u32)in[3] << 16);
    in += 4;
    u8 waiting = 0; // the byte at an even address, until its halfword is complete
    u32 flags = 0, items = 0;
    while (remaining > 0) {
        if (items == 0) {
            flags = *in++;
            items = 8;
            continue;
        }
        if (flags & 0x80) {
            u32 length = (u32)(in[0] >> 4) + 3;
            const u8* from = out - (((u32)(in[0] & 0x0F) << 8 | in[1]) + 1);
            in += 2;
            while (length--) {
                if (remaining)
                    remaining--;
                u8 byte = *from++;
                if ((uintptr_t)out & 1) {
                    out[-1] = waiting;
                    out[0] = byte;
                } else {
                    waiting = byte;
                }
                out++;
            }
        } else {
            u8 byte = *in++;
            if ((uintptr_t)out & 1) {
                out[-1] = waiting;
                out[0] = byte;
            } else {
                waiting = byte;
            }
            out++;
            remaining--;
        }
        flags <<= 1;
        items--;
    }
}
