#ifndef SERVAL_ECS_INTERNAL_H
#define SERVAL_ECS_INTERNAL_H

// Engine-internal: the ECS's slot bookkeeping (ecs.c), and entity_alive() as
// an inline function for the hot paths that ask it about every entity they
// visit, every frame (the VM's scheduler and property ops, vm.c), where a
// call would cost more than the test.

#include "serval/ecs.h"

extern u8 serval_ent_gen[MAX_ENT];    // each slot's generation (a handle's high byte)
extern bool serval_ent_used[MAX_ENT]; // whether the slot holds an entity

// entity_alive(e).
static inline bool serval_entity_alive(Entity e) {
    u32 index = entity_index(e);
    return index < MAX_ENT && serval_ent_used[index] && (ent_mask[index] & C_ALIVE) &&
           serval_ent_gen[index] == entity_generation(e);
}

#endif // SERVAL_ECS_INTERNAL_H
