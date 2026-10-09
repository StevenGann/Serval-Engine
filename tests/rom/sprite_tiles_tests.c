// Tests for runtime sprite tiles, sprite_set_tiles() (src/gba/sprite_tiles.c):
// what reaches OBJ VRAM, when, and what is ignored; and the cost of the copy
// in VBlank. This file is Thumb code in ROM, like a game's code.

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/sprites.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/gba/internal.h"

#ifdef SERVAL_DEBUG
#define WARNINGS(n) (n)
#else
#define WARNINGS(n) 0
#endif

// Cycle budgets only hold for optimized code: Debug builds (-O0) check
// correctness but not timing.
#ifdef __OPTIMIZE__
#define CHECK_TIMING(cond) CHECK(cond)
#else
#define CHECK_TIMING(cond) ((void)(cond))
#endif

enum {
    SPR_CARD,     // 16x16, 2 frames: tiles 0-7
    SPR_PADDED,   // 8x8 with 2 tiles per frame, 3 frames: tiles 8-13
    SPR_DOT,      // 8x8: tile 14
    SPR_STRIP,    // 8x8, 4 frames: tiles 15-18
    SPR_BIG,      // 32x64: tiles 19-50
    SPR_META,     // a metasprite of SPR_DOT
    SPR_UNLOADED, // in the table, in no group loaded
    SPR_ROOM,     // 8x8, in the room group, loaded after a mark: tile 51
    SPRITE_COUNT
};

static const u32 blank[32 * 8]; // all zero: VRAM for frames composed later
static const u32 dot_tiles[8] = {0xD07D07D0, 0xD07D07D0};
static const u32 room_tiles[8] = {0x55555555, 0x55555555};
static const SpritePiece meta_pieces[] = {{.sprite = SPR_DOT}};

static const SpriteAsset card = {.size = SPRITE_16x16, .frame_count = 2, .tiles = blank};
static const SpriteAsset padded = {
    .size = SPRITE_8x8, .tiles_per_frame = 2, .frame_count = 3, .tiles = blank};
static const SpriteAsset dot = {.size = SPRITE_8x8, .tiles = dot_tiles};
static const SpriteAsset strip = {.size = SPRITE_8x8, .frame_count = 4, .tiles = blank};
static const SpriteAsset big = {.size = SPRITE_32x64, .tiles = blank};
static const SpriteAsset meta = {
    .flags = SPRITE_ASSET_METASPRITE, .pieces = meta_pieces, .piece_count = 1};
static const SpriteAsset room = {.size = SPRITE_8x8, .tiles = room_tiles};
static const SpriteAsset* const table[SPRITE_COUNT] = {
    [SPR_CARD] = &card, [SPR_PADDED] = &padded, [SPR_DOT] = &dot,      [SPR_STRIP] = &strip,
    [SPR_BIG] = &big,   [SPR_META] = &meta,     [SPR_UNLOADED] = &dot, [SPR_ROOM] = &room};

static const u16 palettes[16] = {[1] = 0x7FFF};
static const u16 main_ids[] = {SPR_CARD, SPR_PADDED, SPR_DOT, SPR_STRIP, SPR_BIG, SPR_META};
static const SpriteGroup main_group = {
    .sprite_ids = main_ids, .palettes = palettes, .sprite_count = 6, .palette_count = 1};
static const u16 room_ids[] = {SPR_ROOM};
static const SpriteGroup room_group = {
    .sprite_ids = room_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 1};

// Composed at run time, as a game would: in RAM.
static EWRAM_BSS u32 face_a[32 * 8];
static EWRAM_BSS u32 face_b[32 * 8];

// Tile t of OBJ VRAM.
static const u32* obj(u32 t) {
    return (const u32*)MEM_VRAM_OBJ + t * 8;
}

static void show_frame(void) {
    frame_begin();
    frame_end();
}

// The sprite table and the main group, with each pixel of face_a's tile t
// colored t + 1 and face_b's t + 9 (mod 16).
static void load(void) {
    sprite_table_set(table, SPRITE_COUNT); // also resets what is queued
    CHECK(sprite_group_load(&main_group));
    for (u32 w = 0; w < 32 * 8; w++) {
        face_a[w] = 0x11111111u * ((w / 8 + 1) & 15);
        face_b[w] = 0x11111111u * ((w / 8 + 9) & 15);
    }
}

static void copies_in_vblank_at_frame_end(void) {
    load();
    sprite_set_tiles(SPR_CARD, 1, face_a);
    CHECK(obj(4)[0] == 0); // not at the call
    frame_begin();
    sprite_draw(SPR_CARD, 1, 50, 50, 0);
    frame_end();
    // Frame 1's four tiles, and nothing around them: frame 0, the next sprite.
    CHECK(obj(4)[0] == 0x11111111u && obj(5)[3] == 0x22222222u && obj(7)[7] == 0x44444444u);
    CHECK(obj(3)[7] == 0 && obj(8)[0] == 0);
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 4); // the draw shows those tiles

    // Copied once: the queue is empty again, and the buffer is free to change.
    face_a[0] = 0x99999999u;
    show_frame();
    CHECK(obj(4)[0] == 0x11111111u);
}

static void later_call_for_a_frame_replaces_the_earlier(void) {
    load();
    sprite_set_tiles(SPR_CARD, 0, face_a);
    sprite_set_tiles(SPR_CARD, 1, face_a);
    sprite_set_tiles(SPR_CARD, 0, face_b);
    show_frame();
    CHECK(obj(0)[0] == 0x99999999u && obj(3)[7] == 0xCCCCCCCCu); // face_b
    CHECK(obj(4)[0] == 0x11111111u);                             // face_a
}

// tiles_per_frame larger than the size needs: a frame is that many tiles,
// all of them copied.
static void copies_tiles_per_frame_tiles(void) {
    load();
    sprite_set_tiles(SPR_PADDED, 2, face_a);
    show_frame();
    CHECK(obj(12)[0] == 0x11111111u && obj(13)[7] == 0x22222222u); // 8 + 2 x 2
    CHECK(obj(11)[7] == 0 && obj(14)[0] == dot_tiles[0]);
}

// Eight frames queue; the ninth is ignored, warning once. A call for a frame
// already queued still replaces it.
static void queue_holds_eight_frames(void) {
    load();
    CHECK(SPRITE_MAX_TILE_UPDATES == 8);
    u32 before = debug_warning_count();
    sprite_set_tiles(SPR_CARD, 0, face_a);
    sprite_set_tiles(SPR_CARD, 1, face_a);
    for (u8 f = 0; f < 3; f++)
        sprite_set_tiles(SPR_PADDED, f, face_a);
    for (u8 f = 0; f < 3; f++)
        sprite_set_tiles(SPR_STRIP, f, face_a);
    CHECK(debug_warning_count() == before); // 8 so far
    sprite_set_tiles(SPR_STRIP, 3, face_a);
    sprite_set_tiles(SPR_DOT, 0, face_a);
    CHECK(debug_warning_count() == before + WARNINGS(1));
    sprite_set_tiles(SPR_CARD, 0, face_b); // replaces: no new slot
    CHECK(debug_warning_count() == before + WARNINGS(1));
    show_frame();
    CHECK(obj(0)[0] == 0x99999999u && obj(4)[0] == 0x11111111u);
    CHECK(obj(8)[0] == 0x11111111u && obj(12)[0] == 0x11111111u);
    CHECK(obj(15)[0] == 0x11111111u && obj(17)[0] == 0x11111111u);
    CHECK(obj(18)[0] == 0 && obj(14)[0] == dot_tiles[0]); // the two ignored
}

// Each misuse is ignored, takes no place in the queue and warns once (until
// sprite_groups_reset()).
static void misuse_is_ignored_and_warned_once(void) {
    for (u32 round = 0; round < 3; round++) {
        if (round != 1)
            load(); // round 1: the same session, so no new warnings
        u32 before = debug_warning_count();
        sprite_set_tiles(SPRITE_COUNT, 0, face_a); // not in the table
        sprite_set_tiles(SPR_UNLOADED, 0, face_a);
        sprite_set_tiles(SPR_META, 0, face_a);
        sprite_set_tiles(SPR_DOT, 1, face_a); // it has one frame
        sprite_set_tiles(SPR_STRIP, 2, NULL);
        sprite_set_tiles(SPR_STRIP, 3, (const u32*)4); // not a pointer to memory
        CHECK(debug_warning_count() == before + WARNINGS(round == 1 ? 0 : 5));
        // None of them took a slot: eight more fit, the last being SPR_DOT's.
        sprite_set_tiles(SPR_CARD, 0, face_a);
        sprite_set_tiles(SPR_CARD, 1, face_a);
        for (u8 f = 0; f < 3; f++)
            sprite_set_tiles(SPR_PADDED, f, face_a);
        for (u8 f = 0; f < 2; f++)
            sprite_set_tiles(SPR_STRIP, f, face_a);
        sprite_set_tiles(SPR_DOT, 0, face_b);
        CHECK(debug_warning_count() == before + WARNINGS(round == 1 ? 0 : 5));
        show_frame();
        CHECK(obj(14)[0] == 0x99999999u && obj(16)[0] == 0x11111111u);
        CHECK(obj(17)[0] == 0 && obj(18)[0] == 0); // SPR_STRIP's frames 2 and 3
        CHECK(debug_warning_count() == before + WARNINGS(round == 1 ? 0 : 5));
    }
}

// Releasing to a mark drops the copies queued for the sprites it unloads,
// even when they load again in the same frame, and keeps the others;
// resetting drops them all.
static void release_and_reset_drop_queued_copies(void) {
    load();
    u32 mark = sprite_groups_mark();
    CHECK(sprite_group_load(&room_group));
    sprite_set_tiles(SPR_ROOM, 0, face_b);
    sprite_set_tiles(SPR_DOT, 0, face_a); // loaded before the mark: kept
    sprite_groups_release(mark);
    CHECK(sprite_group_load(&room_group)); // the same tile again
    show_frame();
    CHECK(obj(51)[0] == room_tiles[0]);
    CHECK(obj(14)[0] == 0x11111111u);

    // Queued after the reload, it is copied.
    sprite_set_tiles(SPR_ROOM, 0, face_b);
    show_frame();
    CHECK(obj(51)[0] == 0x99999999u);

    sprite_set_tiles(SPR_CARD, 0, face_b);
    sprite_groups_reset();
    CHECK(sprite_group_load(&main_group));
    show_frame();
    CHECK(obj(0)[0] == 0);
}

// The copy's cost in VBlank, from EWRAM (a composed frame) and from ROM,
// and the cost of the call frame_end() makes when nothing is queued.
static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

// Cycles serval_sprite_tiles_commit() takes, less the cost of measuring.
static u32 commit_cycles(void) {
    u32 t0 = cycles();
    u32 t1 = cycles();
    serval_sprite_tiles_commit();
    u32 t2 = cycles();
    return (t2 - t1) - (t1 - t0);
}

static void copy_costs_about_75_cycles_a_tile(void) {
    load();
    VBlankIntrWait();
    u32 empty = commit_cycles();
    sprite_set_tiles(SPR_BIG, 0, face_a);
    u32 from_ewram = commit_cycles();
    CHECK(obj(19)[0] == 0x11111111u && obj(49)[7] == 0xFFFFFFFFu); // its tiles 0 and 30
    sprite_set_tiles(SPR_BIG, 0, blank);
    u32 from_rom = commit_cycles();
    CHECK(obj(19)[0] == 0 && obj(49)[7] == 0);
    debug_log(text_format("sprite_set_tiles: a 32-tile frame copied in %u cycles from EWRAM (%u a "
                          "tile), %u from ROM; nothing queued: %u",
                          from_ewram, from_ewram / 32, from_rom, empty));
    CHECK_TIMING(empty < 60);
    CHECK_TIMING(from_ewram < 32 * 85 && from_rom < 32 * 85);
    show_frame();
}

TEST_SUITE(gba_sprite_tiles_tests, "gba_sprite_tiles",
           {"copies_in_vblank_at_frame_end", copies_in_vblank_at_frame_end},
           {"later_call_for_a_frame_replaces_the_earlier",
            later_call_for_a_frame_replaces_the_earlier},
           {"copies_tiles_per_frame_tiles", copies_tiles_per_frame_tiles},
           {"queue_holds_eight_frames", queue_holds_eight_frames},
           {"misuse_is_ignored_and_warned_once", misuse_is_ignored_and_warned_once},
           {"release_and_reset_drop_queued_copies", release_and_reset_drop_queued_copies},
           {"copy_costs_about_75_cycles_a_tile", copy_costs_about_75_cycles_a_tile});
