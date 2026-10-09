// The Effects example: one scene per screen effect, each in its own file,
// shown one at a time by main.c (which documents what each scene shows).

#ifndef EFFECTS_H
#define EFFECTS_H

#include "serval/serval.h"

// --- Scenes --------------------------------------------------------------------
//
// main.c shows one scene at a time; L and R switch to the previous and the
// next, fading the screen out and in with screen_set_brightness() while the
// scene keeps running (its update() is called during the fades too).
//
// Before a scene's enter() and after its leave(), main.c resets what every
// scene shares: the sprite table (sprite_table_set(NULL, 0), which unloads
// every sprite group), the map layers on backgrounds 1-3 (map_unload(), which
// also forgets their map_set_scroll() offsets), the camera (0, 0), the
// entities (ecs_reset()), the text layer (text_clear(); main.c then prints
// the title bar on row 0; text has a shadow, text_set_shadow(true), from
// boot on) and the backdrop (black). A scene's leave() undoes whatever else
// it set: its effect's state (screen_set_blend(), a raster effect, palette
// writes, ...). The brightness is main.c's while it fades (set every frame,
// before update()): a scene that changes it too does so only when its own
// state changes (blend_fx.c's B button).
typedef struct {
    const char* name;     // shown in the title bar: upper case, at most 20 characters
    void (*enter)(void);  // loads the scene's graphics (the screen is black)
    void (*update)(void); // one frame: logic and drawing, called between frame_begin()
                          // and frame_end()
    void (*leave)(void);  // undoes the scene's own settings (the screen is black)
} Scene;

extern const Scene blend_scene;        // blend_fx.c: alpha blending
extern const Scene palette_scene;      // palette_fx.c: palette writes
extern const Scene sprite_tiles_scene; // sprite_tiles_fx.c: runtime sprite tiles
extern const Scene raster_scene;       // raster_fx.c: raster effects

// Text rows the scenes may use: row 0 is main.c's title bar.
#define TITLE_ROW 0

// --- Art (art.c) ---------------------------------------------------------------
//
// Graphics drawn at boot from ASCII pictures, one character per pixel, as
// fireflies and blackjack do: '.' is transparent (color 0), and `keys` lists
// the picture characters of a palette's colors in order, so the character at
// keys[n] is color n (keys[0] is the transparent one). Draw on the canvas,
// then cut it into 4bpp tiles in a buffer of the scene's (static, in EWRAM:
// SERVAL_EWRAM_BSS) that sprite_group_load() or tileset_load() copies to VRAM.

#define CANVAS_MAX 64 // the canvas is at most 64x64 pixels

// Starts a blank canvas of w x h pixels (multiples of 8, at most CANVAS_MAX).
void canvas_begin(int w, int h);

// Draws `count` rows of a picture with its top-left at (x, y); characters
// not in `keys` (and '.') leave the canvas as it is.
void canvas_draw(const char* const* rows, int count, int x, int y, const char* keys);

// Sets one pixel to color c (0-15); off the canvas, nothing.
void canvas_plot(int x, int y, u32 c);

// Cuts the canvas into 8x8 4bpp tiles at `out`, row by row (the order of a
// sprite frame's tiles, and of a metatile's four tiles in a 16x16 canvas).
// Returns the end of what it wrote.
u32* canvas_pack(u32* out);

#endif // EFFECTS_H
