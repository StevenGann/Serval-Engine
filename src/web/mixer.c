// The web's software mixer for tracker music and sampled sound effects: what
// Maxmod's GBA mixer (third_party/maxmod/source/gba/mixer_asm.s, ARM
// assembly) does, in C, sample for sample, so that the web plays the very
// samples the GBA plays. Maxmod's player (C) sets the channels; maxmod_mixer.c
// hands them here once per tick and the result to Direct Sound (apu.c).
//
// The assembly's algorithm, which this follows (it is Maxmod's, ISC: games
// that link this link Maxmod, whose notice they carry, docs/licensing.md):
//
// - Each channel steps through its sample by freq (20.12 fixed point) per
//   output sample, reading the byte at the integer part (no interpolation).
//   Its fetch buffer (samples copied to IWRAM ahead of reading them) and its
//   loop unrolling change the speed, not the result, and are left out.
// - The volume pans linearly: left = vol * (256 - pan) >> 8, right =
//   vol * pan >> 8. Each sample adds (sample * volume) >> 5, an unsigned
//   11-bit value, to a 16-bit sum per speaker.
// - The samples are unsigned (128 is silence), so the sum carries 4 times
//   every playing channel's volume as an offset (128 * volume >> 5): the sum
//   of the volumes, halved and times 8, is taken off, and the rest shifted
//   down 3 bits and clipped to -128..127. One channel at full volume panned
//   to one side spans the whole 8-bit range.
// - A channel mixes up to its sample's end, then loops back by the loop's
//   length or stops; a stopped channel mixes silence (the sample 128) for the
//   rest of the call, so the offset holds. Channels at zero volume only
//   advance.
//
// Where the assembly's arithmetic breaks down, this keeps to what it means:
// it counts the steps left in a sample with a division that is right only
// below 16 samples a step (65536), and it reads past a sample's end when a
// pattern's sample offset (9xx) starts beyond it; here the count is exact,
// and such a start stops the channel (or wraps it into its loop). Neither
// occurs in the engine's tests or examples.

#include "web.h"

#include <string.h>

#define FRACTION 12 // bits of a position's fraction (Maxmod's MP_SAMPFRAC)
#define SILENCE 128 // the unsigned sample that plays nothing

// The sample header's words before src (mm_mas_gba_sample), read bytewise:
// src - 12 is word-aligned in a bank, but needn't be anywhere else.
static u32 header_word(uintptr_t src, u32 back) {
    u32 word;
    memcpy(&word, (const u8*)src - back, sizeof(word));
    return word;
}

// Adds n steps of one channel, from *read, into the sums, at volumes vl and
// vr (0-255 each), and advances *read.
static void mix_steps(const u8* data, u32* read, u32 step, u32 vl, u32 vr, u16* sum_l, u16* sum_r,
                      u32 n) {
    u32 r = *read;
    if (vl == 0 && vr == 0) {
        *read = r + n * step;
        return;
    }
    for (u32 i = 0; i < n; i++) {
        u32 v = data[r >> FRACTION];
        r += step;
        sum_l[i] = (u16)(sum_l[i] + (v * vl >> 5));
        sum_r[i] = (u16)(sum_r[i] + (v * vr >> 5));
    }
    *read = r;
}

// Adds n steps of silence at volumes vl and vr: the offset a stopped channel
// still owes its sums for the rest of the call.
static void mix_silence(u32 vl, u32 vr, u16* sum_l, u16* sum_r, u32 n) {
    static const u8 silence = SILENCE;
    u32 read = 0;
    mix_steps(&silence, &read, 0, vl, vr, sum_l, sum_r, n);
}

// Mixes one playing channel into the sums.
static void mix_channel(WebMixerChannel* ch, u32 step, u32 vl, u32 vr, u16* sum_l, u16* sum_r,
                        u32 samples) {
    const u8* data = (const u8*)ch->src;
    u32 length = header_word(ch->src, 12);
    u32 loop = header_word(ch->src, 8);
    u32 end = length << FRACTION;
    // A loop that doesn't fit the sample can't come from mmutil (no loop is
    // 0xFFFFFFFF): taken as none, rather than looping forever.
    bool loops = loop != 0xFFFFFFFFu && loop != 0 && loop <= length;
    u32 read = ch->read;
    u32 done = 0;
    for (;;) {
        // At the end (checked after every stretch, the call's last too, as
        // the assembly does, so that a sample ending with the call stops in
        // it): back by the loop's length (more than once only for a start
        // past the end), or stop.
        if (read >= end) {
            if (!loops) {
                ch->src = WEB_MIXER_STOPPED;
                read = 0;
                mix_silence(vl, vr, sum_l + done, sum_r + done, samples - done);
                break;
            }
            u32 loop_start = end - (loop << FRACTION);
            read = read - end < (loop << FRACTION)
                       ? read - (loop << FRACTION)
                       : loop_start + (read - loop_start) % (loop << FRACTION);
        }
        if (done == samples)
            break;
        // The steps to mix: all those left, or up to the sample's end (the
        // last one reading before it). A step of 0 (a rate scaled below
        // 1/4096 of a sample) holds its sample.
        u32 n = samples - done;
        u32 left = end - read;
        if ((uint64_t)n * step > left)
            n = (u32)(((uint64_t)left + step - 1) / step);
        mix_steps(data, &read, step, vl, vr, sum_l + done, sum_r + done, n);
        done += n;
    }
    ch->read = read;
}

static s8 clip(s32 v) {
    return (s8)(v < -128 ? -128 : v > 127 ? 127 : v);
}

void web_mixer_mix(WebMixerChannel* channels, u32 count, u32 rate_scale, u16* work, s8* left,
                   s8* right, u32 samples) {
    if (samples == 0)
        return;
    u16* sum_l = work;
    u16* sum_r = work + samples;
    memset(work, 0, 2 * samples * sizeof(u16));
    u32 total_l = 0, total_r = 0; // the playing channels' volumes, each side
    for (u32 c = 0; c < count; c++) {
        WebMixerChannel* ch = &channels[c];
        if ((ch->src & WEB_MIXER_STOPPED) || ch->freq == 0)
            continue;
        // In 32 bits, as the assembly scales it.
        u32 step = (u32)(ch->freq * rate_scale) >> 14;
        u32 vl = (256 - (u32)ch->pan) * ch->vol >> 8;
        u32 vr = (u32)ch->pan * ch->vol >> 8;
        total_l += vl;
        total_r += vr;
        mix_channel(ch, step, vl, vr, sum_l, sum_r, samples);
    }
    s32 offset_l = (s32)((total_l >> 1) << 3);
    s32 offset_r = (s32)((total_r >> 1) << 3);
    for (u32 i = 0; i < samples; i++) {
        // Shifted as signed values (arithmetic shift: rounding down).
        left[i] = clip(((s32)sum_l[i] - offset_l) >> 3);
        right[i] = clip(((s32)sum_r[i] - offset_r) >> 3);
    }
}
