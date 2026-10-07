// Enemies: the stage's wave table, what each kind does, enemy bullets,
// collisions, explosions and power-ups.

#include "game.h"

// --- Movement patterns (path.h) --------------------------------------------------

// Each step flies `frames` frames at `speed` pixels per frame, turning by
// `turn` every frame; a step without frames lasts forever (the enemy flies on
// until it leaves the field). sys_path() (game.c) turns them into velocity,
// mirrored left-right for waves from the other side.
#define DEG ANGLE_DEG(1)

static const PathStep down_steps[] = {{.speed = FX(3) / 2}};
static const PathStep down_fast_steps[] = {{.speed = FX(5) / 2}};
static const PathStep slow_steps[] = {{.speed = FX(3) / 4}};
// In, a half circle to the right and back up.
static const PathStep swoop_steps[] = {{.frames = 45, .speed = FX(2)},
                                       {.frames = 60, .turn = -3 * DEG, .speed = FX(2)},
                                       {.speed = FX(2)}};
// Down while weaving left and right: close to a sine wave.
static const PathStep weave_steps[] = {{.frames = 20, .turn = 2 * DEG, .speed = FX(1)},
                                       {.frames = 40, .turn = -2 * DEG, .speed = FX(1)},
                                       {.frames = 20, .turn = 2 * DEG, .speed = FX(1)}};
// From the left edge to the right, then curving down.
static const PathStep side_steps[] = {{.frames = 40, .speed = FX(2)},
                                      {.frames = 45, .turn = 2 * DEG, .speed = FX(2)},
                                      {.speed = FX(2)}};
// Down, a pause, then a fast dive.
static const PathStep dive_steps[] = {
    {.frames = 40, .speed = FX(3) / 2}, {.frames = 30, .speed = FX(1) / 4}, {.speed = FX(3)}};
// Down, hover a while, back up.
static const PathStep hover_steps[] = {
    {.frames = 60, .speed = FX(1)}, {.frames = 300}, {.speed = -FX(1) / 2}};
// A long arc across the field from the top left.
static const PathStep arc_steps[] = {{.frames = 240, .turn = -DEG / 2, .speed = FX(3) / 2},
                                     {.speed = FX(3) / 2}};
// Diagonal zigzags: straight runs with sharp 90-degree turns.
static const PathStep zigzag_steps[] = {{.frames = 36, .speed = FX(3) / 2},
                                        {.frames = 1, .turn = -90 * DEG, .speed = FX(3) / 2},
                                        {.frames = 36, .speed = FX(3) / 2},
                                        {.frames = 1, .turn = 90 * DEG, .speed = FX(3) / 2}};

enum { P_DOWN, P_DOWN_FAST, P_SLOW, P_SWOOP, P_WEAVE, P_SIDE, P_DIVE, P_HOVER, P_ARC, P_ZIGZAG };

static const Path paths[] = {
    [P_DOWN] = {PATH_STEPS(down_steps), .heading = ANGLE_DEG(90)},
    [P_DOWN_FAST] = {PATH_STEPS(down_fast_steps), .heading = ANGLE_DEG(90)},
    [P_SLOW] = {PATH_STEPS(slow_steps), .heading = ANGLE_DEG(90)},
    [P_SWOOP] = {PATH_STEPS(swoop_steps), .heading = ANGLE_DEG(90)},
    [P_WEAVE] = {PATH_STEPS(weave_steps), .loop = true, .heading = ANGLE_DEG(90)},
    [P_SIDE] = {PATH_STEPS(side_steps), .heading = ANGLE_DEG(0)},
    [P_DIVE] = {PATH_STEPS(dive_steps), .heading = ANGLE_DEG(90)},
    [P_HOVER] = {PATH_STEPS(hover_steps), .heading = ANGLE_DEG(90)},
    [P_ARC] = {PATH_STEPS(arc_steps), .heading = ANGLE_DEG(60)},
    [P_ZIGZAG] = {PATH_STEPS(zigzag_steps), .loop = true, .heading = ANGLE_DEG(45)},
};

// --- Enemy kinds ------------------------------------------------------------------

typedef struct {
    u16 sprite;
    u16 palette; // 0: the sprite's own; SPRITE_PALETTE(n): another of the group's
    u8 w, h;     // hitbox
    s16 hp;
    u16 points;
} KindInfo;

static const KindInfo kinds[] = {
    [ENEMY_DART] = {SPR_DART, 0, 12, 10, 1, 100},
    [ENEMY_CARRIER] = {SPR_DART, SPRITE_PALETTE(PAL_CARRIER), 12, 10, 8, 500},
    [ENEMY_SPINNER] = {SPR_SPINNER, 0, 12, 12, 4, 200},
    [ENEMY_GUNSHIP] = {SPR_GUNSHIP, 0, 26, 24, 40, 2000},
    [ENEMY_TURRET] = {SPR_TURRET, 0, 12, 12, 8, 300},
    [ENEMY_CANNON] = {SPR_CANNON, 0, 20, 20, 60, 3000},
    [ENEMY_BOSS] = {SPR_BOSS, 0, 48, 36, 0, 0}, // boss.c
    [ENEMY_POD] = {SPR_POD, 0, 20, 20, 0, 0},   // boss.c
};

static bool on_screen(u32 i, int margin);

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
    spr_flags[i] |= info->palette;
    return i;
}

static void spawn_from(const Wave* w, int n) {
    FIXED x = FX(w->x + n * w->dx), y = FX(w->y);
    u32 i = spawn_enemy((EnemyKind)w->kind, x, y, C_VEL | (w->kind == ENEMY_SPINNER ? 0 : C_ANIM));
    if (i == MAX_ENT)
        return; // over the cap: this enemy is skipped
    path_start(entity_at(i), &paths[w->path], w->flags & WAVE_MIRROR ? PATH_MIRROR_X : 0);
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

// --- Cannons --------------------------------------------------------------------

// Big turrets that turn to track the ship. Each has its own angle in
// spr_angle (so its own rotation matrix: the hardware has 32, and at most
// two cannons, one fort's, are on screen) and turns toward the ship by at
// most CANNON_TURN a frame, so a ship that keeps moving stays ahead of the
// barrels. Once aimed to within CANNON_AIM it fires three pairs of shots
// along the barrels, from the muzzles.
#define CANNON_TURN (ANGLE_DEG(3) / 2)
#define CANNON_AIM ANGLE_DEG(8)
#define CANNON_RELOAD 110 // frames from one burst to the next
#define CANNON_MUZZLE 39  // pixels from the cannon's center to its muzzles
#define DEBRIS_FRAMES 48  // a destroyed cannon's head spinning away

void spawn_cannon(int x, int y) {
    u32 i = spawn_enemy(ENEMY_CANNON, FX(x), FX(y), C_GROUND);
    if (i < MAX_ENT) {
        spr_depth[i] = DEPTH_GROUND;
        spr_angle[i] = ANGLE_DEG(90); // barrels down the screen, toward where the ship comes from
        timer[i] = (u16)random_range(1, 40);
    }
}

// Turns `from` toward `to` by at most `step`. Angles are u16 turns, so their
// difference as an s16 is the shorter way round (-180 to 180 degrees), with
// no special case where the angle wraps from 359 degrees to 0.
static u16 turn_toward(u16 from, u16 to, u16 step) {
    s16 diff = (s16)(u16)(to - from);
    if (diff > (s16)step)
        diff = (s16)step;
    else if (diff < -(s16)step)
        diff = (s16)-step;
    return (u16)(from + diff);
}

static void cannon_update(u32 i) {
    FIXED x = center_x(i), y = center_y(i);
    u16 aim = aim_at_player(x, y);
    spr_angle[i] = turn_toward(spr_angle[i], aim, CANNON_TURN);
    u16 phase = timer[i] % CANNON_RELOAD;
    bool aimed = int_abs((s16)(u16)(aim - spr_angle[i])) < CANNON_AIM;
    if (phase == 0 && (!aimed || !on_screen(i, 0) || !player_alive)) {
        timer[i]--; // holds the burst until it has a shot
        phase = CANNON_RELOAD - 1;
    }
    // Three pairs of shots, 12 frames apart, the muzzles flashing for 4.
    spr_frame[i] = phase < 28 && phase % 12 < 4;
    if (phase < 28 && phase % 12 == 0) {
        // One shot from each barrel: CANNON_MUZZLE ahead of the center and
        // 3 pixels to either side of the axis.
        FIXED dx = fx_cos(spr_angle[i]), dy = fx_sin(spr_angle[i]);
        FIXED mx = x + dx * CANNON_MUZZLE, my = y + dy * CANNON_MUZZLE;
        fire_bullet(mx - dy * 3, my + dx * 3, spr_angle[i], FX(2), BULLET_PINK);
        fire_bullet(mx + dy * 3, my - dx * 3, spr_angle[i], FX(2), BULLET_PINK);
    }
}

// The head of a destroyed cannon flies off spinning and shrinking: an effect
// on the screen, the cannon's sprite drawn with SPRITE_SCALED and spr_scale.
static void spawn_debris(u32 cannon) {
    if (fx_count >= MAX_FX)
        return;
    // The cannon's 20 x 20 box, which its sprite's origin is set for (it
    // hits nothing: effects aren't tested for collisions).
    u32 i = spawn(C_FX | C_VEL, SPR_CANNON, 20, 20, center_x(cannon), center_y(cannon));
    if (i == MAX_ENT)
        return;
    fx_count++;
    timer[i] = DEBRIS_FRAMES;
    spr_depth[i] = DEPTH_FX;
    spr_angle[i] = spr_angle[cannon];
    spr_flags[i] |= SPRITE_SCALED;
    spr_scale[i] = FX_ONE;
    vel_x[i] = random_range(-FX(1), FX(1));
    vel_y[i] = FX(1) / 2;
}

// Spins, and shrinks to nothing in steps of 1/16 of its size, so sizes
// repeat from frame to frame rather than each needing a new matrix.
static void debris_update(u32 i) {
    spr_angle[i] = (u16)(spr_angle[i] + ANGLE_DEG(14));
    spr_scale[i] = (s16)((timer[i] * 16 / DEBRIS_FRAMES + 1) * (FX_ONE / 16));
}

// --- Bullets ---------------------------------------------------------------------

u16 aim_at_player(FIXED x, FIXED y) {
    if (!player_alive)
        return ANGLE_DEG(90); // straight down
    return angle_of(player_x() - x, player_y() - y);
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
    if (k == ENEMY_GUNSHIP || k == ENEMY_CANNON) {
        explode(x, y, 4, 8);
        psg_play(SND_BOOM);
    } else {
        explode(x, y, 1, 4);
        psg_play(SND_POP);
    }
    if (k == ENEMY_TURRET)
        stage_destroy_pad(fx_to_int(x), fx_to_int(y) + cam_y); // on the map: world pixels
    if (k == ENEMY_CANNON) {
        stage_destroy_gun(fx_to_int(x), fx_to_int(y) + cam_y);
        spawn_debris(i);
    }
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
    case ENEMY_CANNON:
        cannon_update(i);
        break;
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
        // White (PAL_FLASH) for one frame after a hit (enemy_damage sets
        // flash to 2), so a target under steady fire flickers, not glows.
        if (flash[i] > 0)
            flash[i]--;
        u16 palette = flash[i] == 1 ? SPRITE_PALETTE(PAL_FLASH) : kinds[ek].palette;
        spr_flags[i] = (u16)((spr_flags[i] & ~SPRITE_PALETTE_MASK) | palette);
        if (ek == ENEMY_SPINNER)
            spr_angle[i] = spin;
        if (player_alive || ek == ENEMY_CANNON) // cannons keep turning
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
        else if (spr_id[i] == SPR_CANNON)
            debris_update(i);
    }
}

// The player's shots against everything they can hit.
static void collide_shots(void) {
    for (int a = 0; a < shots.count; a++) {
        u32 s = shots.slot[a];
        for (int b = 0; b < enemies.count; b++) {
            u32 e = enemies.slot[b];
            // A shot is on the screen (SPRITE_SCREEN) and a turret on the
            // map: body_overlap adds the camera to compare them.
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
