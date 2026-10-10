// Tracker music and sampled sound effects (audio.h, docs/audio.md) on the
// web: silent stubs, until the web has a player of its own
// (docs/audio.md#web). The GBA plays them with Maxmod (src/gba/
// sampled_audio.c), whose mixer is ARM assembly feeding Direct Sound's FIFOs
// by DMA, none of which the web emulates.
//
// A stub does nothing harmful: it keeps no state, returns false or SFX_NONE,
// and in debug builds warns on its first call (in the browser console),
// saying what happens instead. The PSG plays as on the GBA.

#include "serval/audio.h"

#include "../core/warn.h"

#ifdef SERVAL_DEBUG
// One bit per stub: each warns on its first call only, not every frame a game
// calls it.
enum {
    STUB_AUDIO_BANK_SET = 1 << 0,
    STUB_MUSIC_PLAY = 1 << 1,
    STUB_MUSIC_STOP = 1 << 2,
    STUB_MUSIC_PLAYING = 1 << 3,
    STUB_MUSIC_PAUSE = 1 << 4,
    STUB_MUSIC_RESUME = 1 << 5,
    STUB_MUSIC_PAUSED = 1 << 6,
    STUB_MUSIC_SET_VOLUME = 1 << 7,
    STUB_MUSIC_SET_SPEED = 1 << 8,
    STUB_SFX_PLAY = 1 << 9,
    STUB_SFX_PLAY_EX = 1 << 10,
    STUB_SFX_STOP = 1 << 11,
    STUB_SFX_PLAYING = 1 << 12,
    STUB_SFX_STOP_ALL = 1 << 13,
    STUB_SFX_SET_VOLUME = 1 << 14,
};
static u32 warned;

static void web_stub(u32 stub, const char* message) {
    if (warned & stub)
        return;
    warned |= stub;
    SERVAL_WARN("%s", message);
}
#define WEB_STUB(stub, message) web_stub(stub, message)
#else
#define WEB_STUB(stub, message) ((void)0)
#endif

// The warnings' common parts. Each whole message stays within 247 characters
// (SERVAL_WARN_MAX - 1, warn.h), or it is cut off.
#define MUSIC ": tracker music doesn't play on the web yet (docs/audio.md#web); "
#define SFX ": sampled sound effects don't play on the web yet (docs/audio.md#web); "

void audio_bank_set(const void* bank) {
    (void)bank;
    WEB_STUB(STUB_AUDIO_BANK_SET, "audio_bank_set: tracker music and sampled sound effects don't "
                                  "play on the web yet (docs/audio.md#web); the bank is ignored");
}

void music_play(u16 music_id, bool loop) {
    (void)music_id;
    (void)loop;
    WEB_STUB(STUB_MUSIC_PLAY, "music_play" MUSIC "nothing plays");
}

void music_stop(void) {
    WEB_STUB(STUB_MUSIC_STOP, "music_stop" MUSIC "nothing plays to stop");
}

bool music_playing(void) {
    WEB_STUB(STUB_MUSIC_PLAYING, "music_playing" MUSIC "it returns false");
    return false;
}

void music_pause(void) {
    WEB_STUB(STUB_MUSIC_PAUSE, "music_pause" MUSIC "nothing plays to pause");
}

void music_resume(void) {
    WEB_STUB(STUB_MUSIC_RESUME, "music_resume" MUSIC "nothing is paused");
}

bool music_paused(void) {
    WEB_STUB(STUB_MUSIC_PAUSED, "music_paused" MUSIC "it returns false");
    return false;
}

void music_set_volume(u8 volume) {
    (void)volume;
    WEB_STUB(STUB_MUSIC_SET_VOLUME, "music_set_volume" MUSIC "the volume is ignored");
}

void music_set_speed(u16 percent) {
    (void)percent;
    WEB_STUB(STUB_MUSIC_SET_SPEED, "music_set_speed" MUSIC "the speed is ignored");
}

Sfx sfx_play(u16 sfx_id) {
    (void)sfx_id;
    WEB_STUB(STUB_SFX_PLAY, "sfx_play" SFX "nothing plays");
    return SFX_NONE;
}

Sfx sfx_play_ex(u16 sfx_id, u8 volume, s8 pan, FIXED pitch, u8 priority) {
    (void)sfx_id;
    (void)volume;
    (void)pan;
    (void)pitch;
    (void)priority;
    WEB_STUB(STUB_SFX_PLAY_EX, "sfx_play_ex" SFX "nothing plays");
    return SFX_NONE;
}

void sfx_stop(Sfx sfx) {
    (void)sfx;
    WEB_STUB(STUB_SFX_STOP, "sfx_stop" SFX "nothing plays to stop");
}

bool sfx_playing(Sfx sfx) {
    (void)sfx;
    WEB_STUB(STUB_SFX_PLAYING, "sfx_playing" SFX "it returns false");
    return false;
}

void sfx_stop_all(void) {
    WEB_STUB(STUB_SFX_STOP_ALL, "sfx_stop_all" SFX "nothing plays to stop");
}

void sfx_set_volume(u8 volume) {
    (void)volume;
    WEB_STUB(STUB_SFX_SET_VOLUME, "sfx_set_volume" SFX "the volume is ignored");
}
