// Paths (see path.h): per-entity turtle steering, turned into velocity by
// sys_path().

#include "serval/path.h"

#include "serval/math.h"
#include "warn.h"

// sys_path() reads them once per frame per pathed entity: EWRAM spares IWRAM.
SERVAL_EWRAM_BSS u16 path_heading[MAX_ENT];
SERVAL_EWRAM_BSS FIXED path_speed[MAX_ENT];
SERVAL_EWRAM_BSS u8 path_step[MAX_ENT];
SERVAL_EWRAM_BSS u16 path_time[MAX_ENT];
SERVAL_EWRAM_BSS static const Path* path_of[MAX_ENT];
SERVAL_EWRAM_BSS static u8 path_flags[MAX_ENT]; // REVERSED, FRESH
// The heading and speed vel_x and vel_y were last set from: the velocity is
// recomputed (sine, cosine, two multiplies) only when they change, so
// straight, constant-speed stretches cost little.
SERVAL_EWRAM_BSS static u16 vel_heading[MAX_ENT];
SERVAL_EWRAM_BSS static FIXED vel_speed[MAX_ENT];

#define REVERSED 1u // mirrored once: turns go the other way
#define FRESH 2u    // just started: the velocity isn't set yet

#ifdef SERVAL_DEBUG
// The entity each slot's path was started for, to catch C_PATH added by hand
// (or kept through entity_create()), which would resume another entity's path.
SERVAL_EWRAM_BSS static Entity path_owner[MAX_ENT];
static bool warned_start, warned_no_vel, warned_by_hand, warned_step;
#endif

// Speeds are capped at this, so speed * cosine (at most 256) fits 32 bits,
// even when a never-ending step accelerates forever.
#define MAX_PATH_SPEED FX(4096)

static bool valid_path(const Path* path) {
    return serval_plausible_pointer(path) && path->step_count > 0 &&
           serval_plausible_pointer(path->steps) &&
           (!path->loop || path->loop_step < path->step_count);
}

void path_start(Entity e, const Path* path, u32 flags) {
    if (!entity_alive(e) || !valid_path(path)) {
#ifdef SERVAL_DEBUG
        if (!warned_start) {
            warned_start = true;
            SERVAL_WARN("path_start: %s; the path is not started",
                        !entity_alive(e) ? "the entity is not alive (stale handle?)"
                                         : "the path is NULL, has no steps or its loop_step is "
                                           "past its last step");
        }
#endif
        return;
    }
    u32 i = entity_index(e);
#ifdef SERVAL_DEBUG
    if (!ent_has(i, C_VEL) && !warned_no_vel) {
        warned_no_vel = true;
        SERVAL_WARN("path_start: the entity has no C_VEL, so sys_path won't move it");
    }
    path_owner[i] = e;
#endif
    u16 heading = path->heading;
    if (flags & PATH_MIRROR_X)
        heading = (u16)(0x8000 - heading); // right <-> left
    if (flags & PATH_MIRROR_Y)
        heading = (u16)(0x10000 - heading); // down <-> up
    path_of[i] = path;
    path_flags[i] = (u8)(FRESH | ((flags ^ (flags >> 1)) & REVERSED)); // exactly one mirror
    path_heading[i] = heading;
    path_speed[i] = path->steps[0].speed;
    path_step[i] = 0;
    path_time[i] = 0;
    ent_mask[i] |= C_PATH;
}

void path_stop(Entity e) {
    if (entity_alive(e))
        ent_mask[entity_index(e)] &= ~C_PATH;
}

bool path_active(Entity e) {
    return entity_alive(e) && ent_has(entity_index(e), C_PATH);
}

// One frame of slot i's path. Returns false if the path is over (or broken).
static bool advance(u32 i) {
    const Path* path = path_of[i];
    u32 s = path_step[i];
    if (path == NULL || s >= path->step_count) {
#ifdef SERVAL_DEBUG
        if (!warned_step) {
            warned_step = true;
            SERVAL_WARN("sys_path: entity %d's path_step is past its path's last step; "
                        "the path stops",
                        (int)i);
        }
#endif
        return false;
    }
    const PathStep* step = &path->steps[s];
    u16 heading = path_heading[i];
    if (step->turn != 0) {
        u32 turn = (u32)step->turn;
        heading = (u16)((path_flags[i] & REVERSED) ? heading - turn : heading + turn);
        path_heading[i] = heading;
    }
    FIXED speed = path_speed[i];
    if (step->accel != 0) {
        speed = int_clamp(speed + step->accel, -MAX_PATH_SPEED, MAX_PATH_SPEED);
        path_speed[i] = speed;
    }
    if (heading != vel_heading[i] || speed != vel_speed[i] || (path_flags[i] & FRESH)) {
        vel_heading[i] = heading;
        vel_speed[i] = speed;
        path_flags[i] &= (u8)~FRESH;
        // Sines are at most 256 and speeds at most FX(4096) (a step's or the
        // game's may be more), so the products fit 32 bits; same as fx_mul.
        FIXED capped = int_clamp(speed, -MAX_PATH_SPEED, MAX_PATH_SPEED);
        vel_x[i] = (fx_cos(heading) * capped) >> FX_SHIFT;
        vel_y[i] = (fx_sin(heading) * capped) >> FX_SHIFT;
    }

    if (step->frames == 0)
        return true; // a step that never ends
    if (++path_time[i] < step->frames)
        return true;
    // The step is done: on to the next one, back to the loop step, or the end.
    if (++s >= path->step_count) {
        if (!path->loop)
            return false;
        s = path->loop_step;
    }
    path_step[i] = (u8)s;
    path_time[i] = 0;
    path_speed[i] = path->steps[s].speed;
    return true;
}

void sys_path(void) {
    ECS_FOR_EACH(i, C_PATH | C_VEL) {
#ifdef SERVAL_DEBUG
        if (path_owner[i] != entity_at(i)) {
            if (!warned_by_hand) {
                warned_by_hand = true;
                SERVAL_WARN("sys_path: entity %d has C_PATH without path_start(); "
                            "its C_PATH is removed",
                            (int)i);
            }
            ent_mask[i] &= ~C_PATH;
            continue;
        }
#endif
        if (!advance(i))
            ent_mask[i] &= ~C_PATH;
    }
}
