// Tests for streamed sprite groups (SPRITE_GROUP_STREAMED; src/gba/sprite_stream.c
// and its hooks in sprites.c), checked against what ends up in VRAM and OAM:
// slots at the top of sprite VRAM, frames copied in the VBlank that shows
// them, frames still in a slot reused, the least recently drawn slot taking a
// new frame, frames past the slots not drawn, marks, and the costs.

// Uses SPRITE_ASSET_LZ77 (planned) on purpose, to check that streamed groups
// refuse it.
#define SERVAL_NO_PLANNED_WARNINGS

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/math.h"
#include "serval/sprites.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/core/warn.h"
#include "../../src/gba/internal.h"

enum { SPR_HERO, SPR_DOT, SPR_META, SPR_SPARK, SPR_FILLER, SPR_PACKED, SPR_BOSS, SPRITE_COUNT };

// The hero: 8 frames of 16x16 (4 tiles each), in EWRAM so that each word can
// say which frame and tile it belongs to (a sprite's tiles may be in RAM).
#define HERO_FRAMES 8
#define HERO_TILES 4
#define MARK 0x5EEDF00Du // written over a slot to see whether it is copied again
static EWRAM_BSS u32 hero_tiles[HERO_FRAMES * HERO_TILES * 8];

static u32 hero_word(u32 frame, u32 tile, u32 word) {
    return 0xA0000000u | frame << 16 | tile << 8 | word;
}

static const SpriteAsset hero = {
    .size = SPRITE_16x16, .frame_count = HERO_FRAMES, .tiles = hero_tiles, .origin_x = 8};
static const u32 dot_tiles[8] = {0x11111111, 0x11111111};
static const SpriteAsset dot = {.size = SPRITE_8x8, .tiles = dot_tiles};
// A metasprite of two hero frames side by side, in two frames.
static const SpritePiece meta_pieces[] = {
    {.x = -8, .sprite = SPR_HERO, .frame = 0},
    {.x = 8, .sprite = SPR_HERO, .frame = 5},
    {.x = -8, .sprite = SPR_HERO, .frame = 1},
    {.x = 8, .sprite = SPR_HERO, .frame = 1, .flags = SPRITE_FLIP_H},
};
static const SpriteAsset meta = {
    .flags = SPRITE_ASSET_METASPRITE, .pieces = meta_pieces, .piece_count = 2, .frame_count = 2};
static const SpriteAsset spark = {.size = SPRITE_8x8, .tiles = dot_tiles};
// Tiles enough to fill sprite VRAM: any ROM bytes serve as pixels.
static const u32* const rom_bytes = (const u32*)0x08000000;
// Set by each test that uses it: tiles_per_frame and frame_count give it any
// number of tiles.
static SpriteAsset filler SERVAL_EWRAM_DATA = {.size = SPRITE_8x8};
static const SpriteAsset packed = {
    .size = SPRITE_8x8, .tiles = dot_tiles, .flags = SPRITE_ASSET_LZ77};
// For the costs: 16 frames of 64x64 (64 tiles each), straight from ROM.
static const SpriteAsset boss = {.size = SPRITE_64x64, .frame_count = 16, .tiles = rom_bytes};

static const SpriteAsset* const table[SPRITE_COUNT] = {
    [SPR_HERO] = &hero,     [SPR_DOT] = &dot,       [SPR_META] = &meta, [SPR_SPARK] = &spark,
    [SPR_FILLER] = &filler, [SPR_PACKED] = &packed, [SPR_BOSS] = &boss,
};

static const u16 palettes[48] = {[1] = 0x1111, [17] = 0x2222, [33] = 0x3333};

// The resident dot (tile 0, bank 0), then the hero streamed (banks 1-2).
static const u16 dot_ids[] = {SPR_DOT};
static const SpriteGroup dot_group = {
    .sprite_ids = dot_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 1};
static const u16 hero_ids[] = {SPR_HERO, SPR_META};
static const SpriteGroup hero_group = {.sprite_ids = hero_ids,
                                       .palettes = palettes + 16,
                                       .sprite_count = 2,
                                       .palette_count = 2,
                                       .flags = SPRITE_GROUP_STREAMED,
                                       .slots = 3};
static const u16 filler_ids[] = {SPR_FILLER};
static const SpriteGroup filler_group = {
    .sprite_ids = filler_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 1};

static const TILE* const obj_tiles = (const TILE*)MEM_VRAM_OBJ;
static TILE* const obj_vram = (TILE*)MEM_VRAM_OBJ;

// The hero's three slots, from the top of sprite VRAM: 1012, 1016, 1020.
#define SLOT(s) (1012u + 4u * (s))

#define OAM_TILE(k) (oam_mem[k].attr2 & ATTR2_ID_MASK)
#define OAM_HIDDEN(k) ((oam_mem[k].attr0 & 0x0300) == 0x0200) // not affine, disabled
#define OAM_BANK(k) (oam_mem[k].attr2 >> 12)

static bool slot_holds(u32 tile, u32 frame) {
    for (u32 t = 0; t < HERO_TILES; t++) {
        for (u32 w = 0; w < 8; w++) {
            if (obj_tiles[tile + t].data[w] != hero_word(frame, t, w))
                return false;
        }
    }
    return true;
}

static void mark_slot(u32 tile) {
    for (u32 t = 0; t < HERO_TILES; t++) {
        for (u32 w = 0; w < 8; w++)
            obj_vram[tile + t].data[w] = MARK;
    }
}

static bool slot_marked(u32 tile) {
    for (u32 t = 0; t < HERO_TILES; t++) {
        for (u32 w = 0; w < 8; w++) {
            if (obj_tiles[tile + t].data[w] != MARK)
                return false;
        }
    }
    return true;
}

// A new sprite table with the resident dot loaded (tile 0, bank 0).
static void load_dot(void) {
    for (u32 f = 0; f < HERO_FRAMES; f++) {
        for (u32 t = 0; t < HERO_TILES; t++) {
            for (u32 w = 0; w < 8; w++)
                hero_tiles[(f * HERO_TILES + t) * 8 + w] = hero_word(f, t, w);
        }
    }
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&dot_group));
}

// The hero with `slots` slots, every slot marked first.
static void load_hero(u32 slots) {
    for (u32 s = 0; s < 3; s++)
        mark_slot(SLOT(s));
    SpriteGroup g = hero_group;
    g.slots = (u8)slots;
    CHECK(sprite_group_load(&g));
}

static void load(u32 slots) {
    load_dot();
    load_hero(slots);
}

// Fills sprite VRAM from tile 1 (after the dot) to the top with resident
// groups: 4 x 255 tiles of ROM bytes, then three dots.
static void fill_vram(void) {
    filler = (SpriteAsset){
        .size = SPRITE_8x8, .tiles = rom_bytes, .tiles_per_frame = 255, .frame_count = 4};
    CHECK(sprite_group_load(&filler_group)); // tiles 1-1020
    CHECK(sprite_group_load(&dot_group));
    CHECK(sprite_group_load(&dot_group));
    CHECK(sprite_group_load(&dot_group)); // 1023
}

// Whether `tile` holds what fill_vram() put there (tiles 1-1020).
static bool holds_filler(u32 tile) {
    for (u32 w = 0; w < 8; w++) {
        if (obj_tiles[tile].data[w] != rom_bytes[(tile - 1) * 8 + w])
            return false;
    }
    return true;
}

// Draws hero frames, a list ending with -1, in one frame.
static void draw_frames(const int* frames) {
    frame_begin();
    for (int k = 0; frames[k] >= 0; k++)
        sprite_draw(SPR_HERO, (u8)frames[k], 20 + 20 * k, 40, 0);
    frame_end();
}

#ifdef SERVAL_DEBUG
// Whether `text` contains `part` (the ROM has no C library's strstr).
static bool contains(const char* text, const char* part) {
    for (; *text; text++) {
        u32 k = 0;
        while (part[k] && text[k] == part[k])
            k++;
        if (!part[k])
            return true;
    }
    return false;
}
#endif

// Checks that `count` warnings were reported since `before` (none in release
// builds, which report nothing).
static void check_warnings(u32 before, u32 count) {
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + count);
#else
    (void)count;
    CHECK(debug_warning_count() == before);
#endif
}

// A streamed group copies only its palettes when it loads: its slots, at
// the top of sprite VRAM, stay as they were until frames are drawn. Resident
// groups load below them, up to the last free tile, and so do the slots of
// the next streamed group.
static void slots_are_taken_from_the_top_of_vram(void) {
    load(3);
    CHECK(slot_marked(SLOT(0)) && slot_marked(SLOT(1)) && slot_marked(SLOT(2)));
    CHECK(pal_obj_bank[1][1] == 0x2222 && pal_obj_bank[2][1] == 0x3333);
    // 1 tile for the dot, 12 for the slots: 1011 free. 4 x 253 doesn't fit
    // (and the failure loads nothing); 4 x 252 and three dots do.
    filler = (SpriteAsset){
        .size = SPRITE_8x8, .tiles = rom_bytes, .tiles_per_frame = 253, .frame_count = 4};
    CHECK(!sprite_group_load(&filler_group));
    filler.tiles_per_frame = 252;
    CHECK(sprite_group_load(&filler_group));
    CHECK(sprite_group_load(&dot_group)); // tile 1009
    CHECK(sprite_group_load(&dot_group)); // 1010
    CHECK(sprite_group_load(&dot_group)); // 1011, the last below the slots
    CHECK(!sprite_group_load(&dot_group));
    CHECK(slot_marked(SLOT(0)));
    // A second streamed group takes its slots below the first's.
    load(3);
    static const u16 spark_ids[] = {SPR_SPARK};
    static const SpriteGroup sparks = {.sprite_ids = spark_ids,
                                       .palettes = palettes,
                                       .sprite_count = 1,
                                       .palette_count = 1,
                                       .flags = SPRITE_GROUP_STREAMED,
                                       .slots = 2};
    CHECK(sprite_group_load(&sparks));
    frame_begin();
    sprite_draw(SPR_SPARK, 0, 10, 10, 0);
    sprite_draw(SPR_HERO, 0, 30, 10, 0);
    frame_end();
    CHECK(OAM_TILE(0) == 1010 && OAM_TILE(1) == SLOT(0));
    CHECK(OAM_BANK(0) == 3 && OAM_BANK(1) == 1);
    CHECK(obj_tiles[1010].data[0] == 0x11111111 && slot_holds(SLOT(0), 0));
}

// A frame drawn for the first time is copied from ROM to its slot by
// frame_end(), in VBlank, not by the draw; the OAM shows it from there.
static void new_frames_are_copied_in_the_vblank_that_shows_them(void) {
    load(3);
    frame_begin();
    sprite_draw(SPR_HERO, 2, 48, 30, SPRITE_FLIP_H);
    CHECK(slot_marked(SLOT(0))); // not yet
    frame_end();
    CHECK(slot_holds(SLOT(0), 2) && slot_marked(SLOT(1)));
    CHECK(OAM_TILE(0) == SLOT(0) && OAM_BANK(0) == 1 && !OAM_HIDDEN(0) && OAM_HIDDEN(1));
    CHECK((oam_mem[0].attr1 & ATTR1_X_MASK) == 40 && (oam_mem[0].attr0 & ATTR0_Y_MASK) == 30);
    CHECK((oam_mem[0].attr0 & ATTR0_SHAPE_MASK) == ATTR0_SQUARE);
    CHECK((oam_mem[0].attr1 & ATTR1_SIZE_MASK) == ATTR1_SIZE_16 &&
          (oam_mem[0].attr1 & ATTR1_HFLIP));
    CHECK((oam_mem[0].attr2 & ATTR2_PRIO_MASK) == ATTR2_PRIO(2));
}

// A frame still in a slot is drawn from it with no copy: written over, the
// slot keeps what was written. Draws of the same frame share its slot, and
// the resident sprites are drawn as before.
static void frames_still_in_a_slot_are_not_copied_again(void) {
    load(3);
    draw_frames((const int[]){2, -1});
    CHECK(slot_holds(SLOT(0), 2));
    mark_slot(SLOT(0));
    frame_begin();
    sprite_draw(SPR_HERO, 2, 10, 10, 0);
    sprite_draw(SPR_DOT, 0, 30, 10, 0);
    sprite_draw(SPR_HERO, 2, 50, 10, 0);
    sprite_draw(SPR_HERO, 3, 70, 10, 0);
    frame_end();
    CHECK(slot_marked(SLOT(0))); // frame 2: no copy
    CHECK(OAM_TILE(0) == SLOT(0) && OAM_TILE(1) == 0 && OAM_TILE(2) == SLOT(0));
    CHECK(OAM_TILE(3) == SLOT(1) && slot_holds(SLOT(1), 3));
    CHECK(sprite_stats().drawn == 4 && sprite_stats().dropped == 0);
}

// A new frame takes the slot drawn from least recently (not one drawn from
// this frame); the others keep their frames.
static void the_least_recently_drawn_slot_takes_a_new_frame(void) {
    load(3);
    draw_frames((const int[]){0, 1, 2, -1}); // slots 0, 1, 2
    CHECK(OAM_TILE(0) == SLOT(0) && OAM_TILE(1) == SLOT(1) && OAM_TILE(2) == SLOT(2));
    draw_frames((const int[]){2, 0, -1}); // slot 1 (frame 1) is now the oldest
    draw_frames((const int[]){3, -1});
    CHECK(OAM_TILE(0) == SLOT(1) && slot_holds(SLOT(1), 3));
    mark_slot(SLOT(0));
    mark_slot(SLOT(2));
    draw_frames((const int[]){0, 2, -1}); // still there: no copies
    CHECK(OAM_TILE(0) == SLOT(0) && OAM_TILE(1) == SLOT(2));
    CHECK(slot_marked(SLOT(0)) && slot_marked(SLOT(2)));
    // Frame 3 (slot 1) was drawn before 0 and 2 were: frame 1 goes there.
    draw_frames((const int[]){1, -1});
    CHECK(OAM_TILE(0) == SLOT(1) && slot_holds(SLOT(1), 1));
    // An animation through every frame in one slot (0 means 1).
    load(0);
    for (u32 f = 0; f < HERO_FRAMES; f++) {
        draw_frames((const int[]){(int)f, -1});
        CHECK(OAM_TILE(0) == 1020 && slot_holds(1020, f) && !OAM_HIDDEN(0));
    }
}

// More distinct frames in one frame than slots: the extra ones are not
// drawn, counted as dropped, with one warning in debug builds; they take no
// slot from the frames drawn.
static void frames_past_the_slots_are_not_drawn(void) {
    load(3);
    for (u32 frame = 0; frame < 2; frame++) {
        u32 before = debug_warning_count();
        frame_begin();
        sprite_draw(SPR_HERO, 0, 10, 10, 0);
        sprite_draw(SPR_HERO, 1, 30, 10, 0);
        sprite_draw(SPR_HERO, 2, 50, 10, 0);
        sprite_draw(SPR_HERO, 3, 70, 10, 0); // no slot left
        sprite_draw(SPR_HERO, 1, 90, 10, 0); // in a slot
        sprite_draw(SPR_DOT, 0, 110, 10, 0);
        frame_end();
        CHECK(OAM_TILE(0) == SLOT(0) && OAM_TILE(1) == SLOT(1) && OAM_TILE(2) == SLOT(2));
        CHECK(OAM_TILE(3) == SLOT(1) && (oam_mem[3].attr1 & ATTR1_X_MASK) == 82);
        CHECK(OAM_TILE(4) == 0 && OAM_HIDDEN(5));
        SpriteStats stats = sprite_stats();
        CHECK(stats.drawn == 5 && stats.dropped == 1);
        check_warnings(before, frame == 0);
        if (frame == 0) {
            mark_slot(SLOT(0));
            mark_slot(SLOT(1));
            mark_slot(SLOT(2));
        }
    }
    // The second time, frames 0-2 were in their slots: nothing was copied.
    CHECK(slot_marked(SLOT(0)) && slot_marked(SLOT(1)) && slot_marked(SLOT(2)));
}

// Draws that take no hardware sprite (off screen, hidden, past the 128th)
// take no slot either.
static void only_shown_draws_take_a_slot(void) {
    load(1);
    ecs_reset();
    u32 i = entity_index(entity_create(C_POS | C_SPR));
    pos_x[i] = pos_y[i] = FX(50);
    spr_id[i] = SPR_HERO;
    spr_frame[i] = 4;
    spr_flags[i] = SPRITE_HIDDEN;
    frame_begin();
    sprite_draw(SPR_HERO, 0, -100, 10, 0); // off screen
    sprite_draw_rotated(SPR_HERO, 1, 10, 300, ANGLE_DEG(45), 0);
    sys_render();                        // hidden
    sprite_draw(SPR_HERO, 2, 10, 10, 0); // takes the one slot
    frame_end();
    CHECK(OAM_TILE(0) == 1020 && slot_holds(1020, 2) && OAM_HIDDEN(1));
    CHECK(sprite_stats().dropped == 0 && sprite_stats().matrices == 0);
    frame_begin();
    for (u32 k = 0; k < 128; k++)
        sprite_draw(SPR_DOT, 0, 10, 10, 0);
    sprite_draw(SPR_HERO, 5, 10, 10, 0); // OAM full: dropped, but no slot
    frame_end();
    CHECK(sprite_stats().dropped == 1);
    mark_slot(1020);
    draw_frames((const int[]){2, -1});
    CHECK(slot_marked(1020)); // frame 2 was still there
    ecs_reset();
}

// Every way of drawing reaches the slots: flips, SPRITE_PALETTE, SPRITE_BLEND,
// rotation and scaling, the render systems' plain and out-of-line paths,
// and sys_animate stepping an entity's frames.
static void every_way_of_drawing_uses_the_slots(void) {
    load(3);
    frame_begin();
    sprite_draw(SPR_HERO, 0, 40, 40, SPRITE_FLIP_V | SPRITE_PALETTE(1));
    sprite_draw_rotated(SPR_HERO, 1, 80, 40, ANGLE_DEG(90), 0);
    sprite_draw_ex(SPR_HERO, 1, 120, 40, 0, FX(2), FX(2), SPRITE_ABOVE_HUD);
    frame_end();
    CHECK(OAM_TILE(0) == SLOT(0) && OAM_BANK(0) == 2 && (oam_mem[0].attr1 & ATTR1_VFLIP));
    CHECK(OAM_TILE(1) == SLOT(1) && (oam_mem[1].attr0 & ATTR0_AFF_DBL) == ATTR0_AFF_DBL);
    CHECK(OAM_TILE(2) == SLOT(1) && (oam_mem[2].attr2 & ATTR2_PRIO_MASK) == ATTR2_PRIO(0));
    CHECK(slot_holds(SLOT(0), 0) && slot_holds(SLOT(1), 1) && sprite_stats().matrices == 2);
    // SPRITE_BLEND: semi-transparent, plain and rotated (attr0 bits 10-11: 1).
    frame_begin();
    sprite_draw(SPR_HERO, 0, 40, 40, SPRITE_BLEND);
    sprite_draw_rotated(SPR_HERO, 1, 80, 40, ANGLE_DEG(90), SPRITE_BLEND);
    frame_end();
    CHECK((oam_mem[0].attr0 & 0x0C00) == 0x0400 && (oam_mem[1].attr0 & 0x0C00) == 0x0400);
    CHECK(OAM_TILE(0) == SLOT(0) && OAM_TILE(1) == SLOT(1));

    // Entities, animated by sys_animate (one display frame per frame).
    ecs_reset();
    u32 a = entity_index(entity_create(C_POS | C_SPR | C_ANIM));
    u32 b = entity_index(entity_create(C_POS | C_SPR));
    pos_x[a] = pos_y[a] = FX(60);
    pos_x[b] = pos_y[b] = FX(90);
    spr_id[a] = spr_id[b] = SPR_HERO;
    spr_frame[b] = 7;
    spr_flags[b] = SPRITE_PALETTE(1); // the out-of-line path
    for (u32 step = 0; step < 4; step++) {
        frame_begin();
        u32 frame = spr_frame[a];
        if (step & 1)
            sys_render_by_depth();
        else
            sys_render();
        frame_end();
        sys_animate();
        CHECK(slot_holds(OAM_TILE(0), frame) && slot_holds(OAM_TILE(1), 7));
        CHECK(OAM_BANK(0) == 1 && OAM_BANK(1) == 2);
    }
    ecs_reset();
}

// A metasprite in a streamed group takes no slot; its pieces take one each
// for the frames of streamed sprites they show. A streamed group of
// metasprites only takes no VRAM.
static void metasprites_draw_streamed_pieces(void) {
    load(3);
    frame_begin();
    sprite_draw(SPR_META, 0, 100, 60, 0);
    sprite_draw(SPR_META, 1, 100, 100, 0); // frame 1 twice, one flipped: one slot
    frame_end();
    CHECK(OAM_TILE(0) == SLOT(0) && slot_holds(SLOT(0), 0));
    CHECK(OAM_TILE(1) == SLOT(1) && slot_holds(SLOT(1), 5));
    CHECK(OAM_TILE(2) == SLOT(2) && OAM_TILE(3) == SLOT(2) && slot_holds(SLOT(2), 1));
    CHECK((oam_mem[3].attr1 & ATTR1_HFLIP) && !(oam_mem[2].attr1 & ATTR1_HFLIP));
    CHECK((oam_mem[0].attr1 & ATTR1_X_MASK) == 100 - 8 - 8);

    static const u16 meta_ids[] = {SPR_META};
    static const SpriteGroup metas = {
        .sprite_ids = meta_ids, .sprite_count = 1, .flags = SPRITE_GROUP_STREAMED, .slots = 5};
    load_dot();
    CHECK(sprite_group_load(&metas));
    fill_vram(); // all 1024 tiles: the metasprites took none
}

static bool hero_drawn(void) {
    frame_begin();
    sprite_draw(SPR_HERO, 0, 50, 50, 0);
    frame_end();
    return !OAM_HIDDEN(0);
}

// Releasing to a mark unloads the streamed groups loaded since, frees their
// slots for the next loads, and drops the copies queued for them, which
// would otherwise overwrite what loads there. So does a reset.
static void marks_release_streamed_groups(void) {
    load_dot();
    u32 mark = sprite_groups_mark();
    load_hero(3);
    CHECK(sprite_groups_mark() != mark); // a streamed group counts as a load
    frame_begin();
    sprite_draw(SPR_HERO, 2, 10, 10, 0); // its copy is queued
    sprite_groups_release(mark);
    fill_vram(); // up to tile 1023: the slots are free again
    frame_end();
    CHECK(holds_filler(SLOT(0)) && holds_filler(SLOT(2))); // the copy was dropped
    u32 before = debug_warning_count();
    CHECK(!hero_drawn() && !hero_drawn()); // not loaded: warns once
    check_warnings(before, 1);
    // Loaded again after the mark: the same slots.
    sprite_groups_release(mark);
    load_hero(3);
    CHECK(hero_drawn() && OAM_TILE(0) == SLOT(0) && slot_holds(SLOT(0), 0));

    // A streamed group loaded before the mark stays, and keeps its frames.
    static const u16 spark_ids[] = {SPR_SPARK};
    static const SpriteGroup sparks = {.sprite_ids = spark_ids,
                                       .palettes = palettes,
                                       .sprite_count = 1,
                                       .palette_count = 1,
                                       .flags = SPRITE_GROUP_STREAMED,
                                       .slots = 2};
    load(3);
    CHECK(hero_drawn());
    mark_slot(SLOT(0));
    mark = sprite_groups_mark();
    CHECK(sprite_group_load(&sparks)); // tiles 1010-1011
    CHECK(sprite_group_load(&dot_group));
    sprite_groups_release(mark);
    CHECK(hero_drawn() && OAM_TILE(0) == SLOT(0) && slot_marked(SLOT(0)));
    CHECK(sprite_group_load(&sparks)); // the same slots again
    frame_begin();
    sprite_draw(SPR_SPARK, 0, 10, 10, 0);
    frame_end();
    CHECK(OAM_TILE(0) == 1010 && OAM_BANK(0) == 3);

    // A reset (here by sprite_table_set) drops the queued copies too.
    load(3);
    frame_begin();
    sprite_draw(SPR_HERO, 2, 10, 10, 0);
    load_dot();
    fill_vram();
    frame_end();
    CHECK(holds_filler(SLOT(0)));
}

// Streamed groups refuse SPRITE_ASSET_LZ77 sprites (their frames are copied
// straight from ROM) and reserved group flags, and stop when the palette
// banks, the 256 slots or the free VRAM run out; each refusal warns and loads
// nothing.
static void streamed_groups_refuse_what_they_cannot_hold(void) {
    static const u16 packed_ids[] = {SPR_PACKED};
    static const u16 spark_ids[] = {SPR_SPARK};
    SpriteGroup g = {.sprite_ids = packed_ids,
                     .palettes = palettes,
                     .sprite_count = 1,
                     .palette_count = 1,
                     .flags = SPRITE_GROUP_STREAMED,
                     .slots = 1};
    sprite_table_set(table, SPRITE_COUNT);
    u32 before = debug_warning_count();
    CHECK(!sprite_group_load(&g));
#ifdef SERVAL_DEBUG
    // Refused as streamed, not only as planned: LZ77 sprites will load in
    // resident groups, never in streamed ones.
    CHECK(contains(serval_warn_text(), "streamed group"));
#endif
    g.sprite_ids = spark_ids;
    for (u32 bit = 1; bit < 8; bit++) {
        g.flags = (u8)(SPRITE_GROUP_STREAMED | 1u << bit);
        CHECK(!sprite_group_load(&g));
    }
    check_warnings(before, 8);
    // As many streamed groups as palette banks (each holds a sprite, which
    // needs a palette), each slot below the last.
    g.flags = SPRITE_GROUP_STREAMED;
    for (u32 k = 0; k < 16; k++)
        CHECK(sprite_group_load(&g));
    CHECK(!sprite_group_load(&g)); // the banks ran out
    frame_begin();
    sprite_draw(SPR_SPARK, 0, 10, 10, 0);
    frame_end();
    CHECK(OAM_TILE(0) == 1024 - 16 && OAM_BANK(0) == 15); // the newest group's

    // 256 slots in all.
    sprite_table_set(table, SPRITE_COUNT);
    g.slots = 255;
    CHECK(sprite_group_load(&g));
    g.slots = 2;
    before = debug_warning_count();
    CHECK(!sprite_group_load(&g));
    check_warnings(before, 1);
    g.slots = 1;
    CHECK(sprite_group_load(&g));

    // Slots that don't fit the free VRAM: none when it is full; one slot of
    // the hero's 4 tiles in the last 4.
    load_dot();
    filler = (SpriteAsset){
        .size = SPRITE_8x8, .tiles = rom_bytes, .tiles_per_frame = 255, .frame_count = 4};
    CHECK(sprite_group_load(&filler_group));
    CHECK(sprite_group_load(&dot_group));
    CHECK(sprite_group_load(&dot_group));
    CHECK(sprite_group_load(&dot_group));
    g.slots = 1;
    CHECK(!sprite_group_load(&g)); // no tile left
    sprite_table_set(table, SPRITE_COUNT);
    filler.frame_count = 4;
    filler.tiles_per_frame = 255;
    CHECK(sprite_group_load(&filler_group)); // tiles 0-1019
    load_hero(0);                            // 1020-1023
    CHECK(!sprite_group_load(&g));
    CHECK(hero_drawn() && OAM_TILE(0) == 1020);
}

// Palette writes (sprite_set_colors()) reach a streamed sprite's group's
// banks, as a resident sprite's: it has no frames of its own, but it is
// loaded. And a streamed group loaded after a write to its banks, in the same
// frame, wins over it, as a resident group does.
static void palette_writes_reach_streamed_sprites(void) {
    static const Color red = 0x001F, blue = 0x7C00;
    load(3); // the hero's palettes are banks 1 and 2
    u32 before = debug_warning_count();
    frame_begin();
    sprite_set_colors(SPR_HERO, 1, &red, 1);   // its palette 0, color 1
    sprite_set_colors(SPR_HERO, 17, &blue, 1); // its palette 1, color 1
    CHECK(pal_obj_bank[1][1] == 0x2222);       // in VBlank, not at once
    frame_end();
    CHECK(debug_warning_count() == before);
    CHECK(pal_obj_bank[1][1] == red && pal_obj_bank[2][1] == blue);
    CHECK(pal_obj_bank[0][1] == 0x1111); // the dot's bank: not the hero's

    load_dot();
    u32 mark = sprite_groups_mark();
    load_hero(3);
    frame_begin();
    sprite_set_colors(SPR_HERO, 1, &red, 1);
    sprite_groups_release(mark);
    load_hero(3); // the same banks, its own colors
    frame_end();
    CHECK(pal_obj_bank[1][1] == 0x2222);
}

// A frame the sprite doesn't have is not drawn and takes no slot (warning
// once in debug builds).
static void bad_frames_are_not_drawn(void) {
    load(1);
    draw_frames((const int[]){3, -1});
    u32 before = debug_warning_count();
    draw_frames((const int[]){HERO_FRAMES, HERO_FRAMES, -1});
    CHECK(OAM_HIDDEN(0));
    check_warnings(before, 1);
    mark_slot(1020);
    draw_frames((const int[]){3, -1});
    CHECK(slot_marked(1020));
}

// --- Costs -------------------------------------------------------------------

// Cycle budgets only hold for optimized code: Debug builds (-O0) check
// correctness but not timing.
#ifdef __OPTIMIZE__
#define CHECK_TIMING(cond) CHECK(cond)
#else
#define CHECK_TIMING(cond) ((void)(cond))
#endif

static u32 cycles_now(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

// What streaming costs: the copies of new frames, frame_end()'s VBlank step
// (timed by calling it directly, before frame_end(), which then has nothing
// to copy), and the draws: a new frame, a frame in its slot (the slot of the
// group's last draw, which is tried first, or found by a search of the
// slots), against a resident sprite's.
static void streaming_costs_are_logged(void) {
    static const u16 boss_ids[] = {SPR_BOSS};
    static const SpriteGroup bosses = {.sprite_ids = boss_ids,
                                       .palettes = palettes,
                                       .sprite_count = 1,
                                       .palette_count = 1,
                                       .flags = SPRITE_GROUP_STREAMED,
                                       .slots = 8};
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&dot_group));
    CHECK(sprite_group_load(&bosses)); // 8 slots of 64 tiles
    frame_begin();
    u32 t0 = cycles_now();
    sprite_draw(SPR_BOSS, 0, 0, 0, 0);
    u32 t1 = cycles_now();
    sprite_draw(SPR_BOSS, 0, 64, 0, 0);
    u32 t2 = cycles_now();
    sprite_draw(SPR_DOT, 0, 128, 0, 0);
    u32 t3 = cycles_now();
    serval_stream_commit();
    u32 t4 = cycles_now();
    frame_end();
    u32 miss = t1 - t0, hit = t2 - t1, resident = t3 - t2, one = t4 - t3;

    // Eight new frames: 512 tiles.
    frame_begin();
    for (u32 f = 8; f < 16; f++)
        sprite_draw(SPR_BOSS, (u8)f, (int)(f & 3) * 60, (int)(f >> 2 & 1) * 64, 0);
    t0 = cycles_now();
    serval_stream_commit();
    t1 = cycles_now();
    frame_end();
    u32 eight = t1 - t0;
    CHECK(sprite_stats().drawn == 8 && sprite_stats().dropped == 0);

    // The same eight again: all in their slots, nothing to copy. Frame 8 is
    // the furthest from the last draw's slot.
    frame_begin();
    for (u32 f = 15; f > 8; f--)
        sprite_draw(SPR_BOSS, (u8)f, (int)(f & 3) * 60, (int)(f >> 2 & 1) * 64, 0);
    t0 = cycles_now();
    sprite_draw(SPR_BOSS, 8, 0, 0, 0);
    t1 = cycles_now();
    serval_stream_commit();
    t2 = cycles_now();
    frame_end();
    u32 search = t1 - t0, idle = t2 - t1;

    // For comparison: the timer reads alone, and the OAM copy, step 1 of
    // the flush.
    t0 = cycles_now();
    t1 = cycles_now();
    oam_copy(oam_mem, serval_shadow_oam, 128);
    t2 = cycles_now();
    u32 timer = t1 - t0, oam = t2 - t1;

    debug_log(text_format("stream: draw new %u cycles, in slot %u (searched %u), resident %u", miss,
                          hit, search, resident));
    debug_log(text_format("stream: VBlank copies: 1 frame of 64x64 %u cycles, 8 frames (512 "
                          "tiles) %u, none %u",
                          one, eight, idle));
    debug_log(text_format("stream: timer reads alone %u cycles, the OAM copy %u", timer, oam));
    // About 56 cycles a tile (sprites.h): 3,550 for a 64x64 frame. Allow
    // for the call and the timer reads.
    CHECK_TIMING(one < 64 * 70 && eight < 512 * 70 && idle < 200);
    CHECK_TIMING(miss < 1500 && hit < 1000 && search < 1200);
}

TEST_SUITE(gba_stream_tests, "sprite streaming",
           {"slots_are_taken_from_the_top_of_vram", slots_are_taken_from_the_top_of_vram},
           {"new_frames_are_copied_in_the_vblank_that_shows_them",
            new_frames_are_copied_in_the_vblank_that_shows_them},
           {"frames_still_in_a_slot_are_not_copied_again",
            frames_still_in_a_slot_are_not_copied_again},
           {"the_least_recently_drawn_slot_takes_a_new_frame",
            the_least_recently_drawn_slot_takes_a_new_frame},
           {"frames_past_the_slots_are_not_drawn", frames_past_the_slots_are_not_drawn},
           {"only_shown_draws_take_a_slot", only_shown_draws_take_a_slot},
           {"every_way_of_drawing_uses_the_slots", every_way_of_drawing_uses_the_slots},
           {"metasprites_draw_streamed_pieces", metasprites_draw_streamed_pieces},
           {"marks_release_streamed_groups", marks_release_streamed_groups},
           {"streamed_groups_refuse_what_they_cannot_hold",
            streamed_groups_refuse_what_they_cannot_hold},
           {"palette_writes_reach_streamed_sprites", palette_writes_reach_streamed_sprites},
           {"bad_frames_are_not_drawn", bad_frames_are_not_drawn},
           {"streaming_costs_are_logged", streaming_costs_are_logged});
