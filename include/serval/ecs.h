#ifndef SERVAL_ECS_H
#define SERVAL_ECS_H

// Fixed-pool bitmask ECS. See docs/ecs.md.
//
// Entities are slots in struct-of-arrays component pools. A system loops over
// the pools and processes entities whose mask contains the components it needs.

#include "serval/platform.h"

#define MAX_ENT 128

// Component bits are assigned by engine modules and games. Bit 31 is reserved:
// it marks a slot as alive, so free slots never match any system.
#define C_ALIVE (1u << 31)

// Generational handle: low 8 bits are the slot index, high 8 bits the slot's
// generation. A handle goes stale when its entity is destroyed.
typedef u16 Entity;

// Never refers to an entity (generations start at 1).
#define ENTITY_NONE ((Entity)0)

// Component mask per slot, including C_ALIVE. Systems read this directly.
extern u32 ent_mask[MAX_ENT];

// Destroys every entity, e.g. on room change. Outstanding handles go stale.
void ecs_reset(void);

// Creates an entity with the given component bits. Returns ENTITY_NONE if the
// pool is full.
Entity entity_create(u32 components);

// Destroys the entity. Does nothing if the handle is stale or ENTITY_NONE.
void entity_destroy(Entity e);

// True if the handle refers to a live entity.
bool entity_alive(Entity e);

static inline u8 entity_index(Entity e) {
    return (u8)(e & 0xFF);
}
static inline u8 entity_generation(Entity e) {
    return (u8)(e >> 8);
}

#endif // SERVAL_ECS_H
