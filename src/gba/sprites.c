#include "serval/sprites.h"
#include "serval/ecs.h"
#include "serval/gba.h"
#include "serval/screen.h"

#include <tonc.h>

#include "../core/warn.h"
#include "internal.h"

// Resident sprite groups only, for now. Tiles are bump-allocated in OBJ VRAM
// and palettes in OBJ palette banks; sprite_groups_reset() frees everything.
// Not yet implemented from docs/sprites.md: streamed sprites, LZ77 groups,
// metasprites, palette sharing, and the global/room watermark.

#define OBJ_TILE_COUNT 1024 // 32 KB of 4bpp tiles in tiled modes
#define OBJ_PALETTE_BANKS 16

// OBJ VRAM as one array of tiles (it spans two of libtonc's 512-tile charblocks).
static TILE* const obj_tiles = (TILE*)MEM_VRAM_OBJ;

static const SpriteAsset* const* sprite_table;
static u16 sprite_count;

static u16 next_tile;
static u8 next_palette_bank;

// Everything sprite_draw needs for a loaded sprite, resolved once at load
// time so drawing is a lookup and a few ORs. frame_count is 0 while the sprite
// is not loaded. Indexed by sprite ID.
typedef struct {
    u16 attr0;        // shape | 4bpp
    u16 attr1;        // size
    u16 attr2;        // first tile | palette bank
    u8 width, height; // pixels
    s8 origin_x, origin_y;
    u8 frame_count;
    u8 tiles_per_frame;
} SpriteDraw;

static EWRAM_BSS SpriteDraw sprite_draws[SPRITE_MAX];

#ifdef SERVAL_DEBUG
// Each problem is reported once per sprite ID, not every frame.
static EWRAM_BSS u32 warned_ids[SPRITE_MAX / 32];
static bool warned_oam_full;

static bool first_warning(u32 id) {
    if (id >= SPRITE_MAX)
        id = SPRITE_MAX - 1; // out-of-range IDs share one slot
    u32 bit = 1u << (id % 32);
    if (warned_ids[id / 32] & bit)
        return false;
    warned_ids[id / 32] |= bit;
    return true;
}

// Called from the draw fast path only when a draw is rejected.
static __attribute__((noinline, cold)) void warn_draw(u32 id, u32 frame) {
    if (!first_warning(id))
        return;
    if (id >= sprite_count)
        SERVAL_WARN("sprite_draw: sprite ID %u is not in the sprite table (%u sprites)", id,
                    sprite_count);
    else if (sprite_draws[id].frame_count == 0)
        SERVAL_WARN("sprite_draw: sprite %u is not loaded; load a sprite group containing it", id);
    else
        SERVAL_WARN("sprite_draw: sprite %u has no frame %u (it has %u)", id, frame,
                    sprite_draws[id].frame_count);
}

static __attribute__((noinline, cold)) void warn_oam_full(void) {
    if (warned_oam_full)
        return;
    warned_oam_full = true;
    SERVAL_WARN("more than 128 sprites drawn in one frame; the extra ones are not shown");
}
#define DRAW_REJECTED(id, frame) warn_draw(id, frame)
#define OAM_FULL() warn_oam_full()
#else
#define DRAW_REJECTED(id, frame) ((void)(id), (void)(frame))
#define OAM_FULL() ((void)0)
#endif

// Width and height in pixels, by [shape][size].
static const u8 sprite_dims[3][4][2] = {
    {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
    {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
    {{8, 16}, {8, 32}, {16, 32}, {32, 64}},
};

void sprite_groups_reset(void) {
    next_tile = 0;
    next_palette_bank = 0;
    memset32(sprite_draws, 0, sizeof(sprite_draws) / 4);
#ifdef SERVAL_DEBUG
    memset32(warned_ids, 0, sizeof(warned_ids) / 4);
    warned_oam_full = false;
#endif
}

void sprite_table_set(const SpriteAsset* const* table, u16 count) {
    if (count > SPRITE_MAX)
        SERVAL_WARN("sprite_table_set: %u sprites, but only the first %u can be used", count,
                    SPRITE_MAX);
    sprite_table = table;
    sprite_count = count < SPRITE_MAX ? count : SPRITE_MAX;
    sprite_groups_reset();
}

// Checks that every sprite in the group can be loaded, reporting the first
// problem in debug builds.
static bool group_supported(const SpriteGroup* group) {
    if (!(group->flags & SPRITE_GROUP_RESIDENT)) {
        SERVAL_WARN("sprite_group_load: only resident groups (SPRITE_GROUP_RESIDENT) are "
                    "supported so far");
        return false;
    }
    for (u32 i = 0; i < group->sprite_count; i++) {
        u16 id = group->sprite_ids[i];
        if (id >= sprite_count) {
            SERVAL_WARN("sprite_group_load: sprite ID %u is not in the sprite table (%u sprites)",
                        id, sprite_count);
            return false;
        }
        const SpriteAsset* sprite = sprite_table[id];
        if (sprite->flags & (SPRITE_ASSET_STREAMED | SPRITE_ASSET_METASPRITE)) {
            SERVAL_WARN("sprite_group_load: sprite %u is streamed or a metasprite, which are not "
                        "supported yet",
                        id);
            return false;
        }
        if (sprite->shape > SPRITE_SHAPE_TALL || sprite->size > 3) {
            SERVAL_WARN("sprite_group_load: sprite %u has an invalid shape or size", id);
            return false;
        }
        if (sprite->palette_slot >= group->palette_count) {
            SERVAL_WARN("sprite_group_load: sprite %u uses palette slot %u, but the group has %u "
                        "palettes",
                        id, sprite->palette_slot, group->palette_count);
            return false;
        }
    }
    return true;
}

bool sprite_group_load(const SpriteGroup* group) {
    if (!group_supported(group))
        return false;
    if (next_tile + group->tile_count > OBJ_TILE_COUNT) {
        SERVAL_WARN("sprite_group_load: needs %u tiles, but only %u of %u are free",
                    group->tile_count, OBJ_TILE_COUNT - next_tile, OBJ_TILE_COUNT);
        return false;
    }
    if (next_palette_bank + group->palette_count > OBJ_PALETTE_BANKS) {
        SERVAL_WARN("sprite_group_load: needs %u palettes, but only %u of %u are free",
                    group->palette_count, OBJ_PALETTE_BANKS - next_palette_bank, OBJ_PALETTE_BANKS);
        return false;
    }

    u32 tile = next_tile;
    for (u32 i = 0; i < group->sprite_count; i++) {
        const SpriteAsset* sprite = sprite_table[group->sprite_ids[i]];
        u32 tiles = (u32)sprite->frame_count * sprite->tiles_per_frame;
        if (tile + tiles > (u32)next_tile + group->tile_count) {
            SERVAL_WARN("sprite_group_load: tile_count is %u, but the group's sprites need more",
                        group->tile_count);
            return false;
        }
        tile += tiles;
    }

    tile = next_tile;
    for (u32 i = 0; i < group->sprite_count; i++) {
        u16 id = group->sprite_ids[i];
        const SpriteAsset* sprite = sprite_table[id];
        u32 tiles = (u32)sprite->frame_count * sprite->tiles_per_frame;
        memcpy32(obj_tiles + tile, sprite->tiles, tiles * (sizeof(TILE) / 4));

        SpriteDraw* d = &sprite_draws[id];
        d->attr0 = (u16)(ATTR0_REG | ATTR0_4BPP | (sprite->shape << 14));
        d->attr1 = (u16)(sprite->size << 14);
        d->attr2 = (u16)(ATTR2_ID(tile) | ATTR2_PALBANK(next_palette_bank + sprite->palette_slot));
        d->width = sprite_dims[sprite->shape][sprite->size][0];
        d->height = sprite_dims[sprite->shape][sprite->size][1];
        d->origin_x = sprite->origin_x;
        d->origin_y = sprite->origin_y;
        d->frame_count = sprite->frame_count;
        d->tiles_per_frame = sprite->tiles_per_frame;
        tile += tiles;
    }

    memcpy16(&pal_obj_bank[next_palette_bank], group->palettes, group->palette_count * 16u);

    next_tile = (u16)(next_tile + group->tile_count);
    next_palette_bank = (u8)(next_palette_bank + group->palette_count);
    return true;
}

// Appends a sprite to the shadow OAM. Shared by sprite_draw and sys_render;
// both run as ARM code from IWRAM, the fastest place to run code on the GBA.
static inline __attribute__((always_inline, target("arm"))) void
draw(u32 id, const SpriteDraw* d, u32 frame, int x, int y, u32 flags) {
    if (frame >= d->frame_count) { // also rejects sprites that are not loaded
        DRAW_REJECTED(id, frame);
        return;
    }
    x -= d->origin_x;
    y -= d->origin_y;
    // On screen if x is in (-width, 240) and y in (-height, 160).
    if ((u32)(x + d->width - 1) >= (u32)(240 + d->width - 1) ||
        (u32)(y + d->height - 1) >= (u32)(160 + d->height - 1))
        return; // off screen: don't spend a hardware sprite on it
    if (serval_oam_used >= 128) {
        OAM_FULL();
        return;
    }

    // Coordinates wrap in hardware (9-bit x, 8-bit y), so negative positions
    // are masked rather than passed through. Flip flags map onto attr1 bits
    // 12-13. The layer flag (bits 2-3) is the hardware priority XOR 2, so no
    // flag means priority 2: behind backgrounds 0-1, in front of 2-3.
    OBJ_ATTR* obj = &serval_shadow_oam[serval_oam_used++];
    obj->attr0 = (u16)(d->attr0 | ((u32)y & ATTR0_Y_MASK));
    obj->attr1 = (u16)(d->attr1 | ((u32)x & ATTR1_X_MASK) | ((flags & 3) << 12));
    obj->attr2 = (u16)(d->attr2 + frame * d->tiles_per_frame + ((((flags >> 2) & 3) ^ 2) << 10));
}

SERVAL_IWRAM_CODE void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags) {
    if (sprite_id < sprite_count)
        draw(sprite_id, &sprite_draws[sprite_id], frame, x, y, flags);
    else
        DRAW_REJECTED(sprite_id, frame);
}

SERVAL_IWRAM_CODE void sys_render(void) {
    for (u32 i = 0; i < MAX_ENT; i++) {
        if ((ent_mask[i] & (C_POS | C_SPR)) != (C_POS | C_SPR))
            continue;
        u32 id = spr_id[i];
        if (id < sprite_count)
            draw(id, &sprite_draws[id], spr_frame[i], fx_to_int(pos_x[i]), fx_to_int(pos_y[i]), 0);
        else
            DRAW_REJECTED(id, spr_frame[i]);
    }
}
