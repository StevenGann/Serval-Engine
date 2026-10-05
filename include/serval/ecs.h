#ifndef SERVAL_ECS_H
#define SERVAL_ECS_H

// Fixed-pool bitmask ECS. See docs/ecs.md.
//
// Entities are slots in struct-of-arrays component pools. A system loops over
// the pools and processes entities whose mask contains the components it needs.

#include "serval/fixed.h"
#include "serval/platform.h"

#define MAX_ENT 128

// Component bits. Bits 0-15 belong to the engine, bits 16-30 to games
// (C_GAME(0) to C_GAME(14)). Bit 31 marks a slot as alive, so free slots never
// match any system.
#define C_POS (1u << 0)  // pos_x, pos_y
#define C_VEL (1u << 1)  // vel_x, vel_y
#define C_SPR (1u << 2)  // spr_id, spr_frame, spr_flags, spr_depth
#define C_BODY (1u << 3) // body_w, body_h, body_bounce, body_friction (physics.h)
#define C_GAME(n) (1u << (16 + (n)))
#define C_ALIVE (1u << 31)

// Generational handle: low 8 bits are the slot index, high 8 bits the slot's
// generation. A handle goes stale when its entity is destroyed.
typedef u16 Entity;

// Never refers to an entity (generations start at 1).
#define ENTITY_NONE ((Entity)0)

// Component mask per slot, including C_ALIVE. Systems read this directly.
extern u32 ent_mask[MAX_ENT];

// Engine component pools, indexed by entity_index(). Zeroed by entity_create().
extern FIXED pos_x[MAX_ENT], pos_y[MAX_ENT]; // C_POS: top-left position in pixels
extern FIXED vel_x[MAX_ENT], vel_y[MAX_ENT]; // C_VEL: pixels per frame
extern u16 spr_id[MAX_ENT];                  // C_SPR: sprite ID (sprites.h)
extern u8 spr_frame[MAX_ENT];                // C_SPR: animation frame
extern u16 spr_flags[MAX_ENT];               // C_SPR: sprite_draw flags (flip, layer)
extern s16 spr_depth[MAX_ENT]; // C_SPR: sys_render_by_depth draws higher depths in front
extern u16 spr_angle[MAX_ENT]; // C_SPR: rotation (sprite_draw_rotated); 0 = unrotated

// True if entity slot i has every component in `mask` (and is alive, when
// `mask` is not 0). Prefer this to testing ent_mask by hand: `ent_mask[i] &
// (A | B)` is true when *either* component is present.
static inline bool ent_has(u32 i, u32 mask) {
    // Free slots have a mask of 0, so they can only match an empty mask; with a
    // constant mask this compiles down to a single test.
    if (mask == 0)
        return (ent_mask[i] & C_ALIVE) != 0;
    return (ent_mask[i] & mask) == mask;
}

// Loops over the slot index `i` of every live entity that has all components
// in `mask`:
//
//     ECS_FOR_EACH(i, C_POS | C_BUNNY) {
//         pos_x[i] += FX(1);
//     }
#define ECS_FOR_EACH(i, mask)                                                                      \
    for (u32 i = 0; i < MAX_ENT; i++)                                                              \
        if (!ent_has(i, (mask))) {                                                                 \
        } else

// Destroys every entity, e.g. on room change. Outstanding handles go stale.
void ecs_reset(void);

// Creates an entity with the given component bits. Returns ENTITY_NONE if the
// pool is full.
Entity entity_create(u32 components);

// Destroys the entity. Does nothing if the handle is stale or ENTITY_NONE.
void entity_destroy(Entity e);

// True if the handle refers to a live entity.
bool entity_alive(Entity e);

// The handle of the live entity in slot `index` (as from ECS_FOR_EACH), or
// ENTITY_NONE if the slot is free. Use it to destroy or keep the entity a
// system is looking at: ECS_FOR_EACH(i, C_ROCK) entity_destroy(entity_at(i));
Entity entity_at(u32 index);

// Systems, run once per frame by the game.
// sys_movement: position += velocity for entities with C_POS and C_VEL.
void sys_movement(void);
// sys_render: draws entities with C_POS and C_SPR using sprite_draw(), with
// their spr_flags. Among sprites on the same layer, lower entity indices are in
// front.
void sys_render(void);
// sys_render_by_depth: like sys_render, but sprites with a higher spr_depth
// are drawn in front of lower ones (equal depths: lower index in front). For
// a top-down look, set spr_depth to the entity's y each frame so sprites lower
// on screen overlap those above. Costs more than sys_render (about 10,000
// cycles for 128 sprites); use it only when draw order matters.
void sys_render_by_depth(void);

static inline u8 entity_index(Entity e) {
    return (u8)(e & 0xFF);
}
static inline u8 entity_generation(Entity e) {
    return (u8)(e >> 8);
}

#endif // SERVAL_ECS_H
