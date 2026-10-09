// Palette writes: sprite_set_colors() (sprites.h) and tileset_set_colors()
// (map.h) write the engine's shadow of palette RAM, and frame_end() copies the
// banks they changed to palette RAM in VBlank (docs/frame-loop.md, step 5).
// See docs/sprites.md#palettes and docs/tilemaps.md#palette-writes.
//
// The shadow holds the banks written since the last frame_end(), nothing
// else. The first write to a bank in a frame copies the bank from palette RAM
// (reading it is fine at any time; only writes wait for VBlank), so its other
// colors are the ones on screen, and later writes to it that frame go to the
// same copy. Code that writes palette RAM at once (sprite_group_load(),
// tileset_load(), screen_set_backdrop()) calls overwritten(), so what it
// writes wins over a write made earlier in the frame, as if both had waited.
//
// Banks are numbered as palette RAM lays them out: 0-15 the background's,
// 16-31 the sprites'. Sprite groups share no banks yet, so copy-on-write
// (sprites.h) has nothing to copy: a group's banks are its own.
//
// The module is linked only if a game writes colors: frame_end() reaches it
// through serval_palette_hooks, which the first write sets.

#include "serval/map.h"
#include "serval/sprites.h"

#include <tonc.h>

#include "../core/warn.h"
#include "internal.h"

#define BANKS 32
#define OBJ_FIRST_COLOR 256                 // sprite palette color 0 in palette RAM
#define BG_WRITABLE (MAP_MAX_PALETTES * 16) // colors 0-239; bank 15 is the text layer's

static EWRAM_BSS u16 shadow[BANKS * 16] ALIGN4;
static u32 dirty; // bit n: bank n is in the shadow, for the next VBlank

#ifdef SERVAL_DEBUG
enum {
    W_SPRITE_NOT_LOADED,
    W_SPRITE_META,
    W_SPRITE_COLORS,
    W_SPRITE_RANGE,
    W_BG_COLORS,
    W_BG_RANGE,
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

// In VBlank: the banks written since the last frame, to palette RAM.
static void flush(void) {
    u32 banks = dirty;
    dirty = 0;
    for (u32 bank = 0; banks; bank++, banks >>= 1) {
        if (banks & 1)
            memcpy32(&pal_bg_mem[bank * 16], &shadow[bank * 16], 16 * 2 / 4);
    }
}

// Palette RAM colors `index` to `index + count - 1` (0-511) were just written
// directly: a write still waiting in the shadow for one of them gives way.
static void overwritten_colors(u32 index, u32 count) {
    for (u32 at = index; at < index + count; at++) {
        if (dirty & (1u << (at / 16)))
            shadow[at] = pal_bg_mem[at];
    }
}

static void overwritten(bool obj, u32 index, u32 count) {
    overwritten_colors(obj ? OBJ_FIRST_COLOR + index : index, count);
}

static const ServalPaletteHooks hooks = {flush, overwritten};

// Writes `count` colors (at least one) into the shadow from palette RAM color
// `at` (0-511) on, for the next VBlank. A bank new to the shadow is copied
// from palette RAM first, unless the colors cover all of it.
static void write(u32 at, const Color* colors, u32 count) {
    serval_palette_hooks = &hooks;
    u32 end = at + count;
    for (u32 bank = at / 16; bank <= (end - 1) / 16; bank++) {
        if (!(dirty & (1u << bank))) {
            dirty |= 1u << bank;
            if (at > bank * 16 || end < bank * 16 + 16)
                memcpy32(&shadow[bank * 16], &pal_bg_mem[bank * 16], 16 * 2 / 4);
        }
    }
    memcpy16(&shadow[at], colors, count);
}

void sprite_set_colors(u16 sprite_id, u32 index, const Color* colors, u32 count) {
    if (count == 0)
        return;
    u32 first_bank = 0;
    int palettes = serval_sprite_palettes(sprite_id, &first_bank);
    if (palettes == 0) {
        WARN_ONCE(W_SPRITE_NOT_LOADED,
                  "sprite_set_colors: sprite %u is not loaded; load a sprite group containing "
                  "it. Nothing changes",
                  sprite_id);
        return;
    }
    if (palettes < 0) {
        WARN_ONCE(W_SPRITE_META,
                  "sprite_set_colors: sprite %u is a metasprite, whose pieces' groups hold the "
                  "colors: write them with a piece's sprite ID. Nothing changes",
                  sprite_id);
        return;
    }
    if (!serval_plausible_pointer(colors)) {
        WARN_ONCE(W_SPRITE_COLORS,
                  "sprite_set_colors: the colors pointer is NULL or not valid; nothing changes");
        return;
    }
    u32 limit = (u32)palettes * 16;
    if (count > limit || index > limit - count) {
        WARN_ONCE(W_SPRITE_RANGE,
                  "sprite_set_colors(%u, %u, ..., %u): past the colors of the sprite's group, "
                  "which has %u palettes (colors 0-%u); nothing changes",
                  sprite_id, index, count, (u32)palettes, limit - 1);
        return;
    }
    write(OBJ_FIRST_COLOR + first_bank * 16 + index, colors, count);
}

void tileset_set_colors(u32 index, const Color* colors, u32 count) {
    if (count == 0)
        return;
    if (count > BG_WRITABLE || index > BG_WRITABLE - count) {
        WARN_ONCE(W_BG_RANGE,
                  "tileset_set_colors(%u, ..., %u): past color %u (palette 15 is the text "
                  "layer's); nothing changes",
                  index, count, BG_WRITABLE - 1);
        return;
    }
    if (!serval_plausible_pointer(colors)) {
        WARN_ONCE(W_BG_COLORS,
                  "tileset_set_colors: the colors pointer is NULL or not valid; nothing changes");
        return;
    }
    if (index == 0) // raster: the backdrop, which raster_backdrop()'s end puts back
        serval_backdrop = colors[0];
    write(index, colors, count);
}
