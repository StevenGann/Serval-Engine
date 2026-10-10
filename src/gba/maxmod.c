// Tracker music and sampled sound effects on the GBA (audio.h,
// docs/audio.md): the sound bank and Maxmod (BlocksDS, vendored in
// third_party/maxmod) mixing into Direct Sound A and B. The calls games and
// scripts make (music_*(), sfx_*()) are sampled_audio.c's, which reach this
// file only through serval_mixer_ops, set by audio_bank_set(): a game that
// calls them without ever registering a bank (a scripted game, whose SYS
// calls reach every one) links none of Maxmod. The web keeps silent stubs
// (src/web/sampled_audio.c, docs/audio.md#web).
//
// audio_bank_set() also hooks the mixer into frame_end() (serval_mixer_hook)
// and into the engine's VBlank handler (serval_vblank_mixer, core.c), as
// palette writes and raster effects hook themselves in. Maxmod is set up with
// mmInit() and static buffers (never mmInitDefault(), which allocates; never
// mmEnd(), which frees): the mixing buffer in IWRAM, the wave buffer and the
// channels in EWRAM.
//
// Fixed settings (docs/audio.md#configuration): 15,768 Hz (Maxmod's 16 kHz),
// 8 module channels, 12 mixer channels shared by the music's notes and the
// effects. A module uses at most 8 channels, so 4 mixer channels are always
// left for effects (an IT's notes kept playing by its new-note actions can
// take more, as background channels effects may take back).
//
// Timing (docs/audio.md#frame-loop). Maxmod's double buffer holds two
// VBlanks of samples per side; at every VBlank mmVBlank() (first in the
// VBlank handler) switches halves, and mmFrame() mixes the next VBlank's
// samples into the half DMA isn't playing. Normally frame_end() runs
// mmFrame() once per frame, after the VBlank flush. When a frame overruns
// (the VBlank comes while the game is still working, so frame_end() won't
// mix before the next one), the VBlank handler mixes instead, at once, well
// before the half is due: no stale audio, and the music keeps time. Not if
// the game is inside a call into Maxmod just then (sfx_play() ...): the call
// mixes as it returns. If a half is ever found unmixed as it starts to play
// (a frame that ends within a few cycles of VBlank, too late for its wait to
// catch that VBlank), the handler mixes it at once, ahead of the DMA reading
// it but for its first few milliseconds.

#include "serval/audio.h"
#include "serval/fixed.h"

#include <maxmod.h>
#include <mm_mas.h>
#include <mm_msl.h>
#include <string.h>
#include <tonc.h>

#include "../core/warn.h"
#include "internal.h"

// Maxmod's internals the engine uses (gba/mixer.c, not in maxmod.h): the
// mixer's half of the double buffer (0 or 0xFF, switched by mmVBlank()), where
// mmFrame() writes its next samples, and mmEnd() without its free().
extern mm_byte mp_mix_seg;
extern mm_addr mp_writepos;
void mmMixerEnd(void);

// The MAS format version this Maxmod reads: what mmutil writes into every
// sample and module of a bank (mmutil's source/version.h, MAS_VERSION, at the
// pinned version: third_party/maxmod/VENDORED.md).
#define MAS_VERSION 0x18

#define MIX_MODE MM_MIX_16KHZ
#define MIX_BYTES MM_MIXLEN_16KHZ // 264 samples a VBlank: mixing buffer and wave buffer
#define HALF (MIX_BYTES / 4)      // one VBlank of one side's samples
#define MODULE_CHANNELS 8
#define MIXER_CHANNELS 12
#define EFFECT_SLOTS 16 // Maxmod's EFFECT_CHANNELS (core/effect.h)

// The mixing buffer is read and written for every sample: IWRAM. The wave
// buffer (read by DMA) and the channels: EWRAM.
static u32 mix_buffer[MIX_BYTES / 4];
static SERVAL_EWRAM_BSS u32 wave_buffer[MIX_BYTES / 4];
static SERVAL_EWRAM_BSS u32 module_channels[MODULE_CHANNELS * MM_SIZEOF_MODCH / 4];
static SERVAL_EWRAM_BSS u32 active_channels[MIXER_CHANNELS * MM_SIZEOF_ACTCH / 4];
static SERVAL_EWRAM_BSS u32 mixer_channels[MIXER_CHANNELS * MM_SIZEOF_MIXCH / 4];

static const msl_head* bank; // the registered bank, or NULL (Maxmod stopped)

static bool music_on;              // music_play() until music_stop(), or the module's end
static bool music_held;            // paused
static volatile bool music_failed; // Maxmod stopped a module that uses too many channels

// One per Maxmod effect channel (the low byte of its handle, minus 1): the
// effect playing there as the game knows it. handle is SFX_NONE when there
// is none. Handles count up (skipping SFX_NONE and those in use), so a stale
// one names a later effect only after 65,535 more have played.
typedef struct {
    Sfx handle;
    mm_sfxhand mm;
    u8 priority;
    u8 volume;
} Slot;
static SERVAL_EWRAM_BSS Slot slots[EFFECT_SLOTS];
static Sfx last_handle;

// The mixer's timing, shared with the VBlank handler.
static volatile bool mixed;   // the half that plays from the next VBlank is mixed
static volatile bool busy;    // the game is inside a call into Maxmod
static volatile bool pending; // ... and the VBlank handler left it this VBlank's mixing

// For tests (serval_mixer_stats, serval_mixer_last), and the check that each
// half is mixed before it plays: EWRAM, touched about once a frame.
static SERVAL_EWRAM_BSS ServalMixerStats stats;
static SERVAL_EWRAM_BSS const s8* last_mixed;

#ifdef SERVAL_DEBUG
enum {
    W_BANK,
    W_MUSIC_ID,
    W_SFX_ID,
    W_SPEED,
    W_SPEED_IDLE,
    W_PITCH,
    W_CHANNELS,
};
static u32 warned;
#define WARN_ONCE(kind, ...)                                                                       \
    do {                                                                                           \
        if (!(warned & (1u << (kind)))) {                                                          \
            warned |= 1u << (kind);                                                                \
            SERVAL_WARN(__VA_ARGS__);                                                              \
        }                                                                                          \
    } while (0)
#else
#define WARN_ONCE(kind, ...) ((void)0)
#endif

// 0-255 to Maxmod's 0-1024: 255 (full) is exactly 1024.
static mm_word volume_1024(u32 volume) {
    return (volume << 2) + ((volume + 1) >> 6);
}

// --- Timing -------------------------------------------------------------------

// mmFrame(): the effects' and the music's ticks, and the next VBlank's samples.
static void mix(void) {
    mmFrame();
    last_mixed = (const s8*)mp_writepos - HALF;
    mixed = true;
    stats.mixes++;
}

// Around every call into Maxmod from the game's side: the VBlank handler
// mixes only while the game is outside Maxmod, and leaves the mixing to the
// call otherwise, which does it before returning (at most a frame's mixing,
// early in the frame: the VBlank that left it has just happened).
static void enter(void) {
    busy = true;
}

static void leave(void) {
    for (;;) {
        u16 ime = REG_IME;
        REG_IME = 0;
        bool mix_now = pending;
        pending = false;
        if (!mix_now)
            busy = false;
        REG_IME = ime;
        if (!mix_now)
            return;
        mix();
        stats.deferred++;
    }
}

// The VBlank handler's part (core.c's handler calls it first, before raster
// effects'): Maxmod's own, then whatever mixing the game can't do in time.
static void vblank(void) {
    mmVBlank();
    // After a VBlank that restarted DMA (mp_mix_seg set) the first half plays,
    // after the others the second.
    const s8* first = (const s8*)wave_buffer;
    const s8* playing = mp_mix_seg ? first : first + HALF;
    if (mixed && last_mixed != playing)
        stats.misplaced++;
    if (!mixed) {
        // The half starting to play now was never mixed. Mix it now, from
        // its start, ahead of the DMA reading it; then point the mixer at the
        // other half (mmVBlank() rewinds the write position to the first
        // half only after the VBlanks that don't restart DMA).
        if (busy) {
            stats.stale++;
        } else {
            mp_writepos = (mm_addr)playing;
            mmFrame();
            mp_writepos = (mm_addr)(mp_mix_seg ? first + HALF : first);
            stats.late++;
            stats.mixes++;
        }
    }
    mixed = false;
    if (!serval_frame_waiting) {
        // The game is still at work (or not calling frame_end() at all):
        // frame_end() can't mix before the next VBlank, so mix now.
        if (busy) {
            pending = true;
        } else {
            mix();
            stats.early++;
        }
    }
}

// frame_end(), after the VBlank flush and the PSG.
static void frame_mix(void) {
    if (!mixed) {
        enter();
        mix();
        leave();
    }
    if (music_failed) {
        music_failed = false;
        WARN_ONCE(W_CHANNELS,
                  "music_play: the module uses more than %d channels, the engine's limit; it "
                  "stopped where it needs more",
                  MODULE_CHANNELS);
    }
}

// Maxmod's song events: a module stopped for using too many channels.
static mm_word song_event(mm_word message, mm_word param) {
    (void)param;
    if (message == MMCB_SONGERROR)
        music_failed = true;
    return 0;
}

ServalMixerStats serval_mixer_stats(void) {
    return stats;
}

const s8* serval_mixer_last(void) {
    return last_mixed;
}

const s8* serval_mixer_wave(void) {
    return (const s8*)wave_buffer;
}

// --- The sound bank -------------------------------------------------------------

// Whether `p` is a GBA sound bank of this Maxmod's format: in memory, aligned,
// the "*maxmod*" mark, and every sample and module where its table says, of
// its kind and of MAS_VERSION.
static bool bank_valid(const msl_head* p) {
    if (!serval_plausible_pointer(p) || ((uintptr_t)p & 3))
        return false;
    static const u8 mark[8] = {'*', 'm', 'a', 'x', 'm', 'o', 'd', '*'};
    const u8* reserved = (const u8*)p->head_data.reserved;
    for (u32 i = 0; i < 8; i++)
        if (reserved[i] != mark[i])
            return false;
    u32 samples = p->head_data.sampleCount;
    u32 count = samples + p->head_data.moduleCount;
    const u32* table = (const u32*)p->sampleTable;
    for (u32 i = 0; i < count; i++) {
        u32 offset = table[i];
        if ((offset & 3) || offset < sizeof(msl_head_data) + 4 * count)
            return false;
        const u8* entry = (const u8*)p + offset;
        if (!serval_plausible_pointer(entry))
            return false;
        const mm_mas_prefix* prefix = (const mm_mas_prefix*)entry;
        u8 type = i < samples ? MAS_TYPE_SAMPLE_GBA : MAS_TYPE_SONG;
        if (prefix->type != type || prefix->version != MAS_VERSION)
            return false;
    }
    return true;
}

// Silences Maxmod and takes it out of the frame loop and the VBlank handler.
static void stop_maxmod(void) {
    serval_mixer_hook = NULL;
    serval_vblank_mixer = NULL;
    serval_vblank_update();
    mmStop();
    mmEffectCancelAll();
    mmMixerEnd();              // Direct Sound off (SOUNDCNT_H = 0), DMA 1 and 2 and timer 0 stopped
    REG_SNDDSCNT = SDS_DMG100; // the tone generators' share back to 100%
}

static const ServalMixerOps maxmod_ops;

void audio_bank_set(const void* new_bank) {
#ifdef SERVAL_DEBUG
    warned = 0;
    serval_mixer_warnings_reset();
#endif
    if (new_bank && !bank_valid(new_bank)) {
        WARN_ONCE(W_BANK, "audio_bank_set: the pointer is not a sound bank built by mmutil for "
                          "the GBA, of this engine's version (serval.json); no bank is "
                          "registered");
        new_bank = NULL;
    }
    if (bank)
        stop_maxmod();
    serval_mixer_ops = NULL;
    bank = new_bank;
    music_on = music_held = false;
    music_failed = false;
    for (u32 i = 0; i < EFFECT_SLOTS; i++)
        slots[i].handle = SFX_NONE;
    if (!bank)
        return;

    memset(module_channels, 0, sizeof(module_channels));
    memset(active_channels, 0, sizeof(active_channels));
    mm_gba_system setup = {
        .mixing_mode = MIX_MODE,
        .mod_channel_count = MODULE_CHANNELS,
        .mix_channel_count = MIXER_CHANNELS,
        .module_channels = module_channels,
        .active_channels = active_channels,
        .mixing_channels = mixer_channels,
        .mixing_memory = mix_buffer,
        .wave_memory = wave_buffer,
        .soundbank = (mm_addr)bank,
    };
    mmInit(&setup); // can't fail: the counts are within its 32
    // mmInit() wrote SOUNDCNT_H whole (0x9A0C: Direct Sound A and B at full
    // volume on timer 0), which sets the tone generators' share to 25%: back
    // to 100%. Reading it doesn't repeat its FIFO resets (they read as 0).
    REG_SNDDSCNT = (u16)((REG_SNDDSCNT & ~SDS_DMG100) | SDS_DMG100);
    mmSetModuleVolume(volume_1024(serval_music_volume()));
    mmSetEffectsVolume(volume_1024(serval_sfx_volume()));
    mmSetEventHandler(song_event);

    // mmInit() cleared the wave buffer and started DMA on its first half;
    // the first VBlank restarts it there, so the first mixing belongs in the
    // second half (mmInit() left the write position on the first, where the
    // mixing would land on the half playing).
    mixed = true;
    last_mixed = (const s8*)wave_buffer;
    mp_writepos = (u8*)wave_buffer + HALF;
    pending = false;
    busy = false;
    serval_mixer_hook = frame_mix;
    serval_vblank_mixer = vblank;
    serval_vblank_update();
    serval_mixer_ops = &maxmod_ops;
}

// --- Tracker music ----------------------------------------------------------------

static void play_module(u16 music_id, bool loop) {
    if (music_id >= mmGetModuleCount()) {
        WARN_ONCE(W_MUSIC_ID,
                  "music_play: the bank has %u modules (MOD_* 0-%d), not %u; nothing "
                  "plays",
                  (unsigned)mmGetModuleCount(), (int)mmGetModuleCount() - 1, music_id);
        return;
    }
    enter();
    mmSetModuleTempo(1024); // speed 100%, before mmStart() reads the module's tempo
    mmStart(music_id, loop ? MM_PLAY_LOOP : MM_PLAY_ONCE);
    leave();
    music_on = true;
    music_held = false;
    music_failed = false;
}

static void stop_module(void) {
    if (!music_on)
        return;
    enter();
    mmStop();
    leave();
    music_on = music_held = false;
}

static bool module_playing(void) {
    // Maxmod's own flag is false while paused, and when a module played once
    // has ended (or stopped for using too many channels).
    if (music_on && !music_held && !mmActive())
        music_on = false;
    return music_on;
}

static void pause_module(void) {
    if (!module_playing() || music_held)
        return;
    enter();
    mmPause(); // its channels hold their place, silent; effects play on
    leave();
    music_held = true;
}

static void resume_module(void) {
    if (!music_held)
        return;
    enter();
    mmResume();
    leave();
    music_held = false;
}

static bool module_paused(void) {
    return music_held;
}

static void module_volume(u8 volume) {
    enter();
    mmSetModuleVolume(volume_1024(volume));
    leave();
}

static void module_speed(u16 percent) {
    if (!module_playing()) {
        WARN_ONCE(W_SPEED_IDLE, "music_set_speed: no module plays (music_play); ignored, and "
                                "music_play() starts at 100%% anyway");
        return;
    }
    if (percent == 0)
        percent = 100;
    if (percent < 50 || percent > 200) {
        WARN_ONCE(W_SPEED, "music_set_speed: %u%% is outside 50-200; playing at %u%%", percent,
                  percent < 50 ? 50u : 200u);
        percent = percent < 50 ? 50 : 200;
    }
    enter();
    mmSetModuleTempo(((u32)percent * 1024 + 50) / 100); // 6.10 fixed point
    leave();
}

// --- Sampled sound effects -----------------------------------------------------------

// The slot of a handle, or NULL for SFX_NONE and stale handles. A slot whose
// effect has ended is freed on the way.
static Slot* find(Sfx sfx) {
    if (sfx == SFX_NONE)
        return NULL;
    for (u32 i = 0; i < EFFECT_SLOTS; i++) {
        Slot* slot = &slots[i];
        if (slot->handle != sfx)
            continue;
        if (mmEffectActive(slot->mm))
            return slot;
        slot->handle = SFX_NONE;
        return NULL;
    }
    return NULL;
}

// The playing effect of lowest priority, the oldest of those, or NULL.
static Slot* lowest(void) {
    Slot* best = NULL;
    for (u32 i = 0; i < EFFECT_SLOTS; i++) {
        Slot* slot = &slots[i];
        if (slot->handle == SFX_NONE)
            continue;
        if (!mmEffectActive(slot->mm)) {
            slot->handle = SFX_NONE;
            continue;
        }
        if (!best || slot->priority < best->priority ||
            (slot->priority == best->priority &&
             (u16)(last_handle - slot->handle) > (u16)(last_handle - best->handle)))
            best = slot;
    }
    return best;
}

static Sfx next_handle(void) {
    for (;;) {
        last_handle++;
        if (last_handle == SFX_NONE)
            continue;
        bool used = false;
        for (u32 i = 0; i < EFFECT_SLOTS; i++)
            used |= slots[i].handle == last_handle;
        if (!used)
            return last_handle;
    }
}

static Sfx effect_play(u16 sfx_id, u8 volume, s8 pan, FIXED pitch, u8 priority) {
    if (sfx_id >= mmGetSampleCount()) {
        WARN_ONCE(W_SFX_ID,
                  "sfx_play: the bank has %u samples (SFX_* 0-%d), not %u; nothing "
                  "plays",
                  (unsigned)mmGetSampleCount(), (int)mmGetSampleCount() - 1, sfx_id);
        return SFX_NONE;
    }
    if (pitch == 0)
        pitch = FX_ONE;
    if (pitch < FX_ONE / 16 || pitch > FX(16)) {
        WARN_ONCE(W_PITCH,
                  "sfx_play_ex: pitch %d/256 is outside FX_ONE / 16 to FX(16) (16 to "
                  "4096); clamped",
                  (int)pitch);
        pitch = pitch < FX_ONE / 16 ? FX_ONE / 16 : FX(16);
    }
    mm_sound_effect effect = {
        .id = sfx_id,
        .rate = (mm_hword)(pitch << 2), // 24.8 to Maxmod's 6.10
        .handle = 0,
        .volume = volume,
        .panning = (mm_byte)(pan + 128),
    };
    enter();
    mm_sfxhand mm = mmEffectEx(&effect);
    if (mm == MM_SFXHAND_INVALID) {
        // No mixer channel free: the playing effect of lowest priority gives
        // way, if this one's is at least as high.
        Slot* victim = lowest();
        if (victim && priority >= victim->priority) {
            mmEffectCancel(victim->mm);
            victim->handle = SFX_NONE;
            mm = mmEffectEx(&effect);
        }
    }
    leave();
    if (mm == MM_SFXHAND_INVALID)
        return SFX_NONE;
    // Maxmod's effect channel was free, so whatever the slot held has ended.
    Slot* slot = &slots[(mm & 0xFF) - 1];
    slot->handle = SFX_NONE;
    slot->handle = next_handle();
    slot->mm = mm;
    slot->priority = priority;
    slot->volume = volume;
    return slot->handle;
}

static void effect_stop(Sfx sfx) {
    Slot* slot = find(sfx);
    if (!slot)
        return;
    enter();
    mmEffectCancel(slot->mm);
    leave();
    slot->handle = SFX_NONE;
}

static bool effect_playing(Sfx sfx) {
    return find(sfx) != NULL;
}

static void effects_stop(void) {
    enter();
    mmEffectCancelAll();
    leave();
    for (u32 i = 0; i < EFFECT_SLOTS; i++)
        slots[i].handle = SFX_NONE;
}

static void effects_volume(u8 volume) {
    enter();
    mmSetEffectsVolume(volume_1024(volume));
    // Maxmod scales an effect's volume when it starts: rescale those playing.
    for (u32 i = 0; i < EFFECT_SLOTS; i++)
        if (slots[i].handle != SFX_NONE)
            mmEffectVolume(slots[i].mm, slots[i].volume);
    leave();
}

// What sampled_audio.c's calls reach, once a bank is registered.
static const ServalMixerOps maxmod_ops = {
    .music_play = play_module,
    .music_stop = stop_module,
    .music_playing = module_playing,
    .music_pause = pause_module,
    .music_resume = resume_module,
    .music_paused = module_paused,
    .music_set_volume = module_volume,
    .music_set_speed = module_speed,
    .sfx_play = effect_play,
    .sfx_stop = effect_stop,
    .sfx_playing = effect_playing,
    .sfx_stop_all = effects_stop,
    .sfx_set_volume = effects_volume,
};
