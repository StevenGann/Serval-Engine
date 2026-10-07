// Tests for map.h's platform-neutral part: map layers as data, the camera,
// runtime cell changes, collision and tag queries and sys_map_movement. Run
// natively and in the test ROM (which also draws the layers; see
// tests/rom/map_tests.c for that). The planned collision types are tested in
// planned_map_tests.c.

#include "serval/debug.h"
#include "serval/map.h"
#include "serval/physics.h"
#include "test.h"

#include <stddef.h>

#include "../src/core/map_internal.h"

#ifdef SERVAL_DEBUG
#define WARNINGS_ON 1
#else
#define WARNINGS_ON 0
#endif

enum { EMPTY, SOLID, ONEWAY, TAGGED };

static const Metatile metatiles[] = {
    [EMPTY] = {{0, 0, 0, 0}, MAP_EMPTY},
    [SOLID] = {{1, 2, 3, 4}, MAP_SOLID},
    [ONEWAY] = {{5, 5, 5, 5}, MAP_ONEWAY},
    [TAGGED] = {{6, 6, 6, 6}, MAP_SOLID | MAP_TAG(2)},
};

#define MAX_CELLS (40 * 16)
static u16 cells[MAX_CELLS];
static MapLayer playfield;

// Loads a playfield drawn as text, one string per metatile row: '.' empty,
// '#' solid, '-' one-way, 'T' solid with a tag.
static void load_rows(const char* const* rows, u16 height) {
    u16 width = 0;
    while (rows[0][width])
        width++;
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            char c = rows[y][x];
            cells[y * width + x] = c == '#' ? SOLID : c == '-' ? ONEWAY : c == 'T' ? TAGGED : EMPTY;
        }
    }
    playfield = (MapLayer){.width = width,
                           .height = height,
                           .cells = cells,
                           .metatiles = metatiles,
                           .metatile_count = 4,
                           .bg = 2};
    CHECK(map_load(&playfield));
}
#define LOAD(rows) load_rows(rows, (u16)(sizeof(rows) / sizeof(rows[0])))

// 20x10 metatiles (320x160 pixels): a floor at metatile row 8 (y 128-143)
// with a gap at columns 10-11, walls at columns 2 and 17, a ceiling block at
// (7, 4), a one-way platform at row 5, columns 12-14.
static const char* const room[] = {
    "....................", // 0
    "....................", // 1
    "....................", // 2
    "....................", // 3
    ".......#............", // 4
    "............---.....", // 5
    "..#..............#..", // 6
    "..#..............#..", // 7
    "##########..########", // 8
    "....................", // 9
};

static void reset(void) {
    ecs_reset();
    physics_set_gravity(0, 0);
    map_unload(1);
    map_unload(2);
    map_unload(3);
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

static void step(int frames) {
    for (int f = 0; f < frames; f++)
        sys_map_movement();
}

// Steps and returns every contact body i had along the way. A body stopped by
// a wall has zero velocity afterwards, so it touches it only once.
static u8 step_contacts(u32 i, int frames) {
    u8 seen = 0;
    for (int f = 0; f < frames; f++) {
        sys_map_movement();
        seen |= body_contact[i];
    }
    return seen;
}

static void collision_reads_metatiles_and_the_edges(void) {
    reset();
    CHECK(map_collision_at(5, 5) == MAP_EMPTY); // no map: nothing collides
    LOAD(room);
    CHECK(map_collision_at(0, 128) == MAP_SOLID);
    CHECK(map_collision_at(15, 143) == MAP_SOLID);
    CHECK(map_collision_at(15, 144) == MAP_EMPTY);
    CHECK(map_collision_at(160, 128) == MAP_EMPTY); // the gap
    CHECK(map_collision_at(200, 80) == MAP_ONEWAY);
    // Outside: solid to the sides (also above and below them), empty above
    // and below the map.
    CHECK(map_collision_at(-1, 50) == MAP_SOLID);
    CHECK(map_collision_at(320, 50) == MAP_SOLID);
    CHECK(map_collision_at(-1, -100) == MAP_SOLID);
    CHECK(map_collision_at(50, -1) == MAP_EMPTY);
    CHECK(map_collision_at(50, 160) == MAP_EMPTY);
    CHECK(map_cell(2, 6) == SOLID);
    CHECK(map_cell(-1, 0) == 0 && map_cell(20, 0) == 0 && map_cell(0, 10) == 0);
    reset();
}

static void tags_come_with_the_collision_byte(void) {
    reset();
    static const char* const tagged[] = {"T."};
    LOAD(tagged);
    u8 c = map_collision_at(3, 3);
    CHECK(MAP_TYPE(c) == MAP_SOLID);
    CHECK(c & MAP_TAG(2));
    CHECK(!(c & MAP_TAG(1)));
    reset();
}

static void map_load_rejects_bad_layers(void) {
    reset();
    u32 before = debug_warning_count();
    MapLayer bad = {.width = 2,
                    .height = 1,
                    .cells = cells,
                    .metatiles = metatiles,
                    .metatile_count = 4,
                    .bg = 0};
    CHECK(!map_load(&bad)); // background 0 is the text layer
    bad.bg = 4;
    CHECK(!map_load(&bad));
    bad.bg = 2;
    bad.width = 0;
    CHECK(!map_load(&bad));
    bad.width = 2;
    bad.cells = NULL;
    CHECK(!map_load(&bad));
    bad.cells = cells;
    bad.metatile_count = 0;
    CHECK(!map_load(&bad));
    CHECK(!map_load(NULL));
    CHECK(map_cell(0, 0) == 0); // nothing was loaded
    map_unload(0);
    map_unload(4);
    CHECK(debug_warning_count() == before + (WARNINGS_ON ? 5 : 0)); // once per kind of problem

    // A cell naming a metatile that doesn't exist loads (with a warning) and
    // collides as empty.
    cells[0] = 9;
    cells[1] = SOLID;
    bad.metatile_count = 4;
    before = debug_warning_count();
    CHECK(map_load(&bad));
    CHECK(map_collision_at(0, 0) == MAP_EMPTY);
    CHECK(map_collision_at(16, 0) == MAP_SOLID);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + 1);
#endif
    reset();
}

// MapLayer.flags has two flags; map_load() refuses the other six bits (a
// later version may give them a meaning), changing nothing.
static void map_load_refuses_unknown_layer_flags(void) {
    reset();
    LOAD(room);
    static const u16 one[1] = {SOLID};
    MapLayer layer = {.width = 1,
                      .height = 1,
                      .cells = one,
                      .metatiles = metatiles,
                      .metatile_count = 4,
                      .bg = 2,
                      .flags = MAP_LAYER_WRAP | MAP_LAYER_FIXED};
    CHECK(map_load(&layer));
    CHECK(map_cell(0, 0) == SOLID);
    LOAD(room);
    u32 before = debug_warning_count();
    for (u32 bit = 2; bit < 8; bit++) {
        layer.flags = (u8)(MAP_LAYER_WRAP | (1u << bit));
        CHECK(!map_load(&layer));
    }
    layer.flags = 0xFF;
    CHECK(!map_load(&layer));
    CHECK(debug_warning_count() == before + WARNINGS_ON); // once
    // The room is still the playfield.
    CHECK(map_cell(0, 0) == EMPTY && map_cell(2, 6) == SOLID);
    layer.bg = 3; // refused on every background
    layer.flags = 1u << 7;
    CHECK(!map_load(&layer));
    reset();
}

// Collision types 10-15 are reserved: they load, with a warning (once), and
// collide as MAP_EMPTY; map_collision_at() returns them as they are.
static void reserved_collision_types_load_and_collide_as_empty(void) {
    reset();
    static const Metatile reserved[] = {
        {{0, 0, 0, 0}, MAP_EMPTY},
        {{0, 0, 0, 0}, MAP_SOLID},
        {{0, 0, 0, 0}, 12 | MAP_TAG(1)},
        {{0, 0, 0, 0}, 15},
    };
    // A floor of type 12 over a solid one, and a type-15 wall.
    static const u16 reserved_cells[] = {0, 0, 0, 3, //
                                         0, 0, 0, 3, //
                                         2, 2, 2, 3, //
                                         1, 1, 1, 1};
    const MapLayer layer = {.width = 4,
                            .height = 4,
                            .cells = reserved_cells,
                            .metatiles = reserved,
                            .metatile_count = 4,
                            .bg = 2};
    u32 before = debug_warning_count();
    CHECK(map_load(&layer));
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    CHECK(map_load(&layer)); // once per run
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    CHECK(map_collision_at(5, 40) == (12 | MAP_TAG(1)));
    physics_set_gravity(0, FX_ONE / 2);
    u32 i = make_body(4, 0, 8, 8);
    step(60);
    CHECK(pos_y[i] == FX(48 - 8)); // through the type-12 row, onto the solid one
    CHECK(body_contact[i] == MAP_CONTACT_FLOOR);
    u32 j = make_body(4, 20, 8, 8); // in row 1, without gravity, moving right
    body_gravity[j] = BODY_GRAVITY(0);
    vel_x[j] = FX(2);
    step(40);
    CHECK(pos_x[j] == FX(64 - 8)); // through the type-15 column to the map's edge
    CHECK(pos_y[j] == FX(20));
    reset();
}

// Metatiles whose tags and collision types differ (a tag is not a type):
// solid spikes, water that isn't solid, a tagged one-way platform.
enum { T_NONE, T_SPIKES, T_WATER, T_LEDGE, T_ROCK, T_COUNT };
#define TAG_SPIKES MAP_TAG(0)
#define TAG_WATER MAP_TAG(1)
static const Metatile tag_metatiles[T_COUNT] = {
    [T_NONE] = {{0, 0, 0, 0}, MAP_EMPTY},
    [T_SPIKES] = {{0, 0, 0, 0}, MAP_SOLID | TAG_SPIKES},
    [T_WATER] = {{0, 0, 0, 0}, MAP_EMPTY | TAG_WATER},
    [T_LEDGE] = {{0, 0, 0, 0}, MAP_ONEWAY | MAP_TAG(2) | MAP_TAG(3)},
    [T_ROCK] = {{0, 0, 0, 0}, MAP_SOLID},
};

// 4x3 metatiles (64x48 pixels); 9 names a metatile the layer doesn't have.
static const u16 tag_cells[4 * 3] = {
    T_NONE, T_SPIKES, T_NONE,  T_WATER, //
    T_ROCK, T_NONE,   T_LEDGE, T_NONE,  //
    T_NONE, T_NONE,   T_NONE,  9,       //
};

static void map_tags_in_ors_the_tags_of_the_metatiles_overlapped(void) {
    reset();
    CHECK(map_tags_in(0, 0, 100, 100) == 0); // no playfield
    const MapLayer layer = {.width = 4,
                            .height = 3,
                            .cells = tag_cells,
                            .metatiles = tag_metatiles,
                            .metatile_count = T_COUNT,
                            .bg = 2};
    CHECK(map_load(&layer));
    // One pixel, then rectangles meeting or crossing metatile edges.
    CHECK(map_tags_in(16, 0, 1, 1) == TAG_SPIKES);
    CHECK(map_tags_in(31, 15, 1, 1) == TAG_SPIKES);
    CHECK(map_tags_in(0, 0, 16, 16) == 0); // ends at the spikes' left edge
    CHECK(map_tags_in(15, 0, 2, 1) == TAG_SPIKES);
    CHECK(map_tags_in(32, 0, 16, 16) == 0); // starts at their right edge
    CHECK(map_tags_in(31, 0, 30, 1) == (TAG_SPIKES | TAG_WATER));
    CHECK(map_tags_in(20, 10, 20, 10) == (TAG_SPIKES | MAP_TAG(2) | MAP_TAG(3)));
    // Only tags: no type bits, also over solid and one-way metatiles.
    CHECK(map_tags_in(0, 0, 64, 48) == (TAG_SPIKES | TAG_WATER | MAP_TAG(2) | MAP_TAG(3)));
    CHECK(map_tags_in(0, 16, 16, 16) == 0); // untagged rock
    // A cell naming a metatile the layer doesn't have: no tags.
    CHECK(map_tags_in(48, 32, 16, 16) == 0);
    // Outside the map nothing is tagged; rectangles partly outside count
    // what they overlap.
    CHECK(map_tags_in(-100, 0, 50, 48) == 0);
    CHECK(map_tags_in(-8, 0, 25, 1) == TAG_SPIKES); // pixels -8 to 16
    CHECK(map_tags_in(-8, 0, 24, 1) == 0);          // -8 to 15
    CHECK(map_tags_in(56, -20, 30, 21) == TAG_WATER);
    CHECK(map_tags_in(56, -20, 30, 20) == 0);
    CHECK(map_tags_in(64, 0, 10, 10) == 0 && map_tags_in(0, 48, 10, 10) == 0);
    CHECK(map_tags_in(0, -1000, 64, 2000) == (TAG_SPIKES | TAG_WATER | MAP_TAG(2) | MAP_TAG(3)));
    // No area: nothing.
    CHECK(map_tags_in(16, 0, 0, 10) == 0 && map_tags_in(16, 0, 10, 0) == 0);
    CHECK(map_tags_in(20, 5, -4, 4) == 0 && map_tags_in(20, 5, 4, -4) == 0);
    // Extreme arguments don't overflow (the host build runs under UBSan).
    const int int_max = 0x7FFFFFFF, int_min = -int_max - 1;
    CHECK(map_tags_in(int_min, int_min, int_max, int_max) == 0);
    CHECK(map_tags_in(-5, 0, int_max, 1) == (TAG_SPIKES | TAG_WATER));
    CHECK(map_tags_in(int_min + 1, 20, int_max, 1) == 0);
    CHECK(map_tags_in(int_min, 20, int_max, int_max) == 0);
    CHECK(map_tags_in(int_max, 0, int_max, int_max) == 0);
    CHECK(map_tags_in(30, int_max, 1, int_max) == 0);
    // Runtime changes count.
    map_set_cell(1, 0, T_NONE); // the spikes are gone
    map_set_cell(0, 2, T_WATER);
    CHECK(map_tags_in(16, 0, 16, 16) == 0);
    CHECK(map_tags_in(0, 40, 1, 1) == TAG_WATER);
    reset();
}

// The use in map.h: what a map body stands on is one pixel below its feet.
static void map_tags_in_finds_what_a_body_stands_on(void) {
    reset();
    static const u16 floor_cells[4 * 3] = {
        T_NONE, T_NONE,   T_NONE, T_NONE, //
        T_NONE, T_NONE,   T_NONE, T_NONE, //
        T_ROCK, T_SPIKES, T_ROCK, T_ROCK, //
    };
    const MapLayer layer = {.width = 4,
                            .height = 3,
                            .cells = floor_cells,
                            .metatiles = tag_metatiles,
                            .metatile_count = T_COUNT,
                            .bg = 2};
    CHECK(map_load(&layer));
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(2, 10, 12, 16);
    step(60);
    CHECK(body_contact[i] & MAP_CONTACT_FLOOR);
    int x = fx_to_int(pos_x[i]), y = fx_to_int(pos_y[i]);
    CHECK(map_tags_in(x, y, body_w[i], body_h[i]) == 0); // it doesn't overlap the floor
    CHECK(map_tags_in(x, y + body_h[i], body_w[i], 1) == 0);
    pos_x[i] = FX(10); // x 10-21: partly over the spikes
    step(1);
    x = fx_to_int(pos_x[i]);
    CHECK(map_tags_in(x, y + body_h[i], body_w[i], 1) & TAG_SPIKES);
    reset();
}

static void camera_is_clamped_to_the_playfield(void) {
    reset();
    camera_set(-50, 1000); // no playfield: anything goes
    CHECK(camera_x() == -50 && camera_y() == 1000);
    LOAD(room); // 320x160: re-clamped when it loads
    CHECK(camera_x() == 0 && camera_y() == 0);
    camera_set(1000, 1000);
    CHECK(camera_x() == 80 && camera_y() == 0);
    camera_set(33, -4);
    CHECK(camera_x() == 33 && camera_y() == 0);
    reset();
}

// Clamping at a playfield's edges is silent: a camera following the player is
// clamped near every edge. On an axis where the playfield is smaller than the
// screen, the camera can only be 0, and asking for anything else there warns,
// once. (A playfield exactly as big as the screen is an ordinary room.)
static void camera_warns_on_a_playfield_smaller_than_the_screen(void) {
    reset();
    u32 before = debug_warning_count();
    LOAD(room); // 320x160: as high as the screen
    camera_set(1000, 1000);
    CHECK(camera_x() == 80 && camera_y() == 0);
    camera_set(-50, -50);
    CHECK(camera_x() == 0 && camera_y() == 0);
    CHECK(debug_warning_count() == before); // clamped at the edges: no warning
    // 0 where the playfield is smaller than the screen, clamped at an edge on
    // the other axis: no warning.
    static const char* const wide[] = {"....................", "...................."}; // 320x32
    LOAD(wide);
    camera_set(1000, 0);
    CHECK(camera_x() == 80 && camera_y() == 0);
    static const char* const narrow[] = {"...", "...", "...", "...", "...", "...",
                                         "...", "...", "...", "...", "...", "..."}; // 48x192
    LOAD(narrow); // re-clamps the camera (x 80 to 0), silently
    CHECK(camera_x() == 0);
    camera_set(0, 500);
    CHECK(camera_x() == 0 && camera_y() == 32);
    CHECK(debug_warning_count() == before);
    static const char* const small[] = {"...", "..."}; // 48x32
    LOAD(small);
    camera_set(0, 0);
    CHECK(debug_warning_count() == before);
    camera_set(10, 0);
    CHECK(camera_x() == 0 && camera_y() == 0);
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    camera_set(0, -10);
    camera_set(10, 10);
    CHECK(camera_x() == 0 && camera_y() == 0);
    CHECK(debug_warning_count() == before + WARNINGS_ON); // once
    reset();
}

static void layers_scroll_by_camera_factor_and_offset(void) {
    reset();
    LOAD(room); // 320x160
    static const u16 one[1] = {0};
    MapLayer far = {.width = 1,
                    .height = 1,
                    .cells = one,
                    .metatiles = metatiles,
                    .metatile_count = 1,
                    .bg = 3,
                    .flags = MAP_LAYER_WRAP,
                    .scroll_factor = FX_ONE / 2};
    MapLayer panel = far;
    panel.bg = 1;
    panel.flags = MAP_LAYER_FIXED;
    panel.scroll_factor = FX_ONE * 2; // not used
    CHECK(map_load(&far) && map_load(&panel));
    camera_set(75, 0);
    CHECK(serval_map_layer_x(&playfield) == 75 && serval_map_layer_y(&playfield) == 0);
    CHECK(serval_map_layer_x(&far) == 37); // rounded down
    CHECK(serval_map_layer_x(&panel) == 0 && serval_map_layer_y(&panel) == 0);
    map_set_scroll(3, -1000, 7);
    map_set_scroll(1, -176, -3);
    CHECK(serval_map_layer_x(&far) == 37 - 1000 && serval_map_layer_y(&far) == 7);
    CHECK(serval_map_layer_x(&panel) == -176 && serval_map_layer_y(&panel) == -3);
    CHECK(serval_map_layer_x(&playfield) == 75); // other backgrounds keep theirs
    // The camera stays clamped to the playfield whatever the offsets.
    camera_set(1000, 0);
    CHECK(camera_x() == 80 && serval_map_layer_x(&far) == 40 - 1000);
    // map_load() keeps a background's offset, map_unload() forgets it.
    CHECK(map_load(&far));
    CHECK(serval_map_layer_y(&far) == 7);
    map_unload(3);
    CHECK(map_load(&far));
    CHECK(serval_map_layer_x(&far) == 40 && serval_map_layer_y(&far) == 0);
    u32 before = debug_warning_count();
    map_set_scroll(0, 5, 5); // the text layer: not a map layer
    map_set_scroll(4, 5, 5);
    CHECK(debug_warning_count() == before + WARNINGS_ON);
    map_unload(1);
    reset();
}

static void changed_cells_override_the_map(void) {
    reset();
    LOAD(room);
    map_set_cell(3, 3, SOLID);
    CHECK(map_cell(3, 3) == SOLID);
    CHECK(map_collision_at(3 * 16 + 5, 3 * 16 + 5) == MAP_SOLID);
    map_set_cell(0, 8, EMPTY); // a floor block broken
    CHECK(map_collision_at(4, 130) == MAP_EMPTY);
    map_set_cell(3, 3, EMPTY); // back to the original: leaves the table
    CHECK(map_collision_at(3 * 16 + 5, 3 * 16 + 5) == MAP_EMPTY);
    // Reloading the playfield forgets the changes.
    LOAD(room);
    CHECK(map_cell(0, 8) == SOLID);
    reset();
}

static void the_change_table_is_bounded(void) {
    reset();
    LOAD(room);
    u32 before = debug_warning_count();
    for (int k = 0; k < MAP_MAX_CHANGES; k++)
        map_set_cell(k % 20, k / 20, TAGGED);
    CHECK(map_cell(19, 2) == TAGGED); // change 59
    map_set_cell(5, 5, TAGGED);       // change 65: no room
    CHECK(map_cell(5, 5) == EMPTY);
    CHECK(debug_warning_count() == before + (WARNINGS_ON ? 1 : 0));
    map_set_cell(0, 0, EMPTY); // undoing one makes room again
    map_set_cell(5, 5, TAGGED);
    CHECK(map_cell(5, 5) == TAGGED);
    map_set_cell(5, 5, SOLID); // changing a changed cell needs no new room
    CHECK(map_cell(5, 5) == SOLID);

    before = debug_warning_count();
    map_set_cell(20, 0, SOLID); // outside
    map_set_cell(-1, 0, SOLID);
    map_set_cell(0, 0, 7); // no such metatile
    CHECK(map_cell(0, 0) == EMPTY);
    map_unload(2);
    map_set_cell(0, 0, SOLID); // no playfield
    CHECK(debug_warning_count() == before + (WARNINGS_ON ? 3 : 0));
    reset();
}

static void bodies_land_flush_on_the_floor(void) {
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(60, 0, 12, 16);
    pos_y[i] += FX_ONE / 3; // a fractional start
    int frames = 0;
    while (!(body_contact[i] & MAP_CONTACT_FLOOR) && frames < 200) {
        step(1);
        frames++;
    }
    CHECK(frames < 200);
    CHECK(pos_y[i] == FX(128 - 16)); // bottom edge flush with the floor's top
    CHECK(vel_y[i] == 0);
    CHECK(pos_x[i] == FX(60));
    // Standing still, it touches the floor every frame and doesn't sink.
    for (int f = 0; f < 30; f++) {
        step(1);
        CHECK(body_contact[i] == MAP_CONTACT_FLOOR);
        CHECK(pos_y[i] == FX(128 - 16));
    }
    reset();
}

static void bodies_land_from_any_fraction(void) {
    // Many speeds and starting fractions: always flush, never inside.
    for (int k = 0; k < 64; k++) {
        reset();
        LOAD(room);
        physics_set_gravity(0, FX_ONE / 8 + k);
        u32 i = make_body(60, 20, 16, 16);
        pos_y[i] += k * 7;
        vel_y[i] = k * 9;
        for (int f = 0; f < 120; f++)
            step(1);
        CHECK(pos_y[i] == FX(112));
        CHECK(body_contact[i] == MAP_CONTACT_FLOOR);
    }
    reset();
}

static void bodies_walk_off_ledges(void) {
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(140, 112, 16, 16); // on the floor, left of the gap (x 160-191)
    vel_x[i] = FX(1);
    step(1);
    CHECK(body_contact[i] == MAP_CONTACT_FLOOR);
    // It is supported while any of its pixel columns is over the floor (up to
    // x 159), and drops from x 160 on, entirely over the gap.
    int frames = 0;
    while (pos_y[i] == FX(112) && frames < 100) {
        vel_x[i] = FX(1);
        step(1);
        frames++;
    }
    CHECK(pos_x[i] == FX(160));
    step(5);
    CHECK(!(body_contact[i] & MAP_CONTACT_FLOOR));
    CHECK(pos_y[i] > FX(112));
    reset();
}

static void walls_stop_bodies_on_both_sides(void) {
    reset();
    LOAD(room);
    // Wall at metatile column 17 (x 272-287), rows 6-7 (y 96-127).
    u32 i = make_body(250, 100, 10, 20);
    pos_x[i] += 77; // fractional
    vel_x[i] = FX(3);
    CHECK(step_contacts(i, 10) == MAP_CONTACT_RIGHT);
    CHECK(pos_x[i] == FX(272 - 10));
    CHECK(vel_x[i] == 0);
    // Pushing into it keeps reporting the contact.
    for (int f = 0; f < 5; f++) {
        vel_x[i] = FX_ONE / 4;
        step(1);
        CHECK(body_contact[i] == MAP_CONTACT_RIGHT && pos_x[i] == FX(262));
    }

    // Wall at column 2 (x 32-47) from the right.
    u32 j = make_body(70, 100, 10, 20);
    pos_x[j] += 200;
    vel_x[j] = -FX(2) - 30;
    CHECK(step_contacts(j, 20) == MAP_CONTACT_LEFT);
    CHECK(pos_x[j] == FX(48));
    CHECK(vel_x[j] == 0);
    reset();
}

static void ceilings_stop_jumps(void) {
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 4);
    // Block at (7, 4): x 112-127, y 64-79. A body below it jumps.
    u32 i = make_body(110, 112, 8, 16);
    vel_y[i] = -FX(6);
    u8 seen = 0;
    for (int f = 0; f < 40; f++) {
        step(1);
        seen |= body_contact[i];
        if (body_contact[i] & MAP_CONTACT_CEILING) {
            CHECK(pos_y[i] == FX(80));
            CHECK(vel_y[i] == 0);
        }
    }
    CHECK(seen & MAP_CONTACT_CEILING);
    CHECK(body_contact[i] == MAP_CONTACT_FLOOR); // fell back down
    reset();
}

static void one_way_platforms_block_only_from_above(void) {
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 4);
    // Platform at row 5 (y 80-95), x 192-239. Jump up through it from the floor.
    u32 i = make_body(200, 112, 16, 16);
    vel_y[i] = -FX(7);
    FIXED highest = pos_y[i];
    for (int f = 0; f < 60; f++) {
        step(1);
        CHECK(!(body_contact[i] & MAP_CONTACT_CEILING));
        if (pos_y[i] < highest)
            highest = pos_y[i];
    }
    CHECK(highest < FX(80 - 16));   // went all the way through
    CHECK(pos_y[i] == FX(80 - 16)); // and landed on top
    CHECK(body_contact[i] == MAP_CONTACT_FLOOR);
    // Standing on it reports the floor every frame.
    step(1);
    CHECK(body_contact[i] == MAP_CONTACT_FLOOR && pos_y[i] == FX(64));

    // A body overlapping the platform's row when it starts falling (it jumped
    // into it from below) falls through.
    u32 j = make_body(220, 84, 8, 8);
    step(20);
    CHECK(pos_y[j] > FX(96));
    // Moving sideways through a one-way row is never blocked.
    physics_set_gravity(0, 0);
    u32 k = make_body(150, 84, 8, 8);
    vel_x[k] = FX(4);
    step(10);
    CHECK(pos_x[k] == FX(190) && body_contact[k] == 0);
    reset();
}

static void fast_bodies_do_not_tunnel(void) {
    reset();
    static const char* const thin[] = {
        "..........", "..........", "..........", "..........", "......#...",
        "......#...", "......#...", "......#...", "##########", "..........",
    };
    LOAD(thin);
    u32 i = make_body(20, 40, 8, 8);
    vel_y[i] = FX(100); // far more than a metatile per frame
    step(1);
    CHECK(pos_y[i] == FX(120));
    CHECK(body_contact[i] == MAP_CONTACT_FLOOR);
    u32 j = make_body(0, 70, 8, 8);
    vel_x[j] = FX(250) + 99;
    step(1);
    CHECK(pos_x[j] == FX(96 - 8));
    CHECK(body_contact[j] == MAP_CONTACT_RIGHT);
    reset();
}

static void big_bodies_check_their_whole_edge(void) {
    reset();
    // One solid metatile at (15, 11), x 240-255, under the right end of a
    // 255-wide body.
    static const char* const floor_block[] = {
        "....................", "....................", "....................",
        "....................", "....................", "....................",
        "....................", "....................", "....................",
        "....................", "....................", "...............#....",
        "....................", "....................", "....................",
    };
    LOAD(floor_block);
    physics_set_gravity(0, FX(1));
    u32 i = make_body(10, 0, 255, 24); // x 10-264
    step(60);
    CHECK(pos_y[i] == FX(11 * 16 - 24));
    CHECK(body_contact[i] == MAP_CONTACT_FLOOR);

    // One solid metatile at (12, 7), x 192-207 and y 112-127, in the middle
    // of a 200-high body's right side.
    static const char* const wall_block[] = {
        "....................", "....................", "....................",
        "....................", "....................", "....................",
        "....................", "............#.......", "....................",
        "....................", "....................", "....................",
        "....................", "....................", "....................",
    };
    LOAD(wall_block);
    physics_set_gravity(0, 0);
    u32 j = make_body(0, 20, 40, 200); // y 20-219
    vel_x[j] = FX(5);
    CHECK(step_contacts(j, 40) == MAP_CONTACT_RIGHT);
    CHECK(pos_x[j] == FX(192 - 40));
    reset();
}

static void the_map_edges_are_walls_but_open_above_and_below(void) {
    reset();
    static const char* const open[] = {"..........", "..........", "....##...."};
    LOAD(open);
    u32 i = make_body(5, 10, 8, 8);
    vel_x[i] = -FX(3);
    CHECK(step_contacts(i, 5) == MAP_CONTACT_LEFT);
    CHECK(pos_x[i] == 0);
    vel_x[i] = FX(200);
    step(1);
    CHECK(pos_x[i] == FX(160 - 8) && body_contact[i] == MAP_CONTACT_RIGHT);
    // Above the map, the sides are still walls.
    u32 j = make_body(2, -100, 8, 8);
    vel_x[j] = -FX(5);
    vel_y[j] = -FX(5);
    step(3);
    CHECK(pos_x[j] == 0 && pos_y[j] == FX(-115));
    // Below it, nothing stops a falling body.
    u32 k = make_body(10, 30, 8, 8);
    vel_y[k] = FX(6);
    step(20);
    CHECK(pos_y[k] == FX(30 + 120) && body_contact[k] == 0);
    reset();
}

static void changed_cells_affect_collision(void) {
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 2);
    u32 i = make_body(16, 0, 16, 16);
    map_set_cell(1, 4, ONEWAY); // a platform appears under it
    step(60);
    CHECK(pos_y[i] == FX(48) && body_contact[i] == MAP_CONTACT_FLOOR);
    map_set_cell(1, 4, EMPTY); // and is gone again: down to the floor
    step(60);
    CHECK(pos_y[i] == FX(112) && body_contact[i] == MAP_CONTACT_FLOOR);
    map_set_cell(1, 8, EMPTY); // the floor breaks
    step(60);
    CHECK(pos_y[i] > FX(144));
    reset();
}

static void other_systems_skip_map_bodies(void) {
    reset();
    physics_set_bounds(0, 0, 50, 50);
    physics_set_gravity(0, FX(1));
    u32 i = make_body(10, 10, 4, 4);
    vel_x[i] = FX(100);
    sys_movement();
    sys_physics();
    CHECK(pos_x[i] == FX(10) && pos_y[i] == FX(10));
    CHECK(vel_x[i] == FX(100) && vel_y[i] == 0);
    ent_mask[i] &= ~C_MAPBODY; // an ordinary body again
    sys_movement();
    sys_physics();
    CHECK(pos_x[i] != FX(10));
    physics_set_bounds(0, 0, 240, 160);
    reset();
}

static void misused_map_bodies_warn(void) {
    reset();
    LOAD(room);
    u32 before = debug_warning_count();
    u32 i = entity_index(entity_create(C_POS | C_VEL | C_MAPBODY)); // no C_BODY
    vel_x[i] = FX(1);
    step(3);
    CHECK(pos_x[i] == 0);            // doesn't move
    u32 j = make_body(50, 50, 0, 0); // no size: moves as 1x1
    vel_x[j] = FX(1);
    step(3);
    CHECK(pos_x[j] == FX(53));
    CHECK(debug_warning_count() == before + (WARNINGS_ON ? 2 : 0));
    map_unload(2); // and without a playfield, bodies collide with nothing
    step(1);
    CHECK(pos_x[j] == FX(54));
    CHECK(debug_warning_count() == before + (WARNINGS_ON ? 3 : 0));
    reset();
}

// Drops a bouncing body onto the floor and returns the highest point (lowest
// pos_y) of each rebound in apex[], up to `count` of them; returns how many.
static int bounce_apexes(u32 i, FIXED* apex, int count, int frames) {
    int n = 0;
    bool rising = false;
    for (int f = 0; f < frames; f++) {
        step(1);
        if (body_contact[i] & MAP_CONTACT_FLOOR && vel_y[i] < 0)
            rising = true;
        if (rising && vel_y[i] >= 0) { // the top of the rebound
            rising = false;
            if (n < count)
                apex[n++] = pos_y[i];
        }
    }
    return n;
}

// body_bounce: rebounds lose speed as in sys_physics, each lower than the one
// before (about bounce^2 of the height), until the body comes to rest.
static void map_bodies_bounce_and_come_to_rest(void) {
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(60, 0, 8, 8); // floor at 128: rests at y 120, 120 below
    body_bounce[i] = 192;           // keeps 3/4 of the speed
    FIXED apex[8];
    int n = bounce_apexes(i, apex, 8, 600);
    CHECK(n >= 4);
    // First rebound: (3/4)^2 of 120 = 67.5 pixels high, so the top at y ~52.5.
    CHECK(apex[0] > FX(46) && apex[0] < FX(59));
    for (int k = 1; k < n; k++)
        CHECK(apex[k] > apex[k - 1]); // lower every time
    // At rest: flush on the floor, still, touching it every frame.
    CHECK(pos_y[i] == FX(120) && vel_y[i] == 0);
    for (int f = 0; f < 10; f++) {
        step(1);
        CHECK(body_contact[i] == MAP_CONTACT_FLOOR);
        CHECK(pos_y[i] == FX(120) && vel_y[i] == 0);
    }
    reset();
}

// Drops map body i, at rest, toward a floor on one axis (axis 0: x, 1: y; dir
// +1 or -1: the way gravity pulls along it) and returns how far its peaks get
// from where it started, along that axis: the largest difference, in either
// direction, over `bounces` rebounds (-1 if it made fewer in `frames`).
static FIXED perfect_drop_error(u32 i, int axis, int dir, int bounces, int frames) {
    FIXED* pos = axis ? pos_y : pos_x;
    FIXED* vel = axis ? vel_y : vel_x;
    const u32 floor = axis ? (dir > 0 ? MAP_CONTACT_FLOOR : MAP_CONTACT_CEILING)
                           : (dir > 0 ? MAP_CONTACT_RIGHT : MAP_CONTACT_LEFT);
    const FIXED start = pos[i] * dir;
    FIXED worst = 0;
    bool rising = false;
    int n = 0;
    for (int f = 0; f < frames && n < bounces; f++) {
        step(1);
        if ((body_contact[i] & floor) && vel[i] * dir < 0)
            rising = true;
        if (rising && vel[i] * dir >= 0) { // the top of the rebound
            rising = false;
            FIXED error = pos[i] * dir - start;
            error = error < 0 ? -error : error;
            worst = error > worst ? error : worst;
            n++;
        }
    }
    return n == bounces ? worst : -1;
}

// body_bounce 255 is a perfect bounce: off a floor, the body rebounds exactly
// to the height it fell from (to within a pixel: the frame steps), every time,
// so it never comes to rest, while 254 loses a little height on every bounce.
// Keeping the speed instead stops the body flush against the floor short of
// where the frame would have taken it and rebounds it lower, by up to a
// frame's fall (13 pixels at 1 pixel per frame per frame). Drops of several
// heights under several gravities, onto the floor, onto a ceiling with the
// body's gravity reversed, and onto a wall with gravity sideways. A body
// already resting on a floor stays at rest, whatever its bounce.
static void map_bodies_bounce_perfectly_at_255(void) {
    static const struct {
        FIXED gravity;
        int height;
    } drops[] = {{FX_ONE / 16, 100}, {FX_ONE / 16, 9}, {FX_ONE / 4, 57},
                 {FX_ONE / 4, 120},  {FX_ONE, 33},     {FX_ONE, 104}};
    const FIXED tolerance = FX(14) / 10;
    for (u32 d = 0; d < sizeof(drops) / sizeof(drops[0]); d++) {
        reset();
        LOAD(room);
        physics_set_gravity(0, drops[d].gravity);
        u32 i = make_body(60, 120 - drops[d].height, 8, 8); // floor at 128: rests at y 120
        body_bounce[i] = 255;
        FIXED error = perfect_drop_error(i, 1, 1, 4, 1200);
        CHECK(error >= 0 && error <= tolerance);
    }
    // Up into the block at (7, 4) (y 64-79) with reversed gravity, and left
    // onto the wall at column 2 (x 32-47) with gravity to the left.
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 4);
    u32 up = make_body(116, 80 + 37, 8, 8);
    body_gravity[up] = BODY_GRAVITY(-16);
    body_bounce[up] = 255;
    FIXED error = perfect_drop_error(up, 1, -1, 4, 600);
    CHECK(error >= 0 && error <= tolerance);
    reset();
    LOAD(room);
    physics_set_gravity(-FX_ONE / 2, 0);
    u32 side = make_body(48 + 70, 100, 8, 8);
    body_bounce[side] = 255;
    error = perfect_drop_error(side, 0, -1, 4, 600);
    CHECK(error >= 0 && error <= tolerance);

    FIXED apex[8];
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 4);
    u32 almost = make_body(80, 0, 8, 8);
    body_bounce[almost] = 254;
    CHECK(bounce_apexes(almost, apex, 8, 1000) == 8);
    for (int k = 1; k < 8; k++)
        CHECK(apex[k] > apex[k - 1]);
    u32 resting = make_body(100, 120, 8, 8); // on the floor
    body_bounce[resting] = 255;
    for (int f = 0; f < 10; f++) {
        step(1);
        CHECK(body_contact[resting] == MAP_CONTACT_FLOOR);
        CHECK(pos_y[resting] == FX(120) && vel_y[resting] == 0);
    }
    // Walls and ceilings: the speed, reversed.
    physics_set_gravity(0, 0);
    u32 i = make_body(60, 100, 8, 8);
    body_bounce[i] = 255;
    vel_x[i] = -FX(4) - 3;
    CHECK(step_contacts(i, 5) == MAP_CONTACT_LEFT);
    CHECK(vel_x[i] == FX(4) + 3);
    u32 j = make_body(116, 90, 8, 8); // under the block at (7, 4): y 64-79
    body_bounce[j] = 255;
    vel_y[j] = -FX(3);
    CHECK(step_contacts(j, 4) == MAP_CONTACT_CEILING);
    CHECK(vel_y[j] == FX(3));
    // Under gravity too, a ceiling is no floor: the speed it hit at, reversed.
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 4);
    u32 k = make_body(116, 90, 8, 8);
    body_bounce[k] = 255;
    vel_y[k] = -FX(3);
    CHECK(step_contacts(k, 5) == MAP_CONTACT_CEILING); // hit on the 5th frame, at -1.75
    CHECK(vel_y[k] == FX(7) / 4);
    reset();
}

// Walls and ceilings use body_bounce too; 0 stops a body, as before.
static void map_bodies_bounce_off_walls_and_ceilings(void) {
    reset();
    LOAD(room);
    u32 i = make_body(60, 100, 8, 8); // between the walls at columns 2 and 17
    body_bounce[i] = 128;
    vel_x[i] = -FX(4);
    CHECK(step_contacts(i, 5) == MAP_CONTACT_LEFT);
    CHECK(vel_x[i] == FX(2)); // half the speed, reversed
    int frames = 0;
    while (!(body_contact[i] & MAP_CONTACT_RIGHT) && frames++ < 200)
        step(1);
    CHECK(pos_x[i] == FX(272 - 8) && vel_x[i] == -FX(1));
    u32 j = make_body(116, 90, 8, 8); // under the block at (7, 4): y 64-79
    body_bounce[j] = 128;
    vel_y[j] = -FX(4);
    CHECK(step_contacts(j, 4) == MAP_CONTACT_CEILING);
    CHECK(vel_y[j] == FX(2));
    u32 k = make_body(60, 100, 8, 8); // no bounce: stops against the wall
    vel_x[k] = -FX(4);
    step(5);
    CHECK(pos_x[k] == FX(48) && vel_x[k] == 0);
    reset();
}

// body_friction slows a body sliding on a floor (rounded up, so even 1/256
// stops it), not one in the air; without friction nothing changes.
static void map_bodies_slide_with_friction(void) {
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(60, 120, 8, 8); // on the floor
    body_friction[i] = 32;
    vel_x[i] = FX(2);
    step(1);
    CHECK(vel_x[i] == FX(2) - FX(2) / 8);
    step(60);
    CHECK(vel_x[i] == 0 && pos_x[i] > FX(70) && pos_x[i] < FX(100));
    u32 slow = make_body(100, 120, 8, 8); // tiny friction, moving left
    body_friction[slow] = 1;
    vel_x[slow] = -FX(1);
    step(300);
    CHECK(vel_x[slow] == 0);
    u32 air = make_body(100, 20, 8, 8); // falling: no friction yet
    body_friction[air] = 128;
    vel_x[air] = FX(1);
    step(5);
    CHECK(vel_x[air] == FX(1));
    u32 none = make_body(120, 120, 8, 8); // no friction: keeps even a crawl
    vel_x[none] = FX_ONE / 32;
    step(30);
    CHECK(vel_x[none] == FX_ONE / 32);
    reset();
}

// body_max_fall: applied after gravity, so the speed never exceeds it; also
// without a playfield.
static void map_bodies_fall_no_faster_than_max_fall(void) {
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(164, 0, 8, 8); // over the gap at columns 10-11
    body_max_fall[i] = FX(3);
    for (int f = 0; f < 40; f++) {
        FIXED before = pos_y[i];
        step(1);
        CHECK(pos_y[i] - before <= FX(3));
        CHECK(vel_y[i] <= FX(3));
    }
    CHECK(vel_y[i] == FX(3));
    // Upward gravity, and a jump against gravity is kept.
    physics_set_gravity(0, -FX_ONE / 2);
    vel_y[i] = 0;
    step(20);
    CHECK(vel_y[i] == -FX(3));
    physics_set_gravity(0, FX_ONE / 4);
    vel_y[i] = -FX(6);
    step(1);
    CHECK(vel_y[i] == -FX(6) + FX_ONE / 4);
    // Landing from max fall: still flush.
    u32 j = make_body(60, 0, 8, 8);
    body_max_fall[j] = FX(5);
    step(60);
    CHECK(pos_y[j] == FX(120) && vel_y[j] == 0);
    map_unload(2);
    u32 k = make_body(0, 0, 8, 8);
    body_max_fall[k] = FX(2);
    step(20);
    CHECK(vel_y[k] == FX(2));
    reset();
}

// body_gravity scales gravity for map bodies too (physics.h).
static void map_bodies_use_their_gravity_scale(void) {
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 4);
    u32 none = make_body(60, 20, 8, 8);
    u32 half = make_body(80, 20, 8, 8);
    u32 up = make_body(114, 100, 8, 8); // under the ceiling block at (7, 4): y 64-79
    body_gravity[none] = BODY_GRAVITY(0);
    body_gravity[half] = BODY_GRAVITY(8);
    body_gravity[up] = BODY_GRAVITY(-16);
    step(4);
    CHECK(vel_y[none] == 0 && pos_y[none] == FX(20));
    CHECK(vel_y[half] == FX_ONE / 2);
    CHECK(vel_y[up] == -FX(1));
    step(30);
    // Reversed gravity: it rests against the ceiling, touching it every frame.
    CHECK(pos_y[up] == FX(80) && vel_y[up] == 0);
    CHECK(body_contact[up] == MAP_CONTACT_CEILING);
    step(1);
    CHECK(body_contact[up] == MAP_CONTACT_CEILING && pos_y[up] == FX(80));
    reset();
}

// sys_physics() reports contacts for bouncing bodies in the same pool, but
// leaves map bodies' contacts alone; an entity that stops being a map body
// doesn't keep its last map contacts.
static void physics_contacts_leave_map_bodies_alone(void) {
    reset();
    LOAD(room);
    physics_set_gravity(0, FX_ONE / 4);
    physics_set_contacts(true);
    u32 walker = make_body(60, 112, 16, 16); // on the floor
    u32 ball = entity_index(entity_create(C_POS | C_VEL | C_BODY));
    pos_x[ball] = FX(0);
    pos_y[ball] = FX(50);
    vel_x[ball] = -FX(2);
    body_w[ball] = body_h[ball] = 8;
    sys_map_movement();
    sys_movement();
    sys_physics();
    CHECK(body_contact[walker] == MAP_CONTACT_FLOOR);
    CHECK(body_contact[ball] == BODY_SIDE_LEFT);
    ent_mask[walker] &= ~C_MAPBODY; // now a bouncing body, inside the bounds
    sys_map_movement();
    sys_movement();
    sys_physics();
    CHECK(body_contact[walker] == 0);
    physics_set_contacts(false);
    reset();
}

TEST_SUITE(map_tests, "map",
           {"collision reads metatiles and the edges", collision_reads_metatiles_and_the_edges},
           {"tags come with the collision byte", tags_come_with_the_collision_byte},
           {"map_load rejects bad layers", map_load_rejects_bad_layers},
           {"map_load refuses unknown layer flags", map_load_refuses_unknown_layer_flags},
           {"reserved collision types load and collide as empty",
            reserved_collision_types_load_and_collide_as_empty},
           // After "map_load rejects bad layers": it loads a cell naming a
           // missing metatile, whose warning that test counts.
           {"map_tags_in ORs the tags of the metatiles overlapped",
            map_tags_in_ors_the_tags_of_the_metatiles_overlapped},
           {"map_tags_in finds what a body stands on", map_tags_in_finds_what_a_body_stands_on},
           // Before other camera tests: the warning comes once per run, so a
           // wrong one there would hide the checks for none here.
           {"camera warns on a playfield smaller than the screen",
            camera_warns_on_a_playfield_smaller_than_the_screen},
           {"camera is clamped to the playfield", camera_is_clamped_to_the_playfield},
           {"layers scroll by camera, factor and offset",
            layers_scroll_by_camera_factor_and_offset},
           {"changed cells override the map", changed_cells_override_the_map},
           {"the change table is bounded", the_change_table_is_bounded},
           {"bodies land flush on the floor", bodies_land_flush_on_the_floor},
           {"bodies land from any fraction", bodies_land_from_any_fraction},
           {"bodies walk off ledges", bodies_walk_off_ledges},
           {"walls stop bodies on both sides", walls_stop_bodies_on_both_sides},
           {"ceilings stop jumps", ceilings_stop_jumps},
           {"one-way platforms block only from above", one_way_platforms_block_only_from_above},
           {"fast bodies do not tunnel", fast_bodies_do_not_tunnel},
           {"big bodies check their whole edge", big_bodies_check_their_whole_edge},
           {"the map edges are walls but open above and below",
            the_map_edges_are_walls_but_open_above_and_below},
           {"changed cells affect collision", changed_cells_affect_collision},
           {"other systems skip map bodies", other_systems_skip_map_bodies},
           {"misused map bodies warn", misused_map_bodies_warn},
           {"map bodies bounce and come to rest", map_bodies_bounce_and_come_to_rest},
           {"map bodies bounce perfectly at 255", map_bodies_bounce_perfectly_at_255},
           {"map bodies bounce off walls and ceilings", map_bodies_bounce_off_walls_and_ceilings},
           {"map bodies slide with friction", map_bodies_slide_with_friction},
           {"map bodies fall no faster than max fall", map_bodies_fall_no_faster_than_max_fall},
           {"map bodies use their gravity scale", map_bodies_use_their_gravity_scale},
           {"physics contacts leave map bodies alone", physics_contacts_leave_map_bodies_alone}, );
