// The planned API of map.h at run time (docs/development.md#planned-api): each
// planned function, called twice, does nothing harmful (returns 0, false or
// its type's "none", changes nothing; loaders refuse data needing a planned
// feature) and in debug builds warns on the first call only. Run natively and
// in the test ROM; cases for stubs in src/gba/, which the host build doesn't
// link, go inside #ifdef SERVAL_GBA.
//
// Also the planned collision types, which load (a game may be written against
// planned API) and collide as this version says until implemented: ladders as
// MAP_EMPTY, slopes as MAP_SOLID, with one warning per kind.

// Calls planned API on purpose: without this, every call would warn.
#define SERVAL_NO_PLANNED_WARNINGS

#include "serval/core.h"
#include "serval/debug.h"
#include "serval/map.h"
#include "serval/physics.h"
#include "test.h"

#ifdef SERVAL_DEBUG
#define WARNINGS_ON 1
#else
#define WARNINGS_ON 0
#endif

static void reset(void) {
    ecs_reset();
    physics_set_gravity(0, 0);
    map_unload(2);
    camera_set(0, 0);
}

static u32 make_body(int x, int y, u8 w, u8 h) {
    u32 i = entity_index(entity_create(C_POS | C_VEL | C_BODY | C_MAPBODY));
    pos_x[i] = FX(x);
    pos_y[i] = FX(y);
    body_w[i] = w;
    body_h[i] = h;
    return i;
}

// Separate metatile sets: map_load() checks every metatile of the playfield,
// so a set with both kinds would give both warnings at once.
static const Metatile ladder_metatiles[] = {
    {{0, 0, 0, 0}, MAP_EMPTY},
    {{0, 0, 0, 0}, MAP_SOLID},
    {{0, 0, 0, 0}, MAP_LADDER | MAP_TAG(0)},
};

// A ladder up column 1, on a floor.
static const u16 ladder_cells[4 * 4] = {
    0, 2, 0, 0, //
    0, 2, 0, 0, //
    0, 2, 0, 0, //
    1, 1, 1, 1, //
};

static void ladders_load_and_collide_as_empty(void) {
    reset();
    const MapLayer layer = {.width = 4,
                            .height = 4,
                            .cells = ladder_cells,
                            .metatiles = ladder_metatiles,
                            .metatile_count = 3,
                            .bg = 2};
    u32 before = debug_warning_count();
    CHECK(map_load(&layer));
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    CHECK(map_load(&layer)); // once per run
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    // map_collision_at() returns the byte as it is.
    CHECK(map_collision_at(20, 20) == (MAP_LADDER | MAP_TAG(0)));
    // A body falls down the ladder onto the floor, never touching a ladder.
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(18, 0, 8, 8);
    bool ladder = false;
    for (int f = 0; f < 60; f++) {
        sys_map_movement();
        ladder |= (body_contact[i] & MAP_CONTACT_LADDER) != 0;
    }
    CHECK(!ladder);
    CHECK(pos_y[i] == FX(48 - 8) && body_contact[i] == MAP_CONTACT_FLOOR);
    // Nor does a body moving sideways through it.
    physics_set_gravity(0, 0);
    u32 j = make_body(0, 20, 8, 8);
    vel_x[j] = FX(2);
    for (int f = 0; f < 30; f++) {
        sys_map_movement();
        ladder |= (body_contact[j] & MAP_CONTACT_LADDER) != 0;
    }
    CHECK(!ladder);
    CHECK(pos_x[j] == FX(64 - 8));
    reset();
}

static const Metatile slope_metatiles[] = {
    {{0, 0, 0, 0}, MAP_EMPTY},        {{0, 0, 0, 0}, MAP_SOLID},
    {{0, 0, 0, 0}, MAP_SLOPE_R},      {{0, 0, 0, 0}, MAP_SLOPE_L},
    {{0, 0, 0, 0}, MAP_SLOPE_R_LOW},  {{0, 0, 0, 0}, MAP_SLOPE_R_HIGH},
    {{0, 0, 0, 0}, MAP_SLOPE_L_HIGH}, {{0, 0, 0, 0}, MAP_SLOPE_L_LOW},
};

// Each slope type once, in row 2, columns 1-6, on a floor.
static const u16 slope_cells[7 * 4] = {
    0, 0, 0, 0, 0, 0, 0, //
    0, 0, 0, 0, 0, 0, 0, //
    0, 2, 3, 4, 5, 6, 7, //
    1, 1, 1, 1, 1, 1, 1, //
};

static void slopes_load_and_collide_as_solid(void) {
    reset();
    const MapLayer layer = {.width = 7,
                            .height = 4,
                            .cells = slope_cells,
                            .metatiles = slope_metatiles,
                            .metatile_count = 8,
                            .bg = 2};
    u32 before = debug_warning_count();
    CHECK(map_load(&layer));
    CHECK(debug_warning_count() == before + WARNINGS_ON); // one warning for six types
    // Bodies dropped onto each land on top of the whole metatile.
    physics_set_gravity(0, FX_ONE / 4);
    u32 bodies[6];
    for (u32 c = 0; c < 6; c++)
        bodies[c] = make_body(16 * (int)(c + 1) + 4, 0, 8, 8);
    for (int f = 0; f < 60; f++)
        sys_map_movement();
    for (u32 c = 0; c < 6; c++) {
        CHECK(pos_y[bodies[c]] == FX(32 - 8));
        CHECK(body_contact[bodies[c]] == MAP_CONTACT_FLOOR);
    }
    // From the side, a slope is a wall, also at its low side.
    physics_set_gravity(0, 0);
    u32 i = make_body(0, 34, 8, 8);
    vel_x[i] = FX(2);
    u8 seen = 0;
    for (int f = 0; f < 10; f++) {
        sys_map_movement();
        seen |= body_contact[i];
    }
    CHECK(seen == MAP_CONTACT_RIGHT && pos_x[i] == FX(16 - 8));
    // Another playfield with slopes: no second warning.
    static const u16 one_slope[2] = {7, 1};
    const MapLayer other = {.width = 1,
                            .height = 2,
                            .cells = one_slope,
                            .metatiles = slope_metatiles,
                            .metatile_count = 8,
                            .bg = 2};
    CHECK(map_load(&other));
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    reset();
}

#ifdef SERVAL_GBA
// Charblock 1, where tileset_load() puts tiles, and background palette RAM,
// read directly: this suite is shared with the host, so it has no libtonc.
#define TILE_VRAM ((const volatile u32*)0x06004000)
#define BG_PALETTE ((const volatile u16*)0x05000000)

static const u32 plain_tiles[2 * 8] = {[8] = 0x11111111, [15] = 0x22222222};
static const u32 packed_tiles[2 * 8] = {0x00004010, 0x33333333, [15] = 0x44444444};
static const u16 plain_palette[16] = {[1] = 0x0123, [15] = 0x7654};

// tileset_load() refuses a tileset needing LZ77 and loads nothing.
static void tileset_load_refuses_lz77(void) {
    const Tileset plain = {
        .tiles = plain_tiles, .tile_count = 2, .palettes = plain_palette, .palette_count = 1};
    CHECK(tileset_load(&plain));
    const Tileset packed = {.tiles = packed_tiles,
                            .tile_count = 2,
                            .palettes = plain_palette,
                            .palette_count = 1,
                            .flags = TILESET_LZ77};
    u32 before = debug_warning_count();
    CHECK(!tileset_load(&packed));
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    CHECK(!tileset_load(&packed));
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    CHECK(TILE_VRAM[0] == 0 && TILE_VRAM[8] == 0x11111111 && TILE_VRAM[15] == 0x22222222);
    // Together with an unknown bit, still refused.
    const Tileset both = {.tiles = packed_tiles, .tile_count = 2, .flags = TILESET_LZ77 | 0x80};
    CHECK(!tileset_load(&both));
    CHECK(TILE_VRAM[1] == 0);
}

// tileset_set_colors() changes nothing, now or at frame_end().
static void tileset_set_colors_changes_nothing(void) {
    const Tileset plain = {
        .tiles = plain_tiles, .tile_count = 2, .palettes = plain_palette, .palette_count = 1};
    CHECK(tileset_load(&plain));
    frame_begin();
    frame_end();
    u16 old[18];
    for (u32 k = 0; k < 18; k++)
        old[k] = BG_PALETTE[k];
    static const Color colors[3] = {0x1111, 0x2222, 0x3333};
    u32 before = debug_warning_count();
    tileset_set_colors(0, colors, 3); // the backdrop and two colors
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    tileset_set_colors(15, colors, 3); // across palettes 0 and 1
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    frame_begin();
    frame_end();
    bool same = true;
    for (u32 k = 0; k < 18; k++)
        same &= BG_PALETTE[k] == old[k];
    CHECK(same);
    CHECK(BG_PALETTE[1] == 0x0123 && BG_PALETTE[15] == 0x7654); // the tileset's
}
#endif

TEST_SUITE(planned_map_tests, "planned_map",
           {"ladders load and collide as empty", ladders_load_and_collide_as_empty},
           {"slopes load and collide as solid", slopes_load_and_collide_as_solid},
#ifdef SERVAL_GBA
           {"tileset_load refuses LZ77", tileset_load_refuses_lz77},
           {"tileset_set_colors changes nothing", tileset_set_colors_changes_nothing},
#endif
);
