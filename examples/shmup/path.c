// The path helper (see path.h).

#include "game.h"

static const Path* path_of[MAX_ENT];
static u8 path_step[MAX_ENT];
static u16 path_time[MAX_ENT]; // frames into the step
static u16 path_heading[MAX_ENT];
static bool path_mirror[MAX_ENT];

// Mirroring left-right reflects a heading across the vertical: right becomes
// left (180 degrees minus the heading), and turns go the other way.
static u16 mirrored(u16 heading, bool mirror) {
    return mirror ? (u16)(0x8000 - heading) : heading;
}

void path_start(u32 i, const Path* path, bool mirror) {
    path_of[i] = path;
    path_step[i] = 0;
    path_time[i] = 0;
    path_mirror[i] = mirror;
    path_heading[i] = mirrored(path->heading, mirror);
    ent_mask[i] |= C_PATH;
}

bool path_finished(u32 i) {
    const Path* path = path_of[i];
    return path->loop == PATH_NO_LOOP && path_step[i] >= path->step_count - 1 &&
           path_time[i] >= path->steps[path->step_count - 1].frames;
}

void path_update_all(void) {
    ECS_FOR_EACH(i, C_PATH | C_VEL) {
        const Path* path = path_of[i];
        const PathStep* step = &path->steps[path_step[i]];
        if (path_time[i] >= step->frames) {
            // The step is over: the next one, the loop, or (no loop) the
            // last step's motion goes on without turning.
            if (path_step[i] + 1 < path->step_count) {
                path_step[i]++;
                path_time[i] = 0;
            } else if (path->loop != PATH_NO_LOOP) {
                path_step[i] = path->loop;
                path_time[i] = 0;
            }
            step = &path->steps[path_step[i]];
        }
        bool turning = path_time[i] < step->frames;
        if (turning) {
            path_time[i]++;
            s32 turn = path_mirror[i] ? -step->turn : step->turn;
            path_heading[i] = (u16)(path_heading[i] + turn);
        }
        vel_x[i] = fx_mul(fx_cos(path_heading[i]), step->speed);
        vel_y[i] = fx_mul(fx_sin(path_heading[i]), step->speed);
    }
}
