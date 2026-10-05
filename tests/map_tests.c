// Tests for map.h's platform-neutral part: map layers as data, the camera,
// runtime cell changes, collision queries and sys_map_movement. Run natively
// and in the test ROM (which also draws the layers; see tests/rom/map_tests.c
// for that).

#include "serval/debug.h"
#include "serval/map.h"
#include "serval/physics.h"
#include "test.h"

#include <stddef.h>

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
    static const char* const small[] = {"...", "..."}; // smaller than the screen
    LOAD(small);
    camera_set(10, 10);
    CHECK(camera_x() == 0 && camera_y() == 0);
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
    // The most bouncy body never gains height.
    u32 j = make_body(80, 0, 8, 8);
    body_bounce[j] = 255;
    n = bounce_apexes(j, apex, 8, 600);
    CHECK(n == 8);
    for (int k = 0; k < n; k++)
        CHECK(apex[k] >= 0);
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
    body_max_fall[i] = 3;
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
    body_max_fall[j] = 5;
    step(60);
    CHECK(pos_y[j] == FX(120) && vel_y[j] == 0);
    map_unload(2);
    u32 k = make_body(0, 0, 8, 8);
    body_max_fall[k] = 2;
    step(20);
    CHECK(vel_y[k] == FX(2));
    reset();
}

TEST_SUITE(map_tests, "map",
           {"collision reads metatiles and the edges", collision_reads_metatiles_and_the_edges},
           {"tags come with the collision byte", tags_come_with_the_collision_byte},
           {"map_load rejects bad layers", map_load_rejects_bad_layers},
           {"camera is clamped to the playfield", camera_is_clamped_to_the_playfield},
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
           {"map bodies bounce off walls and ceilings", map_bodies_bounce_off_walls_and_ceilings},
           {"map bodies slide with friction", map_bodies_slide_with_friction},
           {"map bodies fall no faster than max fall", map_bodies_fall_no_faster_than_max_fall}, );
