#include "serval/ecs.h"
#include "test.h"

enum { C_POS = 1 << 0, C_VEL = 1 << 1 };

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

TEST_SUITE(ecs_tests, "ecs", {"create_sets_mask", create_sets_mask},
           {"slots_are_handed_out_in_order", slots_are_handed_out_in_order},
           {"destroy_makes_handle_stale", destroy_makes_handle_stale},
           {"reused_slot_gets_new_generation", reused_slot_gets_new_generation},
           {"pool_full_returns_none", pool_full_returns_none},
           {"reset_invalidates_all_handles", reset_invalidates_all_handles},
           {"generation_wraps_without_reaching_none", generation_wraps_without_reaching_none},
           {"invalid_handles_are_not_alive", invalid_handles_are_not_alive});
