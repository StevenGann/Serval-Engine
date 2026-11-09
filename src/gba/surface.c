// The pixel surface (surface.h, docs/surface.md): display mode 4, the 8-bit
// bitmap mode, on background 2. Its two pages are the first 38,400 bytes of
// VRAM at 0x06000000 and at 0x0600A000, which is all the VRAM the tiled
// backgrounds use (the text layer's font and map, the tileset, the map
// layers' screenblocks) and, for the second page, sprite tiles 0-431; in
// bitmap modes the hardware shows only sprite tiles 512 and up. So turning
// the surface on turns the text layer off, refuses loaded map layers, and
// moves sprite groups to tiles 512-1023 (sprites.c).
//
// Linked only into games that call it: frame_end() reaches the flip through
// serval_surface_commit_hook, and the modules that must refuse their calls
// while the surface is on read serval_surface_on, both in core.c.
//
// surface_set_colors() is in palette.c, with the other palette writes.

#include "serval/surface.h"

#include <tonc.h>

#include "../core/map_internal.h" // serval_map_layers: whether a map layer is loaded
#include "../core/warn.h"
#include "internal.h"

#define PAGE_WORDS (SURFACE_W * SURFACE_H / 4)
#define FIRST_SPRITE_TILE 512 // the hardware shows no sprite tile below it in bitmap modes

// The background enable bits and what the surface sets in their place.
#define DCNT_BGS (DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3)

_Static_assert(SURFACE_W == SCREEN_WIDTH && SURFACE_H == SCREEN_HEIGHT,
               "the surface is mode 4's page");
_Static_assert(SURFACE_W* SURFACE_H <= VRAM_PAGE_SIZE, "a page must fit before the next");

// The matrix (pa, pb, pc, pd) and reference point of background 2: what
// surface_set_transform() asked for, written to the registers in VBlank, and
// what was last written (the registers are write-only; tests read these).
typedef struct {
    s16 pa, pb, pc, pd;
    s32 x, y;
} Transform;

static const Transform identity = {.pa = FX_ONE, .pd = FX_ONE};

static Transform wanted, written;
static u8 shown;   // the page on screen: 0 or 1
static bool flip;  // surface_flip() since the last frame_end()
static bool moved; // surface_set_transform() since the last frame_end()

#ifdef SERVAL_DEBUG
enum {
    W_MAPS,
    W_TWICE,
    W_END_OFF,
    W_PIXELS_OFF,
    W_FLIP_OFF,
    W_CLEAR_OFF,
    W_TRANSFORM_OFF,
    W_TRANSFORM_RANGE,
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

static u8* page(u32 n) {
    return (u8*)(n ? MEM_VRAM_BACK : MEM_VRAM_FRONT);
}

static void write_transform(void) {
    REG_BG2PA = wanted.pa;
    REG_BG2PB = wanted.pb;
    REG_BG2PC = wanted.pc;
    REG_BG2PD = wanted.pd;
    REG_BG2X = wanted.x;
    REG_BG2Y = wanted.y;
    written = wanted;
    moved = false;
}

// DISPCNT with the surface on: mode 4, background 2 alone, the page shown.
// Everything else (sprites and their mapping, forced blank, windows, the
// "H-Blank interval free" bit a game may set) is kept.
static void write_display(void) {
    REG_DISPCNT = (u16)((REG_DISPCNT & ~(DCNT_MODE_MASK | DCNT_PAGE | DCNT_BGS)) | DCNT_MODE4 |
                        DCNT_BG2 | (shown ? DCNT_PAGE : 0));
}

// frame_end()'s VBlank step, after the map's: the flip and the transform.
// DISPCNT is written every frame, so the map module's last word on a layer
// unloaded just before surface_begin() (it clears the layer's bit a frame
// later) can't hide the surface.
static void commit(void) {
    if (flip) {
        shown ^= 1;
        flip = false;
    }
    write_display();
    if (moved)
        write_transform();
}

bool surface_begin(void) {
    if (serval_surface_on) {
        WARN_ONCE(W_TWICE, "surface_begin: the surface is on already; ignored");
        return true;
    }
    if (serval_map_layers[1] || serval_map_layers[2] || serval_map_layers[3]) {
        WARN_ONCE(W_MAPS,
                  "surface_begin: a map layer is loaded, and the surface takes the map layers' "
                  "video memory: map_unload() backgrounds 1-3 first. The surface stays off");
        return false;
    }
    // What the map module still had to write for the layers just unloaded and
    // for tileset_set_tiles() calls of this frame goes out now, before the
    // pages are cleared, not into them at the next frame_end().
    if (serval_map_prepare_hook)
        serval_map_prepare_hook();
    if (serval_map_commit_hook)
        serval_map_commit_hook();
    if (serval_text_lost_hook) // the text layer is off until the next text call sets it up
        serval_text_lost_hook();

    // Backgrounds off first, so the rest of this frame shows the backdrop
    // rather than tiles as pixels; then the pages, then background 2.
    REG_DISPCNT = (u16)((REG_DISPCNT & ~(DCNT_MODE_MASK | DCNT_PAGE | DCNT_BGS)) | DCNT_MODE4);
    serval_surface_on = true;
    serval_sprite_floor_set(FIRST_SPRITE_TILE); // unloads every sprite group
    memset32(page(0), 0, PAGE_WORDS);
    memset32(page(1), 0, PAGE_WORDS);
    REG_BG2CNT = BG_PRIO(2); // the playfield's place among the layers (docs/tilemaps.md)
    shown = 0;
    flip = false;
    wanted = identity;
    write_transform();
    write_display();
    serval_surface_commit_hook = commit;
    return true;
}

void surface_end(void) {
    if (!serval_surface_on) {
        WARN_ONCE(W_END_OFF, "surface_end: the surface is not on; ignored");
        return;
    }
    serval_surface_commit_hook = NULL;
    serval_surface_on = false;
    REG_DISPCNT = (u16)((REG_DISPCNT & ~(DCNT_MODE_MASK | DCNT_PAGE | DCNT_BGS)) | DCNT_MODE0);
    serval_sprite_floor_set(0); // unloads every sprite group
}

u8* surface_pixels(void) {
    if (!serval_surface_on) {
        WARN_ONCE(W_PIXELS_OFF,
                  "surface_pixels: the surface is not on (call surface_begin() and check what it "
                  "returns); returning NULL");
        return NULL;
    }
    return page(shown ^ 1u);
}

void surface_flip(void) {
    if (!serval_surface_on) {
        WARN_ONCE(W_FLIP_OFF, "surface_flip: the surface is not on; ignored");
        return;
    }
    flip = true;
}

void surface_clear(u8 color) {
    if (!serval_surface_on) {
        WARN_ONCE(W_CLEAR_OFF, "surface_clear: the surface is not on; ignored");
        return;
    }
    memset32(page(shown ^ 1u), color * 0x01010101u, PAGE_WORDS);
}

// v limited to lo..hi, noting whether it had to be.
static s32 limit(FIXED v, s32 lo, s32 hi, bool* clamped) {
    if (v < lo || v > hi) {
        *clamped = true;
        return v < lo ? lo : hi;
    }
    return v;
}

void surface_set_transform(FIXED x0, FIXED y0, FIXED xx, FIXED xy, FIXED yx, FIXED yy) {
    if (!serval_surface_on) {
        WARN_ONCE(W_TRANSFORM_OFF, "surface_set_transform: the surface is not on; ignored");
        return;
    }
    // The registers: 8.8 signed for the matrix, 20.8 signed for the point.
    bool clamped = false;
    wanted.pa = (s16)limit(xx, -0x8000, 0x7FFF, &clamped);
    wanted.pb = (s16)limit(xy, -0x8000, 0x7FFF, &clamped);
    wanted.pc = (s16)limit(yx, -0x8000, 0x7FFF, &clamped);
    wanted.pd = (s16)limit(yy, -0x8000, 0x7FFF, &clamped);
    wanted.x = limit(x0, -0x8000000, 0x7FFFFFF, &clamped);
    wanted.y = limit(y0, -0x8000000, 0x7FFFFFF, &clamped);
    moved = true;
    if (clamped)
        WARN_ONCE(W_TRANSFORM_RANGE,
                  "surface_set_transform: xx, xy, yx and yy go from FX(-128) to just under "
                  "FX(128), x0 and y0 from FX(-524288) to just under FX(524288); clamped");
}

// For tests: the transform last written to background 2's write-only
// registers, as six FIXED in surface_set_transform()'s order.
void serval_surface_transform(FIXED out[6]) {
    out[0] = written.x;
    out[1] = written.y;
    out[2] = written.pa;
    out[3] = written.pb;
    out[4] = written.pc;
    out[5] = written.pd;
}
