#ifndef SERVAL_SURFACE_H
#define SERVAL_SURFACE_H

// The pixel surface: a picture the game draws itself, one byte per pixel,
// for what tiles and sprites can't show: a first-person view, filled
// polygons, plasma. The engine gives the pixels, the page flip and the
// hardware's stretch; what is drawn, and how, is the game's (the raycaster
// example casts its rays and draws its columns itself). See docs/surface.md.
//
// The surface is SURFACE_W x SURFACE_H pixels (the screen's size), each a
// color number: 0 is transparent (the backdrop shows) and 1-255 are colors
// set with surface_set_colors(). There are two pages: one is on screen, the
// game draws into the other (surface_pixels()), and surface_flip() swaps
// them at the next frame_end(), so a picture is never seen half drawn, and
// may take several frames to draw.
//
// While the surface is on, it is the screen's only background: there is no
// text layer and there are no map layers (text.h's and map.h's calls do
// nothing and warn in debug builds). Sprites draw over it as usual, from
// half their tiles (512 instead of 1,024), and brightness fades, alpha
// blending (the surface is LAYER_PLAYFIELD) and the backdrop work as ever. A
// game shows its menus with text, then turns the surface on to play.
//
// GBA: display mode 4, the 8-bit bitmap mode, whose two pages take all of
// the backgrounds' video memory and the first half of the sprites' (which is
// why text, maps and half the sprite tiles go), on background 2, an affine
// background (surface_set_transform()). The web build draws the same.

#include "serval/fixed.h"
#include "serval/platform.h"
#include "serval/screen.h"

// The surface's size in pixels: the screen's.
#define SURFACE_W 240
#define SURFACE_H 160

// Turns the surface on: from now on the screen shows it (both pages start
// transparent, so at first the backdrop), with the identity transform and
// the colors the background palette holds. Returns false, changing nothing,
// while a map layer is loaded: map_unload() them first (warning in debug
// builds). Returns true if the surface is on already (ignored, with a
// warning).
//
// It takes over the video memory of the text layer, the tileset and the map
// layers, and of the first 512 sprite tiles, so it also turns the text layer
// off and unloads every sprite group, as sprite_groups_reset() does: load
// the sprite groups to show over the surface after it (512 tiles are left
// for them). Takes effect at once, not at the next frame_end(): what the
// screen showed is gone from the line being drawn on, so call it behind a
// fade (screen_set_brightness()), or accept one cut frame. Clearing the
// pages takes about a quarter of a frame (66,400 CPU cycles on the GBA).
bool surface_begin(void);

// Turns the surface off: the screen goes back to tiled backgrounds, showing
// only the backdrop until the game sets them up again. Unloads every sprite
// group, as surface_begin() does (all 1,024 tiles are theirs again). The
// video memory the surface used is left as it was, so load the tileset again
// before a map layer (tileset_load(), map_load()); the text layer sets
// itself up again at the next text call. At once, like surface_begin().
// Ignored (warning in debug builds) while the surface is off.
void surface_end(void);

// The page to draw into: SURFACE_W x SURFACE_H bytes, row after row from
// the top, so pixel (x, y) is byte y * SURFACE_W + x. It is the page that
// isn't on screen, so it may be written at any time, over as many frames as
// the picture takes. Each frame_end() after a surface_flip() makes it the
// other page: ask again every frame rather than keeping the pointer. After a
// flip the page holds the picture from two flips ago, not the one on screen.
// NULL (warning in debug builds) while the surface is off.
//
// Write it 2 or 4 pixels at a time (u16 or u32 stores at even addresses),
// never single bytes: the GBA's video memory stores 16 bits at a time, and
// writing one byte there sets the byte beside it to the same value. (The
// web build would store the single byte, so such a bug shows only on the
// GBA.) To draw single pixels, read the pair, change one and write the pair
// back, or draw in a buffer of the game's own in RAM and copy that in words.
// Reading is fine at any size.
u8* surface_pixels(void);

// Shows the page being drawn at the next frame_end(), in VBlank, and makes
// the one on screen the page to draw (surface_pixels() changes then, not at
// this call). Several calls in a frame are one flip. Without it frame_end()
// keeps showing the same page, so a picture may take several frames: draw,
// frame_end(), draw on, then flip. Ignored (warning in debug builds) while
// the surface is off.
void surface_flip(void);

// Fills the page being drawn with one color number (0: transparent). About
// 25,400 CPU cycles on the GBA (9% of a frame). Ignored (warning in debug
// builds) while the surface is off.
void surface_clear(u8 color);

// Sets `count` of the surface's 256 colors from color `index` on. Like the
// other palette writes (sprite_set_colors(), tileset_set_colors()), they
// change at the next frame_end(), in VBlank, with the flip made in the same
// frame. Color 0 is the backdrop (transparent pixels show it), as
// screen_set_backdrop() sets it. The colors are the background palette's:
// surface_begin() leaves in them what the tileset and the text layer had
// put there, and the text layer writes its own (241-248) again when it is
// next set up after surface_end(). Colors past 255 or a NULL table change
// nothing (warning in debug builds), and so does a call while the surface is
// off.
void surface_set_colors(u32 index, const Color* colors, u32 count);

// How the screen shows the page: the screen pixel (sx, sy) shows the surface
// pixel
//     (x0 + xx * sx + xy * sy,  y0 + yx * sx + yy * sy)
// rounded down, or the backdrop where that lies outside the surface. All six
// are 24.8 fixed point (fixed.h). The default, set by surface_begin(), is the
// identity (0, 0, FX(1), 0, 0, FX(1)): the screen shows the surface pixel
// for pixel. The hardware does it as it draws, at no cost in CPU time, so a
// game can draw fewer pixels and let the screen stretch them:
//
//     // The top-left 120 x 80 pixels, doubled to fill the screen.
//     surface_set_transform(0, 0, FX(1) / 2, 0, 0, FX(1) / 2);
//     // Screen columns as surface rows: a column of the view is 160
//     // bytes in a row, drawn in words; 120 columns, doubled in width.
//     surface_set_transform(0, 0, 0, FX(1), FX(1) / 2, 0);
//
// or scroll, zoom and rotate the picture. Applied at the next frame_end(),
// in VBlank, with the flip made in the same frame. xx, xy, yx and yy must lie
// within -128 to just under 128, and x0 and y0 within -524,288 to just under
// 524,288 (the hardware's registers; others are clamped, with a warning in
// debug builds). Ignored (warning in debug builds) while the surface is off.
void surface_set_transform(FIXED x0, FIXED y0, FIXED xx, FIXED xy, FIXED yx, FIXED yy);

#endif // SERVAL_SURFACE_H
