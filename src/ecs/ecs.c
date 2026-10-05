#include "serval/ecs.h"
#include "serval/map.h"
#include "serval/physics.h"

#include "../core/warn.h"

u32 ent_mask[MAX_ENT];
FIXED pos_x[MAX_ENT], pos_y[MAX_ENT];
FIXED vel_x[MAX_ENT], vel_y[MAX_ENT];
u16 spr_id[MAX_ENT];
u8 spr_frame[MAX_ENT];
u16 spr_flags[MAX_ENT];
s16 spr_depth[MAX_ENT];
u16 spr_angle[MAX_ENT];
// Only sys_animate reads it, once per frame: EWRAM spares IWRAM.
SERVAL_EWRAM_BSS u8 spr_anim_time[MAX_ENT];

static u8 ent_gen[MAX_ENT];

// Whether each slot holds an entity, kept apart from ent_mask: games write
// ent_mask, and a slot whose C_ALIVE bit was cleared by hand must neither leak
// nor be freed twice.
static bool ent_used[MAX_ENT];

// Free slot indices as a FIFO ring: a destroyed slot goes to the back, so
// reuse is spread over every free slot and a slot's 255 generations last as
// long as possible before an old handle could match again.
static u8 free_slots[MAX_ENT];
static u32 free_head; // index in free_slots of the next slot handed out
static u32 free_count;

#ifdef SERVAL_DEBUG
static bool warned_full, warned_alive_bit, warned_alive_cleared;
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
    warned_full = warned_alive_bit = warned_alive_cleared = false;
#endif
    free_head = 0;
    free_count = 0;
    // Slots are handed out in ascending order after a reset.
    for (u32 i = 0; i < MAX_ENT; i++) {
        if (ent_used[i] || (ent_mask[i] & C_ALIVE))
            ent_gen[i] = next_generation(ent_gen[i]);
        else if (ent_gen[i] == 0)
            ent_gen[i] = 1;
        ent_used[i] = false;
        ent_mask[i] = 0;
        free_slots[free_count++] = (u8)i;
    }
}

Entity entity_create(u32 components) {
#ifdef SERVAL_DEBUG
    if ((components & C_ALIVE) && !warned_alive_bit) {
        warned_alive_bit = true;
        SERVAL_WARN("entity_create: components include C_ALIVE (bit 31); game components are "
                    "C_GAME(0) to C_GAME(14)");
    }
#endif
    if (free_count == 0) {
#ifdef SERVAL_DEBUG
        if (!warned_full) {
            warned_full = true;
            u32 hidden = 0;
            for (u32 i = 0; i < MAX_ENT; i++)
                hidden += !(ent_mask[i] & C_ALIVE);
            if (hidden)
                SERVAL_WARN(
                    "entity_create: all %u entities are in use, %u without C_ALIVE (keep it "
                    "when writing ent_mask); returning ENTITY_NONE",
                    MAX_ENT, hidden);
            else
                SERVAL_WARN("entity_create: all %u entities are in use; returning ENTITY_NONE",
                            MAX_ENT);
        }
#endif
        return ENTITY_NONE;
    }
    u32 index = free_slots[free_head];
    free_head = (free_head + 1) % MAX_ENT;
    free_count--;
    ent_used[index] = true;
    ent_mask[index] = components | C_ALIVE;
    pos_x[index] = pos_y[index] = 0;
    vel_x[index] = vel_y[index] = 0;
    spr_id[index] = 0;
    spr_frame[index] = 0;
    spr_flags[index] = 0;
    spr_depth[index] = 0;
    spr_angle[index] = 0;
    spr_anim_time[index] = 0;
    body_w[index] = body_h[index] = 0;
    body_bounce[index] = body_friction[index] = body_max_fall[index] = 0;
    return make_handle(index);
}

Entity entity_at(u32 index) {
    if (index >= MAX_ENT || !ent_used[index] || !(ent_mask[index] & C_ALIVE))
        return ENTITY_NONE;
    return make_handle(index);
}

bool entity_alive(Entity e) {
    u32 index = entity_index(e);
    return index < MAX_ENT && ent_used[index] && (ent_mask[index] & C_ALIVE) &&
           ent_gen[index] == entity_generation(e);
}

void entity_destroy(Entity e) {
    u32 index = entity_index(e);
    // Checks ent_used rather than C_ALIVE: an entity whose C_ALIVE bit the game
    // cleared is still destroyed (so it doesn't leak), and a free slot whose
    // bit the game set is not freed a second time.
    if (index >= MAX_ENT || !ent_used[index] || ent_gen[index] != entity_generation(e))
        return;
#ifdef SERVAL_DEBUG
    if (!(ent_mask[index] & C_ALIVE) && !warned_alive_cleared) {
        warned_alive_cleared = true;
        SERVAL_WARN("entity_destroy: entity %u had lost its C_ALIVE bit; keep C_ALIVE when "
                    "writing ent_mask",
                    index);
    }
#endif
    ent_used[index] = false;
    ent_mask[index] = 0;
    ent_gen[index] = next_generation(ent_gen[index]);
    if (free_count < MAX_ENT) {
        free_slots[(free_head + free_count) % MAX_ENT] = (u8)index;
        free_count++;
    }
}

// Runs as ARM code from IWRAM on the GBA (SERVAL_IWRAM_CODE): it touches every
// entity every frame. Map bodies (C_MAPBODY) move with sys_map_movement().
SERVAL_IWRAM_CODE void sys_movement(void) {
    for (u32 i = 0; i < MAX_ENT; i++) {
        if ((ent_mask[i] & (C_POS | C_VEL | C_MAPBODY)) == (C_POS | C_VEL)) {
            pos_x[i] += vel_x[i];
            pos_y[i] += vel_y[i];
        }
    }
}
