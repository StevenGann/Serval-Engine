#include "serval/sprites.h"
#include "serval/ecs.h"
#include "serval/gba.h"
#include "serval/math.h"
#include "serval/screen.h"

#include <tonc.h>

#include "../core/map_internal.h"
#include "../core/sprite_internal.h"
#include "../core/warn.h"
#include "internal.h"

// Resident sprite groups (docs/sprites.md). Tiles are bump-allocated in OBJ
// VRAM and palettes in OBJ palette banks, in load order;
// sprite_groups_release() rolls both back to a mark, sprite_groups_reset() to
// the start. Streamed groups load and draw here too, from slots at the top of
// OBJ VRAM that sprite_stream.c keeps. Planned (declared in sprites.h, stubs
// or refusals here): LZ77 sprites. Palette sharing comes after 1.0.
// Runtime tiles (sprite_set_tiles) are in sprite_tiles.c.
// palettes: palette writes, sprite_set_colors(), are in palette.c.

// Rare paths (building a matrix, drawing a metasprite's pieces) are out of
// line, in ROM on the GBA, so IWRAM code calls them with a long call.
#ifdef SERVAL_GBA
#define ROM_CALL __attribute__((long_call, noinline))
#else
#define ROM_CALL __attribute__((noinline))
#endif

#define OBJ_TILE_COUNT 1024 // 32 KB of 4bpp tiles in tiled modes
#define OBJ_PALETTE_BANKS 16

// OBJ VRAM as one array of tiles (it spans two of libtonc's 512-tile charblocks).
static TILE* const obj_tiles = (TILE*)MEM_VRAM_OBJ;

// The sprite table is serval_sprite_table and serval_sprite_count
// (src/core/sprite_table.c), shared with sys_animate.

static u16 next_tile;
static u8 next_palette_bank;

// Everything sprite_draw needs for a loaded sprite, resolved once at load
// time so drawing is a lookup and a few ORs. frame_count is 0 while the sprite
// is not loaded, and for metasprites, so the usual drawing path rejects them
// with no test of its own and the rejection path draws their pieces
// (meta_frames is their frame count). Indexed by sprite ID.
typedef struct {
    u16 attr0;        // shape | 4bpp
    u16 attr1;        // size
    u16 attr2;        // first tile | palette bank
    u8 width, height; // pixels
    s8 origin_x, origin_y;
    u8 frame_count;
    u8 tiles_per_frame;
    u8 first_palette; // the group's first palette bank, for SPRITE_PALETTE
    u8 palette_count; // the group's palettes
    u8 meta_frames;   // a loaded metasprite's frames, otherwise 0
    u8 segment;       // the mark segment its group loaded in (see Marks below)
} SpriteDraw;

static EWRAM_BSS SpriteDraw sprite_draws[SPRITE_MAX];

// streaming: a sprite of a streamed group has a draw record with no frames of
// its own (frame_count 0, so the usual path rejects it, as a metasprite) but
// with tiles (tiles_per_frame), and the group's index in sprite_stream.c in
// attr2's tile bits: each draw puts its slot's tile there instead.
#define STREAMED(d) ((d)->frame_count == 0 && (d)->tiles_per_frame != 0)

#ifdef SERVAL_DEBUG
// Each problem is reported once per sprite ID, not every frame. IDs past
// SPRITE_MAX share one extra slot, so they don't hide a warning for ID 511.
#define OUT_OF_RANGE_SLOT SPRITE_MAX
static EWRAM_BSS u32 warned_ids[SPRITE_MAX / 32 + 1];
static bool warned_oam_full;
static bool warned_matrices;
static bool warned_palette;
static bool warned_scale_flag;
static bool warned_scale;

static bool first_warning(u32 id) {
    if (id >= SPRITE_MAX)
        id = OUT_OF_RANGE_SLOT;
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
    if (id >= serval_sprite_count)
        SERVAL_WARN("sprite_draw: sprite ID %u is not in the sprite table (%u sprites)", id,
                    serval_sprite_count);
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

static __attribute__((noinline, cold)) void warn_palette(u32 id, u32 palette) {
    if (warned_palette)
        return;
    warned_palette = true;
    SERVAL_WARN("SPRITE_PALETTE(%u) on sprite %u, but its group has %u palettes; drawn with its "
                "own palette",
                palette, id, sprite_draws[id < SPRITE_MAX ? id : 0].palette_count);
}
#define DRAW_REJECTED(id, frame) warn_draw(id, frame)
#define OAM_FULL() (serval_sprites_dropped++, warn_oam_full())
#define BAD_PALETTE(id, palette) warn_palette(id, palette)
#else
#define DRAW_REJECTED(id, frame) ((void)(id), (void)(frame))
#define OAM_FULL() ((void)serval_sprites_dropped++)
#define BAD_PALETTE(id, palette) ((void)(id), (void)(palette))
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

// --- Marks (sprite_groups_mark/release) ---
//
// Groups load in order, and the groups loaded between two marks are always
// released together, so only the marks are recorded: a stack of segments,
// segment k holding the groups loaded after mark k was taken (segment 0:
// after the last reset), each with where the allocator stood when it began.
// Every loaded sprite's draw record names its segment, so releasing to mark
// k clears the records of segments k and up and rolls the allocator back to
// segment k's start. A mark's value is a serial number, never reused, so a
// mark that is no longer on the stack (released past, or from before a reset)
// is recognized and ignored. All of it is touched only by loads, marks and
// releases, so it lives in EWRAM.
#define MAX_MARKS 16 // sprites.h promises 16 nested marks

typedef struct {
    u32 mark;         // the value sprite_groups_mark() returns for it
    u16 first_tile;   // next_tile when the segment began
    u8 first_palette; // next_palette_bank when it began
    bool loaded;      // a group has loaded in it since
} Segment;

static EWRAM_BSS Segment segments[MAX_MARKS + 1];
static EWRAM_BSS u32 top_segment; // the segment loads go into
static EWRAM_BSS u32 last_mark;   // the last serial handed out

// A segment index fits SpriteDraw.segment.
_Static_assert(MAX_MARKS < 256, "SpriteDraw.segment is a u8");

void sprite_groups_reset(void) {
    next_tile = 0;
    next_palette_bank = 0;
    memset32(sprite_draws, 0, sizeof(sprite_draws) / 4);
    // A new serial for segment 0, so marks from before the reset are stale.
    top_segment = 0;
    segments[0] = (Segment){.mark = ++last_mark};
    serval_sprite_tiles_reset(); // sprite tiles: drop every queued copy
    serval_stream_reset();       // streaming: and the slots of streamed groups
#ifdef SERVAL_DEBUG
    memset32(warned_ids, 0, sizeof(warned_ids) / 4);
    warned_oam_full = false;
    warned_matrices = false;
    warned_palette = false;
    warned_scale_flag = false;
    warned_scale = false;
#endif
}

u32 sprite_groups_mark(void) {
    Segment* top = &segments[top_segment];
    if (!top->loaded)
        return top->mark; // nothing loaded since it was taken: the same point
    if (top_segment == MAX_MARKS) {
        SERVAL_WARN("sprite_groups_mark: more than %u nested marks; returning the newest, which "
                    "releases more",
                    MAX_MARKS);
        return top->mark;
    }
    segments[++top_segment] =
        (Segment){.mark = ++last_mark, .first_tile = next_tile, .first_palette = next_palette_bank};
    return last_mark;
}

void sprite_groups_release(u32 mark) {
    u32 k = 0;
    while (k <= top_segment && segments[k].mark != mark)
        k++;
    if (k > top_segment) {
        SERVAL_WARN("sprite_groups_release: %u is not a current mark (released past, or from "
                    "before a reset); ignored",
                    mark);
        return;
    }
    // Rare (room changes), so a pass over the table rather than a list of
    // each segment's sprites. Records not loaded are all zero (segment 0),
    // which only k = 0 clears again.
    for (u32 id = 0; id < serval_sprite_count; id++) {
        if (sprite_draws[id].segment >= k)
            sprite_draws[id] = (SpriteDraw){0};
    }
    next_tile = segments[k].first_tile;
    next_palette_bank = segments[k].first_palette;
    serval_stream_release(k); // streaming: the streamed groups' slots too
    segments[k].loaded = false;
    top_segment = k;
    serval_sprite_tiles_release(next_tile); // sprite tiles: drop the unloaded sprites' copies
}

void sprite_table_set(const SpriteAsset* const* table, u16 count) {
    if (count > SPRITE_MAX)
        SERVAL_WARN("sprite_table_set: %u sprites, but only the first %u can be used", count,
                    SPRITE_MAX);
    serval_sprite_table = table;
    serval_sprite_count = count < SPRITE_MAX ? count : SPRITE_MAX;
    sprite_groups_reset();
}

// The flags a SpritePiece may carry: a subset of the draw flags, since the
// layer and the rest belong to the whole draw. Anything else is refused, so
// that a later version can give the other bits a meaning (docs/releases.md).
#define PIECE_FLAGS (SPRITE_FLIP_H | SPRITE_FLIP_V | SPRITE_PALETTE_MASK | SPRITE_BLEND)

// Checks a metasprite's pieces: each names an ordinary sprite of the table
// and one of its frames, with piece flags only. Reports the first problem (in
// debug builds).
static bool metasprite_ok(u32 id, const SpriteAsset* sprite) {
    (void)id; // only in warnings
    if (!serval_plausible_pointer(sprite->pieces) || sprite->piece_count == 0) {
        SERVAL_WARN("sprite_group_load: metasprite %u needs .pieces and a .piece_count of at least "
                    "1",
                    id);
        return false;
    }
    u32 count = frames_of(sprite) * sprite->piece_count;
    for (u32 k = 0; k < count; k++) {
        const SpritePiece* p = &sprite->pieces[k];
        const SpriteAsset* piece =
            p->sprite < serval_sprite_count ? serval_sprite_table[p->sprite] : NULL;
        if (!serval_plausible_pointer(piece) || (piece->flags & SPRITE_ASSET_METASPRITE) ||
            p->frame >= frames_of(piece)) {
            SERVAL_WARN("sprite_group_load: metasprite %u, piece %u: sprite %u frame %u is not an "
                        "ordinary sprite's frame in the sprite table",
                        id, k, p->sprite, p->frame);
            return false;
        }
        if (p->flags & ~PIECE_FLAGS) {
            SERVAL_WARN("sprite_group_load: metasprite %u, piece %u: flags 0x%x; a piece takes "
                        "only flips, SPRITE_PALETTE, SPRITE_BLEND",
                        id, k, p->flags);
            return false;
        }
    }
    return true;
}

// Checks that every sprite in the group can be loaded and returns the tiles it
// needs, or -1 after reporting the first problem (in debug builds).
static int group_tiles(const SpriteGroup* group) {
    if (!serval_plausible_pointer(group)) {
        SERVAL_WARN("sprite_group_load: the group pointer is NULL or not valid");
        return -1;
    }
    // Planned features first, so their warning names them; then any bit or
    // value this version doesn't know (docs/releases.md: loaders refuse them,
    // so that giving them a meaning later breaks no game).
    // streaming: SPRITE_GROUP_STREAMED is the group's one flag, .slots its field
    bool streamed = group->flags & SPRITE_GROUP_STREAMED;
    if (group->flags & ~SPRITE_GROUP_STREAMED) {
        SERVAL_WARN("sprite_group_load: the group's flags 0x%x hold reserved bits; not loaded",
                    group->flags);
        return -1;
    }
    if (group->slots && !streamed) {
        SERVAL_WARN("sprite_group_load: .slots is %u, but only a streamed group has slots; not "
                    "loaded",
                    group->slots);
        return -1;
    }
    if (group->palette_count && !serval_plausible_pointer(group->palettes)) {
        SERVAL_WARN("sprite_group_load: the group has palette_count %u but no .palettes (NULL or "
                    "not a valid pointer)",
                    group->palette_count);
        return -1;
    }
    if (group->sprite_count && group->sprite_ids && !serval_plausible_pointer(group->sprite_ids)) {
        SERVAL_WARN("sprite_group_load: the group's .sprite_ids is not a valid pointer");
        return -1;
    }
    u32 tiles = 0;
    u32 largest = 0; // streaming: tiles of the largest frame, a streamed group's slot size
    for (u32 i = 0; i < group->sprite_count; i++) {
        u32 id = group_sprite_id(group, i);
        if (id >= serval_sprite_count) {
            SERVAL_WARN("sprite_group_load: sprite ID %u is not in the sprite table (%u sprites)",
                        id, serval_sprite_count);
            return -1;
        }
        const SpriteAsset* sprite = serval_sprite_table[id];
        if (!serval_plausible_pointer(sprite)) {
            SERVAL_WARN("sprite_group_load: sprite table entry %u is NULL or not a valid pointer",
                        id);
            return -1;
        }
        // streaming: streamed frames are copied straight from ROM, never unpacked
        if (streamed && (sprite->flags & SPRITE_ASSET_LZ77)) {
            SERVAL_WARN("sprite_group_load: sprite %u: SPRITE_ASSET_LZ77 in a streamed group, "
                        "whose frames are copied straight from ROM; not loaded",
                        id);
            return -1;
        }
        if (sprite->flags & SPRITE_ASSET_LZ77) {
            SERVAL_WARN("sprite_group_load: sprite %u: SPRITE_ASSET_LZ77 is planned, not "
                        "implemented in this engine version",
                        id);
            return -1;
        }
        // Bit 0 (SPRITE_ASSET_STREAMED before 1.0) and bits 4-7 are reserved.
        if (sprite->flags & ~(SPRITE_ASSET_METASPRITE | SPRITE_ASSET_ANIM_ONCE)) {
            SERVAL_WARN("sprite_group_load: sprite %u has flags 0x%x, with reserved bits", id,
                        sprite->flags);
            return -1;
        }
        if (sprite->flags & SPRITE_ASSET_METASPRITE) {
            if (!metasprite_ok(id, sprite))
                return -1;
            continue; // no tiles of its own
        }
        if (sprite->size < SPRITE_8x8 || sprite->size > SPRITE_32x64) {
            SERVAL_WARN("sprite_group_load: sprite %u has no valid size; set .size, e.g. "
                        "SPRITE_16x16",
                        id);
            return -1;
        }
        if (!serval_plausible_pointer(sprite->tiles)) {
            SERVAL_WARN("sprite_group_load: sprite %u has no .tiles (NULL or not a valid pointer)",
                        id);
            return -1;
        }
        const HardwareSize* hw = &hardware_sizes[sprite->size - 1];
        u32 hardware_tiles = (u32)hw->width * hw->height / 64;
        if (sprite->tiles_per_frame && sprite->tiles_per_frame < hardware_tiles) {
            SERVAL_WARN(
                "sprite_group_load: sprite %u has tiles_per_frame %u, but its size needs %u "
                "tiles per frame; leave it 0 to compute it",
                id, sprite->tiles_per_frame, hardware_tiles);
            return -1;
        }
        if (sprite->palette_slot >= group->palette_count) {
            SERVAL_WARN("sprite_group_load: sprite %u uses palette slot %u, but the group has %u "
                        "palettes",
                        id, sprite->palette_slot, group->palette_count);
            return -1;
        }
        tiles += frames_of(sprite) * tiles_per_frame_of(sprite);
        largest = hardware_tiles > largest ? hardware_tiles : largest; // streaming
    }
    // streaming: a streamed group takes its slots (0 means 1), not its frames
    if (streamed)
        return (int)((group->slots ? group->slots : 1u) * largest);
    return (int)tiles;
}

// streaming: loads a streamed group that group_tiles() has checked, with
// `tiles` tiles of slots: its palettes as a resident group's, and draw records
// with no frames of their own (STREAMED), whose frames are drawn from the
// slots (draw_streamed). A group of metasprites only needs no slots.
static bool load_streamed(const SpriteGroup* group, u32 tiles) {
    u32 slots = group->slots ? group->slots : 1;
    int stream = 0;
    if (tiles) {
        stream = serval_stream_add(slots, tiles / slots, top_segment);
        if (stream < 0)
            return false;
    }
    for (u32 i = 0; i < group->sprite_count; i++) {
        u32 id = group_sprite_id(group, i);
        const SpriteAsset* sprite = serval_sprite_table[id];
        SpriteDraw* d = &sprite_draws[id];
        if (sprite->flags & SPRITE_ASSET_METASPRITE) {
            *d = (SpriteDraw){.meta_frames = (u8)frames_of(sprite), .segment = (u8)top_segment};
            continue;
        }
        const HardwareSize* hw = &hardware_sizes[sprite->size - 1];
        *d = (SpriteDraw){
            .attr0 = (u16)(ATTR0_REG | ATTR0_4BPP | (hw->shape << 14)),
            .attr1 = (u16)(hw->size << 14),
            .attr2 = (u16)(ATTR2_PALBANK(next_palette_bank + sprite->palette_slot) | (u32)stream),
            .width = hw->width,
            .height = hw->height,
            .origin_x = sprite->origin_x,
            .origin_y = sprite->origin_y,
            .tiles_per_frame = (u8)tiles_per_frame_of(sprite),
            .first_palette = next_palette_bank,
            .palette_count = group->palette_count,
            .segment = (u8)top_segment};
    }
    memcpy16(&pal_obj_bank[next_palette_bank], group->palettes, group->palette_count * 16u);
    next_palette_bank = (u8)(next_palette_bank + group->palette_count);
    segments[top_segment].loaded = true;
    return true;
}

bool sprite_group_load(const SpriteGroup* group) {
    int tiles = group_tiles(group);
    if (tiles < 0)
        return false;
    u32 free_end = serval_stream_floor(); // streaming: streamed groups' slots are above it
    if (next_tile + (u32)tiles > free_end) {
        SERVAL_WARN("sprite_group_load: needs %u tiles, but only %u of %u are free", tiles,
                    free_end - next_tile, OBJ_TILE_COUNT);
        return false;
    }
    if (next_palette_bank + group->palette_count > OBJ_PALETTE_BANKS) {
        SERVAL_WARN("sprite_group_load: needs %u palettes, but only %u of %u are free",
                    group->palette_count, OBJ_PALETTE_BANKS - next_palette_bank, OBJ_PALETTE_BANKS);
        return false;
    }
    // streaming: a streamed group's sprites draw from its slots
    if (group->flags & SPRITE_GROUP_STREAMED)
        return load_streamed(group, (u32)tiles);

    u32 tile = next_tile;
    for (u32 i = 0; i < group->sprite_count; i++) {
        u32 id = group_sprite_id(group, i);
        const SpriteAsset* sprite = serval_sprite_table[id];
        SpriteDraw* d = &sprite_draws[id];
        if (sprite->flags & SPRITE_ASSET_METASPRITE) {
            *d = (SpriteDraw){.meta_frames = (u8)frames_of(sprite), .segment = (u8)top_segment};
            continue;
        }
        const HardwareSize* hw = &hardware_sizes[sprite->size - 1];
        u32 frames = frames_of(sprite);
        u32 per_frame = tiles_per_frame_of(sprite);
        memcpy32(obj_tiles + tile, sprite->tiles, frames * per_frame * (sizeof(TILE) / 4));

        d->attr0 = (u16)(ATTR0_REG | ATTR0_4BPP | (hw->shape << 14));
        d->attr1 = (u16)(hw->size << 14);
        d->attr2 = (u16)(ATTR2_ID(tile) | ATTR2_PALBANK(next_palette_bank + sprite->palette_slot));
        d->width = hw->width;
        d->height = hw->height;
        d->origin_x = sprite->origin_x;
        d->origin_y = sprite->origin_y;
        d->frame_count = (u8)frames;
        d->meta_frames = 0;
        d->tiles_per_frame = (u8)per_frame;
        d->first_palette = next_palette_bank;
        d->palette_count = group->palette_count;
        d->segment = (u8)top_segment;
        tile += frames * per_frame;
    }

    memcpy16(&pal_obj_bank[next_palette_bank], group->palettes, group->palette_count * 16u);
    // palettes: the group's colors win over sprite_set_colors() writes made to
    // these banks before it in the frame (palette.c)
    if (serval_palette_hooks)
        serval_palette_hooks->overwritten(true, next_palette_bank * 16u,
                                          group->palette_count * 16u);

    next_tile = (u16)tile;
    next_palette_bank = (u8)(next_palette_bank + group->palette_count);
    segments[top_segment].loaded = true;
    return true;
}

// palettes: sprite_set_colors() (palette.c) finds a sprite's group's banks here.
int serval_sprite_palettes(u32 id, u32* first_bank) {
    if (id >= serval_sprite_count)
        return 0;
    const SpriteDraw* d = &sprite_draws[id];
    if (d->meta_frames)
        return -1;
    if (d->frame_count == 0)
        return 0;
    *first_bank = d->first_palette;
    return d->palette_count;
}

#ifdef SERVAL_DEBUG
static __attribute__((noinline, cold)) void warn_matrices(void) {
    if (warned_matrices)
        return;
    warned_matrices = true;
    SERVAL_WARN("more than 32 rotation/scale matrices in one frame; the extra sprites are drawn "
                "unrotated and unscaled (sprite_stats() counts them)");
}
#define MATRICES_FULL() (serval_sprites_untransformed++, warn_matrices())
#else
#define MATRICES_FULL() ((void)serval_sprites_untransformed++)
#endif

// What each rotation matrix used this frame was made for: the angle and flip
// flags (angle | flips << 16) and the scales (x | y << 16, 8.8 fixed point).
static u32 matrix_turn[32];
static u32 matrix_scale[32];

// Building a new matrix (at most 32 a frame) is out of line, in ROM (see
// ROM_CALL): only the search for one already set up runs for every rotated or
// scaled sprite.

// 1 / scale in 8.8 fixed point, for a scale in 8.8 (not 0), limited to what
// a matrix entry holds. One division per scaled axis of a new matrix (none,
// one or two).
static s32 inverse_scale(s32 scale) {
    s32 inv = (s32)(65536 / scale);
    return inv > 32767 ? 32767 : inv < -32767 ? -32767 : inv;
}

static s16 matrix_entry(s32 trig, s32 inv) {
    s32 v = (trig * inv) >> 8;
    return (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

// Sets up matrix k. It maps screen offsets back to the sprite's pixels, so
// it is the inverse transform: rotate back, then divide by the scales; flips
// mirror the pixels before rotating.
static ROM_CALL __attribute__((cold)) void set_matrix(u32 k, u32 angle, u32 flips, s32 scale_x,
                                                      s32 scale_y) {
    FIXED c = fx_cos((u16)angle), sn = fx_sin((u16)angle);
    FIXED pa = c, pb = sn, pc = -sn, pd = c;
    if (scale_x != FX_ONE) {
        s32 inv = inverse_scale(scale_x);
        pa = matrix_entry(pa, inv);
        pb = matrix_entry(pb, inv);
    }
    if (scale_y != FX_ONE) {
        s32 inv = inverse_scale(scale_y);
        pc = matrix_entry(pc, inv);
        pd = matrix_entry(pd, inv);
    }
    if (flips & SPRITE_FLIP_H) {
        pa = -pa;
        pb = -pb;
    }
    if (flips & SPRITE_FLIP_V) {
        pc = -pc;
        pd = -pd;
    }
    OBJ_AFFINE* m = &((OBJ_AFFINE*)serval_shadow_oam)[k];
    m->pa = (s16)pa;
    m->pb = (s16)pb;
    m->pc = (s16)pc;
    m->pd = (s16)pd;
}

// Returns the index of a rotation matrix for `angle`, the flip flags and the
// scales (8.8; 256 is normal size), reusing one already set up this frame, or
// -1 if all 32 are taken.
static inline SERVAL_ARM __attribute__((always_inline)) int matrix_for(u32 angle, u32 flips,
                                                                       s32 scale_x, s32 scale_y) {
    u32 turn = angle | flips << 16, scale = (u32)(u16)scale_x | (u32)scale_y << 16;
    for (u32 k = 0; k < serval_matrices_used; k++) {
        if (matrix_turn[k] == turn && matrix_scale[k] == scale)
            return (int)k;
    }
    if (serval_matrices_used == 32)
        return -1;
    u32 k = serval_matrices_used++;
    set_matrix(k, angle, flips, scale_x, scale_y);
    matrix_turn[k] = turn;
    matrix_scale[k] = scale;
    return (int)k;
}

// attr2 of a sprite drawn with SPRITE_PALETTE(n): `attr2` with the palette
// bank of its group's palette n instead of its own. `selected` is the flags'
// palette bits (n + 1, shifted).
static inline SERVAL_ARM __attribute__((always_inline)) u32 palette_attr2(u32 id,
                                                                          const SpriteDraw* d,
                                                                          u32 attr2, u32 selected) {
    u32 n = (selected >> 8) - 1;
    if (n >= d->palette_count) {
        BAD_PALETTE(id, n);
        return attr2;
    }
    return (attr2 & ~ATTR2_PALBANK_MASK) | ((d->first_palette + n) << 12);
}

// attr2 for a draw whose flags hold SPRITE_PALETTE or SPRITE_BLEND (the
// callers test both with one mask, so plain draws don't pay for either).
// SPRITE_BLEND also sets the semi-transparent mode in obj's attr0, already
// written: the hardware blends such a sprite over screen_set_blend()'s
// second targets (blend.c).
static inline SERVAL_ARM __attribute__((always_inline)) u32 special_attr2(u32 id,
                                                                          const SpriteDraw* d,
                                                                          OBJ_ATTR* obj, u32 attr2,
                                                                          u32 flags) {
    if (flags & SPRITE_BLEND)
        obj->attr0 = (u16)(obj->attr0 | ATTR0_BLEND); // blending: semi-transparent
    if (flags & SPRITE_PALETTE_MASK)
        attr2 = palette_attr2(id, d, attr2, flags & SPRITE_PALETTE_MASK);
    return attr2;
}

static ROM_CALL void draw_meta(u32 id, u32 frame, int x, int y, u32 flags, u32 angle, s32 scale_x,
                               s32 scale_y);
// streaming: a streamed sprite's frame, from a slot of its group
static ROM_CALL void draw_streamed(u32 id, u32 frame, int x, int y, u32 flags, u32 angle,
                                   s32 scale_x, s32 scale_y);

// The usual drawing path's rejection: a metasprite (drawn here, with no
// transform), or a sprite that isn't loaded or a frame it doesn't have.
// `which` is the sprite ID and the frame << 16: four arguments, all in
// registers, so the call doesn't slow the usual path down.
static ROM_CALL void draw_rejected(u32 which, int x, int y, u32 flags) {
    u32 id = which & 0xFFFF, frame = which >> 16;
    if (STREAMED(&sprite_draws[id])) // streaming
        draw_streamed(id, frame, x, y, flags, 0, FX_ONE, FX_ONE);
    else if (frame < sprite_draws[id].meta_frames)
        draw_meta(id, frame, x, y, flags, 0, FX_ONE, FX_ONE);
    else
        DRAW_REJECTED(id, frame);
}

// Appends a sprite to the shadow OAM. Shared by sprite_draw, sys_render and
// sys_render_by_depth, which all run as ARM code from IWRAM, the fastest place
// to run code on the GBA. `palettes`: whether flags may hold SPRITE_PALETTE or
// SPRITE_BLEND (the render loops send those sprites to draw_transformed,
// keeping the test out of the usual case).
static inline SERVAL_ARM __attribute__((always_inline)) void
draw(u32 id, const SpriteDraw* d, u32 frame, int x, int y, u32 flags, bool palettes) {
    // Also rejects sprites that are not loaded, and metasprites.
    if (__builtin_expect(frame >= d->frame_count, 0)) {
        draw_rejected(id | frame << 16, x, y, flags);
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
    u32 attr2 = d->attr2 + frame * d->tiles_per_frame + ((((flags >> 2) & 3) ^ 2) << 10);
    if (palettes && (flags & (SPRITE_PALETTE_MASK | SPRITE_BLEND)))
        attr2 = special_attr2(id, d, obj, attr2, flags);
    obj->attr2 = (u16)attr2;
}

// Like draw, rotated by `angle` and scaled by scale_x, scale_y (8.8, 256 is
// normal size, negative mirrors) around the sprite's center. Rotated or
// enlarged sprites use the hardware's double-size mode (so corners aren't cut
// off), shifted so the sprite stays centered where the untransformed one
// would be; sprites that are only shrunk (angle 0, scales within +-1) fit in
// their own box and use plain affine mode, which costs half the scanline
// time. Flips go into the matrix, whose index takes attr1 bits 9-13. Falls
// back to an untransformed draw when all 32 matrices are taken; sprites that
// are off screen or don't fit in OAM never take a matrix. draw_affine returns
// false for that fallback.
static inline SERVAL_ARM __attribute__((always_inline)) bool
draw_affine(u32 id, const SpriteDraw* d, u32 frame, int x, int y, u32 flags, u32 angle, s32 scale_x,
            s32 scale_y) {
    if (frame >= d->frame_count) {
        DRAW_REJECTED(id, frame);
        return true;
    }
    bool shrunk = angle == 0 && (u32)(scale_x + FX_ONE) <= 2 * FX_ONE &&
                  (u32)(scale_y + FX_ONE) <= 2 * FX_ONE;
    u32 w = d->width, h = d->height, mode = ATTR0_AFF;
    // The box's top-left corner: the sprite's own, or the double-size box's.
    x -= d->origin_x;
    y -= d->origin_y;
    if (!shrunk) {
        x -= (int)w / 2;
        y -= (int)h / 2;
        w *= 2;
        h *= 2;
        mode = ATTR0_AFF_DBL;
    }
    if ((u32)(x + (int)w - 1) >= 240 + w - 1 || (u32)(y + (int)h - 1) >= 160 + h - 1)
        return true; // off screen
    if (serval_oam_used >= 128) {
        OAM_FULL();
        return true;
    }
    int matrix = matrix_for(angle, flags & 3, scale_x, scale_y);
    if (matrix < 0) {
        MATRICES_FULL();
        return false;
    }
    OBJ_ATTR* obj = &serval_shadow_oam[serval_oam_used++];
    obj->attr0 = (u16)(d->attr0 | mode | ((u32)y & ATTR0_Y_MASK));
    obj->attr1 = (u16)(d->attr1 | ((u32)matrix << 9) | ((u32)x & ATTR1_X_MASK));
    u32 attr2 = d->attr2 + frame * d->tiles_per_frame + ((((flags >> 2) & 3) ^ 2) << 10);
    if (flags & (SPRITE_PALETTE_MASK | SPRITE_BLEND))
        attr2 = special_attr2(id, d, obj, attr2, flags);
    obj->attr2 = (u16)attr2;
    return true;
}

// The out-of-line path for everything but plain sprites: rotated or scaled
// sprites, and the render loops' hidden sprites (SPRITE_HIDDEN, dropped here)
// and sprites with SPRITE_PALETTE or SPRITE_BLEND (drawn untransformed when
// there is no transform). Kept out of line (but in IWRAM) so the render loops stay as fast
// as before for the usual plain sprites. Scales are 8.8 with 256 normal size;
// 0 draws nothing.
static SERVAL_IWRAM_TEXT __attribute__((noinline)) void
draw_transformed(u32 id, const SpriteDraw* d, u32 frame, int x, int y, u32 flags, u32 angle,
                 s32 scale_x, s32 scale_y) {
    if ((flags & SPRITE_HIDDEN) || scale_x == 0 || scale_y == 0)
        return;
    if (frame >= d->frame_count && frame < d->meta_frames) {
        draw_meta(id, frame, x, y, flags, angle, scale_x, scale_y);
        return;
    }
    // streaming: a streamed sprite, from a slot of its group
    if (frame >= d->frame_count && STREAMED(d)) {
        draw_streamed(id, frame, x, y, flags, angle, scale_x, scale_y);
        return;
    }
    // No transform (a sprite with SPRITE_PALETTE), or no matrix left: plain.
    if ((angle == 0 && scale_x == FX_ONE && scale_y == FX_ONE) ||
        !draw_affine(id, d, frame, x, y, flags, angle, scale_x, scale_y))
        draw(id, d, frame, x, y, flags, true);
}

// A metasprite's frame: each piece placed around the pivot, its offset
// flipped, scaled and rotated like the whole, and drawn with the same
// transform. In ROM: metasprites are few, and their pieces go through the
// usual IWRAM paths.
static ROM_CALL void draw_meta(u32 id, u32 frame, int x, int y, u32 flags, u32 angle, s32 scale_x,
                               s32 scale_y) {
    const SpriteAsset* meta = serval_sprite_table[id];
    const SpritePiece* p = meta->pieces + frame * meta->piece_count;
    x -= meta->origin_x; // the pivot, as a sprite's origin: drawn at (x, y) - origin
    y -= meta->origin_y;
    bool transformed = angle != 0 || scale_x != FX_ONE || scale_y != FX_ONE;
    FIXED c = fx_cos((u16)angle), sn = fx_sin((u16)angle);
    for (u32 k = 0; k < meta->piece_count; k++, p++) {
        int dx = flags & SPRITE_FLIP_H ? -p->x : p->x;
        int dy = flags & SPRITE_FLIP_V ? -p->y : p->y;
        if (transformed) {
            // Scaled (8.8), then rotated (8.8): 16.16, rounded to pixels. In
            // 64 bits: a far piece at a large scale overflows 32.
            int64_t sx = (int64_t)dx * scale_x, sy = (int64_t)dy * scale_y;
            dx = (int)((c * sx - sn * sy + 0x8000) >> 16);
            dy = (int)((sn * sx + c * sy + 0x8000) >> 16);
        }
        // The piece's flips on top of the whole's; the whole's palette, or
        // the piece's own if the whole has none; blended if either is; the
        // whole's layer.
        u32 palette = (flags & SPRITE_PALETTE_MASK) ? flags : p->flags;
        u32 piece_flags = (flags & ~(3u | SPRITE_PALETTE_MASK)) | ((flags ^ p->flags) & 3) |
                          (palette & SPRITE_PALETTE_MASK) | (p->flags & SPRITE_BLEND);
        u32 piece = p->sprite;
        const SpriteDraw* d = &sprite_draws[piece];
        // Centered on (x + dx, y + dy): the drawing paths subtract the origin
        // and, for the box, half the size. A piece whose sprite isn't loaded
        // is reported by them.
        int px = x + dx - d->width / 2 + d->origin_x, py = y + dy - d->height / 2 + d->origin_y;
        draw_transformed(piece, d, p->frame, px, py, piece_flags, angle, scale_x, scale_y);
    }
}

// streaming: frame `frame` of streamed sprite `id`. It is drawn as a sprite
// with that one frame, through the usual paths, and only if it took a hardware
// sprite (on screen, not hidden, OAM not full) does it take a slot: the one
// already holding the frame, or one whose frame sprite_stream.c copies in
// VBlank. With every slot taken by this frame's draws, it is not drawn after
// all (counted as dropped). In ROM: draws of streamed sprites are few.
static ROM_CALL void draw_streamed(u32 id, u32 frame, int x, int y, u32 flags, u32 angle,
                                   s32 scale_x, s32 scale_y) {
    const SpriteDraw* d = &sprite_draws[id];
    const SpriteAsset* sprite = serval_sprite_table[id];
    if (frame >= frames_of(sprite)) {
#ifdef SERVAL_DEBUG
        if (first_warning(id))
            SERVAL_WARN("sprite_draw: sprite %u has no frame %u (it has %u)", id, frame,
                        frames_of(sprite));
#endif
        return;
    }
    SpriteDraw one = *d;
    one.frame_count = 1;
    one.attr2 = (u16)(d->attr2 & ~ATTR2_ID_MASK);
    u32 entry = serval_oam_used;
    draw_transformed(id, &one, 0, x, y, flags, angle, scale_x, scale_y);
    if (serval_oam_used == entry)
        return; // not drawn: no slot needed
    int tile = serval_stream_slot(d->attr2 & ATTR2_ID_MASK, id | frame << 16,
                                  sprite->tiles + frame * d->tiles_per_frame * (sizeof(TILE) / 4),
                                  (u32)d->width * d->height / 64);
    if (tile < 0)
        serval_oam_used = entry; // every slot holds a frame drawn this frame
    else
        serval_shadow_oam[entry].attr2 = (u16)(serval_shadow_oam[entry].attr2 + tile);
}

// draw_transformed for entity i, at (x, y) on the screen.
static SERVAL_IWRAM_TEXT __attribute__((noinline)) void draw_entity_transformed(u32 i, int x, int y,
                                                                                u32 flags) {
    u32 id = spr_id[i];
    s32 scale = flags & SPRITE_SCALED ? spr_scale[i] : FX_ONE;
    draw_transformed(id, &sprite_draws[id], spr_frame[i], x, y, flags, spr_angle[i], scale, scale);
}

#ifdef SERVAL_DEBUG
static __attribute__((noinline, cold)) void warn_scale_flag(u32 i) {
    if (warned_scale_flag)
        return;
    warned_scale_flag = true;
    SERVAL_WARN("entity %u has spr_scale %d but no SPRITE_SCALED in spr_flags; drawn at normal "
                "size",
                i, spr_scale[i]);
}
#define SCALE_WITHOUT_FLAG(i, flags)                                                               \
    do {                                                                                           \
        if (spr_scale[i] && !((flags) & SPRITE_SCALED))                                            \
            warn_scale_flag(i);                                                                    \
    } while (0)
#else
#define SCALE_WITHOUT_FLAG(i, flags) ((void)0)
#endif

// The spr_flags bits that a plain draw handles: flips, layer and
// sys_animate's flips. Of bits 0-12, the others are SPRITE_HIDDEN,
// SPRITE_SCALED, SPRITE_PALETTE and SPRITE_BLEND, which send an entity to the
// transformed path (draw_entity).
#define PLAIN_DRAW_FLAGS                                                                           \
    (SPRITE_FLIP_H | SPRITE_FLIP_V | (3u << 2) | SPRITE_ANIM_FLIP_H | SPRITE_ANIM_FLIP_V)
_Static_assert((((0xFFFFu & ~PLAIN_DRAW_FLAGS) << 19) >> 19) ==
                   (SPRITE_HIDDEN | SPRITE_SCALED | SPRITE_PALETTE_MASK | SPRITE_BLEND),
               "draw_entity's test sends exactly these flags to the transformed path");

// Draws entity i (known to have C_POS and C_SPR) at its position minus the
// camera's (or at its position with SPRITE_SCREEN), rotated if it has an
// angle.
static inline SERVAL_ARM __attribute__((always_inline)) void draw_entity(u32 i, int camera_x,
                                                                         int camera_y) {
    u32 flags = spr_flags[i];
    u32 id = spr_id[i];
    // Against the constant SPRITE_MAX rather than the table's size, which the
    // loop would reload for every sprite: entries past the table are never
    // loaded (zero frames), so draw() rejects those IDs.
    if (id >= SPRITE_MAX) {
        DRAW_REJECTED(id, spr_frame[i]);
        return;
    }
    // Screen-space sprites ignore the camera. In ARM code, a test and
    // conditional moves; the camera is loaded only for the others.
    if (flags & SPRITE_SCREEN)
        camera_x = camera_y = 0;
    SCALE_WITHOUT_FLAG(i, flags);
    int x = fx_to_int(pos_x[i]) - camera_x, y = fx_to_int(pos_y[i]) - camera_y;
    // Rotated, scaled (SPRITE_SCALED), hidden and blended (SPRITE_BLEND)
    // sprites and sprites with another palette take the rare transformed
    // path, which drops hidden ones and draws the others: one test keeps the
    // usual case as fast as before. Those four are bits 4 and 7-12 of the
    // flags, which no single ARM immediate covers (the mask before
    // SPRITE_BLEND, 0xF90, was one). So the test clears the plain bits (BIC
    // #0x6F) and ORs what is left, shifted above the angle's 16 bits, with
    // the angle (ORRS with a shifted operand, which also drops bits 13-15:
    // SPRITE_SCREEN and two reserved): two instructions, as before, and no
    // cycles more (bunnymark). Left to itself, GCC folds the mask into the
    // shift ((flags << 19) & 0xFC800000), a constant it must load from
    // memory: +400 cycles for 128 sprites. The empty asm makes `special` a
    // value GCC can't see through, so the BIC stays. spr_scale is read only
    // on the transformed path: loading it for every sprite cost ~1,000
    // cycles for 128 sprites. The call takes four arguments, all in
    // registers: passing the sprite's fields from here spilled to the stack
    // and slowed the usual path too.
    u32 special = flags & ~PLAIN_DRAW_FLAGS;
#ifdef SERVAL_GBA
    __asm__("" : "+r"(special));
#endif
    if (spr_angle[i] | special << 19)
        draw_entity_transformed(i, x, y, flags);
    else
        draw(id, &sprite_draws[id], spr_frame[i], x, y, flags, false);
}

SERVAL_IWRAM_CODE void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags) {
    if (flags & SPRITE_HIDDEN)
        return;
    if (sprite_id < serval_sprite_count)
        draw(sprite_id, &sprite_draws[sprite_id], frame, x, y, flags, true);
    else
        DRAW_REJECTED(sprite_id, frame);
}

SERVAL_IWRAM_CODE void sprite_draw_rotated(u16 sprite_id, u8 frame, int x, int y, u16 angle,
                                           u16 flags) {
    if (flags & SPRITE_HIDDEN)
        return;
    if (angle == 0) // no rotation: no matrix needed
        sprite_draw(sprite_id, frame, x, y, flags);
    else if (sprite_id < serval_sprite_count)
        draw_transformed(sprite_id, &sprite_draws[sprite_id], frame, x, y, flags, angle, FX_ONE,
                         FX_ONE);
    else
        DRAW_REJECTED(sprite_id, frame);
}

#ifdef SERVAL_DEBUG
static __attribute__((noinline, cold)) void warn_scale(FIXED scale_x, FIXED scale_y) {
    if (warned_scale)
        return;
    warned_scale = true;
    SERVAL_WARN("sprite_draw_ex: scale %d/256 x %d/256 is not between -128 and 128 (exclusive); "
                "limited to +-32767/256",
                scale_x, scale_y);
}
#define BAD_SCALE(x, y) warn_scale(x, y)
#else
#define BAD_SCALE(x, y) ((void)(x), (void)(y))
#endif

static s32 clamp_scale(FIXED scale) {
    return scale > 32767 ? 32767 : scale < -32767 ? -32767 : scale;
}

void sprite_draw_ex(u16 sprite_id, u8 frame, int x, int y, u16 angle, FIXED scale_x, FIXED scale_y,
                    u16 flags) {
    if (flags & SPRITE_HIDDEN)
        return;
    if (sprite_id >= serval_sprite_count) {
        DRAW_REJECTED(sprite_id, frame);
        return;
    }
    s32 sx = clamp_scale(scale_x), sy = clamp_scale(scale_y);
    if (sx != scale_x || sy != scale_y)
        BAD_SCALE(scale_x, scale_y);
    draw_transformed(sprite_id, &sprite_draws[sprite_id], frame, x, y, flags, angle, sx, sy);
}

SpriteStats sprite_stats(void) {
    return serval_sprite_stats;
}

bool serval_scanline_stats;

void sprite_stats_scanlines(bool on) {
    serval_scanline_stats = on;
}

// Sprite cycles per scanline (GBATEK): 1210, or 954 with DISPCNT's "H-Blank
// interval free" bit. The web renderer (src/web/ppu.c) draws by the same
// rules, so what it leaves out is what this counts.
#define LINE_CYCLES 1210
#define LINE_CYCLES_HBLANK_FREE 954

// Width and height of each hardware shape (square, wide, tall) and size.
static const u8 shape_sizes[3][4][2] = {
    {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
    {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
    {{8, 16}, {8, 32}, {16, 32}, {32, 64}},
};

static EWRAM_BSS u16 line_demand[SCREEN_H];

// Walks this frame's shadow OAM in order, adding each sprite's cost to the
// scanlines it covers. Out of IWRAM: it is a diagnostic.
void serval_count_scanlines(SpriteStats* stats) {
    u32 budget = (REG_DISPCNT & DCNT_OAM_HBL) ? LINE_CYCLES_HBLANK_FREE : LINE_CYCLES;
    memset32(line_demand, 0, sizeof(line_demand) / 4);
    u32 cut = 0, busiest = 0;
    for (u32 k = 0; k < serval_oam_used; k++) {
        u32 attr0 = serval_shadow_oam[k].attr0, attr1 = serval_shadow_oam[k].attr1;
        bool affine = (attr0 & ATTR0_AFF) != 0;
        u32 shape = (attr0 >> 14) & 3;
        if ((!affine && (attr0 & ATTR0_HIDE)) || shape == 3)
            continue;
        const u8* size = shape_sizes[shape][attr1 >> 14];
        u32 w = size[0], h = size[1], cost = w;
        if (affine) {
            if (attr0 & ATTR0_AFF_DBL_BIT) {
                w *= 2;
                h *= 2;
            }
            cost = 10 + 2 * w;
        }
        u32 top = attr0 & ATTR0_Y_MASK, left = attr1 & ATTR1_X_MASK;
        if ((top >= SCREEN_H && top + h < 228) || (left >= SCREEN_W && left + w < 512))
            continue; // entirely off screen: no cycles
        bool lost = false;
        for (u32 r = 0; r < h; r++) {
            u32 line = (top + r) & 0xFF; // y wraps at 256
            if (line >= SCREEN_H)
                continue;
            u32 before = line_demand[line];
            lost |= before + cost > budget;
            line_demand[line] = (u16)(before + cost);
            busiest = before + cost > busiest ? before + cost : busiest;
        }
        cut += lost;
    }
    stats->cut_short = (u16)cut;
    stats->busiest_line = (u16)(busiest < 0xFFFF ? busiest : 0xFFFF);
}

SERVAL_IWRAM_CODE void sys_render(void) {
    const int camera_x = serval_camera_x, camera_y = serval_camera_y;
    for (u32 i = 0; i < MAX_ENT; i++) {
        if ((ent_mask[i] & (C_POS | C_SPR)) == (C_POS | C_SPR))
            draw_entity(i, camera_x, camera_y);
    }
}

// sys_render_by_depth's draw order, rebuilt every call with a stable
// counting sort of the renderable entities by depth. What it costs depends on
// the depths, not only on the number of sprites:
//   - depths that never increase from one slot to the next (all the same, or
//     each kind of entity created behind the ones before) are already in
//     order: no sort at all;
//   - depths within 256 of each other (screen y coordinates, or a few depths
//     for kinds of entities): one pass with one bucket per depth in that
//     range, so a game with three depths clears and sums three buckets, not
//     256;
//   - otherwise one pass per key byte.
// (Keeping last frame's order and repairing it with an insertion sort
// measured slower on bunnymark: bouncing sprites reorder too much.)
static u8 depth_order[MAX_ENT], depth_scratch[MAX_ENT];
static u16 depth_key[MAX_ENT]; // ascending key = descending depth
// Bucket offsets, cleared a word at a time: the union makes the u32 view
// aligned and the type punning well defined.
static union {
    u32 words[128];
    u16 start[256];
} buckets;

// One stable counting pass on byte `shift` of each key minus `low`, with
// `count` buckets (that byte is below `count`).
static inline SERVAL_ARM __attribute__((always_inline)) void
counting_pass(const u8* from, u8* to, u32 n, u32 shift, u32 low, u32 count) {
    for (u32 w = 0; w < (count + 1) / 2; w++)
        buckets.words[w] = 0;
    for (u32 k = 0; k < n; k++)
        buckets.start[((depth_key[from[k]] - low) >> shift) & 0xFF]++;
    u32 sum = 0;
    for (u32 b = 0; b < count; b++) {
        u32 c = buckets.start[b];
        buckets.start[b] = (u16)sum;
        sum += c;
    }
    for (u32 k = 0; k < n; k++)
        to[buckets.start[((depth_key[from[k]] - low) >> shift) & 0xFF]++] = from[k];
}

SERVAL_IWRAM_CODE void sys_render_by_depth(void) {
    u32 n = 0, low = 0xFFFF, high = 0, previous = 0;
    bool in_order = true;
    for (u32 i = 0; i < MAX_ENT; i++) {
        if ((ent_mask[i] & (C_POS | C_SPR)) != (C_POS | C_SPR))
            continue;
        u32 key = (u16)(0x7FFF - spr_depth[i]);
        depth_key[i] = (u16)key;
        in_order &= key >= previous;
        previous = key;
        low = key < low ? key : low;
        high = key > high ? key : high;
        depth_order[n++] = (u8)i;
    }
    const u8* order = depth_order;
    if (!in_order) {
        // Within 256: one pass over the range's buckets; wider: the low byte
        // over all 256, then the high byte.
        bool wide = high - low >= 256;
        counting_pass(depth_order, depth_scratch, n, 0, wide ? 0 : low,
                      wide ? 256 : high - low + 1);
        order = depth_scratch;
        if (wide) {
            counting_pass(depth_scratch, depth_order, n, 8, 0, 256);
            order = depth_order;
        }
    }

    const int camera_x = serval_camera_x, camera_y = serval_camera_y;
    for (u32 k = 0; k < n; k++) {
        draw_entity(order[k], camera_x, camera_y);
    }
}

// sprite tiles: what sprite `id` is and where its frames are in OBJ VRAM, for
// sprite_set_tiles() (sprite_tiles.c).
ServalSpriteFrames serval_sprite_frames(u32 id) {
    if (id >= serval_sprite_count)
        return (ServalSpriteFrames){.kind = SERVAL_SPRITE_ABSENT};
    const SpriteDraw* d = &sprite_draws[id];
    if (d->meta_frames)
        return (ServalSpriteFrames){.kind = SERVAL_SPRITE_META};
    // streaming: a sprite of a streamed group has no frames of its own either
    // (its frames go to its group's slots as they are drawn), so it is told
    // apart here, before the test below would call it not loaded.
    if (STREAMED(d))
        return (ServalSpriteFrames){.kind = SERVAL_SPRITE_STREAMED};
    if (!d->frame_count)
        return (ServalSpriteFrames){.kind = SERVAL_SPRITE_ABSENT};
    return (ServalSpriteFrames){.first_tile = (u16)(d->attr2 & ATTR2_ID_MASK),
                                .tiles_per_frame = d->tiles_per_frame,
                                .frame_count = d->frame_count,
                                .kind = SERVAL_SPRITE_ORDINARY};
}
