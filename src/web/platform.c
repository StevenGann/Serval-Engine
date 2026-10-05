// The web backend's stand-ins for what the GBA backend gets from the hardware,
// the BIOS and libtonc's assembly: waiting for VBlank (which is where a frame
// is shown, sound is generated and the browser gets control back), BIOS bit
// unpacking, libtonc's fast copies, the debug output and the CPU cycle counter.
//
// The GBA code runs on virtual hardware: its I/O registers, palette RAM, VRAM
// and OAM sit at their GBA addresses in WebAssembly memory (the build makes
// the memory large enough), so it reads and writes them exactly as on the GBA.

#include "serval/debug.h"

#include "../core/warn.h"
#include "../gba/internal.h"
#include "web.h"

#include <emscripten.h>
#include <emscripten/console.h>

#include <string.h>

#define IO_ADDRESS 0x04000000u
#define PALETTE_ADDRESS 0x05000000u
#define VRAM_ADDRESS 0x06000000u
#define OAM_ADDRESS 0x07000000u
#define REG_KEYINPUT_OFFSET 0x130u

// The GBA's refresh rate (16.78 MHz / 280,896 cycles per frame), in
// ten-thousandths of a hertz, and its CPU clock.
#define FRAME_RATE_E4 597275u
#define CPU_HZ 16777216.0

// The most samples one frame can need (at 192 kHz), with room to spare.
#define MAX_FRAME_SAMPLES 4096u

static u8 frame_rgba[WEB_SCREEN_W * WEB_SCREEN_H * 4];
static float frame_audio[MAX_FRAME_SAMPLES * 2];
static u32 sample_rate;
static u32 sample_remainder;

// Power-on state the GBA code relies on: no buttons held (KEYINPUT is active
// low; zeroed memory would read as every button pressed).
__attribute__((constructor)) static void power_on(void) {
    *(volatile u16*)(IO_ADDRESS + REG_KEYINPUT_OFFSET) = 0x03FF;
}

// The browser's audio sample rate (shell.html).
EM_JS(u32, web_sample_rate, (void), { return Module.servalSampleRate(); });

// Shows the frame, queues its sound, then waits for the next frame's turn
// (giving the browser control meanwhile) and writes the buttons held then to
// KEYINPUT. Asynchronous: Asyncify unwinds the game's stack and resumes it.
EM_ASYNC_JS(void, web_present, (const u8* rgba, const float* audio, u32 samples),
            { await Module.servalPresent(rgba, audio, samples); });

// tonc_bios.h. The GBA's VBlank is where the hardware has just finished
// showing a frame, so this is where the web backend draws it.
void VBlankIntrWait(void);
void VBlankIntrWait(void) {
    if (!sample_rate) {
        sample_rate = web_sample_rate();
        web_audio_init(sample_rate);
    }

    const WebVideoMemory memory = {
        .io = (const u16*)IO_ADDRESS,
        .palette = (const u16*)PALETTE_ADDRESS,
        .vram = (const u8*)VRAM_ADDRESS,
        .oam = (const u16*)OAM_ADDRESS,
    };
    web_render(&memory, frame_rgba);

    // sample_rate / 59.7275 samples per frame, carrying the remainder over.
    u32 total = sample_rate * 10000u + sample_remainder;
    u32 samples = total / FRAME_RATE_E4;
    sample_remainder = total % FRAME_RATE_E4;
    if (samples > MAX_FRAME_SAMPLES)
        samples = MAX_FRAME_SAMPLES;
    web_audio_generate((u16*)IO_ADDRESS, frame_audio, samples);

    web_present(frame_rgba, frame_audio, samples);
}

// The CPU cycle counter (core.c): real time, in GBA CPU cycles. A browser runs
// the game much faster than the GBA, so frame_cpu_cycles() says little about
// how the game will run on hardware. (random_entropy() doesn't use it: it
// hashes the input history, so it matches the GBA.)
u32 serval_web_cycles(void) {
    return (u32)(uint64_t)(emscripten_get_now() * (CPU_HZ / 1000.0));
}

// tonc_bios.h: BIOS call 10h, unpacking src_bpp-bit units to dst_bpp bits,
// adding dst_ofs to each (to zero units too if bit 31 is set).
typedef struct {
    u16 src_len;
    u8 src_bpp;
    u8 dst_bpp;
    u32 dst_ofs;
} BitUnPackParams;

void BitUnPack(const void* src, void* dst, const BitUnPackParams* bup);
void BitUnPack(const void* src, void* dst, const BitUnPackParams* bup) {
    const u8* in = src;
    u32* out = dst;
    u32 offset = bup->dst_ofs & 0x7FFFFFFFu;
    bool offset_zeros = (bup->dst_ofs & 0x80000000u) != 0;
    u32 src_mask = (1u << bup->src_bpp) - 1;
    u32 word = 0, bits = 0;
    for (u32 i = 0; i < bup->src_len; i++) {
        for (u32 shift = 0; shift < 8; shift += bup->src_bpp) {
            u32 unit = (in[i] >> shift) & src_mask;
            if (unit || offset_zeros)
                unit += offset;
            word |= unit << bits;
            bits += bup->dst_bpp;
            if (bits >= 32) {
                *out++ = word;
                word = 0;
                bits = 0;
            }
        }
    }
}

// tonc_core.h's copies and fills (assembly on the GBA). Counts are in units
// of the element size.
void memcpy16(void* dst, const void* src, unsigned int hwcount);
void memcpy16(void* dst, const void* src, unsigned int hwcount) {
    memcpy(dst, src, hwcount * 2u);
}

void memcpy32(void* dst, const void* src, unsigned int wdcount);
void memcpy32(void* dst, const void* src, unsigned int wdcount) {
    memcpy(dst, src, wdcount * 4u);
}

void memset16(void* dst, u16 hw, unsigned int hwcount);
void memset16(void* dst, u16 hw, unsigned int hwcount) {
    u16* d = dst;
    while (hwcount--)
        *d++ = hw;
}

void memset32(void* dst, u32 wd, unsigned int wdcount);
void memset32(void* dst, u32 wd, unsigned int wdcount) {
    u32* d = dst;
    while (wdcount--)
        *d++ = wd;
}

// Interrupts don't exist on the web: VBlankIntrWait() waits for the next
// frame by itself. libtonc's irq_init() installs this as the handler.
void isr_master(void);
void isr_master(void) {}

// debug.h, writing to the browser's console.
static u32 warnings;

void debug_log(const char* message) {
    emscripten_console_log(message);
}

void serval_warn(const char* message) {
    warnings++;
    char line[288];
    size_t n = strlen(message);
    if (n > sizeof(line) - 9)
        n = sizeof(line) - 9;
    memcpy(line, "serval: ", 8);
    memcpy(line + 8, message, n);
    line[8 + n] = '\0';
    emscripten_console_warn(line);
}

u32 debug_warning_count(void) {
    return warnings;
}

void debug_exit(int code) {
    emscripten_force_exit(code);
    for (;;) {
    }
}
