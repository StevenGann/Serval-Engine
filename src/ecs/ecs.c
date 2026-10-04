#include "serval/ecs.h"

u32 ent_mask[MAX_ENT];

static u8 ent_gen[MAX_ENT];

// Stack of free slot indices; the top is used next.
static u8 free_slots[MAX_ENT];
static u32 free_count;

static u8 next_generation(u8 gen) {
    gen++;
    return gen ? gen : 1; // 0 is reserved so no handle equals ENTITY_NONE
}

static Entity make_handle(u32 index) {
    return (Entity)((u32)ent_gen[index] << 8 | index);
}

void ecs_reset(void) {
    free_count = 0;
    // Push in reverse so slots are handed out in ascending order.
    for (u32 i = MAX_ENT; i-- > 0;) {
        if (ent_mask[i] & C_ALIVE)
            ent_gen[i] = next_generation(ent_gen[i]);
        else if (ent_gen[i] == 0)
            ent_gen[i] = 1;
        ent_mask[i] = 0;
        free_slots[free_count++] = (u8)i;
    }
}

Entity entity_create(u32 components) {
    if (free_count == 0)
        return ENTITY_NONE;
    u32 index = free_slots[--free_count];
    ent_mask[index] = components | C_ALIVE;
    return make_handle(index);
}

bool entity_alive(Entity e) {
    u32 index = entity_index(e);
    return index < MAX_ENT && (ent_mask[index] & C_ALIVE) && ent_gen[index] == entity_generation(e);
}

void entity_destroy(Entity e) {
    if (!entity_alive(e))
        return;
    u32 index = entity_index(e);
    ent_mask[index] = 0;
    ent_gen[index] = next_generation(ent_gen[index]);
    free_slots[free_count++] = (u8)index;
}
