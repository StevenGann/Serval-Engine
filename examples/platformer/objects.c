// Everything in the level besides the serval: enemies, the fish, gems
// popping out of blocks, bouncing blocks, brick debris and sparkles.

#include "game.h"

#define C_SQUASHED C_GAME(7) // a stomped enemy, flat for a moment
#define C_KNOCKED C_GAME(8)  // an enemy knocked out from below, on its back

#define BEETLE_W 12
#define BEETLE_H 12
#define FROG_W 12
#define FROG_H 14
#define FISH_W 12
#define FISH_H 10

#define BEETLE_SPEED (FX_ONE / 2)
#define FROG_HOP_SPEED FX(4)
#define FROG_HOP_SIDE FX(1)
#define FROG_SIT_MIN 50 // frames a frog sits between hops: random
#define FROG_SIT_MAX 90
#define FISH_SPEED FX(1)
#define FISH_RISE_FRAMES 32
#define SQUASHED_FRAMES 30
#define KNOCKED_FRAMES 70
#define GEM_POP_FRAMES 26
#define SPARKLE_FRAMES 16
#define DESPAWN_BEHIND 48 // pixels left of the screen

// Per-entity game data, beside the engine's component pools.
static u8 kind[MAX_ENT];      // C_ENEMY: SpawnKind
static s8 dir[MAX_ENT];       // C_ENEMY, C_FISH: -1 left, 1 right
static s16 timer[MAX_ENT];    // frames: until a frog hops, an effect ends...
static u8 cell_x[MAX_ENT];    // C_BUMP: the block's metatile...
static u8 cell_y[MAX_ENT];    //
static u16 final_mt[MAX_ENT]; // ...and the metatile it shows once it has bounced

static int next_spawn;  // index in spawns[] of the next enemy to create
static int stomp_combo; // stomps in a row without landing: 100, 200, 400... points

static u32 create(u32 components, u16 sprite, int x, int y) {
    Entity e = entity_create(C_POS | C_SPR | components);
    if (e == ENTITY_NONE)
        return MAX_ENT; // pool full: the object is skipped (never happens in this level)
    u32 i = entity_index(e);
    pos_x[i] = FX(x);
    pos_y[i] = FX(y);
    spr_id[i] = sprite;
    return i;
}

void objects_reset(int first_mx) {
    next_spawn = 0;
    while (next_spawn < spawn_count && spawns[next_spawn].mx < first_mx)
        next_spawn++;
    stomp_combo = 0;
}

// Enemies are created when they come within a metatile of the screen's right
// edge, so they don't use entities (or CPU time) before they are needed.
void objects_spawn_ahead(void) {
    while (next_spawn < spawn_count && spawns[next_spawn].mx * TILE < cam_x + SCREEN_W + TILE) {
        const Spawn* s = &spawns[next_spawn++];
        bool frog = s->kind == SPAWN_FROG;
        int w = frog ? FROG_W : BEETLE_W, h = frog ? FROG_H : BEETLE_H;
        u32 i = create(C_VEL | C_BODY | C_MAPBODY | C_ENEMY | (frog ? 0 : C_ANIM),
                       frog ? SPR_FROG : SPR_BEETLE, s->mx * TILE + (TILE - w) / 2,
                       (s->my + 1) * TILE - h);
        if (i == MAX_ENT)
            continue;
        body_w[i] = (u8)w;
        body_h[i] = (u8)h;
        body_max_fall[i] = MAX_FALL;
        kind[i] = s->kind;
        dir[i] = -1;                          // toward the serval
        timer[i] = (s16)random_range(20, 60); // when a frog first hops
    }
}

static int center_x(u32 i) {
    return fx_to_int(pos_x[i]) + body_w[i] / 2;
}

void objects_update(void) {
    ECS_FOR_EACH(i, C_ENEMY) {
        if (kind[i] == SPAWN_BEETLE) {
            vel_x[i] = dir[i] * BEETLE_SPEED;
        } else if (body_contact[i] & MAP_CONTACT_FLOOR) {
            // A frog sits, then hops toward the serval.
            vel_x[i] = 0;
            if (--timer[i] <= 0) {
                dir[i] = (s8)(player_center_x() < center_x(i) ? -1 : 1);
                vel_x[i] = dir[i] * FROG_HOP_SIDE;
                vel_y[i] = -FROG_HOP_SPEED;
                timer[i] = (s16)random_range(FROG_SIT_MIN, FROG_SIT_MAX);
            }
        }
    }
    ECS_FOR_EACH(i, C_FISH | C_MAPBODY) {
        vel_x[i] = dir[i] * FISH_SPEED;
    }
}

// Knocks an enemy out: upside down, it hops up, bounces on the ground, slides
// to a stop and vanishes a moment later.
static void knock(u32 i) {
    ent_mask[i] &= ~(C_ENEMY | C_ANIM);
    ent_mask[i] |= C_KNOCKED;
    spr_flags[i] |= SPRITE_FLIP_V;
    vel_y[i] = -FX(3);
    vel_x[i] = dir[i] * (FX_ONE / 2);
    body_bounce[i] = 140;  // keeps 55% of its speed off the ground and walls
    body_friction[i] = 24; // loses ~10% of it per frame sliding
    timer[i] = KNOCKED_FRAMES;
    add_score(100);
    psg_play(SND_KICK);
}

void knock_enemies_on(int mx, int my) {
    ECS_FOR_EACH(i, C_ENEMY) {
        int left = fx_to_int(pos_x[i]), bottom = fx_to_int(pos_y[i]) + body_h[i];
        if (int_abs(bottom - my * TILE) <= 2 && left + body_w[i] > mx * TILE &&
            left < (mx + 1) * TILE)
            knock(i);
    }
}

static void squash(u32 i) {
    ent_mask[i] &= ~(C_ENEMY | C_MAPBODY | C_VEL | C_ANIM);
    ent_mask[i] |= C_SQUASHED;
    if (kind[i] == SPAWN_BEETLE) {
        spr_id[i] = SPR_BEETLE_FLAT; // a frog just vanishes
        spr_frame[i] = 0;
    }
    timer[i] = kind[i] == SPAWN_BEETLE ? SQUASHED_FRAMES : 1;
    add_score(100 << int_min(stomp_combo, 5));
    if (++stomp_combo == 8)
        add_life();
    psg_play(SND_STOMP);
    if (kind[i] == SPAWN_FROG)
        spawn_sparkle(center_x(i) - 4, fx_to_int(pos_y[i]));
}

// Beetles walking into each other both turn around.
static void turn_at_each_other(void) {
    ECS_FOR_EACH(a, C_ENEMY) {
        for (u32 b = a + 1; b < MAX_ENT; b++) {
            if (!ent_has(b, C_ENEMY) || !body_overlap(a, b))
                continue;
            if ((center_x(a) < center_x(b)) == (dir[a] > 0)) {
                dir[a] = (s8)-dir[a];
                dir[b] = (s8)-dir[b];
            }
        }
    }
}

static void touch_player(void) {
    if (player_mode != PLAYER_NORMAL)
        return;
    if (body_contact[player] & MAP_CONTACT_FLOOR)
        stomp_combo = 0;
    ECS_FOR_EACH(i, C_ENEMY) {
        u32 side = body_hit_side(player, i); // 0: not touching
        if (side == BODY_SIDE_BOTTOM) {      // the serval came down on it
            squash(i);
            player_bounce();
        } else if (side) {
            player_hurt();
        }
        if (player_mode != PLAYER_NORMAL)
            return;
    }
    ECS_FOR_EACH(i, C_FISH) {
        if (body_overlap(player, i)) {
            entity_destroy(entity_at(i));
            player_grow();
            return;
        }
    }
}

// The y offsets of a bouncing block, frame by frame.
static const s8 bump_offsets[] = {-3, -5, -6, -6, -5, -3, -1, 0};
#define BUMP_FRAMES ((int)sizeof bump_offsets)

void objects_after_move(void) {
    int despawn_x = cam_x - DESPAWN_BEHIND;
    ECS_FOR_EACH(i, C_ENEMY | C_MAPBODY) {
        u8 c = body_contact[i];
        if ((dir[i] < 0 && (c & MAP_CONTACT_LEFT)) || (dir[i] > 0 && (c & MAP_CONTACT_RIGHT)))
            dir[i] = (s8)-dir[i];
        spr_flags[i] = dir[i] < 0 ? SPRITE_FLIP_H : 0; // the art faces right
        if (kind[i] == SPAWN_FROG)
            spr_frame[i] = (c & MAP_CONTACT_FLOOR) ? 0 : 1; // sitting or hopping
    }
    turn_at_each_other();
    touch_player();

    ECS_FOR_EACH(i, C_FISH) {
        if (ent_mask[i] & C_MAPBODY) {
            u8 c = body_contact[i];
            if ((dir[i] < 0 && (c & MAP_CONTACT_LEFT)) || (dir[i] > 0 && (c & MAP_CONTACT_RIGHT)))
                dir[i] = (s8)-dir[i];
            spr_flags[i] = dir[i] < 0 ? SPRITE_FLIP_H : 0;
        } else if (--timer[i] == 0) {
            // Out of the block: from now on it slides along the map. Placed
            // exactly on top: a map body overlapping a solid metatile could
            // fall through it.
            ent_mask[i] |= C_BODY | C_MAPBODY;
            body_max_fall[i] = MAX_FALL;
            pos_y[i] = FX(cell_y[i] * TILE - FISH_H);
            vel_y[i] = 0;
            spr_flags[i] = 0;
        }
    }
    ECS_FOR_EACH(i, C_SQUASHED) {
        if (--timer[i] <= 0)
            entity_destroy(entity_at(i));
    }
    ECS_FOR_EACH(i, C_KNOCKED) {
        if (--timer[i] <= 0) {
            spawn_sparkle(center_x(i) - 4, fx_to_int(pos_y[i]));
            entity_destroy(entity_at(i));
        }
    }
    ECS_FOR_EACH(i, C_GEM_POP) {
        if (--timer[i] == 0) {
            spawn_sparkle(fx_to_int(pos_x[i]) + 4, fx_to_int(pos_y[i]) + 4);
            entity_destroy(entity_at(i));
        }
    }
    ECS_FOR_EACH(i, C_BUMP) {
        int t = BUMP_FRAMES - timer[i]--;
        pos_y[i] = FX(cell_y[i] * TILE + bump_offsets[t]);
        if (timer[i] == 0) {
            level_bump_done(cell_x[i], cell_y[i], final_mt[i]);
            entity_destroy(entity_at(i));
        }
    }
    ECS_FOR_EACH(i, C_SPARKLE) {
        if (--timer[i] <= 0)
            entity_destroy(entity_at(i));
    }

    // Gone: far behind the camera, or fallen out of the level.
    for (u32 i = 0; i < MAX_ENT; i++) {
        if (!ent_has(i, C_POS) || i == player || ent_has(i, C_BUMP))
            continue;
        int x = fx_to_int(pos_x[i]), y = fx_to_int(pos_y[i]);
        if (x < despawn_x || y > LEVEL_PIXEL_H + TILE)
            entity_destroy(entity_at(i));
    }
}

// A gem flies up out of the block, spinning, and vanishes as it falls back.
void spawn_gem_pop(int mx, int my) {
    u32 i = create(C_VEL | C_BODY | C_MAPBODY | C_ANIM | C_GEM_POP, SPR_GEM, mx * TILE,
                   my * TILE - TILE);
    if (i < MAX_ENT) {
        body_w[i] = body_h[i] = TILE;
        body_bounce[i] = 128; // off a block above it
        body_max_fall[i] = MAX_FALL;
        vel_y[i] = -FX(7) / 2;
        timer[i] = GEM_POP_FRAMES;
    }
    add_gem();
}

void spawn_fish(int mx, int my) {
    // Rises out of the block, behind the playfield, so the block hides it at first.
    u32 i =
        create(C_VEL | C_FISH, SPR_FISH, mx * TILE + (TILE - FISH_W) / 2, (my + 1) * TILE - FISH_H);
    if (i < MAX_ENT) {
        body_w[i] = FISH_W;
        body_h[i] = FISH_H;
        vel_y[i] = -FX(TILE) / FISH_RISE_FRAMES;
        dir[i] = 1;
        cell_y[i] = (u8)my;
        timer[i] = FISH_RISE_FRAMES;
        spr_flags[i] = SPRITE_BEHIND_PLAYFIELD;
    }
}

void spawn_bump(int mx, int my, u8 frame, u16 final_metatile) {
    u32 i = create(C_BUMP, SPR_BLOCK, mx * TILE, my * TILE);
    if (i == MAX_ENT) {
        level_bump_done(mx, my, final_metatile);
        return;
    }
    spr_frame[i] = frame;
    cell_x[i] = (u8)mx;
    cell_y[i] = (u8)my;
    final_mt[i] = final_metatile;
    timer[i] = BUMP_FRAMES;
    map_set_cell(mx, my, MT_HIDDEN); // solid, but drawn by the sprite while it bounces
}

// Four pieces of a broken brick fly apart, tumbling, and fall through the
// level: plain physics bodies (sys_physics, with every edge of its bounds
// open), not map bodies.
void spawn_debris(int mx, int my) {
    static const s8 vx[4] = {-1, 1, -1, 1}, vy[4] = {-6, -6, -4, -4};
    for (int k = 0; k < 4; k++) {
        u32 i = create(C_VEL | C_BODY | C_ANIM | C_DEBRIS, SPR_DEBRIS, mx * TILE + (k % 2) * 8,
                       my * TILE + (k / 2) * 8);
        if (i < MAX_ENT) {
            body_w[i] = body_h[i] = 8;
            body_max_fall[i] = MAX_FALL;
            vel_x[i] = vx[k] * FX_ONE;
            vel_y[i] = vy[k] * FX_ONE;
            spr_anim_step[i] = (u8)k; // each piece starts at a different turn
        }
    }
}

void spawn_sparkle(int x, int y) {
    u32 i = create(C_ANIM | C_SPARKLE, SPR_SPARKLE, x, y);
    if (i < MAX_ENT)
        timer[i] = SPARKLE_FRAMES;
}
