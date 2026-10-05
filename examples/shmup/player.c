// The player's ship: movement, autofire at four power levels, focus, bombs,
// dying and respawning.

#include "game.h"

#define SPEED FX(2)       // pixels per frame
#define FOCUS_SPEED FX(1) // while R is held
#define DIAGONAL 181      // 1/sqrt(2) in 256ths: diagonals aren't faster
#define FIRE_EVERY 7      // frames between volleys while A is held
#define SHOT_SPEED FX(7)
#define HITBOX 4             // the ship's hitbox: 4 x 4 pixels in a 16 x 16 sprite
#define RESPAWN_DELAY 90     // frames without a ship after losing one
#define INVULNERABLE 120     // frames blinking after (re)spawning
#define BOMB_INVULNERABLE 90 // frames safe after a bomb
#define START_LIVES 3
#define START_BOMBS 3
#define MAX_BOMBS 5
#define BOMB_DAMAGE 24

u32 player;
int lives, bombs, power;
int invulnerable;
static int fire_cooldown;
static int respawn_timer;
int bomb_glow; // frames left of the bomb's white flash (game.c)

void player_reset(void) {
    lives = START_LIVES;
    bombs = START_BOMBS;
    power = 1;
    player_alive = false;
}

void player_spawn(void) {
    u32 i = spawn(C_PLAYER, SPR_SHIP, HITBOX, HITBOX, FX(FIELD_W / 2), FX(cam_y + FIELD_H - 24));
    if (i == MAX_ENT)
        return; // can't happen with the budget in game.h; tried again next frame
    player = i;
    spr_depth[i] = DEPTH_PLAYER;
    player_alive = true;
    invulnerable = INVULNERABLE;
    bombs = int_max(bombs, START_BOMBS);
    fire_cooldown = 0;
}

FIXED player_x(void) {
    return center_x(player);
}

FIXED player_y(void) {
    return center_y(player);
}

bool player_respawn_due(void) {
    return respawn_timer > 0 && --respawn_timer == 0;
}

// --- Shots -------------------------------------------------------------------

// A shot from (x, y) at `angle` degrees off straight up (positive: right),
// damage stored in kind[].
static void shot(FIXED x, FIXED y, int angle, u8 frame, u8 damage) {
    if (shot_count >= MAX_SHOTS)
        return; // the cap: side shots of a full volley are dropped first (fired last)
    u32 i = spawn(C_SHOT | C_VEL, SPR_SHOT, 6, 8, x, y);
    if (i == MAX_ENT)
        return;
    shot_count++;
    u16 a = (u16)(ANGLE_DEG(-90) + ANGLE_DEG(1) * angle);
    vel_x[i] = fx_mul(fx_cos(a), SHOT_SPEED);
    vel_y[i] = fx_mul(fx_sin(a), SHOT_SPEED);
    spr_frame[i] = frame;
    spr_depth[i] = DEPTH_SHOT;
    kind[i] = damage;
}

// One volley. The twin bolt goes first, so the cap drops side needles, not
// it. Focusing (R) turns the needles straight ahead, beside the bolt: less
// spread, more damage on one target.
static void fire(bool focus) {
    FIXED x = player_x(), y = player_y() - FX(8);
    if (power >= MAX_POWER)
        shot(x, y, 0, SHOT_FRAME_HEAVY, 3);
    else
        shot(x, y, 0, SHOT_FRAME_BOLT, 2);
    if (power >= 2) {
        if (focus) {
            shot(x - FX(7), y + FX(2), 0, SHOT_FRAME_NEEDLE, 1);
            shot(x + FX(7), y + FX(2), 0, SHOT_FRAME_NEEDLE, 1);
        } else {
            shot(x - FX(4), y + FX(2), -8, SHOT_FRAME_NEEDLE, 1);
            shot(x + FX(4), y + FX(2), 8, SHOT_FRAME_NEEDLE, 1);
        }
    }
    if (power >= 3) {
        if (focus) {
            shot(x - FX(12), y + FX(4), 0, SHOT_FRAME_NEEDLE, 1);
            shot(x + FX(12), y + FX(4), 0, SHOT_FRAME_NEEDLE, 1);
        } else {
            shot(x - FX(6), y + FX(4), -18, SHOT_FRAME_NEEDLE, 1);
            shot(x + FX(6), y + FX(4), 18, SHOT_FRAME_NEEDLE, 1);
        }
    }
    psg_play(SND_SHOT);
}

// --- Bomb --------------------------------------------------------------------

// Clears every enemy bullet (10 points each), hurts every enemy on screen,
// and keeps the ship safe for a moment, with a white flash and a roar.
static void bomb(void) {
    bombs--;
    bomb_glow = 16;
    invulnerable = int_max(invulnerable, BOMB_INVULNERABLE);
    clear_bullets(true);
    ECS_FOR_EACH(e, C_ENEMY) {
        int sy = screen_y(e);
        if (sy > -16 && sy < FIELD_H && kind[e] != ENEMY_BOSS && kind[e] != ENEMY_POD)
            enemy_damage(e, BOMB_DAMAGE);
    }
    boss_bomb();
    for (int k = 0; k < 6; k++) {
        int x = random_range(16, FIELD_W - 16); // one call per statement (see explode())
        int y = cam_y + random_range(16, FIELD_H - 16);
        explode(FX(x), FX(y), 1, 0);
    }
    psg_play(SND_BOMB);
}

// --- Each frame --------------------------------------------------------------

void player_update(void) {
    if (bomb_glow > 0)
        bomb_glow--;
    if (!player_alive)
        return;
    u32 i = player;
    bool focus = button_down(BUTTON_R);
    FIXED speed = focus ? FOCUS_SPEED : SPEED;
    int dx = button_down(BUTTON_RIGHT) - button_down(BUTTON_LEFT);
    int dy = button_down(BUTTON_DOWN) - button_down(BUTTON_UP);
    if (dx && dy)
        speed = speed * DIAGONAL / 256;
    // The ship stays inside the field, its hitbox at least 6 pixels from the
    // sides (its wings) and 8 from the top and bottom.
    FIXED x = pos_x[i] + dx * speed, y = pos_y[i] + dy * speed;
    pos_x[i] = int_clamp(x, FX(6 - HITBOX / 2), FX(FIELD_W - 6 - HITBOX / 2));
    pos_y[i] = int_clamp(y, FX(cam_y + 8), FX(cam_y + FIELD_H - 8 - HITBOX));

    // Banking: the banked frame, mirrored for the left. The flame flickers.
    u8 flame = (u8)(frame_count() / 3 % 2);
    spr_frame[i] = (u8)((dx ? SHIP_FRAME_BANK : SHIP_FRAME_LEVEL) + flame);
    u16 flags = dx < 0 ? SPRITE_FLIP_H : 0;
    // Blinking while invulnerable: hidden on alternate 4-frame stretches.
    if (invulnerable > 0) {
        invulnerable--;
        if ((invulnerable / 4) % 2)
            flags |= SPRITE_HIDDEN;
    }
    spr_flags[i] = flags;

    if (fire_cooldown > 0)
        fire_cooldown--;
    if (button_down(BUTTON_A) && fire_cooldown == 0) {
        fire(focus);
        fire_cooldown = FIRE_EVERY;
    }
    if (button_pressed(BUTTON_B) && bombs > 0)
        bomb();
}

// While focusing, the hitbox shows as a bright dot: drawn by hand before
// sys_render_by_depth, so it is in front of the ship.
void player_draw(void) {
    if (player_alive && button_down(BUTTON_R))
        sprite_draw(SPR_HITBOX, 0, fx_to_int(pos_x[player]), fx_to_int(pos_y[player]) - cam_y, 0);
}

void player_hit(void) {
    if (!player_alive || invulnerable > 0)
        return;
    explode(player_x(), player_y(), 3, 10);
    psg_play(SND_PLAYER_DIE);
    entity_destroy(entity_at(player));
    player_alive = false;
    respawn_timer = RESPAWN_DELAY;
    lives--;
    power = int_max(power - 1, 1);
    clear_bullets(false); // a fresh start for the next ship
}

void player_collect(u32 item) {
    if (kind[item] == ITEM_POWER) {
        if (power < MAX_POWER) {
            power++;
            psg_play(SND_ITEM);
        } else {
            add_score(2000);
            psg_play(SND_POWER_MAX);
        }
    } else {
        bombs = int_min(bombs + 1, MAX_BOMBS);
        psg_play(SND_ITEM);
    }
    add_score(100);
}
