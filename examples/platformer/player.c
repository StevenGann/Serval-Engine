// The serval: running, jumping, growing and shrinking, dying.

#include "game.h"

// Hitboxes. The art is 16 pixels wide; the feet are at its bottom row.
#define PLAYER_W 12
#define SMALL_H 15
#define BIG_H 28

// Speeds in pixels per frame, accelerations in pixels per frame per frame.
#define WALK_MAX (FX_ONE * 3 / 2)
#define RUN_MAX (FX_ONE * 5 / 2) // with B held
#define WALK_ACCEL (FX_ONE / 18)
#define RUN_ACCEL (FX_ONE / 14)
#define AIR_ACCEL (FX_ONE / 20)
#define FRICTION (FX_ONE / 20)  // slowing down with no direction held
#define SKID_DECEL (FX_ONE / 7) // turning around while running
#define JUMP_SPEED FX(6)
#define RUN_JUMP_SPEED (FX(6) + FX_ONE / 2) // jumping at a run goes higher
// Releasing A while rising adds this much gravity: tapping A makes short hops.
#define JUMP_RELEASE_GRAVITY (FX_ONE / 2)
#define STOMP_BOUNCE FX(4)
#define STOMP_BOUNCE_HIGH FX(6) // A held while stomping
#define INVULNERABLE_FRAMES 120
#define TRANSFORM_FRAMES 48
#define DEATH_HOP_DELAY 30
#define DEATH_FRAMES 180

u32 player;
bool player_big;
PlayerMode player_mode;
int player_invulnerable;
bool player_hidden;

static int facing = 1; // 1 right, -1 left
static int input_dir;  // direction held this frame
static int run_phase;  // distance run, for the run animation
static int mode_timer;
static bool fell;

int player_center_x(void) {
    return fx_to_int(pos_x[player]) + PLAYER_W / 2;
}

// Makes the serval big or small: its hitbox and its sprite, keeping its feet
// where they are. (The sprites' origins line the art up with the hitbox.)
static void set_size(bool big) {
    u8 h = big ? BIG_H : SMALL_H;
    pos_y[player] += FX(body_h[player] - h);
    body_h[player] = h;
    spr_id[player] = big ? SPR_SERVAL_BIG : SPR_SERVAL_SMALL;
}

void player_spawn(int x, int y) {
    // The first entity after ecs_reset(), so sys_render draws it in front.
    Entity e = entity_create(C_POS | C_VEL | C_BODY | C_MAPBODY | C_SPR | C_PLAYER);
    player = entity_index(e);
    body_w[player] = PLAYER_W;
    body_max_fall[player] = MAX_FALL;
    pos_x[player] = FX(x);
    pos_y[player] = FX(y); // (x, y): the left end of the feet...
    set_size(player_big);  // ...and the hitbox goes up from there
    player_mode = PLAYER_NORMAL;
    player_invulnerable = 0;
    player_hidden = false;
    facing = 1;
}

// Moves v toward 0 by at most step.
static FIXED slow_down(FIXED v, FIXED step) {
    if (v > step)
        return v - step;
    if (v < -step)
        return v + step;
    return 0;
}

void player_control(void) {
    u32 i = player;
    bool on_floor = body_contact[i] & MAP_CONTACT_FLOOR;
    bool run = button_down(BUTTON_B);
    FIXED max = run ? RUN_MAX : WALK_MAX;
    FIXED v = vel_x[i];

    input_dir = 0;
    if (button_down(BUTTON_RIGHT) && !button_down(BUTTON_LEFT))
        input_dir = 1;
    else if (button_down(BUTTON_LEFT) && !button_down(BUTTON_RIGHT))
        input_dir = -1;

    if (input_dir != 0) {
        if (on_floor && v != 0 && (v > 0) != (input_dir > 0)) {
            v += input_dir * SKID_DECEL; // skidding
        } else {
            FIXED accel = !on_floor ? AIR_ACCEL : run ? RUN_ACCEL : WALK_ACCEL;
            FIXED faster = v + input_dir * accel;
            // Over the limit (B let go while running): ease down to it.
            if (int_abs(faster) > max)
                faster = int_abs(v) > max ? slow_down(v, FRICTION) : input_dir * max;
            v = faster;
        }
        if (on_floor)
            facing = input_dir;
    } else if (on_floor) {
        v = slow_down(v, FRICTION);
    }
    vel_x[i] = v;

    if (on_floor && button_pressed(BUTTON_A)) {
        vel_y[i] = int_abs(v) > WALK_MAX + FX_ONE / 4 ? -RUN_JUMP_SPEED : -JUMP_SPEED;
        psg_play(player_big ? SND_JUMP_BIG : SND_JUMP);
    }
    if (vel_y[i] < 0 && !button_down(BUTTON_A))
        vel_y[i] += JUMP_RELEASE_GRAVITY;
}

// The cell the serval's head hit: the one above the middle of the head, or
// if that is open (a hit on a block's corner), the one beside it.
static void hit_ceiling(u32 i) {
    int top = fx_to_int(pos_y[i]) - 1;
    int left = fx_to_int(pos_x[i]);
    int right = left + PLAYER_W - 1;
    int x = left + PLAYER_W / 2;
    if (MAP_TYPE(map_collision_at(x, top)) != MAP_SOLID)
        x = MAP_TYPE(map_collision_at(left, top)) == MAP_SOLID ? left : right;
    level_hit_block(x >> 4, top >> 4, player_big);
}

void player_after_move(void) {
    u32 i = player;
    if (body_contact[i] & MAP_CONTACT_CEILING)
        hit_ceiling(i);

    // The camera never goes back: neither does the serval past its left edge.
    if (pos_x[i] < FX(cam_x)) {
        pos_x[i] = FX(cam_x);
        vel_x[i] = int_max(vel_x[i], 0);
    }
    level_collect_gems(fx_to_int(pos_x[i]), fx_to_int(pos_y[i]), PLAYER_W, body_h[i]);
    if (player_invulnerable > 0)
        player_invulnerable--;
    if (fx_to_int(pos_y[i]) > LEVEL_PIXEL_H)
        player_die(true);
}

void player_auto_walk(void) {
    vel_x[player] = FX_ONE * 5 / 4;
    input_dir = facing = 1;
}

void player_grow(void) {
    if (player_big) {
        add_score(1000);
        psg_play(SND_ONE_UP);
        return;
    }
    player_big = true;
    set_size(true);
    player_mode = PLAYER_GROWING;
    mode_timer = TRANSFORM_FRAMES;
    add_score(1000);
    psg_play(SND_POWER_UP);
}

void player_hurt(void) {
    if (player_invulnerable > 0 || player_mode != PLAYER_NORMAL)
        return;
    if (!player_big) {
        player_die(false);
        return;
    }
    player_big = false;
    set_size(false);
    player_mode = PLAYER_SHRINKING;
    mode_timer = TRANSFORM_FRAMES;
    player_invulnerable = INVULNERABLE_FRAMES;
    psg_play(SND_SHRINK);
}

void player_die(bool fell_in_pit) {
    if (player_mode == PLAYER_DYING)
        return;
    player_mode = PLAYER_DYING;
    mode_timer = 0;
    fell = fell_in_pit;
    player_big = false;
    set_size(false);
    ent_mask[player] &= ~C_MAPBODY; // no more collisions: it falls off the screen
    vel_x[player] = vel_y[player] = 0;
    psg_music_stop();
    psg_play(SND_DEATH);
}

void player_bounce(void) {
    vel_y[player] = button_down(BUTTON_A) ? -STOMP_BOUNCE_HIGH : -STOMP_BOUNCE;
}

// Growing and shrinking freeze the game for a moment while the serval
// flickers between its sizes, ending on the new one; dying shows it hopping
// up and falling off the screen (unless it fell into a pit).
void player_update_mode(void) {
    u32 i = player;
    switch (player_mode) {
    case PLAYER_GROWING:
    case PLAYER_SHRINKING:
        set_size((mode_timer / 6) % 2 ? !player_big : player_big);
        if (--mode_timer == 0)
            player_mode = PLAYER_NORMAL;
        break;
    case PLAYER_DYING:
        mode_timer++;
        if (fell)
            break;
        if (mode_timer == DEATH_HOP_DELAY)
            vel_y[i] = -FX(5);
        if (mode_timer > DEATH_HOP_DELAY) {
            vel_y[i] = int_min(vel_y[i] + GRAVITY, FX(MAX_FALL));
            pos_y[i] += vel_y[i];
        }
        break;
    default:
        break;
    }
}

bool player_dying_done(void) {
    return player_mode == PLAYER_DYING && mode_timer >= (fell ? DEATH_FRAMES - 40 : DEATH_FRAMES);
}

// Picks the serval's frame and flips for sys_render. The frame follows what
// it is doing (standing, running, jumping, skidding), so it is chosen here
// rather than by sys_animate; the run cycle steps with the distance run.
void player_update_sprite(bool goal_slide) {
    u32 i = player;
    bool on_floor = body_contact[i] & MAP_CONTACT_FLOOR;
    int frame = SERVAL_STAND;
    u16 flags = facing < 0 ? SPRITE_FLIP_H : 0;
    if (player_mode == PLAYER_DYING) {
        frame = SERVAL_DEAD;
        flags = 0;
    } else if (goal_slide || !on_floor) {
        frame = SERVAL_JUMP;
    } else if (input_dir != 0 && vel_x[i] != 0 && (vel_x[i] > 0) != (input_dir > 0)) {
        frame = SERVAL_SKID; // the art faces back the other way
        flags = input_dir > 0 ? SPRITE_FLIP_H : 0;
    } else if (vel_x[i] != 0) {
        run_phase += int_abs(vel_x[i]);
        frame = (run_phase / FX(6)) % 2 ? SERVAL_RUN2 : SERVAL_RUN1;
    } else {
        run_phase = 0;
    }
    // Blinking after being hurt, and gone once inside the den.
    bool blink =
        player_invulnerable > 0 && player_mode == PLAYER_NORMAL && (player_invulnerable / 2) % 2;
    if (blink || player_hidden)
        flags |= SPRITE_HIDDEN;
    spr_frame[i] = (u8)frame;
    spr_flags[i] = flags;
}
