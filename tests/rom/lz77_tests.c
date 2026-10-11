// lz77: LZ77-compressed sprites and tilesets (SPRITE_ASSET_LZ77, TILESET_LZ77)
// on the hardware: loaded, they leave VRAM byte for byte as their plain
// versions do (the platformer's castle tileset and dragon, and a sprite with
// padding between frames, packed by tools/lz77.py through serval_add_lz77(),
// tests/CMakeLists.txt); bad data is refused and loads nothing; the BIOS's
// decoder reads VRAM as src/web/lz77.c says (a vector that isn't VRAM-safe);
// and what loading costs. In the GBA test ROM, with mGBA's BIOS (its own
// replacement, without a BIOS file), and in the web test page
// (tests/web/main.c), with the web's stand-in.

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/map.h"
#include "serval/sprites.h"
#include "serval/text.h"

#include <string.h>
#include <tonc.h>

#include "../../src/core/lz77_internal.h"
#include "lz77_tiles.h" // padded_lz77, castle_tiles_lz77, dragon_tiles_lz77

#ifdef SERVAL_DEBUG
#define WARNINGS_ON 1
#else
#define WARNINGS_ON 0
#endif

static const u32 padded_tiles[] = {
#include "../lz77/padded.inc"
};
static const u32 castle_tiles[] = {
#include "../../examples/platformer/castle_tiles.inc"
};
static const u32 dragon_tiles[] = {
#include "../../examples/platformer/dragon_tiles.inc"
};
#define CASTLE_TILES (sizeof(castle_tiles) / 32)
#define DRAGON_FRAMES 50 // 16x16: 4 tiles each

static const u32 dot_tiles[8] = {0x11111111, 0x22222222};
static const u16 palettes[32] = {[1] = 0x1234, [17] = 0x0567};

enum {
    SPR_PADDED,
    SPR_PADDED_LZ,
    SPR_DRAGON,
    SPR_DRAGON_LZ,
    SPR_DOT,
    SPR_BAD, // the bad data tests' sprite, set by each
    SPRITE_COUNT
};
static const SpriteAsset padded = {
    .size = SPRITE_8x8, .tiles = padded_tiles, .tiles_per_frame = 2, .frame_count = 3};
static const SpriteAsset padded_lz = {.size = SPRITE_8x8,
                                      .tiles = padded_lz77,
                                      .tiles_per_frame = 2,
                                      .frame_count = 3,
                                      .flags = SPRITE_ASSET_LZ77};
static const SpriteAsset dragon = {
    .size = SPRITE_16x16, .tiles = dragon_tiles, .frame_count = DRAGON_FRAMES, .palette_slot = 1};
static const SpriteAsset dragon_lz = {.size = SPRITE_16x16,
                                      .tiles = dragon_tiles_lz77,
                                      .frame_count = DRAGON_FRAMES,
                                      .palette_slot = 1,
                                      .flags = SPRITE_ASSET_LZ77};
static const SpriteAsset dot = {.size = SPRITE_8x8, .tiles = dot_tiles};
static SpriteAsset bad SERVAL_EWRAM_DATA;
static const SpriteAsset* const table[SPRITE_COUNT] = {
    [SPR_PADDED] = &padded, [SPR_PADDED_LZ] = &padded_lz,
    [SPR_DRAGON] = &dragon, [SPR_DRAGON_LZ] = &dragon_lz,
    [SPR_DOT] = &dot,       [SPR_BAD] = &bad};

// The plain group and the packed one, in the same order, then a plain
// sprite that lands after them.
static const u16 plain_ids[] = {SPR_PADDED, SPR_DRAGON, SPR_DOT};
static const u16 packed_ids[] = {SPR_PADDED_LZ, SPR_DRAGON_LZ, SPR_DOT};
static const SpriteGroup plain_group = {
    .sprite_ids = plain_ids, .palettes = palettes, .sprite_count = 3, .palette_count = 2};
static const SpriteGroup packed_group = {
    .sprite_ids = packed_ids, .palettes = palettes, .sprite_count = 3, .palette_count = 2};
#define GROUP_TILES (6 + DRAGON_FRAMES * 4 + 1)

static volatile u32* const obj_vram = (volatile u32*)MEM_VRAM_OBJ;
static volatile u32* const bg_vram = (volatile u32*)&tile_mem[1][0]; // tileset_load's charblock

// Fills `words` words of VRAM with a pattern no tile holds (VRAM takes no
// byte writes).
static void scribble(volatile u32* vram, u32 words) {
    for (u32 k = 0; k < words; k++)
        vram[k] = 0xE1E2E3E4u ^ k;
}

static bool scribbled(const volatile u32* vram, u32 from, u32 to) {
    for (u32 k = from; k < to; k++) {
        if (vram[k] != (0xE1E2E3E4u ^ k))
            return false;
    }
    return true;
}

static bool vram_holds(const volatile u32* vram, const u32* tiles, u32 words) {
    for (u32 k = 0; k < words; k++) {
        if (vram[k] != tiles[k])
            return false;
    }
    return true;
}

static void check_warnings(u32 before, u32 count) {
    CHECK(debug_warning_count() == before + count * WARNINGS_ON);
}

static void values_are_fixed(void) {
    CHECK(TILESET_LZ77 == 1 << 0);
    CHECK(SPRITE_ASSET_LZ77 == 1 << 3);
}

// A packed tileset leaves VRAM as its plain version does: every byte of its
// tiles, nothing past them.
static void tilesets_unpack_as_plain_ones_copy(void) {
    const Tileset plain = {.tiles = castle_tiles,
                           .tile_count = CASTLE_TILES,
                           .palettes = palettes,
                           .palette_count = 2};
    const Tileset packed = {.tiles = castle_tiles_lz77,
                            .tile_count = CASTLE_TILES,
                            .palettes = palettes,
                            .palette_count = 2,
                            .flags = TILESET_LZ77};
    u32 words = CASTLE_TILES * 8;
    scribble(bg_vram, words + 64);
    u32 before = debug_warning_count();
    CHECK(tileset_load(&plain));
    CHECK(vram_holds(bg_vram, castle_tiles, words) && scribbled(bg_vram, words, words + 64));
    scribble(bg_vram, words + 64);
    pal_bg_mem[1] = 0;
    CHECK(tileset_load(&packed));
    CHECK(vram_holds(bg_vram, castle_tiles, words) && scribbled(bg_vram, words, words + 64));
    CHECK(pal_bg_mem[1] == 0x1234 && pal_bg_mem[17] == 0x0567);
    // A small one too: the test sprite's tiles as a tileset.
    const Tileset small = {
        .tiles = padded_lz77, .tile_count = sizeof(padded_tiles) / 32, .flags = TILESET_LZ77};
    scribble(bg_vram, 64);
    CHECK(tileset_load(&small));
    CHECK(vram_holds(bg_vram, padded_tiles, sizeof(padded_tiles) / 4) &&
          scribbled(bg_vram, sizeof(padded_tiles) / 4, 64));
    check_warnings(before, 0);
}

static EWRAM_BSS u16 plain_attr2[DRAGON_FRAMES + 4];

// Draws every frame of the plain or the packed sprites and the dot, and
// keeps the OAM entries' attr2s (tile and palette bank).
static void draw_all(bool packed, u16* attr2) {
    frame_begin();
    for (u32 f = 0; f < 3; f++)
        sprite_draw(packed ? SPR_PADDED_LZ : SPR_PADDED, (u8)f, 10, 10, 0);
    for (u32 f = 0; f < DRAGON_FRAMES; f++)
        sprite_draw(packed ? SPR_DRAGON_LZ : SPR_DRAGON, (u8)f, 40, 10, 0);
    sprite_draw(SPR_DOT, 0, 80, 10, 0);
    frame_end();
    for (u32 k = 0; k < DRAGON_FRAMES + 4; k++)
        attr2[k] = oam_mem[k].attr2;
}

// A packed group leaves VRAM as its plain version does (each holds the plain
// tiles, back to back, and nothing past them), its sprites draw from the same
// tiles with the same palettes, and the sprite after them lands where it
// would.
static void sprite_groups_unpack_as_plain_ones_copy(void) {
    static EWRAM_BSS u16 packed_attr2[DRAGON_FRAMES + 4];
    u32 words = GROUP_TILES * 8;
    sprite_table_set(table, SPRITE_COUNT);
    scribble(obj_vram, words + 64);
    u32 before = debug_warning_count();
    CHECK(sprite_group_load(&plain_group));
    CHECK(vram_holds(obj_vram, padded_tiles, 6 * 8));
    CHECK(vram_holds(obj_vram + 6 * 8, dragon_tiles, DRAGON_FRAMES * 4 * 8));
    CHECK(vram_holds(obj_vram + (GROUP_TILES - 1) * 8, dot_tiles, 8));
    CHECK(scribbled(obj_vram, words, words + 64));
    draw_all(false, plain_attr2);

    sprite_groups_reset();
    scribble(obj_vram, words + 64);
    CHECK(sprite_group_load(&packed_group));
    CHECK(vram_holds(obj_vram, padded_tiles, 6 * 8));
    CHECK(vram_holds(obj_vram + 6 * 8, dragon_tiles, DRAGON_FRAMES * 4 * 8));
    CHECK(vram_holds(obj_vram + (GROUP_TILES - 1) * 8, dot_tiles, 8));
    CHECK(scribbled(obj_vram, words, words + 64));
    draw_all(true, packed_attr2);
    CHECK(memcmp(plain_attr2, packed_attr2, sizeof(plain_attr2)) == 0);
    check_warnings(before, 0);
    // Runtime tiles replace a packed sprite's frame as any resident one's.
    frame_begin();
    sprite_set_tiles(SPR_DRAGON_LZ, 7, dragon_tiles); // frame 0's pixels
    frame_end();
    CHECK(vram_holds(obj_vram + (6 + 7 * 4) * 8, dragon_tiles, 4 * 8));
    sprite_groups_reset();
}

// LZ77 data that wouldn't unpack to the sprite's tiles: a header that isn't
// LZ77, the wrong size, not VRAM-safe, a match from before the start or past
// the end, not word-aligned.
static union {
    u32 words[8];
    u8 bytes[32];
} vectors[5];
static const u8 vector_bytes[5][12] = {
    {0x10, 32, 0, 0, 0x40, 'a', 0x00, 0x00},                  // 'a', 3 from 1 back
    {0x10, 32, 0, 0, 0x80, 0x00, 0x01},                       // 3 from 2 back, at the start
    {0x10, 32, 0, 0, 0x30, 'a', 'b', 0xF0, 0x01, 0xF0, 0x01}, // 2 + 18 + 18 bytes
    {0x24, 32, 0, 0},                                         // Huffman
    {0x10, 64, 0, 0, 0x00, 'a'},                              // 64 bytes, not 32
};

static void make_vectors(void) {
    for (u32 v = 0; v < 5; v++) {
        for (u32 k = 0; k < sizeof(vectors[v].bytes); k++)
            vectors[v].bytes[k] = k < sizeof(vector_bytes[v]) ? vector_bytes[v][k] : 0;
    }
}

static void bad_sprite_data_is_refused(void) {
    make_vectors();
    static const u16 bad_ids[] = {SPR_BAD};
    static const SpriteGroup bad_group = {
        .sprite_ids = bad_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 1};
    sprite_table_set(table, SPRITE_COUNT);
    u32 before = debug_warning_count();
    bad = (SpriteAsset){.size = SPRITE_8x8, .flags = SPRITE_ASSET_LZ77};
    for (u32 v = 0; v < 5; v++) {
        bad.tiles = vectors[v].words;
        CHECK(!sprite_group_load(&bad_group));
    }
    // Plain tiles marked LZ77; the castle's packed tiles with a frame too
    // few; the test sprite's off its word boundary.
    bad = dragon_lz;
    bad.frame_count = DRAGON_FRAMES - 1;
    CHECK(!sprite_group_load(&bad_group));
    bad = padded;
    bad.flags = SPRITE_ASSET_LZ77;
    CHECK(!sprite_group_load(&bad_group));
    bad = padded_lz;
    bad.tiles = (const u32*)((const u8*)padded_lz77 + 2);
    CHECK(!sprite_group_load(&bad_group));
    // A metasprite has no tiles to unpack.
    static const SpritePiece piece = {.sprite = SPR_DOT};
    bad = (SpriteAsset){
        .pieces = &piece, .piece_count = 1, .flags = SPRITE_ASSET_METASPRITE | SPRITE_ASSET_LZ77};
    CHECK(!sprite_group_load(&bad_group));
    // A streamed group copies frames from ROM: refused
    // (tests/rom/sprite_stream_tests.c has more).
    SpriteGroup streamed = packed_group;
    streamed.flags = SPRITE_GROUP_STREAMED;
    CHECK(!sprite_group_load(&streamed));
    check_warnings(before, 10);
    // Nothing was loaded: the next group starts at tile 0.
    static const u16 dot_ids[] = {SPR_DOT};
    static const SpriteGroup dot_group = {
        .sprite_ids = dot_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 1};
    CHECK(sprite_group_load(&dot_group));
    frame_begin();
    sprite_draw(SPR_DOT, 0, 10, 10, 0);
    frame_end();
    CHECK((oam_mem[0].attr2 & ATTR2_ID_MASK) == 0 && (oam_mem[0].attr2 >> 12) == 0);
    sprite_groups_reset();
}

static void bad_tileset_data_is_refused(void) {
    make_vectors();
    const Tileset plain = {.tiles = dot_tiles, .tile_count = 1};
    CHECK(tileset_load(&plain));
    u32 before = debug_warning_count();
    Tileset packed = {.tile_count = 1, .flags = TILESET_LZ77};
    for (u32 v = 0; v < 5; v++) {
        packed.tiles = vectors[v].words;
        CHECK(!tileset_load(&packed));
    }
    packed.tiles = castle_tiles_lz77; // 281 tiles, not 1
    CHECK(!tileset_load(&packed));
    packed.tiles = (const u32*)((const u8*)padded_lz77 + 2);
    packed.tile_count = sizeof(padded_tiles) / 32;
    CHECK(!tileset_load(&packed));
    check_warnings(before, 1); // once for all: tileset_load() warns once per kind
    // Good data with a reserved bit too: refused for the bit.
    packed.tiles = padded_lz77;
    packed.flags = TILESET_LZ77 | 1 << 1;
    CHECK(!tileset_load(&packed));
    CHECK(vram_holds(bg_vram, dot_tiles, 8));
}

// The BIOS's 16-bit decoder writes halfwords and reads VRAM for matches, so
// data that isn't VRAM-safe, or runs past its size, unpacks as src/web/lz77.c
// says (tests/web_lz77_tests.c has the same vectors).
static union {
    u32 words[8];
    u8 bytes[32];
} bios_in;

static const void* bios_vector(const u8* bytes, u32 count) {
    for (u32 k = 0; k < sizeof(bios_in.bytes); k++)
        bios_in.bytes[k] = k < count ? bytes[k] : 0;
    return bios_in.words;
}

static void the_bios_unpacks_bad_data_as_the_web_does(void) {
    volatile u16* vram = (volatile u16*)&tile_mem[5][500]; // sprite VRAM, nothing loaded
    // 'a', then 3 from 1 back, over "012345": "a002", then "45" untouched.
    static const u8 run[] = {0x10, 4, 0, 0, 0x40, 'a', 0x00, 0x00};
    vram[0] = '0' | '1' << 8;
    vram[1] = '2' | '3' << 8;
    vram[2] = '4' | '5' << 8;
    LZ77UnCompVram(bios_vector(run, sizeof(run)), (void*)vram);
    CHECK(vram[0] == ('a' | '0' << 8) && vram[1] == ('0' | '2' << 8));
    CHECK(vram[2] == ('4' | '5' << 8));
    // Nine literals: the ninth, at an even address, is never written.
    static const u8 nine[] = {0x10, 9,   0,   0,   0x00, '1',  '2', '3',
                              '4',  '5', '6', '7', '8',  0x00, '9'};
    vram[4] = '.' | '.' << 8;
    LZ77UnCompVram(bios_vector(nine, sizeof(nine)), (void*)vram);
    CHECK(vram[0] == ('1' | '2' << 8) && vram[3] == ('7' | '8' << 8));
    CHECK(vram[4] == ('.' | '.' << 8));
    // A match past the size is finished: 'a', 'b', 4 from 2 back into 4.
    static const u8 over[] = {0x10, 4, 0, 0, 0x20, 'a', 'b', 0x10, 0x01};
    vram[2] = '.' | '.' << 8;
    vram[3] = '.' | '.' << 8;
    LZ77UnCompVram(bios_vector(over, sizeof(over)), (void*)vram);
    CHECK(vram[0] == ('a' | 'b' << 8) && vram[1] == ('a' | 'b' << 8));
    CHECK(vram[2] == ('a' | 'b' << 8) && vram[3] == ('.' | '.' << 8));
}

#ifdef SERVAL_GBA
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

// What loading packed tiles costs, against plain ones: tileset_load() of the
// castle (8,992 bytes) and sprite_group_load() of the dragon (6,400), each
// split into the loaders' check and the BIOS's unpacking. The BIOS is mGBA's
// own (its high-level emulation of the BIOS calls, timed by its estimate),
// not Nintendo's.
static void loading_costs_are_logged(void) {
    const Tileset plain = {.tiles = castle_tiles, .tile_count = CASTLE_TILES};
    const Tileset packed = {
        .tiles = castle_tiles_lz77, .tile_count = CASTLE_TILES, .flags = TILESET_LZ77};
    u32 t0 = cycles_now();
    tileset_load(&plain);
    u32 t1 = cycles_now();
    tileset_load(&packed);
    u32 t2 = cycles_now();
    u32 at;
    serval_lz77_check(castle_tiles_lz77, sizeof(castle_tiles), &at);
    u32 t3 = cycles_now();
    LZ77UnCompVram(castle_tiles_lz77, (void*)bg_vram);
    u32 t4 = cycles_now();
    u32 copy = t1 - t0, load = t2 - t1, check = t3 - t2, bios = t4 - t3;
    debug_log(text_format("lz77: castle tileset, %u bytes (%u packed): copied %u cycles, "
                          "unpacked %u (check %u, BIOS %u)",
                          (u32)sizeof(castle_tiles), (u32)sizeof(castle_tiles_lz77), copy, load,
                          check, bios));
    u32 kb = (u32)sizeof(castle_tiles);
    debug_log(text_format("lz77: per KB unpacked: %u cycles (check %u, BIOS %u); copied: %u",
                          load * 1024 / kb, check * 1024 / kb, bios * 1024 / kb, copy * 1024 / kb));

    static const u16 dragon_ids[] = {SPR_DRAGON};
    static const u16 dragon_lz_ids[] = {SPR_DRAGON_LZ};
    const SpriteGroup plain_dragon = {
        .sprite_ids = dragon_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 2};
    const SpriteGroup packed_dragon = {
        .sprite_ids = dragon_lz_ids, .palettes = palettes, .sprite_count = 1, .palette_count = 2};
    sprite_table_set(table, SPRITE_COUNT);
    t0 = cycles_now();
    sprite_group_load(&plain_dragon);
    t1 = cycles_now();
    sprite_groups_reset();
    t2 = cycles_now();
    sprite_group_load(&packed_dragon);
    t3 = cycles_now();
    sprite_groups_reset();
    u32 sprite_copy = t1 - t0, sprite_load = t3 - t2;
    debug_log(text_format("lz77: dragon, %u bytes (%u packed): group copied %u cycles, "
                          "unpacked %u",
                          (u32)sizeof(dragon_tiles), (u32)sizeof(dragon_tiles_lz77), sprite_copy,
                          sprite_load));
    // Measured in mGBA: about 28,600 cycles per KB unpacked, 10,300 of them
    // the check's and 18,300 the BIOS's; a copy about 1,700. Allow a quarter
    // more.
    CHECK_TIMING(load * 1024 / kb < 36000);
    CHECK_TIMING(check * 1024 / kb < 13000);
    CHECK_TIMING(sprite_load * 1024 / (u32)sizeof(dragon_tiles) < 36000);
}
#define GBA_CASES                                                                                  \
    , {                                                                                            \
        "loading_costs_are_logged", loading_costs_are_logged                                       \
    }
#else
#define GBA_CASES
#endif

TEST_SUITE(gba_lz77_tests, "lz77 loading", {"values_are_fixed", values_are_fixed},
           {"tilesets_unpack_as_plain_ones_copy", tilesets_unpack_as_plain_ones_copy},
           {"sprite_groups_unpack_as_plain_ones_copy", sprite_groups_unpack_as_plain_ones_copy},
           {"bad_sprite_data_is_refused", bad_sprite_data_is_refused},
           {"bad_tileset_data_is_refused", bad_tileset_data_is_refused},
           {"the_bios_unpacks_bad_data_as_the_web_does",
            the_bios_unpacks_bad_data_as_the_web_does} GBA_CASES);
