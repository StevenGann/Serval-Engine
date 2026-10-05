#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/physics.h"
#include "serval/sprites.h"
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

// Fills the pool, so the next destroyed slot is the one reused.
static void fill_pool(void) {
    while (entity_create(0) != ENTITY_NONE) {
    }
}

static void reused_slot_gets_new_generation(void) {
    ecs_reset();
    Entity old = entity_create(C_POS);
    fill_pool();
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
    u32 warnings = debug_warning_count();
    CHECK(entity_create(0) == ENTITY_NONE);
    CHECK(entity_create(0) == ENTITY_NONE);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1); // reported once
#else
    CHECK(debug_warning_count() == warnings);
#endif

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
    for (unsigned i = 0; i < 300 * MAX_ENT; i++) { // every slot wraps at least once
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
    spr_flags[i] = SPRITE_FLIP_H;
    spr_depth[i] = 7;
    body_w[i] = 16;
    body_bounce[i] = 200;
    fill_pool();
    entity_destroy(e);
    Entity again = entity_create(C_POS);
    CHECK(entity_index(again) == i);
    CHECK(pos_x[i] == 0 && vel_y[i] == 0 && spr_id[i] == 0);
    CHECK(spr_flags[i] == 0 && spr_depth[i] == 0 && body_w[i] == 0 && body_bounce[i] == 0);
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

static void ent_has_requires_every_component(void) {
    ecs_reset();
    Entity e = entity_create(C_POS);
    u32 i = entity_index(e);
    CHECK(ent_has(i, C_POS));
    CHECK(!ent_has(i, C_POS | C_VEL)); // not just either
    CHECK(ent_has(i, 0));              // alive
    entity_destroy(e);
    CHECK(!ent_has(i, 0)); // free slots never match
}

static void for_each_visits_matching_entities(void) {
    ecs_reset();
    Entity a = entity_create(C_POS | C_VEL);
    entity_create(C_POS);
    Entity c = entity_create(C_POS | C_VEL | C_SPR);
    u32 visited = 0, count = 0;
    ECS_FOR_EACH(i, C_POS | C_VEL) {
        visited |= 1u << i;
        count++;
    }
    CHECK(count == 2);
    CHECK(visited == ((1u << entity_index(a)) | (1u << entity_index(c))));

    // Usable as the body of an if without swallowing a following else.
    bool else_taken = false;
    if (count == 0)
        ECS_FOR_EACH(i, C_POS) {
            (void)i;
        }
    else
        else_taken = true;
    CHECK(else_taken);
}

static void entity_at_returns_the_live_handle(void) {
    ecs_reset();
    Entity e = entity_create(C_POS);
    u32 i = entity_index(e);
    CHECK(entity_at(i) == e);
    entity_destroy(entity_at(i)); // destroy from a system's loop index
    CHECK(!entity_alive(e));
    CHECK(entity_at(i) == ENTITY_NONE);
    CHECK(entity_at(MAX_ENT) == ENTITY_NONE);
}

static void destroy_all_then_recreate_all(void) {
    ecs_reset();
    Entity handles[MAX_ENT];
    for (u32 i = 0; i < MAX_ENT; i++)
        handles[i] = entity_create(C_POS);
    for (u32 i = 0; i < MAX_ENT; i++)
        entity_destroy(handles[i]);
    u32 seen[MAX_ENT / 32] = {0};
    for (u32 i = 0; i < MAX_ENT; i++) {
        Entity e = entity_create(C_POS);
        CHECK(e != ENTITY_NONE && !entity_alive(handles[i]));
        u32 index = entity_index(e);
        CHECK(!(seen[index / 32] & (1u << (index % 32)))); // every slot exactly once
        seen[index / 32] |= 1u << (index % 32);
    }
    CHECK(entity_create(C_POS) == ENTITY_NONE);
}

// A game that sets C_ALIVE on a free slot by hand must not get that slot
// freed twice (two entities would then share it).
static void forged_alive_bit_does_not_free_twice(void) {
    ecs_reset();
    Entity e = entity_create(C_POS);
    u32 i = entity_index(e);
    entity_destroy(e);
    ent_mask[i] = C_POS | C_ALIVE;
    entity_destroy(entity_at(i));
    entity_destroy(entity_at(i));
    ent_mask[i] = 0;
    u32 seen[MAX_ENT / 32] = {0};
    for (u32 n = 0; n < MAX_ENT; n++) {
        Entity created = entity_create(0);
        CHECK(created != ENTITY_NONE);
        u32 index = entity_index(created);
        CHECK(!(seen[index / 32] & (1u << (index % 32))));
        seen[index / 32] |= 1u << (index % 32);
    }
    CHECK(entity_create(0) == ENTITY_NONE);
}

// A game that overwrites ent_mask without C_ALIVE: the entity matches nothing,
// can still be destroyed, and ecs_reset makes its handle stale.
static void cleared_alive_bit(void) {
    ecs_reset();
    Entity e = entity_create(C_POS);
    u32 i = entity_index(e);
    ent_mask[i] = C_POS; // C_ALIVE lost
    CHECK(!ent_has(i, C_POS) && !ent_has(i, 0));
    u32 count = 0;
    ECS_FOR_EACH(j, C_POS) {
        (void)j;
        count++;
    }
    CHECK(count == 0);
    ecs_reset();
    Entity again = entity_create(C_POS);
    CHECK(entity_index(again) == i);
    CHECK(again != e && !entity_alive(e));

    ent_mask[i] = C_POS;
    u32 warnings = debug_warning_count();
    entity_destroy(again); // frees the slot instead of leaking it
    entity_destroy(again);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1);
#else
    CHECK(debug_warning_count() == warnings);
#endif
    for (u32 n = 0; n < MAX_ENT; n++)
        CHECK(entity_create(0) != ENTITY_NONE);
}

// Freed slots are reused oldest-first, so a stale handle stays stale for
// long churn instead of matching again after its slot's 255 generations.
static void freed_slots_are_reused_oldest_first(void) {
    ecs_reset();
    Entity a = entity_create(0);
    entity_destroy(a);
    CHECK(entity_index(entity_create(0)) != entity_index(a));
    ecs_reset();
    Entity stale = entity_create(0);
    entity_destroy(stale);
    bool aliased = false;
    for (u32 n = 0; n < 1000; n++) {
        entity_destroy(entity_create(0));
        aliased |= entity_alive(stale);
    }
    CHECK(!aliased);
}

static void alive_bit_in_create_mask_warns(void) {
    ecs_reset();
    u32 warnings = debug_warning_count();
    entity_create(C_ALIVE);
    entity_create(C_ALIVE);
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1);
#else
    CHECK(debug_warning_count() == warnings);
#endif
    CHECK(C_GAME(14) == (1u << 30));
}

static void count_matches_live_entities(void) {
    ecs_reset();
    CHECK(ecs_count(0) == 0);
    Entity a = entity_create(C_POS);
    entity_create(C_POS | C_VEL);
    entity_create(C_VEL);
    CHECK(ecs_count(0) == 3);
    CHECK(ecs_count(C_POS) == 2);
    CHECK(ecs_count(C_POS | C_VEL) == 1);
    CHECK(ecs_count(C_GAME(0)) == 0);
    entity_destroy(a);
    CHECK(ecs_count(C_POS) == 1);
    CHECK(ecs_count(0) == 2);
    ecs_reset();
}

TEST_SUITE(ecs_tests, "ecs", {"create_sets_mask", create_sets_mask},
           {"count_matches_live_entities", count_matches_live_entities},
           {"slots_are_handed_out_in_order", slots_are_handed_out_in_order},
           {"destroy_makes_handle_stale", destroy_makes_handle_stale},
           {"reused_slot_gets_new_generation", reused_slot_gets_new_generation},
           {"pool_full_returns_none", pool_full_returns_none},
           {"reset_invalidates_all_handles", reset_invalidates_all_handles},
           {"generation_wraps_without_reaching_none", generation_wraps_without_reaching_none},
           {"invalid_handles_are_not_alive", invalid_handles_are_not_alive},
           {"create_zeroes_components", create_zeroes_components},
           {"movement_adds_velocity_to_position", movement_adds_velocity_to_position},
           {"ent_has_requires_every_component", ent_has_requires_every_component},
           {"for_each_visits_matching_entities", for_each_visits_matching_entities},
           {"entity_at_returns_the_live_handle", entity_at_returns_the_live_handle},
           {"destroy_all_then_recreate_all", destroy_all_then_recreate_all},
           {"forged_alive_bit_does_not_free_twice", forged_alive_bit_does_not_free_twice},
           {"cleared_alive_bit", cleared_alive_bit},
           {"freed_slots_are_reused_oldest_first", freed_slots_are_reused_oldest_first},
           {"alive_bit_in_create_mask_warns", alive_bit_in_create_mask_warns});
