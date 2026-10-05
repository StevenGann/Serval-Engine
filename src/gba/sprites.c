#include "serval/sprites.h"
#include "serval/ecs.h"
#include "serval/gba.h"
#include "serval/math.h"
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
static bool warned_matrices;

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

// SpriteAsset.size values, 1-12, in hardware terms: shape (square, wide,
// tall) and size (0-3), with the width and height in pixels.
typedef struct {
    u8 shape, size, width, height;
} HardwareSize;

static const HardwareSize hardware_sizes[12] = {
    {0, 0, 8, 8},  {0, 1, 16, 16}, {0, 2, 32, 32}, {0, 3, 64, 64}, // SPRITE_8x8 ...
    {1, 0, 16, 8}, {1, 1, 32, 8},  {1, 2, 32, 16}, {1, 3, 64, 32}, // SPRITE_16x8 ...
    {2, 0, 8, 16}, {2, 1, 8, 32},  {2, 2, 16, 32}, {2, 3, 32, 64}, // SPRITE_8x16 ...
};

static u32 frames_of(const SpriteAsset* sprite) {
    return sprite->frame_count ? sprite->frame_count : 1;
}

static u32 tiles_per_frame_of(const SpriteAsset* sprite) {
    if (sprite->tiles_per_frame)
        return sprite->tiles_per_frame;
    const HardwareSize* hw = &hardware_sizes[sprite->size - 1];
    return (u32)hw->width * hw->height / 64;
}

static u32 group_sprite_id(const SpriteGroup* group, u32 i) {
    return group->sprite_ids ? group->sprite_ids[i] : i;
}

void sprite_groups_reset(void) {
    next_tile = 0;
    next_palette_bank = 0;
    memset32(sprite_draws, 0, sizeof(sprite_draws) / 4);
#ifdef SERVAL_DEBUG
    memset32(warned_ids, 0, sizeof(warned_ids) / 4);
    warned_oam_full = false;
    warned_matrices = false;
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

// Checks that every sprite in the group can be loaded and returns the tiles it
// needs, or -1 after reporting the first problem (in debug builds).
static int group_tiles(const SpriteGroup* group) {
    if (group->flags & SPRITE_GROUP_STREAMED) {
        SERVAL_WARN("sprite_group_load: streamed groups are not supported yet");
        return -1;
    }
    u32 tiles = 0;
    for (u32 i = 0; i < group->sprite_count; i++) {
        u32 id = group_sprite_id(group, i);
        if (id >= sprite_count) {
            SERVAL_WARN("sprite_group_load: sprite ID %u is not in the sprite table (%u sprites)",
                        id, sprite_count);
            return -1;
        }
        const SpriteAsset* sprite = sprite_table[id];
        if (sprite->flags & (SPRITE_ASSET_STREAMED | SPRITE_ASSET_METASPRITE)) {
            SERVAL_WARN("sprite_group_load: sprite %u is streamed or a metasprite, which are not "
                        "supported yet",
                        id);
            return -1;
        }
        if (sprite->size < SPRITE_8x8 || sprite->size > SPRITE_32x64) {
            SERVAL_WARN("sprite_group_load: sprite %u has no valid size; set .size, e.g. "
                        "SPRITE_16x16",
                        id);
            return -1;
        }
        if (sprite->palette_slot >= group->palette_count) {
            SERVAL_WARN("sprite_group_load: sprite %u uses palette slot %u, but the group has %u "
                        "palettes",
                        id, sprite->palette_slot, group->palette_count);
            return -1;
        }
        tiles += frames_of(sprite) * tiles_per_frame_of(sprite);
    }
    return (int)tiles;
}

bool sprite_group_load(const SpriteGroup* group) {
    int tiles = group_tiles(group);
    if (tiles < 0)
        return false;
    if (next_tile + (u32)tiles > OBJ_TILE_COUNT) {
        SERVAL_WARN("sprite_group_load: needs %u tiles, but only %u of %u are free", tiles,
                    OBJ_TILE_COUNT - next_tile, OBJ_TILE_COUNT);
        return false;
    }
    if (next_palette_bank + group->palette_count > OBJ_PALETTE_BANKS) {
        SERVAL_WARN("sprite_group_load: needs %u palettes, but only %u of %u are free",
                    group->palette_count, OBJ_PALETTE_BANKS - next_palette_bank, OBJ_PALETTE_BANKS);
        return false;
    }

    u32 tile = next_tile;
    for (u32 i = 0; i < group->sprite_count; i++) {
        u32 id = group_sprite_id(group, i);
        const SpriteAsset* sprite = sprite_table[id];
        const HardwareSize* hw = &hardware_sizes[sprite->size - 1];
        u32 frames = frames_of(sprite);
        u32 per_frame = tiles_per_frame_of(sprite);
        memcpy32(obj_tiles + tile, sprite->tiles, frames * per_frame * (sizeof(TILE) / 4));

        SpriteDraw* d = &sprite_draws[id];
        d->attr0 = (u16)(ATTR0_REG | ATTR0_4BPP | (hw->shape << 14));
        d->attr1 = (u16)(hw->size << 14);
        d->attr2 = (u16)(ATTR2_ID(tile) | ATTR2_PALBANK(next_palette_bank + sprite->palette_slot));
        d->width = hw->width;
        d->height = hw->height;
        d->origin_x = sprite->origin_x;
        d->origin_y = sprite->origin_y;
        d->frame_count = (u8)frames;
        d->tiles_per_frame = (u8)per_frame;
        tile += frames * per_frame;
    }

    memcpy16(&pal_obj_bank[next_palette_bank], group->palettes, group->palette_count * 16u);

    next_tile = (u16)tile;
    next_palette_bank = (u8)(next_palette_bank + group->palette_count);
    return true;
}

#ifdef SERVAL_DEBUG
static __attribute__((noinline, cold)) void warn_matrices(void) {
    if (warned_matrices)
        return;
    warned_matrices = true;
    SERVAL_WARN("more than 32 rotation angles in one frame; the extra sprites are drawn unrotated");
}
#define MATRICES_FULL() warn_matrices()
#else
#define MATRICES_FULL() ((void)0)
#endif

// The angle and flip flags of each rotation matrix used this frame.
static u16 matrix_angle[32];
static u8 matrix_flips[32];

// Returns the index of a rotation matrix for `angle` and the flip flags,
// reusing one already set up this frame, or -1 if all 32 are taken.
static inline __attribute__((always_inline, target("arm"))) int matrix_for(u32 angle, u32 flips) {
    for (u32 k = 0; k < serval_matrices_used; k++) {
        if (matrix_angle[k] == angle && matrix_flips[k] == flips)
            return (int)k;
    }
    if (serval_matrices_used == 32)
        return -1;
    // The matrix maps screen offsets back to the sprite's pixels, so it is the
    // inverse rotation; flips mirror the pixels before rotating.
    FIXED c = fx_cos((u16)angle), sn = fx_sin((u16)angle);
    FIXED pa = c, pb = sn, pc = -sn, pd = c;
    if (flips & SPRITE_FLIP_H) {
        pa = -pa;
        pb = -pb;
    }
    if (flips & SPRITE_FLIP_V) {
        pc = -pc;
        pd = -pd;
    }
    u32 k = serval_matrices_used++;
    OBJ_AFFINE* m = &((OBJ_AFFINE*)serval_shadow_oam)[k];
    m->pa = (s16)pa;
    m->pb = (s16)pb;
    m->pc = (s16)pc;
    m->pd = (s16)pd;
    matrix_angle[k] = (u16)angle;
    matrix_flips[k] = (u8)flips;
    return (int)k;
}

// Appends a sprite to the shadow OAM. Shared by sprite_draw, sys_render and
// sys_render_by_depth, which all run as ARM code from IWRAM, the fastest place
// to run code on the GBA.
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

// Like draw, rotated by a non-zero angle: uses the hardware's double-size mode
// (so the rotated corners aren't cut off), shifted so the sprite stays
// centered where the unrotated one would be. Flips go into the matrix, whose
// index takes attr1 bits 9-13. Falls back to an unrotated draw when all 32
// matrices are taken. Kept out of line (but in IWRAM) so the render loops stay
// as fast as before for the usual unrotated sprites.
static __attribute__((noinline, section(".iwram.text"), target("arm"))) void
draw_rotated(u32 id, const SpriteDraw* d, u32 frame, int x, int y, u32 flags, u32 angle) {
    if (frame >= d->frame_count) {
        DRAW_REJECTED(id, frame);
        return;
    }
    int matrix = matrix_for(angle, flags & 3);
    if (matrix < 0) {
        MATRICES_FULL();
        draw(id, d, frame, x, y, flags);
        return;
    }
    x -= d->origin_x + d->width / 2;
    y -= d->origin_y + d->height / 2;
    u32 w = 2u * d->width, h = 2u * d->height;
    if ((u32)(x + (int)w - 1) >= 240 + w - 1 || (u32)(y + (int)h - 1) >= 160 + h - 1)
        return;
    if (serval_oam_used >= 128) {
        OAM_FULL();
        return;
    }
    OBJ_ATTR* obj = &serval_shadow_oam[serval_oam_used++];
    obj->attr0 = (u16)(d->attr0 | ATTR0_AFF_DBL | ((u32)y & ATTR0_Y_MASK));
    obj->attr1 = (u16)(d->attr1 | ((u32)matrix << 9) | ((u32)x & ATTR1_X_MASK));
    obj->attr2 = (u16)(d->attr2 + frame * d->tiles_per_frame + ((((flags >> 2) & 3) ^ 2) << 10));
}

// Draws entity i (known to have C_POS and C_SPR), rotated if it has an angle.
static inline __attribute__((always_inline, target("arm"))) void draw_entity(u32 i) {
    u32 id = spr_id[i];
    if (id >= sprite_count) {
        DRAW_REJECTED(id, spr_frame[i]);
        return;
    }
    int x = fx_to_int(pos_x[i]), y = fx_to_int(pos_y[i]);
    if (spr_angle[i])
        draw_rotated(id, &sprite_draws[id], spr_frame[i], x, y, spr_flags[i], spr_angle[i]);
    else
        draw(id, &sprite_draws[id], spr_frame[i], x, y, spr_flags[i]);
}

SERVAL_IWRAM_CODE void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags) {
    if (sprite_id < sprite_count)
        draw(sprite_id, &sprite_draws[sprite_id], frame, x, y, flags);
    else
        DRAW_REJECTED(sprite_id, frame);
}

SERVAL_IWRAM_CODE void sprite_draw_rotated(u16 sprite_id, u8 frame, int x, int y, u16 angle,
                                           u16 flags) {
    if (sprite_id < sprite_count)
        draw_rotated(sprite_id, &sprite_draws[sprite_id], frame, x, y, flags, angle);
    else
        DRAW_REJECTED(sprite_id, frame);
}

SERVAL_IWRAM_CODE void sys_render(void) {
    for (u32 i = 0; i < MAX_ENT; i++) {
        if ((ent_mask[i] & (C_POS | C_SPR)) == (C_POS | C_SPR))
            draw_entity(i);
    }
}

// sys_render_by_depth's draw order, rebuilt every call with a stable radix
// sort of the renderable entities by depth: one pass per key byte, and only
// one pass when all keys share their high byte (any depth range under 256,
// such as screen y coordinates). (Keeping last frame's order and repairing it
// with an insertion sort measured slower on bunnymark: bouncing sprites
// reorder too much.)
static u8 depth_order[MAX_ENT], depth_scratch[MAX_ENT];
static u16 depth_key[MAX_ENT]; // ascending key = descending depth
static u16 bucket_start[256];

static inline __attribute__((always_inline, target("arm"))) void radix_pass(const u8* from, u8* to,
                                                                            u32 n, u32 shift) {
    u32* clear = (u32*)bucket_start;
    for (u32 w = 0; w < 128; w++)
        clear[w] = 0;
    for (u32 k = 0; k < n; k++)
        bucket_start[(depth_key[from[k]] >> shift) & 0xFF]++;
    u32 sum = 0;
    for (u32 b = 0; b < 256; b++) {
        u32 c = bucket_start[b];
        bucket_start[b] = (u16)sum;
        sum += c;
    }
    for (u32 k = 0; k < n; k++)
        to[bucket_start[(depth_key[from[k]] >> shift) & 0xFF]++] = from[k];
}

SERVAL_IWRAM_CODE void sys_render_by_depth(void) {
    u32 n = 0, high_and = 0xFF00, high_or = 0;
    for (u32 i = 0; i < MAX_ENT; i++) {
        if ((ent_mask[i] & (C_POS | C_SPR)) != (C_POS | C_SPR))
            continue;
        u32 key = (u16)(0x7FFF - spr_depth[i]);
        depth_key[i] = (u16)key;
        high_and &= key;
        high_or |= key & 0xFF00;
        depth_order[n++] = (u8)i;
    }
    const u8* order = depth_order;
    if (n > 1) {
        radix_pass(depth_order, depth_scratch, n, 0);
        order = depth_scratch;
        if (high_and != high_or) { // keys differ in their high byte too
            radix_pass(depth_scratch, depth_order, n, 8);
            order = depth_order;
        }
    }

    for (u32 k = 0; k < n; k++) {
        draw_entity(order[k]);
    }
}
