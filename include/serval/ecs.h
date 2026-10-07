#ifndef SERVAL_ECS_H
#define SERVAL_ECS_H

// Fixed-pool bitmask ECS. See docs/ecs.md.
//
// Entities are slots in struct-of-arrays component pools. A system loops over
// the pools and processes entities whose mask contains the components it needs.

#include "serval/fixed.h"
#include "serval/platform.h"

#define MAX_ENT 128

// Component bits, in ent_mask and the masks entity_create(), ent_has(),
// ECS_FOR_EACH, ecs_count() and ecs_gather() take:
//   - Bits 0-6, the engine's components: C_POS, C_VEL, C_SPR, C_BODY and
//     C_ANIM here, C_MAPBODY (bit 4) in map.h, C_PATH (bit 6) in path.h.
//   - Bits 7-15, reserved for engine components of later versions. Never use
//     them: entity_create() leaves them out (warning once in debug builds),
//     and nothing checks them in ent_mask, where a later engine version would
//     give them a meaning.
//   - Bits 16-30, the game's: C_GAME(0) to C_GAME(14), 15 of them; there is
//     no C_GAME(15). A constant n outside 0-14 is a compile error ("size of
//     array is negative").
//   - Bit 31, C_ALIVE, marks a slot as alive, so free slots never match any
//     system. entity_create() adds it (warning once in debug builds if the
//     mask passed in has it already).
#define C_POS (1u << 0)  // pos_x, pos_y
#define C_VEL (1u << 1)  // vel_x, vel_y
#define C_SPR (1u << 2)  // spr_id, spr_frame, spr_flags, spr_depth, spr_angle, spr_scale
#define C_BODY (1u << 3) // body_w, body_h and the other body_* pools (physics.h)
// C_MAPBODY (1u << 4), bodies that collide with the map, is in map.h.
#define C_ANIM                                                                                     \
    (1u << 5) // spr_anim_time, spr_anim_step; with C_SPR, sys_animate plays the
              // sprite's animation
// C_PATH (1u << 6), entities following a path (sys_path), is in path.h.
#define C_GAME(n) ((1u << (16 + (n))) + 0u * (u32)sizeof(char[(unsigned)(n) < 15u ? 1 : -1]))
#define C_ALIVE (1u << 31)

// Generational handle: low 8 bits are the slot index, high 8 bits the slot's
// generation (1-255). A handle goes stale when its entity is destroyed or the
// ECS is reset. Freed slots are reused oldest-first, so a slot comes back
// only after every other free slot has been used; a stale handle could match
// a new entity again only after its slot has been reused 255 times.
typedef u16 Entity;

// Never refers to an entity (generations start at 1).
#define ENTITY_NONE ((Entity)0)

// Component mask per slot, including C_ALIVE. Systems read this directly.
// Games may add and remove components here, engine or their own
// (ent_mask[i] |= C_SPR; ent_mask[i] &= ~C_SHIELD): a permanent part of the
// API, which later versions may wrap in helpers but never take away. Keep
// C_ALIVE: a slot without it matches no system and no ent_has(). Leave bits
// 7-15 clear (reserved, above), and add C_PATH only through path_start()
// (path.h). Create and destroy entities only with entity_create() and
// entity_destroy().
extern u32 ent_mask[MAX_ENT];

// Engine component pools, indexed by entity_index(). Zeroed by entity_create().
extern FIXED pos_x[MAX_ENT], pos_y[MAX_ENT]; // C_POS: top-left position in pixels
extern FIXED vel_x[MAX_ENT], vel_y[MAX_ENT]; // C_VEL: pixels per frame
extern u16 spr_id[MAX_ENT];                  // C_SPR: sprite ID (sprites.h)
extern u8 spr_frame[MAX_ENT];                // C_SPR: animation frame
extern u16 spr_flags[MAX_ENT];               // C_SPR: SPRITE_* draw flags (sprites.h)
extern s16 spr_depth[MAX_ENT]; // C_SPR: sys_render_by_depth draws higher depths in front
extern u16 spr_angle[MAX_ENT]; // C_SPR: rotation (sprite_draw_rotated); 0 = unrotated
// C_SPR: size for sprite_draw_ex(), used only with SPRITE_SCALED in spr_flags
// (sprites.h): 256ths, so FX_ONE is normal size, FX_ONE / 2 half, FX(2)
// twice, 0 nothing; negative mirrors. One scale for both axes, -128 to 127.99.
extern s16 spr_scale[MAX_ENT];
extern u8 spr_anim_time[MAX_ENT]; // C_ANIM: frames spr_frame has shown so far (sys_animate)
extern u8 spr_anim_step[MAX_ENT]; // C_ANIM: step of the sprite's frame_order (sys_animate)

// True if entity slot i is alive and has every component in `mask` (0 tests
// only that it is alive). Prefer this to testing ent_mask by hand: `ent_mask[i]
// & (A | B)` is true when *either* component is present.
static inline bool ent_has(u32 i, u32 mask) {
    // With a constant mask this compiles down to one mask-and-compare.
    return (ent_mask[i] & (mask | C_ALIVE)) == (mask | C_ALIVE);
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

// Destroys every entity, e.g. on room change. Outstanding handles go stale,
// including those of entities whose C_ALIVE bit was cleared by hand.
void ecs_reset(void);

// Creates an entity with the given component bits (C_ALIVE is added). Its
// components are zeroed. Returns ENTITY_NONE if the pool is full (warning
// once in debug builds). Reserved bits 7-15 in `components` are left out of
// its mask (warning once in debug builds), so no entity has one before an
// engine version gives it a meaning. The warnings are reported again after
// ecs_reset().
Entity entity_create(u32 components);

// Destroys the entity. Does nothing if the handle is stale or ENTITY_NONE.
// Also frees an entity whose C_ALIVE bit was cleared by hand (warning in
// debug builds).
void entity_destroy(Entity e);

// True if the handle refers to a live entity.
bool entity_alive(Entity e);

// The handle of the live entity in slot `index` (as from ECS_FOR_EACH), or
// ENTITY_NONE if the slot is free. Use it to destroy or keep the entity a
// system is looking at: ECS_FOR_EACH(i, C_ROCK) entity_destroy(entity_at(i));
Entity entity_at(u32 index);

// The number of live entities that have every component in `mask` (0 counts
// all live entities), e.g. the rocks left in a wave: ecs_count(C_ROCK). Scans
// every slot, but as ARM code in IWRAM: about 870 cycles, against about 4,300
// for an ECS_FOR_EACH in game code.
u32 ecs_count(u32 mask);

// Lists the slot indices of the live entities that have every component in
// `mask` (0 lists all live entities) in `out`, in ascending order, and returns
// how many there are. `out` must hold MAX_ENT entries. Like ecs_count(), it
// costs about a quarter of an ECS_FOR_EACH in game code (about 1,000 cycles;
// ECS_FOR_EACH visits all 128 slots however few match), so a game that loops
// over the same kind several times a frame, or over pairs of kinds, gathers
// each kind once per frame and loops over the lists:
//
//     static u8 shots[MAX_ENT], enemies[MAX_ENT];
//     u32 shot_count = ecs_gather(C_SHOT, shots);
//     u32 enemy_count = ecs_gather(C_ENEMY, enemies);
//     for (u32 s = 0; s < shot_count; s++)
//         for (u32 e = 0; e < enemy_count; e++)
//             if (body_overlap(shots[s], enemies[e])) ...
//
// The lists are a snapshot: entities created afterwards aren't in them, and
// destroying a listed entity leaves its index in the list (check
// ent_has(i, 0) before using an entry if an earlier step may have destroyed
// it).
u32 ecs_gather(u32 mask, u8* out);

// The number of entities entity_create() can still create (0: the pool is
// full and it would return ENTITY_NONE, with a warning in debug builds).
// Optional entities, such as particles and other effects, can be created
// only while there is room, keeping a reserve for those that matter:
// `if (ecs_free_count() > 8) spawn_spark(x, y);`. Entities whose C_ALIVE bit
// the game cleared still hold their slot.
u32 ecs_free_count(void);

// Systems, run once per frame by the game.
// sys_movement: position += velocity for entities with C_POS and C_VEL, except
// map bodies (C_MAPBODY, map.h), which sys_map_movement() moves.
void sys_movement(void);
// sys_render: draws entities with C_POS and C_SPR using sprite_draw() (or
// sprite_draw_ex() when spr_angle is not 0 or spr_flags has SPRITE_SCALED), with their spr_flags,
// at their position minus the camera's (camera_set(), map.h; (0, 0) unless a game scrolls), or at
// their position with SPRITE_SCREEN. Among sprites on the same layer, lower entity indices are in
// front.
void sys_render(void);
// sys_render_by_depth: like sys_render, but sprites with a higher spr_depth
// are drawn in front of lower ones (equal depths: lower index in front). For
// a top-down look, set spr_depth to the entity's y each frame so sprites lower
// on screen overlap those above, or give each kind of entity its own depth
// (bricks behind balls). The extra cost depends on the depths: about 400
// cycles over sys_render for 128 sprites whose depths never decrease from one
// slot to the next (all equal, or each kind created in front of the ones
// before: no sort), one counting pass for depths within 256 of each other
// (two depths, 88 sprites: about 4,100; depth = y, 128 sprites: about
// 8,200), two passes for wider ranges.
void sys_render_by_depth(void);
// sys_animate: plays the animation of entities with C_SPR and C_ANIM: each
// call counts one frame in spr_anim_time, and once spr_frame has shown for
// its time in the sprite's frame_times (sprites.h; one frame each if NULL),
// moves to the next frame, looping back to 0 after the last, or staying on
// the last for sprites with SPRITE_ASSET_ANIM_ONCE (then spr_frame ==
// frame_count - 1 means the animation is over). Run it once per frame, before
// rendering. To start an animation (or switch to another sprite's), set
// spr_id, spr_frame = 0 and spr_anim_time = 0; a frame the sprite doesn't have
// restarts it (warning in debug builds).
// Sprites with a frame_order play its steps instead: spr_anim_step is the
// step (frame_times are per step; with SPRITE_ASSET_ANIM_ONCE, spr_anim_step
// == order_length - 1 means it is over) and sys_animate sets spr_frame to the
// step's frame and its flips in spr_flags, XORed with the game's flips (the
// SPRITE_ANIM_FLIP_* bits record them; assign spr_flags whole or toggle flips
// with ^=). To start one, zero spr_anim_step and spr_anim_time.
void sys_animate(void);
// True if e is alive, has C_SPR and C_ANIM, its sprite has
// SPRITE_ASSET_ANIM_ONCE, and it is on its last frame (or, with a
// frame_order, its last step), where sys_animate leaves it.
bool anim_finished(Entity e);

static inline u8 entity_index(Entity e) {
    return (u8)(e & 0xFF);
}
static inline u8 entity_generation(Entity e) {
    return (u8)(e >> 8);
}

#endif // SERVAL_ECS_H
