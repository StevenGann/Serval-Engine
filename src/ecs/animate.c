// sys_animate: steps entities' sprite animations by their sprites'
// frame_times. Portable: it only reads the sprite table and the ECS pools.

#include "serval/ecs.h"
#include "serval/sprites.h"

#include "../core/sprite_internal.h"
#include "../core/warn.h"

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
#define BAD_SPRITE(i, id) warn_sprite(i, id)
#define BAD_FRAME(i, id, frame) warn_frame(i, id, frame)
#else
#define BAD_SPRITE(i, id) ((void)0)
#define BAD_FRAME(i, id, frame) ((void)0)
#endif

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
        u32 frames = sprite->frame_count ? sprite->frame_count : 1;
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
