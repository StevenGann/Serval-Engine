#ifndef SERVAL_GBA_INTERNAL_H
#define SERVAL_GBA_INTERNAL_H

// Engine-internal state shared between GBA modules. Not part of the public API.

#include <tonc_oam.h>

#include "serval/audio.h"
#include "serval/sprites.h"
#include <tonc_types.h>

// Code generation for the GBA backend's hot paths: ARM code (for helpers
// inlined into IWRAM functions) and placement in IWRAM. Empty in web builds,
// which compile this backend for WebAssembly (src/web/).
#ifdef SERVAL_GBA
#define SERVAL_ARM __attribute__((target("arm")))
#define SERVAL_IWRAM_TEXT __attribute__((section(".iwram.text"), target("arm")))
#else
#define SERVAL_ARM
#define SERVAL_IWRAM_TEXT
#endif

// The shadow OAM: rebuilt from draw calls every frame and copied to OAM in
// VBlank by frame_end(). serval_oam_used counts this frame's entries.
extern OBJ_ATTR serval_shadow_oam[128];
extern u32 serval_oam_used;
// Rotation matrices used this frame. The 32 matrices live in the shadow OAM's
// otherwise unused fourth halfwords (OBJ_AFFINE overlay), copied with it.
extern u32 serval_matrices_used;
// Draws lost this frame because all 128 hardware sprites were used, and
// rotated or scaled draws drawn plain because all 32 matrices were; with the
// two above, saved by frame_end() for sprite_stats().
extern u32 serval_sprites_dropped;
extern u32 serval_sprites_untransformed;
extern SpriteStats serval_sprite_stats;
// sprite_stats_scanlines(): whether frame_end() fills in cut_short and
// busiest_line, and the function that does it, from the shadow OAM.
extern bool serval_scanline_stats;
void serval_count_scanlines(SpriteStats* stats);

// PSG sound effects (psg.c): set up by serval_init(), advanced once per frame
// by frame_end().
void serval_psg_init(void);
void serval_psg_update(void);
// Text layer (text.c), for the splash screen: whether the game's text layer
// is set up, switching it off again, and printing through any BG palette bank.
bool serval_text_active(void);
void serval_text_deactivate(void);
void serval_text_print_bank(int col, int row, const char* s, u32 palbank);

// Plays a sound that isn't in the game's sound table (whatever the
// priorities), and ends the sound on one channel (music using it comes back).
void serval_psg_play_sound(const PsgSound* sound);
void serval_psg_silence(u32 channel);

// For music (music.c): register writes for a tone (sweep off; control bits
// from serval_psg_control, volume 0-15 as is) and for silence, and whether a
// sound effect holds a channel.
u16 serval_psg_control(u32 channel, u32 duty, u32 volume, s32 fade);
void serval_psg_tone(u32 channel, u16 control, u16 rate);
void serval_psg_quiet(u32 channel);
bool serval_psg_sfx_active(u32 channel);

// The music player, hooked in by psg_music_play() so that games without music
// don't link it. serval_psg_update() calls update() after stepping the sound
// effects; resume(channel) when a sound effect ends on a channel in
// serval_music_channels (the channels whose track still plays, bits
// 1 << PSG_*); psg_stop_all() calls stop(). Sound effects below
// serval_music_priority don't play on the music's channels.
typedef struct {
    void (*update)(void);
    void (*resume)(u32 channel);
    void (*stop)(void);
} ServalMusicHooks;
extern const ServalMusicHooks* serval_music_hooks;
extern u8 serval_music_channels;
extern u8 serval_music_priority;

// Map layers (map.c): frame_end() calls these, once a map layer has been
// loaded, before waiting for VBlank (to update the screenblock copies) and in
// VBlank (to copy them to VRAM). NULL until then, so games without maps don't
// link the map code.
extern void (*serval_map_prepare_hook)(void);
extern void (*serval_map_commit_hook)(void);
void serval_map_prepare(void);
void serval_map_commit(void);
// The scroll register values last written for background bg (1-3), x in the
// low halfword and y in the high one (for tests: they are write-only).
u32 serval_map_scroll(u32 bg);

#ifdef SERVAL_WEB
// Web builds: real time in GBA CPU cycles, standing in for the timers that
// count cycles on the GBA (src/web/platform.c).
u32 serval_web_cycles(void);
#endif

// The frequency register value last written for a PSG channel (for tests:
// the hardware's square frequency bits are write-only).
u16 serval_psg_rate(u32 channel);

#endif // SERVAL_GBA_INTERNAL_H
