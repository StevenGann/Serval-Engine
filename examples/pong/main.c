// pong: single-player Pong against a simple computer opponent.
//
// Demonstrates:
//   - A complete small game: title screen, serve, play, pause, game over
//   - Engine physics for the ball (sys_physics with open left and right edges,
//     so it bounces off the top and bottom but scores by leaving the screen)
//   - Collisions with body_overlap: paddles are bodies without velocity, which
//     the game moves itself
//   - Several sprites sharing tiles with different palettes (the paddles)
//   - A simple AI, fixed-point math (fx_mul) and int_clamp
//   - Seeding random numbers from the player's timing (random_entropy)
//   - Text for the score, the net and messages
//
// What to expect when booting the ROM:
//   - A black screen with "P O N G", "PRESS START" and the controls.
//   - After START: a dotted net down the middle, your cyan paddle on the left,
//     the computer's orange paddle on the right, a white ball in the center and
//     the score line at the top: "YOU 0 ... 0 CPU".
//   - The ball waits a moment, then serves. It bounces off the top and bottom
//     edges and off the paddles; the farther from a paddle's center it hits,
//     the steeper it flies back. Each hit makes it a little faster.
//   - Up and Down move your paddle. The computer follows the ball, a bit slower
//     than you and not perfectly, so it can be beaten.
//   - A ball past a paddle scores for the other side and is served again,
//     toward whoever conceded. First to 7 wins: "YOU WIN!" or "CPU WINS", then
//     START plays again.
//   - START pauses and resumes during a game.
//   - No sound.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, START = Enter.)
//
// Uses only Serval Engine's API; no third-party headers.

#include "serval/serval.h"

// --- Assets ------------------------------------------------------------------

enum { SPR_BALL, SPR_PADDLE_PLAYER, SPR_PADDLE_CPU, SPRITE_COUNT };
enum { PAL_BALL, PAL_PLAYER, PAL_CPU, PALETTE_COUNT };

// An 8x8 round ball, color 1.
static const u32 ball_tiles[8] = {
    0x00111100, 0x01111110, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x01111110, 0x00111100,
};

// An 8x32 paddle with rounded ends: four 8x8 tiles, top to bottom.
static const u32 paddle_tiles[32] = {
    0x00111100, 0x01111110, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x01111110, 0x00111100,
};

static const u16 palettes[PALETTE_COUNT][16] = {
    [PAL_BALL] = {0, COLOR_RGB(255, 255, 255)},
    [PAL_PLAYER] = {0, COLOR_RGB(80, 220, 255)},
    [PAL_CPU] = {0, COLOR_RGB(255, 150, 60)},
};

static const SpriteAsset ball_sprite = {
    .size = SPRITE_8x8, .tiles = ball_tiles, .palette_slot = PAL_BALL};
static const SpriteAsset player_paddle_sprite = {
    .size = SPRITE_8x32, .tiles = paddle_tiles, .palette_slot = PAL_PLAYER};
static const SpriteAsset cpu_paddle_sprite = {
    .size = SPRITE_8x32, .tiles = paddle_tiles, .palette_slot = PAL_CPU};

static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_BALL] = &ball_sprite,
    [SPR_PADDLE_PLAYER] = &player_paddle_sprite,
    [SPR_PADDLE_CPU] = &cpu_paddle_sprite,
};

static const SpriteGroup pong_group = {
    .palettes = &palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = PALETTE_COUNT,
};

// --- Game --------------------------------------------------------------------

#define FIELD_TOP 8 // below the score line
#define BALL_SIZE 8
#define PADDLE_W 8
#define PADDLE_H 32
#define PLAYER_X 8
#define CPU_X (SCREEN_W - 8 - PADDLE_W)

#define PLAYER_SPEED FX(3)
#define CPU_SPEED FX(2) // a little slower than the player
#define BALL_START_SPEED FX(2)
#define BALL_SPEEDUP (FX_ONE / 4) // per paddle hit
#define BALL_MAX_SPEED FX(5)
#define SERVE_DELAY 45 // frames the ball waits before a serve
#define WINNING_SCORE 7

typedef enum { TITLE, SERVE, PLAY, PAUSED, GAME_OVER } State;

static State state;
static u32 ball, player, cpu; // entity slot indices
static FIXED ball_speed;
static int player_score, cpu_score;
static int serve_timer, serve_direction; // direction: -1 toward the player, 1 toward the CPU
static FIXED cpu_aim;                    // where on its paddle the CPU tries to hit the ball

static u32 create(u32 sprite, int w, int h, u32 components) {
    u32 i = entity_index(entity_create(C_POS | C_SPR | C_BODY | components));
    spr_id[i] = (u16)sprite;
    body_w[i] = (u8)w;
    body_h[i] = (u8)h;
    return i;
}

static FIXED center_y(u32 i) {
    return pos_y[i] + FX(body_h[i]) / 2;
}

static void draw_net(void) {
    for (int row = 1; row < TEXT_ROWS; row++)
        text_print(TEXT_COLS / 2, row, ":");
}

static void draw_score(void) {
    text_print_line(0, 0, text_format("    YOU %-2d          %2d CPU", player_score, cpu_score));
}

// Prints a line of text centered on a row, clearing the rest of the row.
static void print_centered(int row, const char* s) {
    int length = 0;
    while (s[length])
        length++;
    text_print_line(0, row, "");
    text_print((TEXT_COLS - length) / 2, row, s);
}

// The title or game-over screen: the game's name and two lines of text.
static void show_message(const char* line1, const char* line2) {
    text_clear();
    print_centered(7, "P O N G");
    print_centered(10, line1);
    print_centered(12, line2);
}

static void center_paddles(void) {
    pos_y[player] = pos_y[cpu] = FX((FIELD_TOP + SCREEN_H - PADDLE_H) / 2);
}

static void start_serve(int direction) {
    state = SERVE;
    serve_timer = SERVE_DELAY;
    serve_direction = direction;
    ball_speed = BALL_START_SPEED;
    pos_x[ball] = FX((SCREEN_W - BALL_SIZE) / 2);
    pos_y[ball] = FX((FIELD_TOP + SCREEN_H - BALL_SIZE) / 2);
    vel_x[ball] = vel_y[ball] = 0;
}

static void start_game(void) {
    player_score = cpu_score = 0;
    center_paddles();
    text_clear();
    draw_net();
    draw_score();
    start_serve(random_range(0, 1) ? 1 : -1);
}

// Moves a paddle by up to `speed` toward `target` (its center's y), keeping it
// on the field.
static void move_paddle(u32 paddle, FIXED target, FIXED speed) {
    FIXED delta = int_clamp(target - center_y(paddle), -speed, speed);
    pos_y[paddle] = int_clamp(pos_y[paddle] + delta, FX(FIELD_TOP), FX(SCREEN_H - PADDLE_H));
}

static void control_player(void) {
    FIXED target = center_y(player);
    if (button_down(BUTTON_UP))
        target -= PLAYER_SPEED;
    if (button_down(BUTTON_DOWN))
        target += PLAYER_SPEED;
    move_paddle(player, target, PLAYER_SPEED);
}

// The computer follows the ball while it's coming its way, aiming to hit it
// slightly off center (cpu_aim, chosen at random each rally), and drifts back
// to the middle otherwise. Its speed limit is what makes it beatable.
static void control_cpu(void) {
    FIXED target = vel_x[ball] > 0 ? center_y(ball) + cpu_aim : FX((FIELD_TOP + SCREEN_H) / 2);
    move_paddle(cpu, target, CPU_SPEED);
}

// Bounces the ball off a paddle it overlaps: sends it back toward the other
// side, faster, at an angle set by how far from the paddle's center it hit.
static void hit_paddle(u32 paddle, int direction) {
    if (!body_overlap(ball, paddle) || (direction > 0) == (vel_x[ball] > 0))
        return; // no contact, or already moving away from this paddle
    ball_speed = int_clamp(ball_speed + BALL_SPEEDUP, 0, BALL_MAX_SPEED);
    FIXED offset = center_y(ball) - center_y(paddle); // -20 to 20 pixels
    vel_x[ball] = direction * ball_speed;
    vel_y[ball] = int_clamp(fx_mul(offset, ball_speed) / 16, -ball_speed, ball_speed);
    // Put the ball against the paddle's face so it can't hit twice.
    pos_x[ball] = direction > 0 ? pos_x[paddle] + FX(PADDLE_W) : pos_x[paddle] - FX(BALL_SIZE);
    if (direction > 0)
        cpu_aim = FX(random_range(-12, 12)); // a new rally toward the CPU
}

static void score_point(bool for_player) {
    if (for_player)
        player_score++;
    else
        cpu_score++;
    draw_score();
    if (player_score == WINNING_SCORE || cpu_score == WINNING_SCORE) {
        state = GAME_OVER;
        show_message(player_score == WINNING_SCORE ? "YOU WIN!" : "CPU WINS", "PRESS START");
        draw_score();
        return;
    }
    start_serve(for_player ? 1 : -1); // toward whoever conceded
}

static void update_play(void) {
    control_player();
    control_cpu();
    sys_movement();
    sys_physics();
    hit_paddle(player, 1);
    hit_paddle(cpu, -1);
    if (pos_x[ball] < -FX(BALL_SIZE))
        score_point(false);
    else if (pos_x[ball] > FX(SCREEN_W))
        score_point(true);
}

int main(void) {
    serval_init();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&pong_group);
    screen_set_backdrop(COLOR_RGB(0, 0, 0));
    physics_set_bounds(0, FIELD_TOP, SCREEN_W, SCREEN_H);
    physics_set_open_edges(PHYSICS_EDGE_LEFT | PHYSICS_EDGE_RIGHT);

    ball = create(SPR_BALL, BALL_SIZE, BALL_SIZE, C_VEL);
    spr_flags[ball] = SPRITE_ABOVE_HUD; // over the net, which is text
    player = create(SPR_PADDLE_PLAYER, PADDLE_W, PADDLE_H, 0);
    cpu = create(SPR_PADDLE_CPU, PADDLE_W, PADDLE_H, 0);
    pos_x[player] = FX(PLAYER_X);
    pos_x[cpu] = FX(CPU_X);
    center_paddles();
    start_serve(1);

    state = TITLE;
    show_message("PRESS START", "UP/DOWN:MOVE");

    for (;;) {
        frame_begin();
        bool start = button_pressed(BUTTON_START);

        switch (state) {
        case TITLE:
        case GAME_OVER:
            if (start) {
                // The moment START is pressed varies, so every game differs.
                random_seed(random_entropy());
                start_game();
            }
            break;
        case SERVE:
            control_player();
            control_cpu();
            if (start) {
                state = PAUSED;
                print_centered(10, "PAUSED");
            } else if (--serve_timer == 0) {
                state = PLAY;
                vel_x[ball] = serve_direction * ball_speed;
                vel_y[ball] = FX(random_range(-1, 1)) / 2;
                cpu_aim = FX(random_range(-12, 12));
            }
            break;
        case PLAY:
            if (start) {
                state = PAUSED;
                print_centered(10, "PAUSED");
            } else {
                update_play();
            }
            break;
        case PAUSED:
            if (start) {
                state = vel_x[ball] ? PLAY : SERVE;
                text_print_line(0, 10, "");
                draw_net();
            }
            break;
        }

        if (state != TITLE && state != GAME_OVER)
            sys_render(); // ball and paddles only during a game
        frame_end();
    }
}
