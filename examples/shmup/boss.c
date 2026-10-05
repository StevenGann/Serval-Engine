// The boss, the Hive Lantern: a 64x64 core between two 32x32 gun pods, three
// entities moved together. It comes down from the top, sways from side to
// side and attacks in three phases set by the core's hit points:
//   1. the pods fire aimed three-way bursts in turn, the core slow rings;
//   2. the core spins a double spiral, the pods (if left) single aimed shots;
//   3. enraged (red, and the music speeds up): rings and five-way spreads,
//      swaying faster.
// The pods can be shot off for points (their attacks stop). Destroying the
// core ends the fight in a chain of explosions.

#include "game.h"

#define CORE_HP 840
#define POD_HP 100
#define PHASE2_HP 560 // the core's hit points where phase 2 begins...
#define PHASE3_HP 280 // ...and phase 3
#define HOME_Y 44     // the core's center on screen while fighting
#define DYING_FRAMES 150

typedef enum { BOSS_NONE, BOSS_ENTERING, BOSS_FIGHTING, BOSS_DYING, BOSS_DEFEATED } BossState;

static BossState state;
static Entity core, pods[2];
static int t;              // frames in the current state
static u16 swing;          // sway angle
static u16 spiral;         // the spiral's angle
static int core_x, core_y; // the core's center on screen
static bool enraged;       // phase 3 has begun (the tune is faster)

void boss_reset(void) {
    state = BOSS_NONE;
    core = pods[0] = pods[1] = ENTITY_NONE;
}

bool boss_active(void) {
    return state == BOSS_ENTERING || state == BOSS_FIGHTING;
}

bool boss_defeated(void) {
    return state == BOSS_DEFEATED;
}

static Entity spawn_part(EnemyKind k, u16 sprite, int w, int h, int hit_points) {
    // Not C_ENEMY yet: shots pass through while it comes in.
    u32 i = spawn(0, sprite, w, h, FX(FIELD_W / 2), FX(cam_y - 48));
    if (i == MAX_ENT)
        return ENTITY_NONE;
    enemy_count++;
    kind[i] = (u8)k;
    hp[i] = (s16)hit_points;
    flash[i] = 0;
    timer[i] = 0;
    drops[i] = ITEM_NONE;
    spr_depth[i] = DEPTH_BOSS;
    return entity_at(i);
}

void boss_start(void) {
    core = spawn_part(ENEMY_BOSS, SPR_BOSS, 48, 36, CORE_HP);
    pods[0] = spawn_part(ENEMY_POD, SPR_POD, 20, 20, POD_HP);
    pods[1] = spawn_part(ENEMY_POD, SPR_POD, 20, 20, POD_HP);
    state = BOSS_ENTERING;
    t = 0;
    swing = 0;
    core_x = FIELD_W / 2;
    core_y = -48;
    enraged = false;
    psg_music_play(&boss_song);
}

static int phase(void) {
    int h = hp[entity_index(core)];
    return h > PHASE2_HP ? 1 : h > PHASE3_HP ? 2 : 3;
}

// Puts the parts where they belong around the core's center (screen
// coordinates): pods to the sides, a little lower, swinging out of step.
static void place_parts(void) {
    u32 c = entity_index(core);
    pos_x[c] = FX(core_x - body_w[c] / 2);
    pos_y[c] = FX(cam_y + core_y - body_h[c] / 2);
    for (int k = 0; k < 2; k++) {
        if (!entity_alive(pods[k]))
            continue;
        u32 p = entity_index(pods[k]);
        int bob = fx_to_int(fx_sin((u16)(swing * 2 + k * ANGLE_DEG(180))) * 3);
        pos_x[p] = FX(core_x + (k ? 36 : -36) - body_w[p] / 2);
        pos_y[p] = FX(cam_y + core_y + 8 + bob - body_h[p] / 2);
    }
}

// The flash after a hit, and the core's red look in phase 3. Under steady
// fire a big target would be white all the time, so the boss's parts flash
// white only one frame in four while they are being hit.
static void update_sprites(void) {
    bool blink = frame_count() % 4 == 0;
    u32 c = entity_index(core);
    if (flash[c] > 0)
        flash[c]--;
    bool rage = state == BOSS_FIGHTING && phase() == 3;
    spr_id[c] = flash[c] && blink ? SPR_BOSS_FLASH : rage ? SPR_BOSS_RAGE : SPR_BOSS;
    for (int k = 0; k < 2; k++) {
        if (!entity_alive(pods[k]))
            continue;
        u32 p = entity_index(pods[k]);
        if (flash[p] > 0)
            flash[p]--;
        spr_id[p] = flash[p] && blink ? SPR_POD_FLASH : SPR_POD;
    }
}

static void attack(void) {
    int ph = phase();
    FIXED cx = FX(core_x), cy = FX(cam_y + core_y + 6);
    if (ph == 1) {
        // Pods in turn: an aimed three-way burst every 40 frames.
        if (t % 40 == 0) {
            Entity pod = pods[t / 40 % 2];
            if (entity_alive(pod)) {
                u32 p = entity_index(pod);
                FIXED px = center_x(p), py = center_y(p) + FX(10);
                fire_spread(px, py, aim_at_player(px, py), 3, ANGLE_DEG(12), FX(3) / 2,
                            BULLET_PINK);
            }
        }
        if (t % 150 == 75)
            fire_ring(cx, cy, 10, (u16)(t * 97), FX(1), BULLET_BLUE);
    } else if (ph == 2) {
        // A double spiral: two bullets every 6 frames, opposite each other,
        // turning 14 degrees each time. About 40 bullets fly at once; past
        // the cap the spiral gets gaps.
        if (t % 6 == 0) {
            spiral = (u16)(spiral + ANGLE_DEG(14));
            fire_bullet(cx, cy, spiral, FX(5) / 4, BULLET_BLUE);
            fire_bullet(cx, cy, (u16)(spiral + ANGLE_DEG(180)), FX(5) / 4, BULLET_BLUE);
        }
        if (t % 60 == 30) {
            for (int k = 0; k < 2; k++) {
                if (!entity_alive(pods[k]))
                    continue;
                u32 p = entity_index(pods[k]);
                FIXED px = center_x(p), py = center_y(p) + FX(10);
                fire_bullet(px, py, aim_at_player(px, py), FX(7) / 4, BULLET_PINK);
            }
        }
    } else {
        if (t % 50 == 0)
            fire_ring(cx, cy, 14, (u16)(t / 50 % 2 ? ANGLE_DEG(13) : 0), FX(5) / 4, BULLET_BLUE);
        if (t % 70 == 35)
            fire_spread(cx, cy, aim_at_player(cx, cy), 5, ANGLE_DEG(12), FX(7) / 4, BULLET_PINK);
    }
}

static void become_target(Entity e) {
    if (entity_alive(e))
        ent_mask[entity_index(e)] |= C_ENEMY;
}

void boss_update(void) {
    if (state == BOSS_NONE || state == BOSS_DEFEATED)
        return;
    t++;
    switch (state) {
    case BOSS_ENTERING:
        core_y = -48 + t / 2;
        if (core_y >= HOME_Y) {
            state = BOSS_FIGHTING;
            t = 0;
            become_target(core);
            become_target(pods[0]);
            become_target(pods[1]);
        }
        break;
    case BOSS_FIGHTING: {
        bool rage = phase() == 3;
        if (rage && !enraged) {
            enraged = true;
            psg_music_set_tempo(BOSS_RAGE_TEMPO); // faster from where it is
        }
        swing = (u16)(swing + (rage ? ANGLE_DEG(1) + ANGLE_DEG(1) / 2 : ANGLE_DEG(1)));
        core_x = FIELD_W / 2 + fx_to_int(fx_sin(swing) * 44);
        core_y = HOME_Y + fx_to_int(fx_sin((u16)(swing * 2)) * 8);
        if (player_alive)
            attack();
        break;
    }
    case BOSS_DYING:
        // Explosions all over the core, faster toward the end.
        if (t % (t < 90 ? 8 : 4) == 0) {
            int x = core_x + random_range(-24, 24); // one call per statement (see explode())
            int y = cam_y + core_y + random_range(-24, 24);
            explode(FX(x), FX(y), 1, 3);
            psg_play(SND_BOOM);
        }
        if (t >= DYING_FRAMES) {
            explode(FX(core_x), FX(cam_y + core_y), 6, 16);
            entity_destroy(core);
            core = ENTITY_NONE;
            state = BOSS_DEFEATED;
            return;
        }
        break;
    default:
        break;
    }
    place_parts();
    update_sprites();
}

void boss_part_destroyed(u32 i) {
    if (kind[i] == ENEMY_POD) {
        explode(center_x(i), center_y(i), 3, 8);
        psg_play(SND_BOOM);
        add_score(5000);
        entity_destroy(entity_at(i));
        return;
    }
    // The core: the fight is over.
    add_score(50000);
    clear_bullets(true);
    for (int k = 0; k < 2; k++) {
        if (entity_alive(pods[k])) {
            u32 p = entity_index(pods[k]);
            explode(center_x(p), center_y(p), 3, 6);
            entity_destroy(pods[k]);
        }
    }
    ent_mask[i] &= ~C_ENEMY;
    state = BOSS_DYING;
    t = 0;
    psg_music_stop();
    psg_play(SND_BOOM);
}

// A bomb takes a chunk off every part that can be hit.
void boss_bomb(void) {
    if (state != BOSS_FIGHTING)
        return;
    for (int k = 0; k < 2; k++) {
        if (entity_alive(pods[k]))
            enemy_damage(entity_index(pods[k]), 40);
    }
    if (entity_alive(core))
        enemy_damage(entity_index(core), 40);
}
