#include "serval/sprites.h"
#include "serval/gba.h"
#include "serval/screen.h"

#include <tonc.h>

// Resident sprite groups only, for now. Tiles are bump-allocated in OBJ VRAM
// and palettes in OBJ palette banks; sprite_groups_reset() frees everything.
// Not yet implemented from docs/sprites.md: streamed sprites, LZ77 groups,
// metasprites, palette sharing, and the global/room watermark.

#define OBJ_TILE_COUNT 1024 // 32 KB of 4bpp tiles in tiled modes
#define OBJ_PALETTE_BANKS 16
#define NOT_LOADED 0xFFFF

// OBJ VRAM as one array of tiles (it spans two of libtonc's 512-tile charblocks).
static TILE* const obj_tiles = (TILE*)MEM_VRAM_OBJ;

static const SpriteAsset* const* sprite_table;
static u16 sprite_count;

static u16 next_tile;
static u8 next_palette_bank;

// Where each loaded sprite's tiles and palette ended up. Indexed by sprite ID.
static EWRAM_BSS u16 sprite_tile_base[SPRITE_MAX];
static EWRAM_BSS u8 sprite_palette_bank[SPRITE_MAX];

// Width and height in pixels, by [shape][size].
static const u8 sprite_dims[3][4][2] = {
    {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
    {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
    {{8, 16}, {8, 32}, {16, 32}, {32, 64}},
};

void sprite_groups_reset(void) {
    next_tile = 0;
    next_palette_bank = 0;
    for (u32 id = 0; id < SPRITE_MAX; id++)
        sprite_tile_base[id] = NOT_LOADED;
}

void sprite_table_set(const SpriteAsset* const* table, u16 count) {
    sprite_table = table;
    sprite_count = count < SPRITE_MAX ? count : SPRITE_MAX;
    sprite_groups_reset();
}

static bool group_supported(const SpriteGroup* group) {
    if (!(group->flags & SPRITE_GROUP_RESIDENT))
        return false;
    for (u32 i = 0; i < group->sprite_count; i++) {
        u16 id = group->sprite_ids[i];
        if (id >= sprite_count)
            return false;
        const SpriteAsset* sprite = sprite_table[id];
        if (sprite->flags & (SPRITE_ASSET_STREAMED | SPRITE_ASSET_METASPRITE))
            return false;
        if (sprite->shape > SPRITE_SHAPE_TALL || sprite->size > 3 ||
            sprite->palette_slot >= group->palette_count)
            return false;
    }
    return true;
}

bool sprite_group_load(const SpriteGroup* group) {
    if (!group_supported(group))
        return false;
    if (next_tile + group->tile_count > OBJ_TILE_COUNT ||
        next_palette_bank + group->palette_count > OBJ_PALETTE_BANKS)
        return false;

    u32 tile = next_tile;
    for (u32 i = 0; i < group->sprite_count; i++) {
        u16 id = group->sprite_ids[i];
        const SpriteAsset* sprite = sprite_table[id];
        u32 tiles = (u32)sprite->frame_count * sprite->tiles_per_frame;
        if (tile + tiles > (u32)next_tile + group->tile_count)
            return false; // group->tile_count is inconsistent with its sprites

        memcpy32(obj_tiles + tile, sprite->tiles, tiles * (sizeof(TILE) / 4));
        sprite_tile_base[id] = (u16)tile;
        sprite_palette_bank[id] = (u8)(next_palette_bank + sprite->palette_slot);
        tile += tiles;
    }

    memcpy16(&pal_obj_bank[next_palette_bank], group->palettes, group->palette_count * 16u);

    next_tile = (u16)(next_tile + group->tile_count);
    next_palette_bank = (u8)(next_palette_bank + group->palette_count);
    return true;
}

void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags) {
    if (sprite_id >= sprite_count || sprite_tile_base[sprite_id] == NOT_LOADED)
        return;
    const SpriteAsset* sprite = sprite_table[sprite_id];
    if (frame >= sprite->frame_count)
        return;

    x -= sprite->origin_x;
    y -= sprite->origin_y;
    int width = sprite_dims[sprite->shape][sprite->size][0];
    int height = sprite_dims[sprite->shape][sprite->size][1];
    if (x >= screen_width() || y >= screen_height() || x + width <= 0 || y + height <= 0)
        return; // off screen: don't spend a hardware sprite on it

    // Coordinates wrap in hardware (9-bit x, 8-bit y), so negative positions
    // must be masked rather than passed through.
    u16 attr0 = (u16)(ATTR0_REG | ATTR0_4BPP | ((u32)y & ATTR0_Y_MASK) | (sprite->shape << 14));
    u16 attr1 = (u16)(((u32)x & ATTR1_X_MASK) | (sprite->size << 14));
    if (flags & SPRITE_FLIP_H)
        attr1 |= ATTR1_HFLIP;
    if (flags & SPRITE_FLIP_V)
        attr1 |= ATTR1_VFLIP;
    u32 tile = sprite_tile_base[sprite_id] + (u32)frame * sprite->tiles_per_frame;
    u16 attr2 = (u16)(ATTR2_ID(tile) | ATTR2_PRIO((flags >> 2) & 3) |
                      ATTR2_PALBANK(sprite_palette_bank[sprite_id]));

    gba_oam_submit(attr0, attr1, attr2);
}
