#include "serval/ecs.h"

#include "../core/warn.h"

u32 ent_mask[MAX_ENT];
FIXED pos_x[MAX_ENT], pos_y[MAX_ENT];
FIXED vel_x[MAX_ENT], vel_y[MAX_ENT];
u16 spr_id[MAX_ENT];
u8 spr_frame[MAX_ENT];

static u8 ent_gen[MAX_ENT];

// Stack of free slot indices; the top is used next.
static u8 free_slots[MAX_ENT];
static u32 free_count;

#ifdef SERVAL_DEBUG
static bool warned_full;
#endif

static u8 next_generation(u8 gen) {
    gen++;
    return gen ? gen : 1; // 0 is reserved so no handle equals ENTITY_NONE
}

static Entity make_handle(u32 index) {
    return (Entity)((u32)ent_gen[index] << 8 | index);
}

void ecs_reset(void) {
#ifdef SERVAL_DEBUG
    warned_full = false;
#endif
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
    if (free_count == 0) {
#ifdef SERVAL_DEBUG
        if (!warned_full) {
            warned_full = true;
            SERVAL_WARN("entity_create: all %u entities are in use; returning ENTITY_NONE",
                        MAX_ENT);
        }
#endif
        return ENTITY_NONE;
    }
    u32 index = free_slots[--free_count];
    ent_mask[index] = components | C_ALIVE;
    pos_x[index] = pos_y[index] = 0;
    vel_x[index] = vel_y[index] = 0;
    spr_id[index] = 0;
    spr_frame[index] = 0;
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

// Runs as ARM code from IWRAM on the GBA (SERVAL_IWRAM_CODE): it touches every
// entity every frame.
SERVAL_IWRAM_CODE void sys_movement(void) {
    for (u32 i = 0; i < MAX_ENT; i++) {
        if ((ent_mask[i] & (C_POS | C_VEL)) == (C_POS | C_VEL)) {
            pos_x[i] += vel_x[i];
            pos_y[i] += vel_y[i];
        }
    }
}
