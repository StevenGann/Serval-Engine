// ladders and slopes: what sys_map_movement() costs per map body on the GBA,
// in CPU cycles (the cascaded timers serval_init() starts), logged: bodies
// walking on flat ground (on a map without slopes or ladders, and on one
// with them), walking up and down 45-degree and 1:2 slopes, and climbing
// ladders. This file is Thumb code in ROM, like a game's code.

#include "../test.h"
#include "serval/debug.h"
#include "serval/map.h"
#include "serval/physics.h"
#include "serval/text.h"

#include <tonc.h>

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

// Cycle budgets only hold for optimized code: Debug builds (-O0) check
// correctness but not timing.
#ifdef __OPTIMIZE__
#define CHECK_TIMING(cond) CHECK(cond)
#else
#define CHECK_TIMING(cond) ((void)(cond))
#endif

enum { EMPTY, SOLID, SLOPE_R, SLOPE_L, R_LOW, R_HIGH, LADDER, METATILES };

static const Metatile flat_metatiles[] = {
    [EMPTY] = {{0, 0, 0, 0}, MAP_EMPTY},
    [SOLID] = {{0, 0, 0, 0}, MAP_SOLID},
};

static const Metatile all_metatiles[METATILES] = {
    [EMPTY] = {{0, 0, 0, 0}, MAP_EMPTY},       [SOLID] = {{0, 0, 0, 0}, MAP_SOLID},
    [SLOPE_R] = {{0, 0, 0, 0}, MAP_SLOPE_R},   [SLOPE_L] = {{0, 0, 0, 0}, MAP_SLOPE_L},
    [R_LOW] = {{0, 0, 0, 0}, MAP_SLOPE_R_LOW}, [R_HIGH] = {{0, 0, 0, 0}, MAP_SLOPE_R_HIGH},
    [LADDER] = {{0, 0, 0, 0}, MAP_LADDER},
};

#define MAP_W 48
#define MAP_H 24
static SERVAL_EWRAM_BSS u16 cells[MAP_W * MAP_H];
static MapLayer playfield;

static void load(const Metatile* metatiles, u16 count) {
    playfield = (MapLayer){.width = MAP_W,
                           .height = MAP_H,
                           .cells = cells,
                           .metatiles = metatiles,
                           .metatile_count = count,
                           .bg = 2};
    CHECK(map_load(&playfield));
}

// A floor along the bottom row.
static void build_flat(void) {
    for (u32 k = 0; k < MAP_W * MAP_H; k++)
        cells[k] = k / MAP_W == MAP_H - 1 ? SOLID : EMPTY;
}

// A 45-degree hill rising to the right, one metatile per row, from the
// bottom-left up to the top-right, solid underneath.
static void build_45(void) {
    for (u32 y = 0; y < MAP_H; y++) {
        for (u32 x = 0; x < MAP_W; x++) {
            int surface = MAP_H - 1 - (int)x; // the row of the slope in column x
            cells[y * MAP_W + x] = (int)y == surface ? SLOPE_R : (int)y > surface ? SOLID : EMPTY;
        }
    }
}

// A 1:2 slope rising to the right: _LOW then _HIGH in each row.
static void build_1_2(void) {
    for (u32 y = 0; y < MAP_H; y++) {
        for (u32 x = 0; x < MAP_W; x++) {
            int surface = MAP_H - 1 - (int)x / 2;
            cells[y * MAP_W + x] = (int)y == surface  ? (x % 2 ? R_HIGH : R_LOW)
                                   : (int)y > surface ? SOLID
                                                      : EMPTY;
        }
    }
}

// Ladders up every other column, on a floor.
static void build_ladders(void) {
    for (u32 y = 0; y < MAP_H; y++) {
        for (u32 x = 0; x < MAP_W; x++)
            cells[y * MAP_W + x] = y == MAP_H - 1 ? SOLID : x % 2 ? LADDER : EMPTY;
    }
}

#define BODIES 16

static void reset(void) {
    ecs_reset();
    map_unload(2);
    camera_set(0, 0);
    physics_set_gravity(0, FX_ONE / 4);
}

static u32 make_body(int x, int y) {
    u32 i = entity_index(entity_create(C_POS | C_VEL | C_BODY | C_MAPBODY));
    pos_x[i] = FX(x);
    pos_y[i] = FX(y);
    body_w[i] = 12;
    body_h[i] = 15;
    body_max_fall[i] = FX(4);
    return i;
}

// sys_map_movement()'s cycles per body: the cheapest of a few frames (so an
// interrupt doesn't count), after `settle` frames, every body's horizontal
// velocity set again each frame as a game's controls would.
static u32 per_body(int settle, FIXED vx) {
    for (int f = 0; f < settle; f++)
        sys_map_movement();
    u32 best = 0xFFFFFFFF;
    for (int frame = 0; frame < 4; frame++) {
        ECS_FOR_EACH(i, C_MAPBODY) {
            vel_x[i] = i % 2 ? vx : -vx;
        }
        u32 t0 = cycles();
        sys_map_movement();
        u32 t = cycles() - t0;
        best = t < best ? t : best;
    }
    return best / BODIES;
}

static u32 flat_cost(const Metatile* metatiles, u16 count) {
    reset();
    build_flat();
    load(metatiles, count);
    for (int k = 0; k < BODIES; k++)
        make_body(40 + k * 32, (MAP_H - 1) * 16 - 15);
    return per_body(2, FX_ONE * 3 / 2);
}

// Bodies along a slope, every 20 pixels from x 24 (a little above it: they
// land in the settling frames), walking up and down it.
static u32 slope_cost(void (*build)(void), int rise_per_metatile) {
    reset();
    build();
    load(all_metatiles, METATILES);
    for (int k = 0; k < BODIES; k++) {
        int x = 24 + k * 20;
        int foot = x + 6;
        int surface = MAP_H * 16 - (foot + 1) * rise_per_metatile / 16;
        make_body(x, surface - 15 - 2);
    }
    u32 cost = per_body(8, FX_ONE * 3 / 2);
    ECS_FOR_EACH(i, C_MAPBODY) {
        CHECK(body_contact[i] == MAP_CONTACT_FLOOR); // on the slope, not in it
    }
    return cost;
}

static u32 ladder_cost(void) {
    reset();
    build_ladders();
    load(all_metatiles, METATILES);
    for (int k = 0; k < BODIES; k++) {
        u32 i = make_body(16 + 32 * k + 2, 200);
        body_gravity[i] = BODY_GRAVITY(0);
        vel_y[i] = -FX_ONE;
    }
    return per_body(1, 0);
}

static void map_movement_costs(void) {
    u32 flat = flat_cost(flat_metatiles, 2);
    u32 flat_with_slopes = flat_cost(all_metatiles, METATILES);
    u32 slope_45 = slope_cost(build_45, 16);
    u32 slope_1_2 = slope_cost(build_1_2, 8);
    u32 ladders = ladder_cost();
    debug_log(text_format("map movement per body: flat ground %u cycles, on a map with slopes "
                          "and ladders %u",
                          flat, flat_with_slopes));
    debug_log(text_format("map movement per body: 45-degree slope %u cycles, 1:2 slope %u, "
                          "climbing a ladder %u",
                          slope_45, slope_1_2, ladders));
    // Release build: about 1,480, 2,900, 3,400, 3,100 and 1,600 (the
    // ECS_FOR_EACH over 128 slots is about 640 of each). Before ladders and
    // slopes, flat ground cost 1,554 (this test on 35d361c): no more now.
    CHECK_TIMING(flat < 1554);
    CHECK_TIMING(flat_with_slopes < 3500 && slope_45 < 3500 && slope_1_2 < 3500);
    CHECK_TIMING(ladders < 2000);
    reset();
    physics_set_gravity(0, 0);
}

TEST_SUITE(gba_map_movement_cost_tests, "gba map movement cost",
           {"map movement costs", map_movement_costs});
