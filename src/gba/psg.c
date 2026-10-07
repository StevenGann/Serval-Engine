#include "serval/audio.h"

#include <tonc.h>

#include "../core/psg_sequencer.h"
#include "../core/warn.h"
#include "internal.h"

// PSG sound effects on tone channels 1 (square with sweep), 2 (square) and 4
// (noise). The hardware plays each tone and its volume envelope by itself;
// the engine only times notes and lengths, once per frame (serval_psg_update,
// called by frame_end). Music (music.c) plays on the same channels whenever
// no sound effect holds them. Tone channel 3, the wave channel (PSG_WAVE,
// psg_waves_set), is planned: sounds on it are refused, and nothing here
// touches its registers or wave RAM.

#define CHANNELS 3

typedef struct {
    const PsgSound* sound; // NULL: idle
    u32 note;              // index into sound->notes
    u32 frames_left;       // of the current note; 0 with sound set: no time limit
} Voice;

static const PsgSound* const* psg_table;
static u16 psg_count;
static Voice voices[CHANNELS];

const ServalMusicHooks* serval_music_hooks;
u8 serval_music_channels;
u8 serval_music_priority;

// Last frequency register value written per channel. The hardware's square
// frequency bits are write-only, so this is the only way to read them back.
static u16 rates[CHANNELS];

u16 serval_psg_rate(u32 channel) {
    return channel < CHANNELS ? rates[channel] : 0;
}

#ifdef SERVAL_DEBUG
// Each kind of problem is reported once (until the next psg_table_set), not
// every time a faulty sound plays.
enum {
    WARN_BAD_ID = 1 << 0,
    WARN_NULL_SOUND = 1 << 1,
    WARN_CHANNEL = 1 << 2,
    WARN_NOTES = 1 << 3,
    WARN_MELODY_FRAMES = 1 << 4,
    WARN_DUTY = 1 << 5,
    WARN_FADE = 1 << 6,
    WARN_SLIDE = 1 << 7,
    WARN_SLIDE_CHANNEL = 1 << 8,
    WARN_VOLUME = 1 << 9,
    WARN_TABLE = 1 << 10,
    WARN_SLIDE_CUTOFF = 1 << 11,
    WARN_WAVE = 1 << 12,
};
static u32 warned;

static bool first_warning(u32 problem) {
    if (warned & problem)
        return false;
    warned |= problem;
    return true;
}
#define WARN_ONCE(problem, ...)                                                                    \
    do {                                                                                           \
        if (first_warning(problem))                                                                \
            SERVAL_WARN(__VA_ARGS__);                                                              \
    } while (0)
#else
#define WARN_ONCE(problem, ...) ((void)0)
#endif

// Clamps an envelope or slide step to the hardware's -7..7.
static u32 step_of(s8 value) {
    u32 step = (u32)(value < 0 ? -value : value);
    return step > 7 ? 7 : step;
}

// The starting volume of a sound or track: 0 means 15, except for a fade-in,
// which would have nowhere to go from full volume and starts from silence.
static u32 start_volume(u32 volume, s32 fade) {
    volume = volume ? volume : fade > 0 ? 0 : 15;
    return volume > 15 ? 15 : volume;
}

u16 serval_psg_control(u32 channel, u32 duty, u32 volume, s32 fade) {
    u32 bits = (volume > 15 ? 15 : volume) << 12;
    if (fade > 0)
        bits |= 1u << 11 | step_of((s8)fade) << 8; // fade in
    else if (fade < 0)
        bits |= step_of((s8)fade) << 8; // fade out
    if (channel != PSG_NOISE) {
        // PSG_DUTY_12..PSG_DUTY_75 are 1..4 so 0 can mean the default; the
        // register takes 0..3.
        duty = duty ? duty - 1u : PSG_DUTY_50 - 1u;
        bits |= (duty & 3) << 6;
    }
    return (u16)bits;
}

// Control register (SOUNDxCNT_H for squares, SOUND4CNT_L for noise): duty,
// envelope and starting volume. Out-of-range fields (reported by psg_play) are
// clamped or masked.
static u16 control_bits(const PsgSound* s) {
    return serval_psg_control(s->channel, s->duty, start_volume(s->volume, s->fade), s->fade);
}

// Frames until a tone fading out from volume v in envelope steps of
// step / 64 s is silent: v * step * 59.73 / 64, rounded up, plus one for the
// envelope clock's phase.
static u32 fade_frames(u32 volume, u32 step) {
    return ((volume * step * 239 + 255) >> 8) + 1;
}

// Frequency register value for a square wave: f = 131072 / (2048 - n).
static u16 square_rate(u32 hz) {
    if (hz < 64)
        hz = 64;
    if (hz > 131072)
        hz = 131072;
    return (u16)(2048 - 131072 / hz);
}

// 524288 / r for the noise divider r = 0..7, with r = 0 meaning 0.5. (The ARM7
// has no hardware divider.)
static const u32 noise_base[8] = {1048576, 524288, 262144, 174762, 131072, 104857, 87381, 74898};

// Noise frequency register: f = 524288 / r / 2^(s+1), with r = 0 meaning 0.5.
// Picks the r and s that come closest to hz.
static u16 noise_rate(u32 hz) {
    u32 best = 0, best_error = 0xFFFFFFFF;
    for (u32 s = 0; s < 14; s++) {
        for (u32 r = 0; r < 8; r++) {
            u32 f = noise_base[r] >> (s + 1);
            u32 error = f > hz ? f - hz : hz - f;
            if (error < best_error) {
                best_error = error;
                best = s << 4 | r;
            }
        }
    }
    return (u16)best;
}

void serval_psg_quiet(u32 channel) {
    // Volume 0 with a restart makes the channel go quiet immediately.
    switch (channel) {
    case PSG_SQUARE1:
        REG_SND1SWEEP = 0x0008; // sweep off
        REG_SND1CNT = 0;
        REG_SND1FREQ = 0x8000;
        break;
    case PSG_SQUARE2:
        REG_SND2CNT = 0;
        REG_SND2FREQ = 0x8000;
        break;
    case PSG_NOISE:
        REG_SND4CNT = 0;
        REG_SND4FREQ = 0x8000;
        break;
    }
}

void serval_psg_tone(u32 channel, u16 control, u16 rate) {
    rates[channel] = rate;
    switch (channel) {
    case PSG_SQUARE1:
        REG_SND1SWEEP = 0x0008; // sweep off
        REG_SND1CNT = control;
        REG_SND1FREQ = (u16)(0x8000 | rate);
        break;
    case PSG_SQUARE2:
        REG_SND2CNT = control;
        REG_SND2FREQ = (u16)(0x8000 | rate);
        break;
    case PSG_NOISE:
        REG_SND4CNT = control;
        REG_SND4FREQ = (u16)(0x8000 | rate);
        break;
    }
}

bool serval_psg_sfx_active(u32 channel) {
    return voices[channel].sound != NULL;
}

static void silence(u32 channel) {
    serval_psg_quiet(channel);
    voices[channel].sound = NULL;
}

// Ends the sound effect on a channel; music using the channel comes back.
static void end_sound(u32 channel) {
    silence(channel);
    if (serval_music_hooks && (serval_music_channels & 1u << channel))
        serval_music_hooks->resume(channel);
}

// Starts a tone at hz on the sound's channel (0 Hz: a rest, silent).
static void start_tone(const PsgSound* s, u32 hz) {
    if (hz == 0) {
        serval_psg_quiet(s->channel);
        return;
    }
    u16 control = control_bits(s);
    switch (s->channel) {
    case PSG_SQUARE1: {
        u32 sweep = 0x0008; // no sweep
        if (s->slide) {
            u32 time = step_of(s->slide);
            u32 size = s->slide_size ? (s->slide_size > 7 ? 7u : s->slide_size) : 1u;
            sweep = time << 4 | (s->slide < 0 ? 0x0008 : 0) | size;
        }
        REG_SND1SWEEP = (u16)sweep;
        REG_SND1CNT = control;
        rates[PSG_SQUARE1] = square_rate(hz);
        REG_SND1FREQ = (u16)(0x8000 | rates[PSG_SQUARE1]);
        break;
    }
    case PSG_SQUARE2:
        REG_SND2CNT = control;
        rates[PSG_SQUARE2] = square_rate(hz);
        REG_SND2FREQ = (u16)(0x8000 | rates[PSG_SQUARE2]);
        break;
    default:
        REG_SND4CNT = control;
        rates[PSG_NOISE] = noise_rate(hz);
        REG_SND4FREQ = (u16)(0x8000 | rates[PSG_NOISE]);
        break;
    }
}

void serval_psg_init(void) {
    REG_SNDSTAT = SSTAT_ENABLE; // must come before the other sound registers
    // Tone generators at full volume, channels 1, 2 and 4 on both speakers.
    REG_SNDDMGCNT = SDMG_BUILD_LR(SDMG_SQR1 | SDMG_SQR2 | SDMG_NOISE, 7);
    REG_SNDDSCNT = SDS_DMG100;
    psg_stop_all();
}

void psg_table_set(const PsgSound* const* table, u16 count) {
#ifdef SERVAL_DEBUG
    warned = 0;
    serval_psg_seq_reset_warnings();
#endif
    if (count && !serval_plausible_pointer(table)) {
        WARN_ONCE(WARN_TABLE, "psg_table_set: the sound table pointer is not valid; no sounds "
                              "will play");
        count = 0;
    }
    psg_table = table;
    psg_count = count;
    // Sounds from the old table stop; the music plays on.
    for (u32 c = 0; c < CHANNELS; c++)
        if (voices[c].sound)
            end_sound(c);
}

void psg_stop_all(void) {
    if (serval_music_hooks)
        serval_music_hooks->stop();
    for (u32 c = 0; c < CHANNELS; c++)
        silence(c);
}

static void play(const PsgSound* s) {
    Voice* v = &voices[s->channel];
    v->sound = s;
    v->note = 0;
    v->frames_left = s->frames;
    // A tone fading out without a length ends when it is silent, so it
    // doesn't hold its channel (against lower priorities and the music)
    // forever.
    if (!s->frames && s->fade < 0)
        v->frames_left = fade_frames(start_volume(s->volume, s->fade), step_of(s->fade));
    start_tone(s, s->note_count ? s->notes[0] : s->frequency);
}

#ifdef SERVAL_DEBUG
// Frame (counting fractions) after the start of a tone at frequency register
// value n that an upward pitch slide silences it, in 1/128 frames: the
// hardware stops the channel when its next sweep step would pass the top
// value, 2047, checked when the tone starts and after every step (one each
// time / 128 s, adding n >> shift). ~0u: never.
static u32 slide_cutoff(u32 n, u32 time, u32 shift) {
    for (u32 k = 0;; k++) {
        u32 delta = n >> shift;
        if (n + delta > 2047)
            return k * time * 60; // k * time / 128 s, at 60 (~59.73) frames a second
        if (delta == 0)
            return ~0u;
        n += delta;
    }
}

// Reports a sound whose upward slide silences it before it ends: a slide past
// the highest pitch the channel can play cuts it off.
static void check_slide(const PsgSound* s, u32 id) {
    if (s->channel != PSG_SQUARE1 || s->slide <= 0 || (warned & WARN_SLIDE_CUTOFF))
        return;
    u32 time = step_of(s->slide);
    u32 shift = s->slide_size ? (s->slide_size > 7 ? 7u : s->slide_size) : 1u;
    // How long each tone plays, in frames: ~0u for a tone held until replaced.
    u32 frames = s->frames;
    if (!frames)
        frames =
            s->fade < 0 ? fade_frames(start_volume(s->volume, s->fade), step_of(s->fade)) : ~0u;
    u32 count = s->note_count ? s->note_count : 1;
    for (u32 i = 0; i < count; i++) {
        u32 hz = s->note_count ? s->notes[i] : s->frequency;
        if (!hz)
            continue;
        u32 cutoff = slide_cutoff(square_rate(hz), time, shift);
        if (cutoff == ~0u || (frames != ~0u && cutoff >= frames * 128))
            continue;
        // The frame it is cut off in, counting from 1.
        u32 frame = cutoff / 128 + 1;
        if (frames == ~0u)
            WARN_ONCE(WARN_SLIDE_CUTOFF,
                      "psg_play: sound %u slides up past the highest pitch in frame %u, which "
                      "silences it. For a longer sound, start lower (.frequency), slide slower "
                      "(.slide closer to 7) or in smaller steps (.slide_size closer to 7)",
                      id, frame);
        else
            WARN_ONCE(WARN_SLIDE_CUTOFF,
                      "psg_play: sound %u slides up past the highest pitch in frame %u of %u, "
                      "which silences it early. Start lower (.frequency), slide slower (.slide "
                      "closer to 7) or in smaller steps (.slide_size closer to 7), or shorten "
                      ".frames",
                      id, frame, frames);
        return;
    }
}
#endif

// Checks a sound before it plays: returns false (after reporting it) for
// sounds that can't play, and reports fields that will be clamped.
static bool playable(const PsgSound* s, u32 id) {
    (void)id; // only used by warnings, which release builds drop
    if (!serval_plausible_pointer(s)) {
        WARN_ONCE(WARN_NULL_SOUND, "psg_play: sound table entry %u is NULL or not a valid pointer",
                  id);
        return false;
    }
    if (s->channel == PSG_WAVE) {
        // Planned (audio.h): refused like an invalid channel until the wave
        // channel is implemented, but reported as planned, and apart from
        // invalid channels, so that each is reported once.
        WARN_ONCE(WARN_WAVE,
                  "psg_play: the PSG wave channel is planned, not implemented in this engine "
                  "version; sound %u (PSG_WAVE) is skipped",
                  id);
        return false;
    }
    if (s->channel >= CHANNELS) {
        WARN_ONCE(WARN_CHANNEL,
                  "psg_play: sound %u has an invalid channel (%u); use PSG_SQUARE1, PSG_SQUARE2 "
                  "or PSG_NOISE",
                  id, s->channel);
        return false;
    }
    if (s->note_count && !serval_plausible_pointer(s->notes)) {
        WARN_ONCE(WARN_NOTES, "psg_play: sound %u has note_count %u but .notes is not set", id,
                  s->note_count);
        return false;
    }
    if (s->note_count > 1 && s->frames == 0) {
        WARN_ONCE(WARN_MELODY_FRAMES,
                  "psg_play: sound %u is a melody with .frames 0; set .frames to each note's "
                  "length",
                  id);
        return false;
    }
#ifdef SERVAL_DEBUG
    if (s->duty > PSG_DUTY_75 && s->channel != PSG_NOISE)
        WARN_ONCE(WARN_DUTY, "psg_play: sound %u has .duty %u; use PSG_DUTY_12 to PSG_DUTY_75", id,
                  s->duty);
    if (s->volume > 15)
        WARN_ONCE(WARN_VOLUME, "psg_play: sound %u has .volume %u; the maximum, 15, is used", id,
                  s->volume);
    if (s->fade < -7 || s->fade > 7)
        WARN_ONCE(WARN_FADE, "psg_play: sound %u has .fade %d; use -7 to 7 (clamped)", id, s->fade);
    if (s->slide && s->channel != PSG_SQUARE1)
        WARN_ONCE(WARN_SLIDE_CHANNEL,
                  "psg_play: sound %u has a .slide, which only PSG_SQUARE1 can play; it is "
                  "ignored",
                  id);
    else if (s->slide < -7 || s->slide > 7 || s->slide_size > 7)
        WARN_ONCE(WARN_SLIDE,
                  "psg_play: sound %u has .slide %d, .slide_size %u; use -7 to 7 and 0 to 7 "
                  "(clamped)",
                  id, s->slide, s->slide_size);
    check_slide(s, id);
#endif
    return true;
}

void psg_play(u16 sound_id) {
    if (sound_id >= psg_count) {
        WARN_ONCE(WARN_BAD_ID, "psg_play: sound ID %u is not in the sound table (%u sounds)",
                  sound_id, psg_count);
        return;
    }
    const PsgSound* s = psg_table[sound_id];
    if (!playable(s, sound_id))
        return;
    // A sound of higher priority keeps its channel, and so does music of
    // higher priority.
    const PsgSound* playing = voices[s->channel].sound;
    if (playing ? playing->priority > s->priority
                : (serval_music_channels & 1u << s->channel) && serval_music_priority > s->priority)
        return;
    play(s);
}

// Planned (audio.h, docs/audio.md#wave-channel): the wave channel isn't
// implemented, so the table is ignored and wave RAM left alone. Warns on the
// first call only (debug builds), whatever psg_table_set() does.
void psg_waves_set(const u32* waves, u8 count) {
    (void)waves;
    (void)count;
#ifdef SERVAL_DEBUG
    static bool warned_waves;
    if (!warned_waves) {
        warned_waves = true;
        SERVAL_WARN("psg_waves_set: the PSG wave channel is planned, not implemented in this "
                    "engine version; the waveforms are ignored");
    }
#endif
}

void serval_psg_play_sound(const PsgSound* sound) {
    play(sound);
}

void serval_psg_silence(u32 channel) {
    if (channel < CHANNELS)
        end_sound(channel);
}

void serval_psg_update(void) {
    for (u32 c = 0; c < CHANNELS; c++) {
        Voice* v = &voices[c];
        if (!v->sound || v->frames_left == 0 || --v->frames_left > 0)
            continue;
        const PsgSound* s = v->sound;
        if (s->note_count && ++v->note < s->note_count) {
            v->frames_left = s->frames;
            start_tone(s, s->notes[v->note]);
        } else {
            end_sound(c);
        }
    }
    if (serval_music_hooks)
        serval_music_hooks->update();
}
