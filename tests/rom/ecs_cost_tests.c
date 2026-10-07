// Costs of the ECS queries and of sys_physics' loops on the GBA, measured in
// CPU cycles with the cascaded timers serval_init() starts, and logged. This
// file is Thumb code in ROM, like a game's code.

#include "../test.h"
#include "serval/debug.h"
#include "serval/ecs.h"
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

#define C_MATCH C_GAME(0)

static bool in_iwram(u32 address) {
    return (address & 0xFF000000) == 0x03000000;
}

// A full pool with 10 matching entities spread over it: the case where a
// full scan costs the most for what it finds.
static void fill_with_ten_matches(void) {
    ecs_reset();
    for (u32 i = 0; i < MAX_ENT; i++)
        entity_create(i % 13 == 5 ? C_POS | C_MATCH : C_POS);
}

static void count_and_gather_are_cheap(void) {
    fill_with_ten_matches();
    CHECK(in_iwram((u32)&ecs_count) && in_iwram((u32)&ecs_gather));

    u32 t0 = cycles();
    u32 counted = ecs_count(C_MATCH);
    u32 t1 = cycles();
    static u8 list[MAX_ENT];
    u32 gathered = ecs_gather(C_MATCH, list);
    u32 t2 = cycles();
    u32 looped = 0;
    ECS_FOR_EACH(i, C_MATCH) {
        looped++;
    }
    u32 t3 = cycles();
    CHECK(counted == 10 && gathered == 10 && looped == 10);
    CHECK(list[0] == 5 && list[9] == 122);
    debug_log(text_format("ecs: 128 slots, 10 matches: ecs_count %u cycles, ecs_gather %u, "
                          "ECS_FOR_EACH from ROM %u",
                          t1 - t0, t2 - t1, t3 - t2));
    // About 870 and 1,000 (with the call and timer reads); in ROM, ecs_count
    // took about 2,900.
    CHECK_TIMING(t1 - t0 < 1200);
    CHECK_TIMING(t2 - t1 < 1200);
    CHECK_TIMING((t1 - t0) * 3 < t3 - t2 && (t2 - t1) * 3 < t3 - t2);
}

// 32 bodies bouncing around a 100x100 box, half of them on the floor.
#define BODIES 32

static void make_bodies(void) {
    ecs_reset();
    physics_set_bounds(0, 0, 100, 100);
    physics_set_open_edges(0);
    physics_set_wrap(false, false);
    physics_set_contacts(false);
    physics_set_gravity(0, FX_ONE / 8);
    for (u32 k = 0; k < BODIES; k++) {
        u32 i = entity_index(entity_create(C_POS | C_VEL | C_BODY));
        pos_x[i] = FX((int)(k * 3));
        pos_y[i] = k & 1 ? FX(90) : FX((int)k);
        vel_x[i] = FX(1) + (FIXED)k;
        vel_y[i] = k & 1 ? 0 : -FX(1);
        body_w[i] = body_h[i] = 10;
        body_bounce[i] = 200;
    }
}

// The cheapest of a few frames, so an interrupt doesn't count.
static u32 physics_cycles(void) {
    u32 best = 0xFFFFFFFF;
    for (int frame = 0; frame < 4; frame++) {
        sys_movement();
        u32 t0 = cycles();
        sys_physics();
        u32 t = cycles() - t0;
        best = t < best ? t : best;
    }
    return best;
}

static void physics_loop_costs(void) {
    make_bodies();
    u32 fast = physics_cycles();
    make_bodies();
    physics_set_contacts(true);
    u32 contacts = physics_cycles();
    make_bodies();
    physics_set_wrap(true, false);
    u32 wrapping = physics_cycles();
    make_bodies();
    for (u32 i = 0; i < 4; i++)
        body_gravity[i] = BODY_GRAVITY(0);
    u32 scaled = physics_cycles();
    debug_log(text_format("physics: %d bodies: fast loop %u cycles, contacts on %u, wrapping x %u, "
                          "4 with body_gravity %u",
                          BODIES, fast, contacts, wrapping, scaled));
    // Release build: about 6,500 for the fast loop, 9,400 with contacts, 9,700
    // wrapping and 10,800 with 4 scaled bodies (handled in ROM); a little
    // more with debug checks.
    CHECK_TIMING(fast < 10000);
    CHECK_TIMING(contacts < fast + BODIES * 100);
    CHECK_TIMING(scaled < wrapping + 4 * 600);
    physics_set_contacts(false);
    physics_set_wrap(false, false);
    physics_set_gravity(0, 0);
    physics_set_bounds(0, 0, 240, 160);
    ecs_reset();
}

TEST_SUITE(gba_ecs_cost_tests, "gba_ecs_cost",
           {"count_and_gather_are_cheap", count_and_gather_are_cheap},
           {"physics_loop_costs", physics_loop_costs});
