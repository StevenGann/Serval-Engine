// What Maxmod's player expects of its GBA mixer, on the web: the functions and
// variables of Maxmod's source/gba/mixer.c and mixer_asm.s, which web builds
// compile in place of those two (third_party/CMakeLists.txt). Maxmod's player
// (source/core), its mmInit() and mmFrame() (source/gba/main_gba.c) and the
// engine's own front end (src/gba/maxmod.c, src/gba/sampled_audio.c) are the
// GBA's, unchanged; so is everything they do with the mixer's buffers.
//
// On the GBA, Maxmod mixes each VBlank's samples into one half of a wave
// buffer while DMA 1 and 2 feed the other half to Direct Sound A (left) and B
// (right) at timer 0's rate, and mmVBlank() swaps the halves. Here the mixing
// is mixer.c's (Maxmod's assembly, in C, sample for sample), into the same
// buffers, and mmVBlank() hands the half Direct Sound played since the last
// VBlank to the web's sound (apu.c), which adds it to that frame's PSG.
// The web's VBlankIntrWait() runs the engine's VBlank handler's Maxmod part
// (platform.c), as the GBA's VBlank interrupt does, so the timing is the
// GBA's: the half a frame_end() mixes plays from the next VBlank, a frame
// after the PSG notes that frame_end() starts, as on the hardware.
//
// The registers are written as on the GBA (SOUNDCNT_H: A on the left, B on
// the right, both at 100%; timer 0; DMA 1 and 2), so the I/O state the engine
// and its tests read is the GBA's; the web's sound reads SOUNDCNT_H and
// SOUNDCNT_X, and ignores the timer and the DMA (the halves come from here).
//
// Linked only with Maxmod, which only audio_bank_set() links (src/gba/
// maxmod.c): games without a sound bank link none of this.

#include <maxmod.h>
#include <stddef.h>
#include <string.h>

#include "core/channel_types.h"
#include "core/mixer.h"
#include "gba/mixer.h"

#include "web.h"

#define IO ((const u16*)0x04000000) // the I/O registers, where the GBA code writes them

// Maxmod's player writes mm_mixer_channel; mixer.c reads WebMixerChannel.
_Static_assert(sizeof(WebMixerChannel) == sizeof(mm_mixer_channel), "mixer channel size");
_Static_assert(offsetof(WebMixerChannel, src) == offsetof(mm_mixer_channel, src), "src");
_Static_assert(offsetof(WebMixerChannel, read) == offsetof(mm_mixer_channel, read), "read");
_Static_assert(offsetof(WebMixerChannel, vol) == offsetof(mm_mixer_channel, vol), "vol");
_Static_assert(offsetof(WebMixerChannel, pan) == offsetof(mm_mixer_channel, pan), "pan");
_Static_assert(offsetof(WebMixerChannel, freq) == offsetof(mm_mixer_channel, freq), "freq");
_Static_assert(WEB_MIXER_STOPPED == MIXCH_GBA_SRC_STOPPED, "the stopped flag");
_Static_assert(MP_SAMPFRAC == 12, "positions' fraction");

// Maxmod's mixer state, by its names: the player (mm_mix_channels, mm_bpmdv),
// mmFrame() (mm_mixlen) and the engine's front end (mp_mix_seg, mp_writepos)
// read them.
mm_mixer_channel* mm_mix_channels;
mm_word mm_mixlen;   // samples a VBlank
mm_word mm_bpmdv;    // the mixing rate * 2.5, for the player's ticks
mm_byte mp_mix_seg;  // 0 or 0xFF, swapped every VBlank: the half playing
mm_addr mp_writepos; // where the next mixing goes (the left speaker's half)

static mm_word channel_count;
static u16* mix_buffer; // the mixing buffer (mixer.c's sums)
static s8* wave_buffer; // left: two halves of mm_mixlen; right: two more
static u32 rate_scale;  // 15,768 * 16384 / the mixing rate
static bool vblank_on;
static mm_voidfunc vblank_function;

// --- The channels, as the player sets them -----------------------------------

void mmMixerSetVolume(int channel, mm_word volume) {
    mm_mix_channels[channel].vol = (mm_byte)volume;
}

void mmMixerSetPan(int channel, mm_byte panning) {
    mm_mix_channels[channel].pan = panning;
}

void mmMixerMulFreq(int channel, mm_word factor) {
    mm_mix_channels[channel].freq = (mm_mix_channels[channel].freq * factor) >> 10;
}

void mmMixerStopChannel(int channel) {
    mm_mix_channels[channel].src = MIXCH_GBA_SRC_STOPPED;
}

void mmMixerSetRead(int channel, mm_word value) {
    mm_mix_channels[channel].read = value;
}

void mmMixerSetFreq(int channel, mm_word rate) {
    mm_mix_channels[channel].freq = rate << 2;
}

// --- Mixing and the VBlank ------------------------------------------------------

// mmFrame()'s mixing, a tick's samples at a time: into the half at
// mp_writepos, the left speaker's samples there and the right's two halves on.
void mmMixerMix(mm_word samples_count) {
    if (samples_count == 0)
        return;
    s8* left = mp_writepos;
    web_mixer_mix((WebMixerChannel*)mm_mix_channels, channel_count, rate_scale, mix_buffer, left,
                  left + 2 * mm_mixlen, samples_count);
    mp_writepos = left + samples_count;
}

// The VBlank: the half Direct Sound played since the last one (the first half
// after a VBlank that restarted DMA, mp_mix_seg set; the second after the
// others) goes to the speakers, then the halves swap as on the GBA.
void mmVBlank(void) {
    if (vblank_on) {
        const s8* played = wave_buffer + (mp_mix_seg ? 0 : mm_mixlen);
        web_audio_direct_sound(IO, played, played + 2 * mm_mixlen, mm_mixlen);
        mp_mix_seg = (mm_byte)~mp_mix_seg;
        if (!mp_mix_seg)
            mp_writepos = wave_buffer;
    }
    if (vblank_function)
        vblank_function();
}

void mmSetVBlankHandler(mm_voidfunc function) {
    vblank_function = function;
}

mm_voidfunc mmGetVBlankHandler(void) {
    return vblank_function;
}

// --- Starting and stopping ----------------------------------------------------------

void mmMixerInit(mm_gba_system* setup) {
    // Maxmod's rates (mm_mixmode): samples a VBlank (round(rate / 59.737)),
    // 15,768 * 16384 / rate, and rate * 2.5; and timer 0's reload.
    static const u16 lengths[] = {136, 176, 224, 264, 304, 352, 448, 528};
    static const u16 scales[] = {31812, 24576, 19310, 16384, 14228, 12288, 9655, 8192};
    static const u32 bpm_divisors[] = {20302, 26280, 33447, 39420, 45393, 52560, 66895, 78840};
    static const u16 timer_reloads[] = {(u16)-2066, (u16)-1596, (u16)-1254, (u16)-1064,
                                        (u16)-924,  (u16)-798,  (u16)-627,  (u16)-532};
    u32 mode = (u32)setup->mixing_mode;
    if (mode >= sizeof(lengths) / sizeof(lengths[0]))
        mode = MM_MIX_16KHZ;

    channel_count = setup->mix_channel_count;
    mm_mix_channels = setup->mixing_channels;
    mix_buffer = setup->mixing_memory;
    wave_buffer = setup->wave_memory;
    mp_writepos = wave_buffer;
    mm_mixlen = lengths[mode];
    rate_scale = scales[mode];
    mm_bpmdv = bpm_divisors[mode];

    memset(wave_buffer, 0, mm_mixlen * 4);
    mp_mix_seg = 0;
    for (mm_word i = 0; i < channel_count; i++)
        mm_mix_channels[i].src = MIXCH_GBA_SRC_STOPPED;
    vblank_on = true;

    // As Maxmod sets the hardware: Direct Sound A and B on timer 0, A left
    // and B right, at 100%, FIFOs reset; DMA 1 and 2 on the halves, sound
    // on, timer 0 at the rate.
    REG_SOUNDCNT_H = 0;
    REG_SOUNDCNT_H = 0x9A0C;
    REG_DMA1SAD = (mm_word)(uintptr_t)wave_buffer;
    REG_DMA2SAD = (mm_word)(uintptr_t)wave_buffer + mm_mixlen * 2;
    REG_DMA1DAD = (mm_word)(uintptr_t)REG_SGFIFOA;
    REG_DMA2DAD = (mm_word)(uintptr_t)REG_SGFIFOB;
    REG_DMA1CNT = 0xB6000000;
    REG_DMA2CNT = 0xB6000000;
    REG_SOUNDCNT_X = 0x80;
    REG_TM0CNT = timer_reloads[mode] | (0x80u << 16);
}

void mmMixerEnd(void) {
    REG_SOUNDCNT_H = 0; // Direct Sound silent
    vblank_on = false;
    REG_DMA1CNT = 0;
    REG_DMA2CNT = 0;
    REG_TM0CNT = 0;
}
