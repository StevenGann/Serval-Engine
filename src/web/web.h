#ifndef SERVAL_WEB_H
#define SERVAL_WEB_H

// The web backend runs the GBA backend (src/gba/) unchanged on virtual GBA
// hardware: the I/O registers, palette RAM, VRAM and OAM sit at their GBA
// addresses in WebAssembly memory, where the GBA code reads and writes them.
// These functions turn that state into pixels and samples once per frame.
// They take the memory regions as pointers, so host unit tests can run them on
// plain arrays. Not part of the public API.

#include "serval/platform.h"

#define WEB_SCREEN_W 240
#define WEB_SCREEN_H 160

// The GBA memory regions the video and sound hardware read.
typedef struct {
    const u16* io;      // I/O registers, 0x04000000 (1 KiB), indexed by offset / 2
    const u16* palette; // palette RAM, 0x05000000: 256 BG colors, then 256 sprite colors
    const u8* vram;     // VRAM, 0x06000000 (96 KiB)
    const u16* oam;     // OAM, 0x07000000 (1 KiB)
    // raster: what HBlank DMA reads: the `bytes` bytes at GBA address
    // `address`, or NULL where nothing is readable. NULL: no HBlank DMA.
    const u8* (*dma_source)(u32 address, u32 bytes);
} WebVideoMemory;

// Renders the frame the hardware would show for this state, as 240x160
// row-major RGBA8888 (bytes R, G, B, A; A is always 255). DMA channels set to
// start at HBlank (raster effects) copy between the lines, as on the GBA, from
// their source address on (as if restarted in VBlank, which the engine does),
// into a copy of the I/O registers and palette RAM: the state passed in is
// left as it was.
void web_render(const WebVideoMemory* mem, u8* rgba);

// Starts the sound hardware at its power-on state, generating samples at
// sample_rate (Hz).
void web_audio_init(u32 sample_rate);

// Applies the sound register writes made since the last call, then generates
// count stereo sample frames (interleaved left, right; -1 to 1) of PSG output.
// The trigger bits (bit 15 of SOUND1CNT_X, SOUND2CNT_H, SOUND3CNT_X and
// SOUND4CNT_H) restart their channel and are cleared in io: the hardware's
// are write-only, so the GBA code never reads them back.
void web_audio_generate(u16* io, float* out, u32 count);

#endif // SERVAL_WEB_H
