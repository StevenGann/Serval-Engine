// The castle's boss (stage 1-4): a dragon on a stone bridge over the lava,
// and the lever past it that drops the bridge.
//
// The dragon is one entity, a map body 28x36 standing on the bridge, drawn
// by sys_render as a metasprite (SPR_DRAGON: 48x48, nine hardware sprites):
// its wings, head and tail stick out of its body. It sleeps until the serval
// comes near, roars, then paces the bridge, now breathing a fireball at the
// serval, now crouching and hopping straight up. Touching it or its fire
// hurts. To get past it the serval jumps over it, or runs under it while it
// hops. The camera holds at the arena (camera_stop_x) with the lever at the
// screen's right edge. Walking into the lever pulls it: the backdrop flashes,
// the dragon flashes white (another palette of its group, SPRITE_PALETTE)
// and freezes, and the bridge falls one metatile at a time (map_set_cell)
// from the lever's end. The dragon falls into the lava and sinks out of
// sight, drawn behind the playfield. Then the serval walks out through the
// gate (goal_start_exit).

#include "stage_castle.h"

#define DRAGON_W 28
#define DRAGON_H 36
#define WAKE_DISTANCE 168 // pixels between the serval's middle and the dragon's
#define PACE_SPEED (FX_ONE / 2)
#define PACE_MIN 60 // frames of pacing between attacks (random)
#define PACE_MAX 110
#define ROAR_FRAMES 64
#define WINDUP_FRAMES 26 // head thrown back: a fireball is coming
#define BREATHE_FRAMES 22
#define CROUCH_FRAMES 18 // crouching: a hop is coming
#define LAND_FRAMES 10
#define HOP_SPEED (FX(5) + FX_ONE * 3 / 8) // rises about 58 pixels, 43 frames in the air
#define BREATH_SPEED (FX(7) / 4)
#define BREATH_W 10
#define BREATH_H 8
#define BREATH_AIM ANGLE_DEG(30) // fireballs fly at most this far from level
// Where the mouth is, from the pivot (the picture's middle), facing left.
#define MOUTH_X (-20)
#define MOUTH_Y (-6)
// The lever and the bridge.
#define FLASH_FRAMES 36
#define COLLAPSE_DELAY 30 // frames from the lever to the first piece falling
#define COLLAPSE_STEP 5   // frames between pieces
#define SINK_SPEED (FX_ONE / 2)
#define EXIT_DELAY 70 // frames after the dragon is gone, before the serval leaves

typedef enum {
    ASLEEP,
    ROARING,
    PACING,
    WINDUP,
    BREATHING,
    CROUCHING,
    HOPPING,
    LANDING,
    DOOMED, // the lever is pulled: it stands, flashing, until the bridge goes
    FALLING,
    SINKING,
    GONE
} DragonState;

// In EWRAM: IWRAM is nearly full in debug builds.
static struct {
    u32 slot;        // its entity, or MAX_ENT before it spawns
    u8 state;        // DragonState
    s8 facing;       // -1 left (as drawn), 1 right
    s16 timer;       // frames left in this state
    int target_x;    // pacing toward this middle x
    int left, right; // the middles it paces between
    int lava_y;      // world y of the lava's surface under the bridge
    bool lever_pulled;
    int flash;       // frames of the lever's flash left
    int collapse_mx; // the next bridge column to fall (below the first: done)
    int collapse_timer;
    int exit_timer;
} boss SERVAL_EWRAM_BSS;

static int center_x(u32 i) {
    return fx_to_int(pos_x[i]) + body_w[i] / 2;
}

void boss_reset(void) {
    boss.slot = MAX_ENT;
    boss.state = ASLEEP;
    boss.lever_pulled = false;
    boss.flash = 0;
    boss.collapse_mx = -1;
    boss.exit_timer = 0;
    // The arena: the screen ending two metatiles past the lever.
    if (lever_mx >= 0)
        camera_stop_x = (lever_mx + 2) * TILE - SCREEN_W;
}

void boss_spawn(const Spawn* s) {
    // Standing on the bridge, its middle at the right edge of its cell.
    u32 i = object_create(C_VEL | C_BODY | C_MAPBODY | C_DRAGON, SPR_DRAGON,
                          (s->mx + 1) * TILE - DRAGON_W / 2, (s->my + 1) * TILE - DRAGON_H);
    if (i == MAX_ENT)
        return;
    body_w[i] = DRAGON_W;
    body_h[i] = DRAGON_H;
    body_max_fall[i] = MAX_FALL;
    boss.slot = i;
    boss.state = ASLEEP;
    boss.facing = -1;
    // It paces the bridge's right part, clear of both its ends.
    boss.left = (bridge_first_mx + 4) * TILE;
    boss.right = (bridge_last_mx - 1) * TILE;
    boss.lava_y = (bridge_my + 1) * TILE + TILE / 2;
}

// --- Fireballs -------------------------------------------------------------------

// A fireball from the mouth, aimed at the serval but never steeper than
// BREATH_AIM.
static void breathe(u32 i) {
    int pivot_x = fx_to_int(pos_x[i]) + DRAGON_W / 2;
    int pivot_y = fx_to_int(pos_y[i]) + DRAGON_H - 24;
    int mx = boss.facing < 0 ? pivot_x + MOUTH_X : pivot_x - MOUTH_X;
    int my = pivot_y + MOUTH_Y;
    int tx = player_center_x(), ty = fx_to_int(pos_y[player]) + body_h[player] / 2;
    u16 level = boss.facing < 0 ? ANGLE_DEG(180) : 0;
    s16 off = (s16)(angle_of(FX(tx - mx), FX(ty - my)) - level);
    u16 aim = (u16)(level + int_clamp(off, -BREATH_AIM, BREATH_AIM));
    u32 b = object_create(C_VEL | C_BODY | C_ANIM | C_BREATH, SPR_BREATH, mx - BREATH_W / 2,
                          my - BREATH_H / 2);
    if (b == MAX_ENT)
        return;
    body_w[b] = BREATH_W;
    body_h[b] = BREATH_H;
    body_gravity[b] = BODY_GRAVITY(0); // it flies straight: sys_physics leaves it be
    vel_x[b] = fx_mul(fx_cos(aim), BREATH_SPEED);
    vel_y[b] = fx_mul(fx_sin(aim), BREATH_SPEED);
    spr_flags[b] = vel_x[b] > 0 ? SPRITE_FLIP_H : 0; // the art flies left
    psg_play(SND_BREATH);
}

static void douse_fireballs(void) {
    ECS_FOR_EACH(b, C_BREATH) {
        spawn_sparkle(center_x(b) - 4, fx_to_int(pos_y[b]));
        entity_destroy(entity_at(b));
    }
}

// --- The dragon ----------------------------------------------------------------

static void set_state(DragonState s, int frames) {
    boss.state = (u8)s;
    boss.timer = (s16)frames;
}

static void start_pacing(u32 i) {
    // Somewhere else on its stretch of the bridge, at least two metatiles away.
    int x = center_x(i);
    do
        boss.target_x = random_range(boss.left, boss.right);
    while (int_abs(boss.target_x - x) < 2 * TILE && boss.right - boss.left > 4 * TILE);
    set_state(PACING, random_range(PACE_MIN, PACE_MAX));
}

// After pacing: a fireball, or a hop (more likely with the serval close).
static void attack(u32 i) {
    bool close = int_abs(player_center_x() - center_x(i)) < 5 * TILE;
    if (random_range(0, 99) < (close ? 50 : 30))
        set_state(CROUCHING, CROUCH_FRAMES);
    else
        set_state(WINDUP, WINDUP_FRAMES);
}

static void update_dragon(u32 i) {
    int x = center_x(i);
    bool on_floor = body_contact[i] & MAP_CONTACT_FLOOR;
    if (boss.state < DOOMED && boss.state != HOPPING)
        boss.facing = (s8)(player_center_x() < x ? -1 : 1); // it watches the serval
    if (boss.state != HOPPING && boss.state != FALLING && boss.state != SINKING)
        vel_x[i] = 0;
    u8 frame = DRAGON_STAND;
    switch (boss.state) {
    case ASLEEP:
        if (int_abs(player_center_x() - x) < WAKE_DISTANCE || cam_x >= camera_stop_x) {
            set_state(ROARING, ROAR_FRAMES);
            psg_play(SND_ROAR);
            psg_play(SND_STING);
        }
        break;
    case ROARING:
        frame = DRAGON_ROAR;
        if (--boss.timer <= 0)
            start_pacing(i);
        break;
    case PACING: {
        int dx = boss.target_x - x;
        if (int_abs(dx) > 1)
            vel_x[i] = dx < 0 ? -PACE_SPEED : PACE_SPEED;
        frame = (frame_count() / 10) % 2 ? DRAGON_WALK2 : DRAGON_WALK1;
        if (int_abs(dx) <= 1)
            frame = DRAGON_STAND;
        if (--boss.timer <= 0)
            attack(i);
        break;
    }
    case WINDUP:
        frame = DRAGON_ROAR;
        if (--boss.timer <= 0) {
            breathe(i);
            set_state(BREATHING, BREATHE_FRAMES);
        }
        break;
    case BREATHING:
        frame = DRAGON_BREATHE;
        if (--boss.timer <= 0)
            start_pacing(i);
        break;
    case CROUCHING:
        frame = DRAGON_CROUCH;
        if (--boss.timer <= 0 && on_floor) {
            vel_y[i] = -HOP_SPEED; // straight up: the serval can run under it
            set_state(HOPPING, 0);
        }
        break;
    case HOPPING:
        frame = DRAGON_HOP;
        break;
    case LANDING:
        frame = DRAGON_CROUCH;
        if (--boss.timer <= 0)
            start_pacing(i);
        break;
    case DOOMED:
        frame = DRAGON_HURT;
        if (!on_floor && vel_y[i] > 0)
            set_state(FALLING, 0); // the bridge has gone from under it
        break;
    case FALLING:
        frame = DRAGON_HURT;
        if (fx_to_int(pos_y[i]) + DRAGON_H >= boss.lava_y) {
            // Into the lava: it sinks out of sight, behind the playfield,
            // through the lava's floor (no longer a map body).
            ent_mask[i] &= ~C_MAPBODY;
            body_gravity[i] = BODY_GRAVITY(0);
            vel_x[i] = 0;
            vel_y[i] = SINK_SPEED;
            spawn_sparkle(x - 16, boss.lava_y - 8);
            spawn_sparkle(x + 8, boss.lava_y - 10);
            psg_play(SND_SPLASH);
            set_state(SINKING, 0);
        }
        break;
    case SINKING:
        frame = DRAGON_HURT;
        if (fx_to_int(pos_y[i]) - 12 > boss.lava_y) { // its picture's top, under the surface
            entity_destroy(entity_at(i));
            boss.slot = MAX_ENT;
            set_state(GONE, 0);
            boss.exit_timer = EXIT_DELAY;
            return;
        }
        break;
    }
    spr_frame[i] = frame;
    u16 flags = boss.facing > 0 ? SPRITE_FLIP_H : 0;
    // Struck by the lever: it flashes white (its group's flash palette).
    if (boss.state >= DOOMED && (frame_count() / 4) % 2)
        flags |= SPRITE_PALETTE(CS_PAL_DRAGON_FLASH);
    // In the lava: drawn behind the playfield, so the lava hides it.
    if (boss.state == SINKING)
        flags |= SPRITE_BEHIND_PLAYFIELD;
    spr_flags[i] = flags;
}

// --- The lever and the bridge ---------------------------------------------------

static void pull_lever(void) {
    boss.lever_pulled = true;
    map_set_cell(lever_mx, lever_my, MT_LEVER_PULLED);
    psg_music_stop();
    psg_play(SND_LEVER);
    boss.flash = FLASH_FRAMES;
    boss.collapse_mx = bridge_last_mx;
    boss.collapse_timer = COLLAPSE_DELAY;
    douse_fireballs();
    if (boss.slot != MAX_ENT && boss.state < DOOMED) {
        u32 i = boss.slot;
        vel_x[i] = 0;
        if (vel_y[i] < 0)
            vel_y[i] = 0; // a hop ends at once
        boss.state = DOOMED;
        spr_flags[i] &= ~SPRITE_PALETTE_MASK;
    }
}

// The bridge falls from the lever's end, a metatile every few frames, each in
// four tumbling pieces. 12 cells of map_set_cell's 64 (MAP_MAX_CHANGES).
static void collapse_bridge(void) {
    if (boss.collapse_mx < bridge_first_mx || --boss.collapse_timer > 0)
        return;
    map_set_cell(boss.collapse_mx, bridge_my, MT_EMPTY);
    spawn_debris(boss.collapse_mx, bridge_my);
    psg_play(SND_CRUMBLE);
    boss.collapse_mx--;
    boss.collapse_timer = COLLAPSE_STEP;
}

void boss_update(void) {
    if (!boss.lever_pulled && lever_mx >= 0 && player_center_x() >= lever_mx * TILE + 4)
        pull_lever();
    if (boss.slot != MAX_ENT)
        update_dragon(boss.slot);
    if (boss.lever_pulled) {
        collapse_bridge();
        // Once the dragon is gone and the bridge down, out through the gate.
        if (boss.state == GONE && boss.collapse_mx < bridge_first_mx && --boss.exit_timer <= 0) {
            camera_stop_x = level_pixel_w;
            goal_start_exit();
        }
    }
}

void boss_after_move(void) {
    // Fireballs: they hurt, and are gone once off the screen.
    ECS_FOR_EACH(b, C_BREATH) {
        int x = fx_to_int(pos_x[b]), y = fx_to_int(pos_y[b]);
        if (x < cam_x - TILE || x > cam_x + SCREEN_W || y < cam_y - TILE || y > cam_y + SCREEN_H) {
            entity_destroy(entity_at(b));
            continue;
        }
        if (body_overlap(player, b))
            player_hurt();
    }
    if (boss.slot == MAX_ENT)
        return;
    u32 i = boss.slot;
    if (boss.state < DOOMED) {
        // It keeps to its stretch of the bridge.
        int x = center_x(i);
        if (x < boss.left || x > boss.right) {
            pos_x[i] = FX(int_clamp(x, boss.left, boss.right) - DRAGON_W / 2);
            boss.target_x = int_clamp(boss.target_x, boss.left, boss.right);
        }
        if (boss.state == HOPPING && (body_contact[i] & MAP_CONTACT_FLOOR)) {
            set_state(LANDING, LAND_FRAMES);
            psg_play(SND_HOP);
        }
        if (body_overlap(player, i))
            player_hurt();
    }
}

void boss_effects(Color* backdrop) {
    if (boss.flash > 0) {
        *backdrop = color_mix(*backdrop, COLOR_RGB(255, 236, 200), (u32)(boss.flash * 6));
        boss.flash--;
    }
}

bool boss_holds_serval(void) {
    return boss.lever_pulled;
}
