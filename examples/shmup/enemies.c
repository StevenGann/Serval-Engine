// Enemies: the stage's wave table, what each kind does, enemy bullets,
// collisions, explosions and power-ups.

#include "game.h"

// --- Movement patterns (path.h) --------------------------------------------------

#define DEG ANGLE_DEG(1)

static const PathStep down_steps[] = {{255, 0, FX(3) / 2}};
static const PathStep down_fast_steps[] = {{255, 0, FX(5) / 2}};
static const PathStep slow_steps[] = {{255, 0, FX(3) / 4}};
// In, a half circle to the right and back up.
static const PathStep swoop_steps[] = {{45, 0, FX(2)}, {60, -3 * DEG, FX(2)}, {255, 0, FX(2)}};
// Down while weaving left and right: close to a sine wave.
static const PathStep weave_steps[] = {
    {20, 2 * DEG, FX(1)}, {40, -2 * DEG, FX(1)}, {20, 2 * DEG, FX(1)}};
// From the left edge to the right, then curving down.
static const PathStep side_steps[] = {{40, 0, FX(2)}, {45, 2 * DEG, FX(2)}, {255, 0, FX(2)}};
// Down, a pause, then a fast dive.
static const PathStep dive_steps[] = {{40, 0, FX(3) / 2}, {30, 0, FX(1) / 4}, {255, 0, FX(3)}};
// Down, hover a while, back up.
static const PathStep hover_steps[] = {{60, 0, FX(1)}, {300, 0, 0}, {255, 0, -FX(1) / 2}};
// A long arc across the field from the top left.
static const PathStep arc_steps[] = {{240, -DEG / 2, FX(3) / 2}, {255, 0, FX(3) / 2}};
// Diagonal zigzags: straight runs with sharp 90-degree turns.
static const PathStep zigzag_steps[] = {
    {36, 0, FX(3) / 2}, {1, -90 * DEG, FX(3) / 2}, {36, 0, FX(3) / 2}, {1, 90 * DEG, FX(3) / 2}};

enum { P_DOWN, P_DOWN_FAST, P_SLOW, P_SWOOP, P_WEAVE, P_SIDE, P_DIVE, P_HOVER, P_ARC, P_ZIGZAG };

#define PATH(heading, steps, loop) {ANGLE_DEG(heading), sizeof steps / sizeof steps[0], loop, steps}
static const Path paths[] = {
    [P_DOWN] = PATH(90, down_steps, PATH_NO_LOOP),
    [P_DOWN_FAST] = PATH(90, down_fast_steps, PATH_NO_LOOP),
    [P_SLOW] = PATH(90, slow_steps, PATH_NO_LOOP),
    [P_SWOOP] = PATH(90, swoop_steps, PATH_NO_LOOP),
    [P_WEAVE] = PATH(90, weave_steps, 0),
    [P_SIDE] = PATH(0, side_steps, PATH_NO_LOOP),
    [P_DIVE] = PATH(90, dive_steps, PATH_NO_LOOP),
    [P_HOVER] = PATH(90, hover_steps, PATH_NO_LOOP),
    [P_ARC] = PATH(60, arc_steps, PATH_NO_LOOP),
    [P_ZIGZAG] = PATH(45, zigzag_steps, 0),
};

// --- Enemy kinds ------------------------------------------------------------------

typedef struct {
    u16 sprite, flash_sprite;
    u8 w, h; // hitbox
    s16 hp;
    u16 points;
} KindInfo;

static const KindInfo kinds[] = {
    [ENEMY_DART] = {SPR_DART, SPR_DART_FLASH, 12, 10, 1, 100},
    [ENEMY_CARRIER] = {SPR_CARRIER, SPR_DART_FLASH, 12, 10, 8, 500},
    [ENEMY_SPINNER] = {SPR_SPINNER, SPR_SPINNER_FLASH, 12, 12, 4, 200},
    [ENEMY_GUNSHIP] = {SPR_GUNSHIP, SPR_GUNSHIP_FLASH, 26, 24, 40, 2000},
    [ENEMY_TURRET] = {SPR_TURRET, SPR_TURRET_FLASH, 12, 12, 8, 300},
    [ENEMY_BOSS] = {SPR_BOSS, SPR_BOSS_FLASH, 48, 36, 0, 0}, // boss.c
    [ENEMY_POD] = {SPR_POD, SPR_POD_FLASH, 20, 20, 0, 0},    // boss.c
};

static u8 fire_at[MAX_ENT]; // C_ENEMY: the age (timer) at which a dart fires, 0 if never
static u16 spin;            // spinners share one rotation (and one rotation matrix)

// --- The wave table ------------------------------------------------------------

#define WAVE_MIRROR 1 // the path flipped left-right
#define WAVE_FIRE 2   // these enemies shoot
#define WAVE_POWER 4  // the last one drops a power-up...
#define WAVE_BOMB 8   // ...or a bomb

typedef struct {
    u16 time; // stage_frame when the first enemy appears
    u8 kind;  // EnemyKind
    u8 path;  // P_*
    s16 x, y; // screen position of the first enemy's center (y < 0: above the field)
    u8 count; // enemies in the wave
    u8 gap;   // frames between them
    s8 dx;    // x step from one to the next (a row when gap is 0)
    u8 flags; // WAVE_*
} Wave;

// The stage, about 110 seconds (stage.c shows which segments pass when: the
// hull's turrets come at about 1400-1800, 2900-3300, 4200-4600 and
// 5200-5600, so the waves around them are lighter).
static const Wave waves[] = {
    {120, ENEMY_DART, P_DOWN, 44, -12, 5, 14, 0, 0},
    {320, ENEMY_DART, P_DOWN, 132, -12, 5, 14, 0, 0},
    {520, ENEMY_DART, P_SWOOP, 40, -12, 6, 10, 0, WAVE_FIRE},
    {740, ENEMY_DART, P_SWOOP, 136, -12, 6, 10, 0, WAVE_FIRE | WAVE_MIRROR},
    {950, ENEMY_CARRIER, P_SLOW, 88, -12, 1, 0, 0, WAVE_POWER},
    {1080, ENEMY_SPINNER, P_WEAVE, 48, -12, 3, 40, 0, WAVE_FIRE},
    {1250, ENEMY_SPINNER, P_WEAVE, 128, -12, 3, 40, 0, WAVE_FIRE | WAVE_MIRROR},
    {1450, ENEMY_DART, P_DOWN_FAST, 30, -12, 4, 12, 0, 0},
    {1600, ENEMY_DART, P_DOWN_FAST, 146, -12, 4, 12, 0, 0},
    {1850, ENEMY_GUNSHIP, P_HOVER, 88, -20, 1, 0, 0, WAVE_FIRE | WAVE_BOMB},
    {2250, ENEMY_DART, P_SIDE, -12, 24, 6, 10, 0, WAVE_FIRE},
    {2400, ENEMY_DART, P_SIDE, FIELD_W + 12, 40, 6, 10, 0, WAVE_FIRE | WAVE_MIRROR},
    {2580, ENEMY_CARRIER, P_SLOW, 50, -12, 1, 0, 0, WAVE_POWER},
    {2700, ENEMY_DART, P_ZIGZAG, 30, -12, 5, 12, 0, 0},
    {2950, ENEMY_SPINNER, P_WEAVE, 88, -12, 4, 35, 0, WAVE_FIRE},
    {3200, ENEMY_DART, P_ARC, -8, 10, 6, 10, 0, WAVE_FIRE},
    {3450, ENEMY_DART, P_ARC, FIELD_W + 8, 10, 6, 10, 0, WAVE_FIRE | WAVE_MIRROR},
    {3650, ENEMY_GUNSHIP, P_HOVER, 50, -20, 1, 0, 0, WAVE_FIRE | WAVE_POWER},
    {3800, ENEMY_GUNSHIP, P_HOVER, 126, -20, 1, 0, 0, WAVE_FIRE},
    {4150, ENEMY_DART, P_DIVE, 24, -12, 4, 0, 42, WAVE_FIRE},
    {4400, ENEMY_DART, P_SWOOP, 40, -12, 6, 10, 0, WAVE_FIRE},
    {4560, ENEMY_DART, P_SWOOP, 136, -12, 6, 10, 0, WAVE_FIRE | WAVE_MIRROR},
    {4750, ENEMY_CARRIER, P_SLOW, 126, -12, 1, 0, 0, WAVE_POWER},
    {4850, ENEMY_SPINNER, P_WEAVE, 40, -12, 3, 40, 0, WAVE_FIRE},
    {5000, ENEMY_SPINNER, P_WEAVE, 136, -12, 3, 40, 0, WAVE_FIRE | WAVE_MIRROR},
    {5250, ENEMY_DART, P_DOWN_FAST, 60, -12, 4, 12, 0, WAVE_FIRE},
    {5450, ENEMY_DART, P_DOWN_FAST, 116, -12, 4, 12, 0, WAVE_FIRE},
    {5700, ENEMY_GUNSHIP, P_HOVER, 88, -20, 1, 0, 0, WAVE_FIRE | WAVE_BOMB},
    {5950, ENEMY_DART, P_SIDE, -12, 20, 6, 8, 0, WAVE_FIRE},
    {6050, ENEMY_DART, P_SIDE, FIELD_W + 12, 36, 6, 8, 0, WAVE_FIRE | WAVE_MIRROR},
    {6200, ENEMY_DART, P_DIVE, 24, -12, 4, 0, 42, WAVE_FIRE},
};
#define WAVE_COUNT ((int)(sizeof waves / sizeof waves[0]))

// Waves being spawned, one enemy every `gap` frames.
#define MAX_SPAWNERS 4
static struct {
    const Wave* wave;
    int spawned, wait;
} spawners[MAX_SPAWNERS];
static int next_wave;

void enemies_reset(void) {
    next_wave = 0;
    for (int k = 0; k < MAX_SPAWNERS; k++)
        spawners[k].wave = 0;
}

// Creates an enemy of a kind centered at (x, y) in world pixels.
static u32 spawn_enemy(EnemyKind k, FIXED x, FIXED y, u32 components) {
    if (enemy_count >= MAX_ENEMIES - 3) // the last 3 are the boss's
        return MAX_ENT;
    const KindInfo* info = &kinds[k];
    u32 i = spawn(C_ENEMY | components, info->sprite, info->w, info->h, x, y);
    if (i == MAX_ENT)
        return MAX_ENT;
    enemy_count++;
    kind[i] = (u8)k;
    hp[i] = info->hp;
    timer[i] = 0;
    flash[i] = 0;
    drops[i] = ITEM_NONE;
    fire_at[i] = 0;
    spr_depth[i] = DEPTH_ENEMY;
    return i;
}

static void spawn_from(const Wave* w, int n) {
    FIXED x = FX(w->x + n * w->dx), y = FX(cam_y + w->y);
    u32 i = spawn_enemy((EnemyKind)w->kind, x, y, C_VEL | (w->kind == ENEMY_SPINNER ? 0 : C_ANIM));
    if (i == MAX_ENT)
        return; // over the cap: this enemy is skipped
    path_start(i, &paths[w->path], w->flags & WAVE_MIRROR);
    if (w->flags & WAVE_FIRE)
        fire_at[i] = (u8)random_range(30, 70);
    if (n == w->count - 1)
        drops[i] = (w->flags & WAVE_POWER)  ? ITEM_POWER
                   : (w->flags & WAVE_BOMB) ? ITEM_BOMB
                                            : ITEM_NONE;
}

void enemies_spawn_waves(void) {
    while (next_wave < WAVE_COUNT && stage_frame >= waves[next_wave].time) {
        for (int k = 0; k < MAX_SPAWNERS; k++) {
            if (!spawners[k].wave) {
                spawners[k].wave = &waves[next_wave];
                spawners[k].spawned = 0;
                spawners[k].wait = 0;
                break;
            }
        }
        next_wave++;
    }
    for (int k = 0; k < MAX_SPAWNERS; k++) {
        const Wave* w = spawners[k].wave;
        if (!w || --spawners[k].wait > 0)
            continue;
        do {
            spawn_from(w, spawners[k].spawned++);
        } while (w->gap == 0 && spawners[k].spawned < w->count);
        spawners[k].wait = w->gap;
        if (spawners[k].spawned >= w->count)
            spawners[k].wave = 0;
    }
}

void spawn_turret(int x, int y) {
    u32 i = spawn_enemy(ENEMY_TURRET, FX(x), FX(y), C_GROUND);
    if (i < MAX_ENT) {
        spr_depth[i] = DEPTH_GROUND;
        timer[i] = (u16)random_range(0, 60); // turrets don't fire in step
    }
}

// --- Bullets ---------------------------------------------------------------------

// atan(i / 32) for i = 0 to 32, as u16 angles: for aiming without division
// beyond one ratio (the engine has no atan2).
static const u16 atan_table[33] = {0,    326,  651,  975,  1297, 1617, 1933, 2246, 2555,
                                   2860, 3159, 3453, 3742, 4025, 4302, 4572, 4836, 5094,
                                   5344, 5589, 5826, 6058, 6282, 6500, 6712, 6917, 7117,
                                   7310, 7498, 7679, 7856, 8026, 8192};

// The heading from (0, 0) toward (dx, dy), clockwise from right. Accurate to
// about a degree: the ratio of the shorter to the longer side, in 32nds, picks
// an entry of the table, and the signs pick the octant.
static u16 angle_toward(FIXED dx, FIXED dy) {
    s32 ax = int_abs(dx), ay = int_abs(dy);
    if (ax == 0 && ay == 0)
        return ANGLE_DEG(90);
    u16 a = ax >= ay ? atan_table[(ay * 32 + ax / 2) / ax]
                     : (u16)(ANGLE_DEG(90) - atan_table[(ax * 32 + ay / 2) / ay]);
    if (dx < 0)
        a = (u16)(ANGLE_DEG(180) - a);
    if (dy < 0)
        a = (u16)-a;
    return a;
}

u16 aim_at_player(FIXED x, FIXED y) {
    if (!player_alive)
        return ANGLE_DEG(90); // straight down
    return angle_toward(player_x() - x, player_y() - y);
}

bool fire_bullet(FIXED x, FIXED y, u16 angle, FIXED speed, u8 frame) {
    if (ebullet_count >= MAX_EBULLETS)
        return false; // the cap: patterns thin out instead of failing
    u32 i = spawn(C_EBULLET | C_VEL, SPR_BULLET, 4, 4, x, y);
    if (i == MAX_ENT)
        return false;
    ebullet_count++;
    vel_x[i] = fx_mul(fx_cos(angle), speed);
    vel_y[i] = fx_mul(fx_sin(angle), speed);
    spr_frame[i] = frame;
    spr_depth[i] = DEPTH_EBULLET;
    return true;
}

void fire_spread(FIXED x, FIXED y, u16 angle, int count, u16 step, FIXED speed, u8 frame) {
    u16 a = (u16)(angle - step * (count - 1) / 2);
    for (int k = 0; k < count; k++, a = (u16)(a + step))
        fire_bullet(x, y, a, speed, frame);
}

void fire_ring(FIXED x, FIXED y, int count, u16 offset, FIXED speed, u8 frame) {
    u16 step = (u16)(0x10000 / count);
    for (int k = 0; k < count; k++)
        fire_bullet(x, y, (u16)(offset + k * step), speed, frame);
}

void clear_bullets(bool score_them) {
    ECS_FOR_EACH(b, C_EBULLET) {
        if (score_them)
            add_score(10);
        entity_destroy(entity_at(b));
    }
}

// --- Effects and items -----------------------------------------------------------

// `booms` explosions around (x, y) and `sparks` sparks flying out: as many as
// the effect cap allows (an explosion amid many others gets fewer sparks).
void explode(FIXED x, FIXED y, int booms, int sparks) {
    for (int k = 0; k < booms && fx_count < MAX_FX; k++) {
        // Two statements, not two random_range() calls in one argument
        // list: C leaves the order of arguments open (GCC for the GBA and
        // clang for the web differ), and the same input must give the same
        // game on both.
        int spread = booms > 1 ? 10 : 0;
        FIXED bx = x + FX(random_range(-spread, spread));
        FIXED by = y + FX(random_range(-spread, spread));
        u32 i = spawn(C_FX | C_ANIM, SPR_BOOM, 2, 2, bx, by);
        if (i == MAX_ENT)
            return;
        fx_count++;
        timer[i] = BOOM_FRAMES;
        spr_depth[i] = DEPTH_FX;
    }
    for (int k = 0; k < sparks && fx_count < MAX_FX; k++) {
        u16 dir = (u16)random_u32();
        FIXED speed = FX_ONE / 2 + random_range(0, FX(2));
        u32 i = spawn(C_FX | C_ANIM | C_VEL, SPR_SPARK, 2, 2, x, y);
        if (i == MAX_ENT)
            return;
        fx_count++;
        vel_x[i] = fx_mul(fx_cos(dir), speed);
        vel_y[i] = fx_mul(fx_sin(dir), speed);
        timer[i] = SPARK_FRAMES;
        spr_depth[i] = DEPTH_FX;
    }
}

void spawn_item(FIXED x, FIXED y, ItemKind item) {
    if (item_count >= MAX_ITEMS)
        return;
    u32 i = spawn(C_ITEM | C_VEL | C_ANIM, item == ITEM_POWER ? SPR_ITEM_POWER : SPR_ITEM_BOMB, 14,
                  14, x, y);
    if (i == MAX_ENT)
        return;
    item_count++;
    kind[i] = (u8)item;
    vel_y[i] = FX_ONE / 2; // drifts down the screen
    timer[i] = 0;
    spr_depth[i] = DEPTH_ITEM;
}

// --- Damage --------------------------------------------------------------------

static void enemy_killed(u32 i) {
    EnemyKind k = (EnemyKind)kind[i];
    if (k == ENEMY_BOSS || k == ENEMY_POD) {
        boss_part_destroyed(i);
        return;
    }
    add_score(kinds[k].points);
    FIXED x = center_x(i), y = center_y(i);
    if (k == ENEMY_GUNSHIP) {
        explode(x, y, 4, 8);
        psg_play(SND_BOOM);
    } else {
        explode(x, y, 1, 4);
        psg_play(SND_POP);
    }
    if (k == ENEMY_TURRET)
        stage_destroy_pad(fx_to_int(x), fx_to_int(y));
    if (drops[i] != ITEM_NONE)
        spawn_item(x, y, (ItemKind)drops[i]);
    entity_destroy(entity_at(i));
}

void enemy_damage(u32 i, int damage) {
    if (hp[i] <= 0)
        return;
    hp[i] = (s16)(hp[i] - damage);
    flash[i] = 2;
    if (hp[i] <= 0)
        enemy_killed(i);
}

// --- Each frame ------------------------------------------------------------------

static bool on_screen(u32 i, int margin) {
    int sy = screen_y(i);
    return sy > margin && sy < FIELD_H - 40;
}

static void enemy_fire(u32 i) {
    FIXED x = center_x(i), y = center_y(i);
    u16 t = timer[i];
    switch ((EnemyKind)kind[i]) {
    case ENEMY_DART:
        if (fire_at[i] && t == fire_at[i] && on_screen(i, 0))
            fire_bullet(x, y, aim_at_player(x, y), FX(7) / 4, BULLET_PINK);
        break;
    case ENEMY_SPINNER:
        if (fire_at[i] && t % 90 == fire_at[i] && on_screen(i, 0))
            fire_spread(x, y, aim_at_player(x, y), 3, ANGLE_DEG(20), FX(5) / 4, BULLET_BLUE);
        break;
    case ENEMY_GUNSHIP:
        // While hovering: a ring and an aimed spread in turn.
        if (t >= 70 && t < 360 && t % 45 == 0) {
            if (t / 45 % 2)
                fire_ring(x, y + FX(8), 12, (u16)(t * 300), FX(5) / 4, BULLET_BLUE);
            else
                fire_spread(x, y + FX(8), aim_at_player(x, y), 5, ANGLE_DEG(14), FX(3) / 2,
                            BULLET_PINK);
        }
        break;
    case ENEMY_TURRET: {
        // Eye open for the half second before a two-shot burst.
        u16 phase = t % 100;
        spr_frame[i] = phase >= 70 || phase < 10 ? 0 : 1;
        if ((phase == 0 || phase == 8) && on_screen(i, 8) && player_alive)
            fire_bullet(x, y, aim_at_player(x, y), FX(3) / 2, BULLET_PINK);
        break;
    }
    default:
        break;
    }
}

void enemies_update(void) {
    spin = (u16)(spin + ANGLE_DEG(6));
    for (int k = 0; k < enemies.count; k++) {
        u32 i = enemies.slot[k];
        if (!ent_has(i, C_ENEMY))
            continue;
        timer[i]++;
        EnemyKind ek = (EnemyKind)kind[i];
        if (ek == ENEMY_BOSS || ek == ENEMY_POD)
            continue; // boss.c
        // The white "hit" twin for one frame after a hit (enemy_damage sets
        // flash to 2), so a target under steady fire flickers, not glows.
        if (flash[i] > 0)
            flash[i]--;
        spr_id[i] = flash[i] == 1 ? kinds[ek].flash_sprite : kinds[ek].sprite;
        if (ek == ENEMY_SPINNER)
            spr_angle[i] = spin;
        if (player_alive)
            enemy_fire(i);
    }
    for (int k = 0; k < items.count; k++) {
        // Items sway sideways as they drift down.
        u32 i = items.slot[k];
        timer[i]++;
        vel_x[i] = fx_sin((u16)(timer[i] * ANGLE_DEG(3))) / 2;
    }
    for (int k = 0; k < effects.count; k++) {
        u32 i = effects.slot[k];
        if (timer[i] == 0 || --timer[i] == 0)
            entity_destroy(entity_at(i));
    }
}

// The player's shots against everything they can hit.
static void collide_shots(void) {
    for (int a = 0; a < shots.count; a++) {
        u32 s = shots.slot[a];
        for (int b = 0; b < enemies.count; b++) {
            u32 e = enemies.slot[b];
            if (ent_has(e, C_ENEMY) && hp[e] > 0 && body_overlap(s, e)) {
                enemy_damage(e, kind[s]);
                entity_destroy(entity_at(s));
                break; // this shot is gone
            }
        }
    }
}

void enemies_collide(void) {
    collide_shots();
    if (!player_alive)
        return;
    for (int k = 0; k < items.count; k++) {
        u32 it = items.slot[k];
        if (ent_has(it, C_ITEM) && body_overlap(player, it)) {
            player_collect(it);
            entity_destroy(entity_at(it));
        }
    }
    if (invulnerable > 0)
        return;
    for (int k = 0; k < ebullets.count; k++) {
        u32 b = ebullets.slot[k];
        if (ent_has(b, C_EBULLET) && body_overlap(player, b)) {
            player_hit();
            return;
        }
    }
    // Flying enemies (not turrets on the ground below) hurt too.
    for (int k = 0; k < enemies.count; k++) {
        u32 e = enemies.slot[k];
        if (ent_has(e, C_ENEMY) && !ent_has(e, C_GROUND) && body_overlap(player, e)) {
            player_hit();
            return;
        }
    }
}

// Removes what has left the field. (Entities created this frame aren't in the
// lists yet: they are checked next frame.)
void enemies_cull(void) {
    for (int k = 0; k < shots.count; k++) {
        u32 i = shots.slot[k];
        if (ent_has(i, C_SHOT) && (screen_y(i) < -8 || pos_x[i] < FX(-8) || pos_x[i] > FX(FIELD_W)))
            entity_destroy(entity_at(i));
    }
    for (int k = 0; k < ebullets.count; k++) {
        u32 i = ebullets.slot[k];
        int sy = screen_y(i);
        if (ent_has(i, C_EBULLET) &&
            (sy < -8 || sy > FIELD_H || pos_x[i] < FX(-8) || pos_x[i] > FX(FIELD_W + 4)))
            entity_destroy(entity_at(i));
    }
    for (int k = 0; k < items.count; k++) {
        u32 i = items.slot[k];
        if (ent_has(i, C_ITEM) && screen_y(i) > FIELD_H)
            entity_destroy(entity_at(i));
    }
    for (int k = 0; k < enemies.count; k++) {
        u32 i = enemies.slot[k];
        if (!ent_has(i, C_ENEMY) || kind[i] == ENEMY_BOSS || kind[i] == ENEMY_POD)
            continue;
        int sy = screen_y(i), x = fx_to_int(pos_x[i]);
        bool gone = ent_has(i, C_GROUND) ? sy > FIELD_H + 8
                                         : timer[i] > 30 && (sy > FIELD_H + 24 || sy < -64 ||
                                                             x < -48 || x > FIELD_W + 48);
        if (gone)
            entity_destroy(entity_at(i));
    }
}
