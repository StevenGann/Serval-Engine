// ladders and slopes: tests for map.h's floor slopes (MAP_SLOPE_*) and
// ladders (MAP_LADDER, MAP_CONTACT_LADDER) in sys_map_movement, as
// docs/tilemaps.md#collision-types specifies them. Run natively and in the
// test ROM. The rest of map collision is in map_tests.c, which also runs its
// body tests on a playfield with slopes (sys_map_movement's general path).

#include "serval/debug.h"
#include "serval/map.h"
#include "serval/physics.h"
#include "serval/vm.h"
#include "test.h"

#include "ladder_climber_script.h"

enum { EMPTY, SOLID, ONEWAY, LADDER, R, L, R_LOW, R_HIGH, L_HIGH, L_LOW, METATILES };

static const Metatile metatiles[METATILES] = {
    [EMPTY] = {{0, 0, 0, 0}, MAP_EMPTY},
    [SOLID] = {{1, 1, 1, 1}, MAP_SOLID},
    [ONEWAY] = {{2, 2, 2, 2}, MAP_ONEWAY},
    [LADDER] = {{3, 3, 3, 3}, MAP_LADDER | MAP_TAG(1)},
    [R] = {{4, 4, 4, 4}, MAP_SLOPE_R},
    [L] = {{5, 5, 5, 5}, MAP_SLOPE_L},
    [R_LOW] = {{6, 6, 6, 6}, MAP_SLOPE_R_LOW},
    [R_HIGH] = {{7, 7, 7, 7}, MAP_SLOPE_R_HIGH},
    [L_HIGH] = {{8, 8, 8, 8}, MAP_SLOPE_L_HIGH | MAP_TAG(3)},
    [L_LOW] = {{9, 9, 9, 9}, MAP_SLOPE_L_LOW},
};

// The height of each slope's solid part in each pixel column, as
// docs/tilemaps.md's table gives it (R first, in type order): written out,
// not computed, so the engine's formulas are checked against the spec.
static const u8 heights[6][16] = {
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},        // MAP_SLOPE_R
    {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1},        // MAP_SLOPE_L
    {1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8},               // MAP_SLOPE_R_LOW
    {9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16}, // _R_HIGH
    {16, 16, 15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9, 9}, // _L_HIGH
    {8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1},               // MAP_SLOPE_L_LOW
};

#define MAX_CELLS (40 * 16)
// In EWRAM, to keep the test ROM's IWRAM for the engine's code.
static SERVAL_EWRAM_BSS u16 cells[MAX_CELLS];
static MapLayer playfield;
static u16 map_w, map_h;

static u16 cell_of(char c) {
    switch (c) {
    case '#':
        return SOLID;
    case '-':
        return ONEWAY;
    case 'H':
        return LADDER;
    case '/':
        return R;
    case '\\':
        return L;
    case 'a':
        return R_LOW;
    case 'b':
        return R_HIGH;
    case 'c':
        return L_HIGH;
    case 'd':
        return L_LOW;
    default:
        return EMPTY;
    }
}

// Loads a playfield drawn as text, one string per metatile row: '.' empty,
// '#' solid, '-' one-way, 'H' ladder, '/' MAP_SLOPE_R, '\' MAP_SLOPE_L, 'a'
// _R_LOW, 'b' _R_HIGH, 'c' _L_HIGH, 'd' _L_LOW.
static void load_rows(const char* const* rows, u16 height) {
    u16 width = 0;
    while (rows[0][width])
        width++;
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++)
            cells[y * width + x] = cell_of(rows[y][x]);
    }
    map_w = width;
    map_h = height;
    playfield = (MapLayer){.width = width,
                           .height = height,
                           .cells = cells,
                           .metatiles = metatiles,
                           .metatile_count = METATILES,
                           .bg = 2};
    CHECK(map_load(&playfield));
}
#define LOAD(rows) load_rows(rows, (u16)(sizeof(rows) / sizeof(rows[0])))

// The y a body's bottom edge rests at on the ground in pixel column x: the
// top of the highest solid pixel or one-way metatile there (the maps below
// have nothing above the ground they are walked on), or the map's bottom.
static int surface(int x) {
    for (int my = 0; my < map_h; my++) {
        u16 cell = map_cell(x >> 4, my);
        if (cell == SOLID || cell == ONEWAY)
            return my * 16;
        if (cell >= R && cell <= L_LOW)
            return my * 16 + 16 - heights[cell - R][x & 15];
    }
    return map_h * 16;
}

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

static void step(int frames) {
    for (int f = 0; f < frames; f++)
        sys_map_movement();
}

// The pixel column of a body's foot: its horizontal centre.
static int foot(u32 i) {
    return (pos_x[i] + (FX(body_w[i]) >> 1)) >> FX_SHIFT;
}

// True if body i stands on the ground under its foot.
static bool on_ground(u32 i) {
    return pos_y[i] + FX(body_h[i]) == FX(surface(foot(i))) &&
           (body_contact[i] & MAP_CONTACT_FLOOR);
}

// --- Slopes ------------------------------------------------------------------

// tilemaps.md's table: the surface of each slope in each pixel column, for a
// body of one pixel and for a wider one with its foot there. They land
// flush on it, with MAP_CONTACT_FLOOR, and stay.
static void slopes_are_solid_below_their_line(void) {
    for (u16 type = R; type <= L_LOW; type++) {
        reset();
        static const char* const one[] = {"...", ".X.", "###"};
        LOAD(one);
        cells[4] = type; // the X
        physics_set_gravity(0, FX_ONE / 4);
        CHECK(MAP_TYPE(map_collision_at(16, 16)) == MAP_TYPE(metatiles[type].collision));
        CHECK(map_collision_at(31, 31) == metatiles[type].collision); // tags too
        for (int c = 0; c < 16; c++) {
            int top = 32 - heights[type - R][c];
            u32 thin = make_body(16 + c, 0, 1, 1);
            u32 wide = make_body(16 + c - 6, 2, 12, 9);
            pos_y[thin] += FX_ONE / 3; // fractions land flush too
            step(40);
            CHECK(pos_y[thin] == FX(top - 1) && body_contact[thin] == MAP_CONTACT_FLOOR);
            CHECK(pos_y[wide] == FX(top - 9) && body_contact[wide] == MAP_CONTACT_FLOOR);
            CHECK(vel_y[thin] == 0 && vel_y[wide] == 0);
            step(5);
            CHECK(pos_y[thin] == FX(top - 1) && pos_y[wide] == FX(top - 9));
            entity_destroy(entity_at(thin));
            entity_destroy(entity_at(wide));
        }
    }
    reset();
}

// A 45-degree hill (rows 5-6, columns 4-9), a 1:2 one (columns 12-21), two
// 45-degree peaks with a valley between them (columns 26-29), on a floor.
static const char* const hills[] = {
    "................................",    // 0
    "................................",    // 1
    "................................",    // 2
    "................................",    // 3
    "................................",    // 4
    "...../##\\.....ab##cd............",   // 5
    "..../####\\..ab######cd..../\\/\\..", // 6
    "################################",    // 7
};

// Walks body i (on the ground) at `speed` until its foot passes `until`,
// checking every frame that it stands on the ground under its foot: up and
// down slopes without stair-stepping, flying off a crest or leaving it.
static bool walk(u32 i, FIXED speed, int until) {
    bool ok = true;
    for (int f = 0; f < 600; f++) {
        if (speed > 0 ? foot(i) >= until : foot(i) <= until)
            return ok;
        vel_x[i] = speed;
        int before = foot(i);
        step(1);
        ok &= on_ground(i) && foot(i) != before;
    }
    return false;
}

static void bodies_walk_up_and_down_slopes(void) {
    static const u8 widths[] = {1, 12, 16, 32, 64};
    static const FIXED speeds[] = {FX_ONE / 4, FX_ONE, FX_ONE * 3 / 2, FX(3), FX(7), FX(9) + 77};
    for (u32 k = 0; k < sizeof(widths); k++) {
        for (u32 s = 0; s < sizeof(speeds) / sizeof(speeds[0]); s++) {
            reset();
            LOAD(hills);
            physics_set_gravity(0, FX_ONE / 4);
            u8 w = widths[k];
            u32 i = make_body(40 - w / 2, 112 - 20, w, 20);
            step(10); // lands on the floor
            CHECK(on_ground(i));
            // A pixel per frame or less moves the foot a column every few
            // frames: walk() wants a new column every frame.
            FIXED v = speeds[s];
            if (v < FX_ONE) {
                for (int f = 0; f < 1800 && foot(i) < 470; f++) {
                    vel_x[i] = v;
                    step(1);
                    CHECK(on_ground(i));
                }
                CHECK(foot(i) >= 470); // nothing stopped it
                for (int f = 0; f < 1800 && foot(i) > 40; f++) {
                    vel_x[i] = -v;
                    step(1);
                    CHECK(on_ground(i));
                }
                CHECK(foot(i) <= 40);
                continue;
            }
            CHECK(walk(i, v, 470));
            CHECK(walk(i, -v, 40));
        }
    }
    reset();
}

// Walking down at speed, a body stays on a slope; a body that isn't walking
// (it jumped, or has no gravity) doesn't follow the ground down.
static void bodies_stick_to_slopes_only_when_walking(void) {
    reset();
    LOAD(hills);
    physics_set_gravity(0, FX_ONE / 2);
    u32 i = make_body(100, 60, 12, 16); // on the 45-degree hill's top
    step(10);
    CHECK(pos_y[i] == FX(80 - 16) && on_ground(i));
    // Off the right side at 6 pixels a frame: on the ground every frame,
    // down to the floor.
    CHECK(walk(i, FX(6), 200));
    // Jumping down it: not stuck, it leaves the ground.
    pos_x[i] = FX(100);
    pos_y[i] = FX(80 - 16);
    step(2);
    CHECK(on_ground(i));
    vel_x[i] = FX(3);
    vel_y[i] = -FX(2);
    step(1);
    CHECK(!(body_contact[i] & MAP_CONTACT_FLOOR));
    CHECK(pos_y[i] < FX(80 - 16));
    // Without gravity, a body moving sideways off the top floats on.
    pos_x[i] = FX(100);
    pos_y[i] = FX(80 - 16);
    vel_y[i] = 0;
    body_gravity[i] = BODY_GRAVITY(0);
    step(2);
    vel_x[i] = FX(2);
    step(20);
    CHECK(pos_y[i] == FX(80 - 16) && body_contact[i] == 0);
    reset();
}

// A body without gravity moving into a slope slides up it, touching the
// floor; moving back, it doesn't come down.
static void bodies_without_gravity_slide_up_slopes(void) {
    reset();
    LOAD(hills);
    u32 i = make_body(20, 112 - 10, 10, 10);
    vel_x[i] = FX(2);
    u8 seen = 0;
    for (int f = 0; f < 40; f++) {
        step(1);
        seen |= body_contact[i];
        if (foot(i) >= 64 && foot(i) < 96)
            CHECK(on_ground(i));
    }
    CHECK(seen == MAP_CONTACT_FLOOR);
    CHECK(pos_y[i] == FX(80 - 10)); // on the hill's top
    vel_x[i] = -FX(2);
    step(40);
    CHECK(pos_y[i] == FX(80 - 10) && body_contact[i] == 0);
    reset();
}

// Jumping onto a slope from the floor, from either side and at several
// speeds: it lands on the surface under its foot and stays there.
static void bodies_land_on_slopes_from_a_jump(void) {
    for (int k = 0; k < 12; k++) {
        reset();
        LOAD(hills);
        physics_set_gravity(0, FX_ONE / 4);
        bool right = k % 2 == 0;
        u32 i = make_body(right ? 20 + k : 200 - k, 112 - 15, 12, 15);
        step(2);
        FIXED vx = (right ? 1 : -1) * (FX(2) + k * 16);
        vel_y[i] = -FX(5) - k * 7;
        bool landed = false;
        for (int f = 0; f < 120 && !landed; f++) {
            vel_x[i] = vx;
            step(1);
            landed = (body_contact[i] & MAP_CONTACT_FLOOR) && f > 2;
        }
        CHECK(landed && on_ground(i) && vel_y[i] == 0);
        int x = foot(i);
        CHECK(x > 64 && x < 160); // on the hill, not the floor
        vel_x[i] = 0;
        step(10);
        CHECK(on_ground(i) && foot(i) == x);
    }
    reset();
}

// A slope's high side (or the 8- and 9-pixel middle sides of a 1:2 slope's
// halves), exposed, is a wall from the side, on the floor and in the air;
// its low end, a pixel high, is walked onto.
static void exposed_slope_sides_are_walls(void) {
    static const char* const lone[] = {
        "..........", //
        "..........", //
        "....X.....", // the slope at (4, 2), x 64-79
        "##########", //
    };
    static const struct {
        u16 type;
        bool from_left; // its side facing left is the exposed one
    } sides[] = {{R, false},     {L, true},       {R_LOW, false},
                 {R_HIGH, true}, {L_HIGH, false}, {L_LOW, true}};
    for (u32 k = 0; k < 6; k++) {
        reset();
        LOAD(lone);
        cells[2 * 10 + 4] = sides[k].type;
        physics_set_gravity(0, FX_ONE / 4);
        bool left = sides[k].from_left;
        u32 i = make_body(left ? 30 : 110, 48 - 12, 12, 12);
        step(2);
        u8 seen = 0;
        for (int f = 0; f < 40; f++) {
            vel_x[i] = left ? FX(2) : -FX(2);
            step(1);
            seen |= body_contact[i];
        }
        CHECK(seen == (MAP_CONTACT_FLOOR | (left ? MAP_CONTACT_RIGHT : MAP_CONTACT_LEFT)));
        CHECK(pos_x[i] == (left ? FX(64 - 12) : FX(80)));
        CHECK(pos_y[i] == FX(48 - 12));
        // In the air beside it (y 40-45), also a wall.
        u32 j = make_body(left ? 40 : 90, 40, 12, 6);
        body_gravity[j] = BODY_GRAVITY(0);
        vel_x[j] = left ? FX(3) : -FX(3);
        step(10);
        CHECK(pos_x[j] == (left ? FX(64 - 12) : FX(80)));
    }
    reset();
}

// From below, every slope is a ceiling across its width.
static void slopes_are_ceilings_from_below(void) {
    for (u16 type = R; type <= L_LOW; type++) {
        reset();
        static const char* const above[] = {"......", "..X...", "......", "......", "######"};
        LOAD(above);
        cells[1 * 6 + 2] = type;
        for (int x = 32 - 2; x < 48; x += 3) {
            u32 i = make_body(x, 50, 3, 4);
            vel_y[i] = -FX(5);
            step(10);
            CHECK(pos_y[i] == FX(32));
            entity_destroy(entity_at(i));
        }
        u32 thin = make_body(32, 50, 1, 1); // under the lowest column
        vel_y[thin] = -FX(3);
        step(10);
        CHECK(pos_y[thin] == FX(32) && body_contact[thin] == 0);
    }
    reset();
}

// Up a ramp with nothing after it, a body walks off its top and falls,
// never rising above it: no launch.
static void bodies_walk_off_a_ramp_without_launching(void) {
    reset();
    static const char* const ramp[] = {
        "..........", //
        "..........", //
        "..........", //
        "....ab....", // the ramp's top half: x 64-95, its top y 48
        "..ab##....", //
        "###.......", // the floor: y 80
        "..........", //
        "..........", //
    };
    LOAD(ramp);
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(4, 80 - 12, 12, 12);
    step(2);
    CHECK(walk(i, FX(3), 92)); // to the top: its foot at x 94
    int highest = 0x7FFFFFFF;
    for (int f = 0; f < 30; f++) {
        vel_x[i] = FX(3);
        step(1);
        int top = fx_to_int(pos_y[i]);
        highest = top < highest ? top : highest;
    }
    CHECK(highest >= 48 - 12); // never above the ramp's top
    CHECK(pos_y[i] > FX(80));  // fell past the floor's level
    reset();
}

// A one-way floor at a slope's foot and a one-way bridge level with its top
// are walked onto from it; a slope down to a wall stops at the wall.
static void slopes_join_one_way_platforms_and_walls(void) {
    reset();
    static const char* const joins[] = {
        "..............",  //
        "..............",  //
        "..............",  //
        "..............",  //
        ".../----\\.....", // a one-way bridge (x 64-127) between two hills' tops (y 64)
        "../#....#\\....", //
        "--------------",  // one-way floors at their feet (y 96)
    };
    LOAD(joins);
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(4, 96 - 14, 12, 14);
    step(2);
    CHECK(on_ground(i));
    CHECK(walk(i, FX(2), 200));
    CHECK(walk(i, -FX(2), 10));
    // Under the bridge, it can jump up through it, and lands on it.
    pos_x[i] = FX(90);
    vel_x[i] = 0;
    step(2);
    CHECK(pos_y[i] == FX(96 - 14) && body_contact[i] == MAP_CONTACT_FLOOR);
    vel_y[i] = -FX(6);
    step(60);
    CHECK(on_ground(i) && pos_y[i] == FX(64 - 14));
    reset();
    static const char* const walled[] = {
        "........", //
        "........", //
        ".#/####.", // down the slope (x 32-47) to the wall (x 16-31)
        "########", //
    };
    LOAD(walled);
    physics_set_gravity(0, FX_ONE / 4);
    u32 j = make_body(80, 32 - 12, 12, 12);
    step(2);
    CHECK(walk(j, -FX(2), 41));
    u8 seen = 0;
    for (int f = 0; f < 10; f++) {
        vel_x[j] = -FX(2);
        step(1);
        seen |= body_contact[j];
    }
    CHECK(pos_x[j] == FX(32) && seen == (MAP_CONTACT_FLOOR | MAP_CONTACT_LEFT));
    CHECK(on_ground(j));
    reset();
}

// A wide body landing with its foot on the floor beside a hill lands on the
// floor: the solid metatile under the hill's second slope, under its side,
// is that slope's foundation, not a floor.
static void wide_bodies_land_on_the_floor_beside_a_hill(void) {
    reset();
    LOAD(hills);
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(60 - 24, 0, 48, 16); // foot at x 60, right side over x 80-83
    step(60);
    CHECK(on_ground(i) && pos_y[i] == FX(112 - 16));
    reset();
}

// A body whose foot ends up inside a slope (a cell changed under it) can
// move out of it, and doesn't fall through the floor under it.
static void bodies_move_out_of_slopes_changed_under_them(void) {
    reset();
    static const char* const flat[] = {"......", "......", "######"};
    LOAD(flat);
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(20, 32 - 12, 12, 12);
    step(2);
    map_set_cell(1, 1, R); // around its foot: x 16-31
    step(5);
    CHECK(pos_y[i] == FX(32 - 12) && body_contact[i] == MAP_CONTACT_FLOOR);
    vel_x[i] = FX(1);
    step(20);
    CHECK(pos_x[i] == FX(40) && pos_y[i] == FX(32 - 12));
    reset();
}

// A perfect bounce on a slope rebounds straight up, to the height it fell
// from, as on a floor.
static void bodies_bounce_on_slopes(void) {
    reset();
    LOAD(hills);
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(70 - 4, 20, 8, 8); // foot at x 70, over the 45-degree slope
    body_bounce[i] = 255;
    int highest = 0;
    bool bounced = false;
    for (int f = 0; f < 300; f++) {
        step(1);
        bounced |= (body_contact[i] & MAP_CONTACT_FLOOR) != 0;
        if (bounced && f > 60) {
            int y = fx_to_int(pos_y[i]);
            highest = highest == 0 || y < highest ? y : highest;
        }
    }
    CHECK(bounced && pos_x[i] == FX(70 - 4));
    CHECK(highest >= 19 && highest <= 21);
    reset();
}

// --- Ladders -----------------------------------------------------------------

// A ladder up column 2 to a ledge (columns 3-5, row 3), on a floor.
static const char* const ladder_room[] = {
    "........", // 0
    "........", // 1
    "........", // 2
    "..H###..", // 3
    "..H.....", // 4
    "..H.....", // 5
    "..H.....", // 6
    "########", // 7
};

// Ladders load with no warning, read as themselves and are no walls.
static void ladders_load_and_are_not_solid(void) {
    reset();
    u32 before = debug_warning_count();
    LOAD(ladder_room);
    CHECK(debug_warning_count() == before);
    CHECK(map_collision_at(40, 60) == (MAP_LADDER | MAP_TAG(1)));
    // Sideways through it, on the floor, with or without gravity.
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(0, 112 - 10, 10, 10);
    u8 seen = 0;
    bool overlapped = false, after = false;
    for (int f = 0; f < 40; f++) {
        vel_x[i] = FX(2);
        step(1);
        seen |= body_contact[i];
        int x = fx_to_int(pos_x[i]);
        bool over = x + 10 > 32 && x < 48;
        CHECK(((body_contact[i] & MAP_CONTACT_LADDER) != 0) == over);
        overlapped |= over;
        after |= x >= 48;
    }
    CHECK(overlapped && after);
    CHECK(seen == (MAP_CONTACT_FLOOR | MAP_CONTACT_LADDER));
    reset();
}

// MAP_CONTACT_LADDER: set while the body overlaps a ladder metatile or
// stands on one (the row below its feet), not when it is beside or above one.
static void ladder_contact_is_overlap_or_standing_on_one(void) {
    reset();
    LOAD(ladder_room);
    static const struct {
        int x, y;
        bool ladder;
    } cases[] = {
        {22, 80, false}, // beside it, to the left (x 22-31)
        {23, 80, true},  // overlapping its first column
        {48, 80, false}, // beside it, to the right
        {47, 80, true},  // overlapping its last column
        {36, 38, true},  // standing on its top (y 48)
        {36, 37, false}, // a pixel above that
        {36, 2, false},  // well above it
        {36, 103, true}, // standing on the floor at its foot
    };
    for (u32 k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        reset();
        LOAD(ladder_room);
        u32 i = make_body(cases[k].x, cases[k].y, 10, 10);
        step(1); // no gravity, no velocity: it stays
        CHECK(((body_contact[i] & MAP_CONTACT_LADDER) != 0) == cases[k].ladder);
    }
    reset();
}

// The top of a ladder is a floor to a body falling onto it with gravity,
// like a one-way platform; a body without gravity, or whose gravity points
// up, passes; a body already inside it falls down the ladder.
static void the_top_of_a_ladder_is_a_floor(void) {
    reset();
    LOAD(ladder_room);
    physics_set_gravity(0, FX_ONE / 4);
    u32 falls = make_body(36, 0, 8, 8);
    u32 still = make_body(36, 0, 8, 8);
    body_gravity[still] = BODY_GRAVITY(0);
    vel_y[still] = FX(2);
    u32 up = make_body(36, 0, 8, 8);
    body_gravity[up] = BODY_GRAVITY(-16); // its gravity pulls up
    vel_y[up] = FX(5);
    u32 inside = make_body(36, 50, 8, 8); // below the top's top edge
    u32 edge = make_body(26, 0, 10, 8);   // x 26-35: only its right side over it
    step(80);
    CHECK(pos_y[edge] == FX(48 - 8)); // held up by its side, as on a ledge
    CHECK(pos_y[falls] == FX(48 - 8));
    CHECK(body_contact[falls] == (MAP_CONTACT_FLOOR | MAP_CONTACT_LADDER));
    CHECK(pos_y[still] > FX(48));
    CHECK(pos_y[up] > FX(48) || pos_y[up] < 0); // went through it, either way
    CHECK(pos_y[inside] == FX(112 - 8));
    CHECK(body_contact[inside] == (MAP_CONTACT_FLOOR | MAP_CONTACT_LADDER));
    // Standing on it, it walks onto the ledge beside it.
    vel_x[falls] = FX(2);
    step(10);
    CHECK(pos_y[falls] == FX(48 - 8) && fx_to_int(pos_x[falls]) > 48);
    reset();
}

// Climbing as a game does: up the ladder with no gravity while it touches
// it, gravity back once it doesn't, standing on the top; then pressing down
// from the top, through it, back to the floor.
static void bodies_climb_ladders(void) {
    reset();
    LOAD(ladder_room);
    physics_set_gravity(0, FX_ONE / 4);
    u32 i = make_body(35, 112 - 14, 10, 14);
    step(2);
    CHECK(body_contact[i] == (MAP_CONTACT_FLOOR | MAP_CONTACT_LADDER));
    body_gravity[i] = BODY_GRAVITY(0);
    int frames = 0;
    while ((body_contact[i] & MAP_CONTACT_LADDER) && frames++ < 200) {
        vel_y[i] = -FX(1);
        step(1);
        if (pos_y[i] + FX(14) < FX(48))
            break;
    }
    // Over the top: gravity again, it lands on it.
    body_gravity[i] = 0;
    vel_y[i] = 0;
    step(10);
    CHECK(pos_y[i] == FX(48 - 14));
    CHECK(body_contact[i] == (MAP_CONTACT_FLOOR | MAP_CONTACT_LADDER));
    // Down through the top: no gravity, moving down, until it reaches the
    // floor.
    body_gravity[i] = BODY_GRAVITY(0);
    for (frames = 0; frames < 200; frames++) {
        vel_y[i] = FX(1);
        step(1);
        CHECK(body_contact[i] & MAP_CONTACT_LADDER);
        if (body_contact[i] & MAP_CONTACT_FLOOR)
            break;
    }
    CHECK(frames == 112 - 48);
    CHECK(pos_y[i] == FX(112 - 14));
    reset();
}

// Ladders beside solid metatiles: walls still stop bodies, and a ladder up
// through a hole in a ceiling lets a narrow body through, not a wide one.
static void ladders_beside_solid_metatiles(void) {
    reset();
    static const char* const shaft[] = {
        "........", //
        "........", //
        "###H####", // the ceiling, with the ladder through it
        "...H....", //
        "...H....", //
        "########", //
    };
    LOAD(shaft);
    u32 narrow = make_body(52, 80 - 12, 8, 12);
    u32 wide = make_body(46, 80 - 12, 20, 12);
    body_gravity[narrow] = body_gravity[wide] = BODY_GRAVITY(0);
    u8 seen = 0;
    for (int f = 0; f < 60; f++) {
        vel_y[narrow] = vel_y[wide] = -FX(1);
        step(1);
        seen |= body_contact[wide];
    }
    CHECK(pos_y[narrow] == FX(80 - 12 - 60));
    CHECK(pos_y[wide] == FX(48) && seen == (MAP_CONTACT_CEILING | MAP_CONTACT_LADDER));
    // In the hole, the ceiling's sides are walls.
    pos_y[narrow] = FX(36);
    vel_y[narrow] = 0;
    seen = 0;
    for (int f = 0; f < 10; f++) {
        vel_x[narrow] = FX(2);
        step(1);
        seen |= body_contact[narrow];
    }
    CHECK(pos_x[narrow] == FX(64 - 8) && seen == (MAP_CONTACT_RIGHT | MAP_CONTACT_LADDER));
    reset();
}

// --- Scripts -----------------------------------------------------------------

// A Lua script reads MAP_CONTACT_LADDER from body_contact by its C name and
// climbs (tests/ladder_climber.lua, built by serval_add_script()).
static void scripts_climb_with_map_contact_ladder(void) {
    reset();
    LOAD(ladder_room);
    physics_set_gravity(0, FX_ONE / 4);
    CHECK(vm_load(ladder_climber_script, ladder_climber_script_size));
    Entity e = entity_create(C_POS | C_VEL | C_BODY | C_MAPBODY);
    u32 i = entity_index(e);
    pos_x[i] = FX(35);
    pos_y[i] = FX(112 - 14);
    vm_attach(e, OBJ_CLIMBER);
    for (int f = 0; f < 120; f++) {
        vm_step();
        sys_map_movement();
        vm_events();
    }
    CHECK(vm_global(G_TOUCHED) == 1);
    CHECK(vm_global(G_CLIMBED) == 112 - 48 + 1); // up to a pixel over the top
    CHECK(vm_global(G_STATE) == 2);
    CHECK(pos_y[i] == FX(48 - 14)); // on the top
    CHECK(body_contact[i] == (MAP_CONTACT_FLOOR | MAP_CONTACT_LADDER));
    vm_unload();
    reset();
}

TEST_SUITE(
    map_slope_tests, "map slopes and ladders",
    {"slopes are solid below their line", slopes_are_solid_below_their_line},
    {"bodies walk up and down slopes", bodies_walk_up_and_down_slopes},
    {"bodies stick to slopes only when walking", bodies_stick_to_slopes_only_when_walking},
    {"bodies without gravity slide up slopes", bodies_without_gravity_slide_up_slopes},
    {"bodies land on slopes from a jump", bodies_land_on_slopes_from_a_jump},
    {"exposed slope sides are walls", exposed_slope_sides_are_walls},
    {"slopes are ceilings from below", slopes_are_ceilings_from_below},
    {"bodies walk off a ramp without launching", bodies_walk_off_a_ramp_without_launching},
    {"slopes join one-way platforms and walls", slopes_join_one_way_platforms_and_walls},
    {"wide bodies land on the floor beside a hill", wide_bodies_land_on_the_floor_beside_a_hill},
    {"bodies move out of slopes changed under them", bodies_move_out_of_slopes_changed_under_them},
    {"bodies bounce on slopes", bodies_bounce_on_slopes},
    {"ladders load and are not solid", ladders_load_and_are_not_solid},
    {"ladder contact is overlap or standing on one", ladder_contact_is_overlap_or_standing_on_one},
    {"the top of a ladder is a floor", the_top_of_a_ladder_is_a_floor},
    {"bodies climb ladders", bodies_climb_ladders},
    {"ladders beside solid metatiles", ladders_beside_solid_metatiles},
    {"scripts climb with MAP_CONTACT_LADDER", scripts_climb_with_map_contact_ladder}, );
