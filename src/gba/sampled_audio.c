// Tracker music and sampled sound effects (audio.h, docs/audio.md): the calls
// games and scripts make, on the GBA. Each checks for a sound bank and goes on
// to Maxmod (maxmod.c) through serval_mixer_ops, which audio_bank_set() sets:
// a game that calls them without ever registering a bank (every scripted
// game reaches them through the VM's SYS calls) links none of Maxmod and none
// of its IWRAM. Without a bank they play nothing (music_play() and sfx_play()
// warn once); the volumes set before a bank are kept for it.

#include "serval/audio.h"
#include "serval/fixed.h"

#include "../core/warn.h"
#include "internal.h"

const ServalMixerOps* serval_mixer_ops;
// 0-255; 255, the default, leaves the module's and each effect's own. Kept
// inverted, so that zeroed memory is the default.
static u8 music_volume_inv;
static u8 sfx_volume_inv;

#ifdef SERVAL_DEBUG
enum { W_NO_BANK = 1 << 0, W_NO_MUSIC = 1 << 1 };
static u32 warned;

void serval_mixer_warnings_reset(void) {
    warned = 0;
}

static void warn_once(u32 kind, const char* function) {
    if (warned & kind)
        return;
    warned |= kind;
    if (kind == W_NO_BANK)
        SERVAL_WARN("%s: no sound bank is registered (audio_bank_set); nothing plays", function);
    else
        SERVAL_WARN("%s: no module plays (music_play); ignored, and music_play() starts at 100%% "
                    "anyway",
                    function);
}
#define WARN_ONCE(kind, function) warn_once(kind, function)
#else
#define WARN_ONCE(kind, function) ((void)0)
#endif

u8 serval_music_volume(void) {
    return (u8)(255 - music_volume_inv);
}

u8 serval_sfx_volume(void) {
    return (u8)(255 - sfx_volume_inv);
}

void music_play(u16 music_id, bool loop) {
    if (serval_mixer_ops)
        serval_mixer_ops->music_play(music_id, loop);
    else
        WARN_ONCE(W_NO_BANK, "music_play");
}

void music_stop(void) {
    if (serval_mixer_ops)
        serval_mixer_ops->music_stop();
}

bool music_playing(void) {
    return serval_mixer_ops && serval_mixer_ops->music_playing();
}

void music_pause(void) {
    if (serval_mixer_ops)
        serval_mixer_ops->music_pause();
}

void music_resume(void) {
    if (serval_mixer_ops)
        serval_mixer_ops->music_resume();
}

bool music_paused(void) {
    return serval_mixer_ops && serval_mixer_ops->music_paused();
}

void music_set_volume(u8 volume) {
    music_volume_inv = (u8)(255 - volume);
    if (serval_mixer_ops)
        serval_mixer_ops->music_set_volume(volume);
}

void music_set_speed(u16 percent) {
    if (serval_mixer_ops)
        serval_mixer_ops->music_set_speed(percent);
    else
        WARN_ONCE(W_NO_MUSIC, "music_set_speed");
}

Sfx sfx_play(u16 sfx_id) {
    return sfx_play_ex(sfx_id, 255, 0, FX_ONE, 0);
}

Sfx sfx_play_ex(u16 sfx_id, u8 volume, s8 pan, FIXED pitch, u8 priority) {
    if (serval_mixer_ops)
        return serval_mixer_ops->sfx_play(sfx_id, volume, pan, pitch, priority);
    WARN_ONCE(W_NO_BANK, "sfx_play");
    return SFX_NONE;
}

void sfx_stop(Sfx sfx) {
    if (serval_mixer_ops)
        serval_mixer_ops->sfx_stop(sfx);
}

bool sfx_playing(Sfx sfx) {
    return serval_mixer_ops && serval_mixer_ops->sfx_playing(sfx);
}

void sfx_stop_all(void) {
    if (serval_mixer_ops)
        serval_mixer_ops->sfx_stop_all();
}

void sfx_set_volume(u8 volume) {
    sfx_volume_inv = (u8)(255 - volume);
    if (serval_mixer_ops)
        serval_mixer_ops->sfx_set_volume(volume);
}
