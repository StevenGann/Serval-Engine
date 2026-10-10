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

// The most Direct Sound samples one web_audio_direct_sound() call plays: a
// frame at 61 kHz (Maxmod's rates reach 31,536 Hz, 528 samples a frame).
#define WEB_DIRECT_SOUND_MAX 1024

// Direct Sound A and B, the GBA's two 8-bit sample channels (Maxmod plays on
// them): adds what FIFO A and FIFO B play over the samples the last
// web_audio_generate() call made, count signed 8-bit samples each (at most
// WEB_DIRECT_SOUND_MAX; the rest are dropped), at an even pace, mixed as
// SOUNDCNT_H (in io) says: each on the left, the right or both, at 50% or
// 100%; silent while sound is off. The GBA plays them at a timer's rate
// (Maxmod's 15,768 Hz: 264 a frame); here they are spread over the page's
// samples, each held as the hardware holds it (an output sample is the exact
// average of the held level over its time). Only games with a sound bank
// link it (maxmod_mixer.c).
void web_audio_direct_sound(const u16* io, const s8* a, const s8* b, u32 count);

// --- Tracker music and sampled sound effects (mixer.c) ------------------------
//
// The web plays them with Maxmod's player, which is C, and this mixer in place
// of Maxmod's GBA one (source/gba/mixer_asm.s, ARM assembly): it mixes as that
// one does, sample for sample, into the same buffers (maxmod_mixer.c joins the
// two).

// One mixer channel, as Maxmod's player sets it: Maxmod's GBA
// mm_mixer_channel (third_party/maxmod/source/core/channel_types.h), field for
// field, on 32-bit targets (maxmod_mixer.c checks the layout; the host tests
// use this struct only).
typedef struct {
    // The sample's data: unsigned 8-bit (128 is silence), after the header
    // mmutil writes for the GBA (mm_mas_gba_sample): the length in samples, a
    // u32 at src - 12, and the loop's length, a u32 at src - 8 (0xFFFFFFFF:
    // none; a loop ends where the sample does). WEB_MIXER_STOPPED set: the
    // channel is silent, and costs nothing.
    uintptr_t src;
    u32 read; // where it plays, in samples, 20.12 fixed point
    u8 vol;   // 0-255
    u8 pan;   // 0 (left) to 255 (right); 128 is centred
    u8 unused_0;
    u8 unused_1;
    u32 freq; // samples a step, 20.12 fixed point, at 15,768 Hz (see rate_scale); 0: paused
} WebMixerChannel;

#define WEB_MIXER_STOPPED ((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1))

// Mixes `samples` steps of the channels into left and right, signed 8-bit,
// as Maxmod's GBA mixer does: each channel's sample at its position
// (truncated, no interpolation), times its volume panned linearly (left
// volume * (256 - pan) / 256, right volume * pan / 256), summed and scaled so
// that one channel at full volume panned to one side spans the whole 8-bit
// range, and clipped. Positions advance by freq * rate_scale / 16384
// (Maxmod's mm_ratescale: 16384 at its 15,768 Hz, where a freq of 4096 plays
// one sample a step); a channel that reaches its sample's end loops back by
// the loop's length, or stops (src gets WEB_MIXER_STOPPED, read 0). work is
// 2 * samples halfwords of scratch (Maxmod's mixing buffer).
void web_mixer_mix(WebMixerChannel* channels, u32 count, u32 rate_scale, u16* work, s8* left,
                   s8* right, u32 samples);

#endif // SERVAL_WEB_H
