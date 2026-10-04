#include "serval/ecs.h"
#include "test.h"

static void create_sets_mask(void) {
    ecs_reset();
    Entity e = entity_create(C_POS | C_VEL);
    CHECK(e != ENTITY_NONE);
    CHECK(entity_alive(e));
    CHECK(ent_mask[entity_index(e)] == (C_POS | C_VEL | C_ALIVE));
}

static void slots_are_handed_out_in_order(void) {
    ecs_reset();
    CHECK(entity_index(entity_create(0)) == 0);
    CHECK(entity_index(entity_create(0)) == 1);
}

static void destroy_makes_handle_stale(void) {
    ecs_reset();
    Entity e = entity_create(C_POS);
    entity_destroy(e);
    CHECK(!entity_alive(e));
    CHECK(ent_mask[entity_index(e)] == 0);
}

static void reused_slot_gets_new_generation(void) {
    ecs_reset();
    Entity old = entity_create(C_POS);
    entity_destroy(old);
    Entity reused = entity_create(C_VEL);
    CHECK(entity_index(reused) == entity_index(old));
    CHECK(entity_generation(reused) != entity_generation(old));
    CHECK(!entity_alive(old));
    CHECK(entity_alive(reused));

    entity_destroy(old); // stale handle must not destroy the new entity
    CHECK(entity_alive(reused));
}

static void pool_full_returns_none(void) {
    ecs_reset();
    Entity first = ENTITY_NONE;
    for (unsigned i = 0; i < MAX_ENT; i++) {
        Entity e = entity_create(0);
        CHECK(e != ENTITY_NONE);
        if (i == 0)
            first = e;
    }
    CHECK(entity_create(0) == ENTITY_NONE);

    entity_destroy(first);
    CHECK(entity_create(0) != ENTITY_NONE);
}

static void reset_invalidates_all_handles(void) {
    ecs_reset();
    Entity e = entity_create(C_POS);
    ecs_reset();
    CHECK(!entity_alive(e));
    CHECK(entity_alive(entity_create(C_POS)));
}

static void generation_wraps_without_reaching_none(void) {
    ecs_reset();
    for (unsigned i = 0; i < 600; i++) {
        Entity e = entity_create(0);
        CHECK(e != ENTITY_NONE);
        CHECK(entity_generation(e) != 0);
        entity_destroy(e);
    }
}

static void invalid_handles_are_not_alive(void) {
    ecs_reset();
    CHECK(!entity_alive(ENTITY_NONE));
    CHECK(!entity_alive((Entity)(1u << 8 | 200u))); // index out of range
    entity_destroy(ENTITY_NONE);                    // no-op
}

static void create_zeroes_components(void) {
    ecs_reset();
    Entity e = entity_create(C_POS | C_VEL);
    u32 i = entity_index(e);
    pos_x[i] = FX(5);
    vel_y[i] = FX(2);
    spr_id[i] = 9;
    entity_destroy(e);
    Entity again = entity_create(C_POS);
    CHECK(entity_index(again) == i);
    CHECK(pos_x[i] == 0 && vel_y[i] == 0 && spr_id[i] == 0);
}

static void movement_adds_velocity_to_position(void) {
    ecs_reset();
    Entity moving = entity_create(C_POS | C_VEL);
    Entity still = entity_create(C_POS); // no velocity component
    u32 m = entity_index(moving), s = entity_index(still);
    pos_x[m] = FX(10);
    pos_y[m] = FX(20);
    vel_x[m] = FX(1) / 2; // half a pixel per frame
    vel_y[m] = -FX(3);
    vel_x[s] = FX(7); // ignored: still has no C_VEL
    sys_movement();
    sys_movement();
    CHECK(pos_x[m] == FX(11) && pos_y[m] == FX(14));
    CHECK(pos_x[s] == 0);
    CHECK(fx_to_int(-FX(1) / 2) == -1); // rounds toward negative infinity
}

TEST_SUITE(ecs_tests, "ecs", {"create_sets_mask", create_sets_mask},
           {"slots_are_handed_out_in_order", slots_are_handed_out_in_order},
           {"destroy_makes_handle_stale", destroy_makes_handle_stale},
           {"reused_slot_gets_new_generation", reused_slot_gets_new_generation},
           {"pool_full_returns_none", pool_full_returns_none},
           {"reset_invalidates_all_handles", reset_invalidates_all_handles},
           {"generation_wraps_without_reaching_none", generation_wraps_without_reaching_none},
           {"invalid_handles_are_not_alive", invalid_handles_are_not_alive},
           {"create_zeroes_components", create_zeroes_components},
           {"movement_adds_velocity_to_position", movement_adds_velocity_to_position});
