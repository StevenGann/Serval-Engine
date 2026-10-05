// sys_animate: steps entities' sprite animations by their sprites'
// frame_times, through their frame_order if they have one. Portable: it only
// reads the sprite table and the ECS pools.

#include "serval/ecs.h"
#include "serval/sprites.h"

#include "../core/sprite_internal.h"
#include "../core/warn.h"

// play_order() moves a frame_order entry's flips (bits 6-7) to spr_flags'
// flips (bits 0-1) and records them in bits 5-6.
_Static_assert(SPRITE_FRAME_FLIP_H >> 6 == SPRITE_FLIP_H &&
                   SPRITE_FRAME_FLIP_V >> 6 == SPRITE_FLIP_V,
               "frame_order flip bits");
_Static_assert(SPRITE_ANIM_FLIP_H == SPRITE_FLIP_H << 5 && SPRITE_ANIM_FLIP_V == SPRITE_FLIP_V << 5,
               "spr_flags animation flip bits");

#ifdef SERVAL_DEBUG
static bool warned_sprite, warned_frame;

static __attribute__((noinline, cold)) void warn_sprite(u32 i, u32 id) {
    if (warned_sprite)
        return;
    warned_sprite = true;
    if (id >= serval_sprite_count)
        SERVAL_WARN("sys_animate: entity %u has sprite ID %u, which is not in the sprite table "
                    "(%u sprites)",
                    i, id, serval_sprite_count);
    else
        SERVAL_WARN("sys_animate: sprite table entry %u (entity %u) is NULL or not valid", id, i);
}

static __attribute__((noinline, cold)) void warn_frame(u32 i, u32 id, u32 frame) {
    if (warned_frame)
        return;
    warned_frame = true;
    SERVAL_WARN("sys_animate: sprite %u has no frame %u (entity %u); restarted. With a new "
                "spr_id, also zero spr_frame and spr_anim_time",
                id, frame, i);
}
static bool warned_order, warned_length, warned_step, warned_order_frame;

// order_length set, but frame_order missing: nothing to play.
static __attribute__((noinline, cold)) bool bad_order(u32 id, const SpriteAsset* sprite) {
    if (serval_plausible_pointer(sprite->frame_order))
        return false;
    if (!warned_order) {
        warned_order = true;
        SERVAL_WARN("sys_animate: sprite %u has an order_length but its frame_order is %s", id,
                    sprite->frame_order ? "not a valid pointer" : "NULL");
    }
    return true;
}

// frame_order set, but order_length 0: frames play in order, frame_order unused.
static __attribute__((noinline, cold)) void warn_no_length(u32 id) {
    if (warned_length)
        return;
    warned_length = true;
    SERVAL_WARN("sys_animate: sprite %u has a frame_order but order_length 0, so it plays its "
                "frames in order; set order_length to its number of steps",
                id);
}

static __attribute__((noinline, cold)) void warn_step(u32 i, u32 id, u32 step) {
    if (warned_step)
        return;
    warned_step = true;
    SERVAL_WARN("sys_animate: sprite %u has no frame_order step %u (entity %u); restarted. "
                "With a new spr_id, also zero spr_anim_step and spr_anim_time",
                id, step, i);
}

static __attribute__((noinline, cold)) void warn_order_frame(u32 id, u32 step, u32 frame) {
    if (warned_order_frame)
        return;
    warned_order_frame = true;
    SERVAL_WARN("sys_animate: sprite %u's frame_order step %u is frame %u, which it doesn't "
                "have; showing frame 0",
                id, step, frame);
}
#define BAD_SPRITE(i, id) warn_sprite(i, id)
#define BAD_FRAME(i, id, frame) warn_frame(i, id, frame)
#define BAD_ORDER(id, sprite) bad_order(id, sprite)
#define BAD_STEP(i, id, step) warn_step(i, id, step)
#define BAD_ORDER_FRAME(id, step, frame) warn_order_frame(id, step, frame)
#define CHECK_NO_LENGTH(id, sprite)                                                                \
    do {                                                                                           \
        if ((sprite)->frame_order)                                                                 \
            warn_no_length(id);                                                                    \
    } while (0)
#else
#define BAD_SPRITE(i, id) ((void)0)
#define BAD_FRAME(i, id, frame) ((void)0)
#define BAD_ORDER(id, sprite) (!(sprite)->frame_order)
#define BAD_STEP(i, id, step) ((void)0)
#define BAD_ORDER_FRAME(id, step, frame) ((void)0)
#define CHECK_NO_LENGTH(id, sprite) ((void)0)
#endif

// The frame_order path, out of line so sprites without one don't pay for it.
// spr_anim_step is the step; spr_frame and the flips in spr_flags follow it.
// counts: frame_count | order_length << 8, already read by sys_animate.
static __attribute__((noinline)) void play_order(u32 i, u32 id, const SpriteAsset* sprite,
                                                 u32 counts) {
    (void)id; // only for warnings
    if (BAD_ORDER(id, sprite))
        return;
    u32 steps = counts >> 8;
    u32 step = spr_anim_step[i];
    if (step >= steps) {
        BAD_STEP(i, id, step);
        step = 0;
        spr_anim_step[i] = 0;
        spr_anim_time[i] = 0;
    } else {
        bool last = step + 1 == steps;
        u32 duration = sprite->frame_times ? sprite->frame_times[step] : 1;
        // Finished (played once) or held: stays on this step.
        if (!(last && (sprite->flags & SPRITE_ASSET_ANIM_ONCE)) && duration) {
            u32 elapsed = spr_anim_time[i] + 1u;
            if (elapsed < duration) {
                spr_anim_time[i] = (u8)elapsed;
            } else {
                spr_anim_time[i] = 0;
                step = last ? 0 : step + 1;
                spr_anim_step[i] = (u8)step;
            }
        }
    }
    // Set even when the step stays, so a game's spr_anim_step = 0 shows at once.
    u32 entry = sprite->frame_order[step];
    u32 frame = entry & (SPRITE_FRAME_FLIP_H - 1);
    if (frame && frame >= (counts & 0xFF)) { // frame 0 always exists (frame_count 0 means 1)
        BAD_ORDER_FRAME(id, step, frame);
        frame = 0;
    }
    spr_frame[i] = (u8)frame;
    // Swap the flips applied for the previous step (recorded in bits 5-6) for
    // this step's, leaving the game's own flips in bits 0-1 as they are.
    u32 flags = spr_flags[i];
    u32 change = ((flags >> 5) ^ (entry >> 6)) & 3;
    if (change)
        spr_flags[i] = (u16)(flags ^ change ^ (change << 5));
}

void sys_animate(void) {
    const SpriteAsset* const* table = serval_sprite_table;
    const u32 count = serval_sprite_count;
    for (u32 i = 0; i < MAX_ENT; i++) {
        if (!ent_has(i, C_SPR | C_ANIM))
            continue;
        u32 id = spr_id[i];
        const SpriteAsset* sprite = id < count ? table[id] : NULL;
        if (!serval_plausible_pointer(sprite)) {
            BAD_SPRITE(i, id);
            continue;
        }
        // frame_count and order_length are adjacent, so this is one read from
        // ROM, not two (a read costs about 20 cycles per entity).
        u32 counts = sprite->frame_count | (u32)sprite->order_length << 8;
        if (counts > 0xFF) {
            play_order(i, id, sprite, counts);
            continue;
        }
        CHECK_NO_LENGTH(id, sprite); // debug builds only
        u32 frames = counts ? counts : 1;
        u32 frame = spr_frame[i];
        if (frame >= frames) {
            BAD_FRAME(i, id, frame);
            spr_frame[i] = 0;
            spr_anim_time[i] = 0;
            continue;
        }
        bool last = frame + 1 == frames;
        if (last && (sprite->flags & SPRITE_ASSET_ANIM_ONCE))
            continue; // finished: stays on the last frame
        u32 duration = sprite->frame_times ? sprite->frame_times[frame] : 1;
        if (duration == 0)
            continue; // held
        u32 elapsed = spr_anim_time[i] + 1u;
        if (elapsed < duration) {
            spr_anim_time[i] = (u8)elapsed;
            continue;
        }
        spr_anim_time[i] = 0;
        spr_frame[i] = (u8)(last ? 0 : frame + 1);
    }
}
