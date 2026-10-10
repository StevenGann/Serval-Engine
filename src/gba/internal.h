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

// streaming: streamed sprite groups (sprite_stream.c,
// docs/sprites.md#residency-modes). Their slots are taken from the top of
// sprite VRAM down to serval_stream_floor() (1024 with none), where the groups
// loaded from tile 0 up must stop. serval_stream_add() takes `slots` slots of
// `slot_tiles` tiles for a group loaded in mark segment `segment` and returns
// its index, or -1 (warns) past 256 slots in all; the caller has checked
// that they fit in VRAM and that a palette bank was free (so there are at
// most 16 such groups). serval_stream_slot() returns the first
// tile of the slot holding `key` (sprite ID | frame << 16) for this frame:
// the slot it is in already, or the least recently drawn one not drawn from
// this frame, with its `tiles` tiles from `from` queued for VBlank; -1 (counted
// in serval_sprites_dropped, warns) when every slot holds a frame drawn this
// frame. serval_stream_release() unloads the groups of segment `segment` and
// up with their queued copies, serval_stream_reset() all of them, and
// serval_stream_commit() is frame_end()'s VBlank step: the queued copies, then
// the next frame.
u32 serval_stream_floor(void);
int serval_stream_add(u32 slots, u32 slot_tiles, u32 segment);
int serval_stream_slot(u32 group, u32 key, const u32* from, u32 tiles);
void serval_stream_release(u32 segment);
void serval_stream_reset(void);
void serval_stream_commit(void);

// sprite tiles: runtime sprite tiles (sprite_tiles.c). frame_end() calls
// serval_sprite_tiles_commit() in VBlank to copy the frames sprite_set_tiles()
// queued (step 3 of the flush, docs/frame-loop.md). sprite_groups_reset()
// calls serval_sprite_tiles_reset() (drops every queued copy) and
// sprite_groups_release() serval_sprite_tiles_release(first_tile) (drops
// those to OBJ VRAM tile first_tile and up: the tiles it frees).
void serval_sprite_tiles_commit(void);
void serval_sprite_tiles_reset(void);
void serval_sprite_tiles_release(u32 first_tile);
// sprite tiles: what a sprite ID is (sprites.c's draw records), for
// sprite_set_tiles(): an ordinary sprite has frame_count frames of
// tiles_per_frame tiles each, frame 0's first at OBJ VRAM tile first_tile.
enum {
    SERVAL_SPRITE_ABSENT,   // not loaded (or not in the sprite table)
    SERVAL_SPRITE_ORDINARY, // loaded, with tiles of its own
    SERVAL_SPRITE_META,     // a loaded metasprite: no tiles of its own
    SERVAL_SPRITE_STREAMED, // a sprite of a loaded streamed group
};
typedef struct {
    u16 first_tile;
    u8 tiles_per_frame;
    u8 frame_count;
    u8 kind; // SERVAL_SPRITE_*
} ServalSpriteFrames;
ServalSpriteFrames serval_sprite_frames(u32 id);

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

// wave channel: PSG channel 3 (wave.c), which psg.c drives for PSG_WAVE.
// serval_wave_tone() starts a note: control as from serval_psg_control() (the
// volume and fade, and the waveform's number in bits 0-7), rate the frequency
// register value; serval_wave_quiet() stops the channel; serval_wave_update()
// steps the note's fade, once a frame (serval_psg_update) while
// serval_wave_fading is set. For tests: serval_wave_bank(), the wave RAM bank
// playing.
void serval_wave_tone(u16 control, u16 rate);
void serval_wave_quiet(void);
void serval_wave_update(void);
extern u8 serval_wave_fading;
u32 serval_wave_bank(void);

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

// palettes: palette writes (palette.c), set by the first sprite_set_colors()
// or tileset_set_colors() call, so games without palette writes don't link
// them; NULL until then. flush(), in VBlank (frame_end), copies the banks
// written since the last frame to palette RAM. overwritten(obj, index,
// count): something has just written `count` colors of palette RAM directly,
// from color `index` of the sprite palettes (obj) or the background's on
// (sprite_group_load, tileset_load, screen_set_backdrop), so those colors win
// over palette writes made before it in the frame.
typedef struct {
    void (*flush)(void);
    void (*overwritten)(bool obj, u32 index, u32 count);
} ServalPaletteHooks;
extern const ServalPaletteHooks* serval_palette_hooks;
// palettes: the palette banks of the group sprite `id` was loaded with, for
// sprite_set_colors() (sprites.c): returns the group's palette count and sets
// *first_bank, or returns 0 if the sprite isn't loaded, -1 for a metasprite.
int serval_sprite_palettes(u32 id, u32* first_bank);
// raster: raster effects (raster.c), hooked into frame_end() by the first
// raster_scroll() or raster_backdrop(): prepare() before VBlank, and before
// the map's (it sets serval_map_span), commit() in VBlank, after the map's.
extern void (*serval_raster_prepare_hook)(void);
extern void (*serval_raster_commit_hook)(void);
// raster: how far past the screen's the layer pixels raster_scroll()'s lines
// show reach on background bg (index 1-3): from x sx + x_lo to sx + 239 + x_hi
// and from y sy + y_lo to sy + 159 + y_hi, (sx, sy) being the layer's scroll.
// The map streaming keeps them in VRAM (map.c), reading a background's span
// only if its bit (1 << bg) is in serval_map_spans, which is 0 without the
// effect: map games without it pay one test per background.
typedef struct {
    int x_lo, x_hi, y_lo, y_hi;
} ServalMapSpan;
extern ServalMapSpan serval_map_span[4];
extern u32 serval_map_spans;
// raster: the backdrop color last set, by screen_set_backdrop() (core.c) or
// as color 0 by tileset_set_colors() (palette.c), which raster_backdrop()'s
// end puts back; and whether raster_backdrop() is on (then
// screen_set_backdrop() only remembers its color, and a palette write's
// color 0 is overwritten in VBlank by line 0's).
extern Color serval_backdrop;
extern bool serval_backdrop_raster;
// raster: for tests: the SCREEN_H + 1 values DMA 0 copies in this frame
// (entry y for line y; frame_end() writes entry 0 itself), or NULL.
const u16* serval_raster_lines(void);

#ifdef SERVAL_WEB
// Web builds: real time in GBA CPU cycles, standing in for the timers that
// count cycles on the GBA (src/web/platform.c).
u32 serval_web_cycles(void);
#endif

// The frequency register value last written for a PSG channel (for tests:
// the hardware's square frequency bits are write-only).
u16 serval_psg_rate(u32 channel);

#endif // SERVAL_GBA_INTERNAL_H
