#ifndef SERVAL_SCREEN_H
#define SERVAL_SCREEN_H

// Screen size, colors and screen-wide effects: the backdrop, brightness
// fades, color mixing, and (planned) alpha blending and raster effects. See
// docs/runtime-systems.md#special-effects.

#include "serval/platform.h"

// A color in the target's native format: on the GBA, BGR555 (red in bits
// 0-4, green in bits 5-9, blue in bits 10-14, each 0-31; bit 15 unused).
typedef u16 Color;

// Builds a Color from 8-bit components (0-255), dropping precision the target
// cannot show. Usable in static initializers, e.g. palettes.
#define COLOR_RGB(r, g, b) ((Color)(((r) >> 3) | (((g) >> 3) << 5) | (((b) >> 3) << 10)))

// Mixes two colors: `amount` 0 gives a, 256 gives b, and the amounts between
// go from a to b in even steps (128 is halfway). Red, green and blue are
// mixed separately, each as (a x (256 - amount) + b x amount) / 256, rounded
// down, so color_mix(a, b, t) == color_mix(b, a, 256 - t). The unused bit 15
// is 0 in the result. Amounts past 256 are clamped to 256 (warning in debug
// builds). For palette effects, e.g. a palette faded toward black, white or a
// dusk tint by mixing each of its ROM colors (sprite_set_colors() and
// tileset_set_colors() will write the result), or a flash of the backdrop
// (screen_set_backdrop). At a multiple of 16 it matches the hardware: it is
// what alpha blending (screen_set_blend) shows for a over b with weights
// 16 - amount / 16 and amount / 16, and with b white, what
// screen_set_brightness(amount / 16) does to a. Costs about 140 cycles per
// color on the GBA, so mixing a whole 256-color palette takes about 13% of a
// frame: fade the banks that need it.
Color color_mix(Color a, Color b, u32 amount);

// Screen size in pixels, as constants for array sizes, static initializers and
// case labels. (libtonc's SCREEN_WIDTH is the same value under another name.)
#define SCREEN_W 240
#define SCREEN_H 160

static inline int screen_width(void) {
    return SCREEN_W;
}

static inline int screen_height(void) {
    return SCREEN_H;
}

// Sets the color shown wherever nothing else is drawn (the backdrop: GBA
// background palette color 0). Takes effect at once. raster_backdrop()
// (planned) replaces it line by line while set.
void screen_set_backdrop(Color color);

// Brightness of the whole screen (backgrounds, sprites and backdrop): from
// SCREEN_BRIGHTNESS_MIN (-16, black) through 0 (normal, the default) to
// SCREEN_BRIGHTNESS_MAX (16, white); values outside are clamped (warning in
// debug builds). Stays until changed. To fade, step it once per frame, e.g.
// from 0 to -16 over 16 frames. Takes effect at once, like
// screen_set_backdrop(): set it before the frame's game logic (right after
// frame_begin()) so the change lines up with the next frame.
//
// It uses the hardware's color special effect (GBA: BLDCNT and BLDY), which
// does one effect at a time, and alpha blending (screen_set_blend and
// SPRITE_BLEND sprites, planned) needs it too. So while the brightness is not
// 0, blending pauses: everything is drawn opaque, then faded. It resumes, with
// the settings last given to screen_set_blend(), once the brightness is back
// at 0. To fade a scene and keep its blending, fade its palettes with
// color_mix() instead. The splash screen (serval_splash) borrows the effect
// and puts the game's settings back.
#define SCREEN_BRIGHTNESS_MIN (-16)
#define SCREEN_BRIGHTNESS_MAX 16
void screen_set_brightness(int level);

// --- Alpha blending (planned) -------------------------------------------------

// Screen layers for screen_set_blend(), combinable with |: the four
// backgrounds, named after their default roles (docs/tilemaps.md#default-layer-roles;
// each bit stands for its background whatever it shows), all sprites as one
// layer, and the backdrop. The values are the hardware's (GBA: BLDCNT's
// target bits; libtonc's LAYER_BG0 ... LAYER_BD are other names for them).
#define LAYER_HUD (1 << 0)        // background 0: the text layer (text.h)
#define LAYER_FOREGROUND (1 << 1) // background 1: in front of sprites
#define LAYER_PLAYFIELD (1 << 2)  // background 2: the playfield
#define LAYER_BACKGROUND (1 << 3) // background 3: the parallax background
#define LAYER_SPRITES (1 << 4)    // every sprite
#define LAYER_BACKDROP (1 << 5)   // the backdrop color (screen_set_backdrop)
#define LAYER_ALL 0x3F

// Planned: alpha blending, which makes layers see-through. Where a pixel of
// one of the `top` layers (LAYER_* bits) is in front of a pixel of one of the
// `bottom` layers, the screen shows, for each 5-bit channel (red, green,
// blue), min(31, (top x top_weight + bottom x bottom_weight) / 16), rounded
// down. Weights go from 0 to 16 (16 is the whole color); larger ones are
// clamped to 16 (warning in debug builds). Weights summing to 16 mix (8 and
// 8: half and half; color_mix() computes the same); weights summing past 16
// brighten (16 and 16 adds the colors: glows, light beams). A see-through
// foreground:
//     screen_set_blend(LAYER_FOREGROUND, LAYER_ALL & ~LAYER_FOREGROUND, 8, 8);
//
// Only the frontmost pixel blends, with the pixel directly behind it, and
// only if that one's layer is in `bottom`; otherwise the front pixel is drawn
// opaque. A layer can be in both masks. Sprites are one layer here, so a
// sprite never blends with a sprite behind it: where two overlap, the front
// one blends with the layer behind both, and the sprite behind doesn't show.
// Sprites drawn with SPRITE_BLEND (sprites.h) blend over the `bottom` layers
// with these weights whether or not `top` has LAYER_SPRITES; with `top` 0,
// only they blend (shadows, ghosts). With `bottom` 0 nothing blends, so
// screen_set_blend(0, 0, 0, 0), the default, is off. Bits outside LAYER_ALL
// are ignored (warning in debug builds). Applied at the next frame_end(), in
// VBlank, and kept until changed.
//
// Blending shares the hardware's one color effect with the brightness: while
// screen_set_brightness() is not 0, blending pauses (everything, SPRITE_BLEND
// sprites included, is drawn opaque, then faded) and these settings come back
// when the brightness returns to 0. Once implemented, the web build draws it
// as the GBA does (its renderer already has the hardware's blending). Until
// then, it does nothing (everything stays opaque) and warns once in debug
// builds.
SERVAL_PLANNED("alpha blending, docs/runtime-systems.md#alpha-blending")
void screen_set_blend(u32 top, u32 bottom, u32 top_weight, u32 bottom_weight);

// --- Raster effects (planned) -------------------------------------------------
//
// A value per scanline. The hardware draws the screen one line at a time, and
// a raster effect changes one setting between lines, so each of the SCREEN_H
// lines can scroll a background differently (a rippling lake, heat haze,
// bands of a sky moving at different speeds) or have its own backdrop color
// (a sky gradient). The game passes a table of SCREEN_H values, entry 0 for
// the top line. The engine reads the table at every frame_end() and doesn't
// copy it at the call, so it must stay valid while the effect is set (static
// or const data, not a local array), and changing its values between frames
// animates the effect. Each frame_end() applies the table as it is then to
// the frame drawn next, all of it, never from halfway down a frame. One
// effect at a time: setting another replaces the one set. A new effect,
// and raster_clear(), take effect at the next frame_end() too.
//
// GBA: DMA channel 0, started by each horizontal blank (HBlank DMA). DMA 0
// can read only internal memory, not ROM, so it never reads the game's table:
// each frame_end() copies the table into a buffer in RAM, for both effects
// (raster_scroll adds the layer's scroll as it copies), and a table in ROM
// works like one in RAM. DMA 0 and the HBlank interrupt are reserved for
// raster effects: games must not use them, also before this is implemented
// (docs/core-api.md). The web build will apply the table line by line as it
// draws (it draws each frame at once, from the state at VBlank, so it needs
// the per-line values rather than the hardware's mid-frame writes).
// Until implemented, these functions do nothing and warn once each in debug
// builds: layers scroll as a whole and the backdrop is one color.

// Planned: scrolls each line of background bg's map layer (1-3) by its own
// offset, added to the layer's scroll position (sx, sy): the camera times the
// layer's scroll_factor plus its map_set_scroll() offset, or the offset alone
// on a MAP_LAYER_FIXED layer (map.h). Screen line y shows the layer from
// layer pixel (sx + offsets[y], sy + y) on, or with `vertical`, from
// (sx, sy + y + offsets[y]). E.g. a ripple: a few pixels of a sine wave,
// moved along every frame. On the playfield (background 2) only the picture
// moves: collision, entities and the camera stay where they are.
//
// VRAM holds a little more of a layer than the screen shows (the hardware's
// 256x256-pixel background, streamed around (sx, sy)), so one frame's
// offsets must lie within 9 pixels of each other horizontally (e.g. -4 to 4)
// and within 89 vertically. The exception is an axis on which the layer
// wraps (MAP_LAYER_WRAP) and its map is 16, 8, 4, 2 or 1 metatiles long:
// VRAM then holds all of it, so any offsets work (sky bands of a repeating
// strip). Offsets spread further show wrong tiles at the edges of the lines
// furthest out (warning in debug builds). The effect belongs to the
// background: it stays when the layer there is unloaded or replaced.
// Ignored (warning in debug builds) for a background outside 1-3 or a NULL
// table. The table can be const data in ROM: frame_end() copies it to RAM.
SERVAL_PLANNED("raster effects, docs/runtime-systems.md#raster-effects")
void raster_scroll(u32 bg, bool vertical, const s16* offsets);

// Planned: gives each screen line y its own backdrop color, colors[y],
// instead of screen_set_backdrop()'s one color (e.g. a sky gradient behind
// the layers). When the effect ends, the backdrop is screen_set_backdrop()'s
// color again: the one last set, also if set while the effect was on.
// Ignored (warning in debug builds) for a NULL table. The table can be const
// data in ROM: frame_end() copies it to RAM.
SERVAL_PLANNED("raster effects, docs/runtime-systems.md#raster-effects")
void raster_backdrop(const Color* colors);

// Planned: ends the raster effect set, if any, at the next frame_end().
SERVAL_PLANNED("raster effects, docs/runtime-systems.md#raster-effects")
void raster_clear(void);

#endif // SERVAL_SCREEN_H
