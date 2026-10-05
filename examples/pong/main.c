// pong: single-player Pong against a simple computer opponent.
//
// Demonstrates:
//   - The engine's splash screen (serval_splash)
//   - A complete small game: title screen, serve, play, pause, game over
//   - Engine physics for the ball (sys_physics with open left and right edges,
//     so it bounces off the top and bottom but scores by leaving the screen)
//   - Collisions with body_overlap: paddles are bodies without velocity, which
//     the game moves itself
//   - Shaded multi-color sprites; several sprites sharing tiles with different
//     palettes (the paddles and their hit flash); a trail drawn with
//     sprite_draw next to entities drawn by sys_render
//   - Sound effects and jingles on the tone generators (audio.h: psg_play)
//   - A simple AI, fixed-point math (fx_mul) and int_clamp
//   - Seeding random numbers from the player's timing (random_entropy)
//   - Text for the score, the net and messages
//
// What to expect when booting the ROM:
//   - First the Serval Engine splash: "made with" and "Serval Engine" fade in
//     on black, a coin-like jingle plays, and they fade out (about 3 seconds;
//     any button skips it once the text is in).
//   - A dark navy screen with "P O N G", "PRESS START" and the controls
//     ("UP/DOWN:MOVE  START:PAUSE").
//   - After START (a rising four-note chime): a dotted net down the middle,
//     your glossy blue paddle on the left, the computer's orange one on the
//     right, a shaded white ball in the center and the score line at the top:
//     "YOU 0 ... 0 CPU".
//   - The ball waits a moment, then serves with a short tick. In flight it
//     leaves a trail of three fading dots. It bounces off the top and bottom
//     edges (a low blip) and off the paddles (a beep: higher for yours); the
//     farther from a paddle's center it hits, the steeper it flies back. Each
//     hit makes it a little faster and makes that paddle flash white.
//   - Up and Down move your paddle. The computer follows the ball, a bit slower
//     than you and not perfectly, so it can be beaten.
//   - A ball past a paddle's face scores for the other side (the paddle's
//     ends don't catch a ball that is already past it): the background glows
//     blue (your point, with a rising tone) or orange (the computer's, with a
//     falling tone), and the ball is served again toward whoever conceded.
//   - First to 7 wins: "YOU WIN!" with a fanfare, or "CPU WINS" with a sad
//     descending tune. START then plays again.
//   - START pauses and resumes during a game, with a tick.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, START = Enter.)
//
// Uses only Serval Engine's API; no third-party headers.

#include "serval/serval.h"

// --- Assets ------------------------------------------------------------------

enum {
    SPR_BALL,
    SPR_PADDLE_PLAYER,
    SPR_PADDLE_CPU,
    SPR_PADDLE_FLASH, // a paddle for a few frames after it hits the ball
    SPR_TRAIL1,       // the ball's trail, largest to smallest
    SPR_TRAIL2,
    SPR_TRAIL3,
    SPRITE_COUNT
};
enum {
    PAL_PLAYER,
    PAL_CPU,
    PAL_FLASH,
    PAL_BALL,
    PAL_TRAIL1,
    PAL_TRAIL2,
    PAL_TRAIL3,
    PALETTE_COUNT
};

// Pixel art, 4 bits per pixel, generated from these drawings (8 pixels wide,
// tiles top to bottom). Colors: 1 base, 2 highlight, 3 shadow, 4 outline.
//
//   paddle (8x32)   ball (8x8)   trail 1    trail 2    trail 3
//   ..KKKK..        ..KKKK..     ........   ........   ........
//   .KHHBBK.        .KHHBBK.     ..BBBB..   ........   ........
//   KHHBBBSK        KHHBBBSK     .BBBBBB.   ...BB...   ........
//   KHBBBBSK  x26   KHBBBBSK     .BBBBBB.   ..BBBB..   ...BB...
//   KHBBBSSK        KBBBBBSK     .BBBBBB.   ..BBBB..   ...BB...
//   .KBBSSK.        KBBBBSSK     .BBBBBB.   ...BB...   ........
//   ..KKKK..        .KBSSSK.     ..BBBB..   ........   ........
//                   ..KKKK..     ........   ........   ........
static const u32 paddle_tiles[32] = {
    0x00444400, 0x04112240, 0x43111224, 0x43111124, 0x43111124, 0x43111124, 0x43111124, 0x43111124,
    0x43111124, 0x43111124, 0x43111124, 0x43111124, 0x43111124, 0x43111124, 0x43111124, 0x43111124,
    0x43111124, 0x43111124, 0x43111124, 0x43111124, 0x43111124, 0x43111124, 0x43111124, 0x43111124,
    0x43111124, 0x43111124, 0x43111124, 0x43111124, 0x43111124, 0x43311124, 0x04331140, 0x00444400,
};

static const u32 ball_tiles[8] = {
    0x00444400, 0x04112240, 0x43111224, 0x43111124, 0x43111114, 0x43311114, 0x04333140, 0x00444400,
};

static const u32 trail1_tiles[8] = {
    0x00000000, 0x00111100, 0x01111110, 0x01111110, 0x01111110, 0x01111110, 0x00111100, 0x00000000,
};

static const u32 trail2_tiles[8] = {
    0x00000000, 0x00000000, 0x00011000, 0x00111100, 0x00111100, 0x00011000, 0x00000000, 0x00000000,
};

static const u32 trail3_tiles[8] = {
    0x00000000, 0x00000000, 0x00000000, 0x00011000, 0x00011000, 0x00000000, 0x00000000, 0x00000000,
};

// Base, highlight, shadow, outline.
static const u16 palettes[PALETTE_COUNT][16] = {
    [PAL_PLAYER] = {0, COLOR_RGB(40, 180, 255), COLOR_RGB(180, 240, 255), COLOR_RGB(10, 90, 170),
                    COLOR_RGB(5, 30, 70)},
    [PAL_CPU] = {0, COLOR_RGB(255, 130, 40), COLOR_RGB(255, 220, 150), COLOR_RGB(170, 60, 10),
                 COLOR_RGB(70, 20, 5)},
    [PAL_FLASH] = {0, COLOR_RGB(255, 255, 255), COLOR_RGB(255, 255, 255), COLOR_RGB(200, 230, 255),
                   COLOR_RGB(120, 140, 160)},
    [PAL_BALL] = {0, COLOR_RGB(235, 235, 235), COLOR_RGB(255, 255, 255), COLOR_RGB(150, 150, 170),
                  COLOR_RGB(60, 60, 80)},
    [PAL_TRAIL1] = {0, COLOR_RGB(150, 150, 170)},
    [PAL_TRAIL2] = {0, COLOR_RGB(100, 100, 120)},
    [PAL_TRAIL3] = {0, COLOR_RGB(60, 60, 80)},
};

#define PADDLE_SPRITE(palette)                                                                     \
    {.size = SPRITE_8x32, .tiles = paddle_tiles, .palette_slot = (palette)}

static const SpriteAsset sprites[SPRITE_COUNT] = {
    [SPR_BALL] = {.size = SPRITE_8x8, .tiles = ball_tiles, .palette_slot = PAL_BALL},
    [SPR_PADDLE_PLAYER] = PADDLE_SPRITE(PAL_PLAYER),
    [SPR_PADDLE_CPU] = PADDLE_SPRITE(PAL_CPU),
    [SPR_PADDLE_FLASH] = PADDLE_SPRITE(PAL_FLASH),
    [SPR_TRAIL1] = {.size = SPRITE_8x8, .tiles = trail1_tiles, .palette_slot = PAL_TRAIL1},
    [SPR_TRAIL2] = {.size = SPRITE_8x8, .tiles = trail2_tiles, .palette_slot = PAL_TRAIL2},
    [SPR_TRAIL3] = {.size = SPRITE_8x8, .tiles = trail3_tiles, .palette_slot = PAL_TRAIL3},
};

static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_BALL] = &sprites[SPR_BALL],
    [SPR_PADDLE_PLAYER] = &sprites[SPR_PADDLE_PLAYER],
    [SPR_PADDLE_CPU] = &sprites[SPR_PADDLE_CPU],
    [SPR_PADDLE_FLASH] = &sprites[SPR_PADDLE_FLASH],
    [SPR_TRAIL1] = &sprites[SPR_TRAIL1],
    [SPR_TRAIL2] = &sprites[SPR_TRAIL2],
    [SPR_TRAIL3] = &sprites[SPR_TRAIL3],
};

static const SpriteGroup pong_group = {
    .palettes = &palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = PALETTE_COUNT,
};

// Sound effects, all on the tone generators (no CPU cost while playing).
enum {
    SND_START,
    SND_SERVE,
    SND_HIT_PLAYER,
    SND_HIT_CPU,
    SND_WALL,
    SND_POINT_PLAYER,
    SND_POINT_CPU,
    SND_WIN,
    SND_LOSE,
    SND_PAUSE,
    SOUND_COUNT
};

// Notes in Hz (0 = rest).
static const u16 start_notes[] = {523, 659, 784, 1047}; // C E G C, rising
static const u16 win_notes[] = {523, 659, 784, 1047, 0, 784, 1047};
static const u16 lose_notes[] = {392, 370, 349, 330, 0, 262}; // sliding down, then low C

static const PsgSound sounds[SOUND_COUNT] = {
    [SND_START] = {.channel = PSG_SQUARE2,
                   .duty = PSG_DUTY_25,
                   .frames = 4,
                   .notes = start_notes,
                   .note_count = 4},
    [SND_SERVE] =
        {.channel = PSG_SQUARE2, .duty = PSG_DUTY_12, .frequency = 1047, .frames = 3, .volume = 9},
    [SND_HIT_PLAYER] = {.channel = PSG_SQUARE1, .frequency = 880, .frames = 8, .fade = -1},
    [SND_HIT_CPU] = {.channel = PSG_SQUARE1, .frequency = 659, .frames = 8, .fade = -1},
    [SND_WALL] = {.channel = PSG_SQUARE2, .frequency = 330, .frames = 5, .fade = -1, .volume = 11},
    [SND_POINT_PLAYER] =
        {.channel = PSG_SQUARE1, .frequency = 523, .frames = 24, .slide = 2, .slide_size = 3},
    [SND_POINT_CPU] =
        {.channel = PSG_SQUARE1, .frequency = 392, .frames = 30, .slide = -3, .slide_size = 3},
    [SND_WIN] = {.channel = PSG_SQUARE2,
                 .duty = PSG_DUTY_25,
                 .frames = 7,
                 .notes = win_notes,
                 .note_count = 7},
    [SND_LOSE] = {.channel = PSG_SQUARE2,
                  .duty = PSG_DUTY_25,
                  .frames = 10,
                  .notes = lose_notes,
                  .note_count = 6},
    [SND_PAUSE] = {.channel = PSG_SQUARE2, .duty = PSG_DUTY_12, .frequency = 784, .frames = 3},
};

static const PsgSound* const sound_table[SOUND_COUNT] = {
    [SND_START] = &sounds[SND_START],
    [SND_SERVE] = &sounds[SND_SERVE],
    [SND_HIT_PLAYER] = &sounds[SND_HIT_PLAYER],
    [SND_HIT_CPU] = &sounds[SND_HIT_CPU],
    [SND_WALL] = &sounds[SND_WALL],
    [SND_POINT_PLAYER] = &sounds[SND_POINT_PLAYER],
    [SND_POINT_CPU] = &sounds[SND_POINT_CPU],
    [SND_WIN] = &sounds[SND_WIN],
    [SND_LOSE] = &sounds[SND_LOSE],
    [SND_PAUSE] = &sounds[SND_PAUSE],
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

#define FLASH_FRAMES 6        // a paddle glows this long after a hit
#define SCORE_FLASH_FRAMES 24 // the background glows this long after a point
#define TRAIL_LENGTH 7        // ball positions remembered: now and 6 frames back
#define BACKDROP_R 8          // the court's dark navy
#define BACKDROP_G 8
#define BACKDROP_B 20

typedef enum { TITLE, SERVE, PLAY, PAUSED, GAME_OVER } State;

static State state;
static u32 ball, player, cpu; // entity slot indices
static FIXED ball_speed;
static int player_score, cpu_score;
static int serve_timer, serve_direction; // direction: -1 toward the player, 1 toward the CPU
static FIXED cpu_aim;                    // where on its paddle the CPU tries to hit the ball

static int player_flash, cpu_flash;                      // frames left of a paddle's hit flash
static int score_flash;                                  // frames left of the background flash
static u8 score_flash_r, score_flash_g, score_flash_b;   // its color
static int trail_x[TRAIL_LENGTH], trail_y[TRAIL_LENGTH]; // recent ball positions
static int trail_head, trail_count;

// Creates an entity with a sprite and a w x h body. Returns its slot index, or
// MAX_ENT if the engine's entity pool is full.
static u32 create(u32 sprite, int w, int h, u32 components) {
    Entity e = entity_create(C_POS | C_SPR | C_BODY | components);
    if (e == ENTITY_NONE)
        return MAX_ENT;
    u32 i = entity_index(e);
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

// The title or game-over screen: the game's name and two lines of text.
static void show_message(const char* line1, const char* line2) {
    text_clear();
    text_print_centered(7, "P O N G");
    text_print_centered(10, line1);
    text_print_centered(12, line2);
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
    trail_count = 0;
}

static void start_game(void) {
    psg_play(SND_START);
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
// Only the paddle's face counts: a ball that was already past it last frame
// (pos - vel) can't be caught by the paddle's end or from behind; it scores.
// (body_hit_side() is stricter: a ball that was in front of the face last
// frame but crossed the face's line just beyond the paddle's end, then met
// the end, hit the end (BODY_SIDE_TOP/BOTTOM) and would pass through. Pong
// catches it.)
static void hit_paddle(u32 paddle, int direction) {
    if (!body_overlap(ball, paddle) || (direction > 0) == (vel_x[ball] > 0))
        return; // no contact, or already moving away from this paddle
    // The ball's left edge last frame.
    FIXED was_x = pos_x[ball] - vel_x[ball];
    bool was_in_front = direction > 0 ? was_x >= pos_x[paddle] + FX(PADDLE_W)
                                      : was_x + FX(BALL_SIZE) <= pos_x[paddle];
    if (!was_in_front)
        return;
    ball_speed = int_clamp(ball_speed + BALL_SPEEDUP, 0, BALL_MAX_SPEED);
    FIXED offset = center_y(ball) - center_y(paddle); // -20 to 20 pixels
    vel_x[ball] = direction * ball_speed;
    vel_y[ball] = int_clamp(fx_mul(offset, ball_speed) / 16, -ball_speed, ball_speed);
    // Put the ball against the paddle's face so it can't hit twice.
    pos_x[ball] = direction > 0 ? pos_x[paddle] + FX(PADDLE_W) : pos_x[paddle] - FX(BALL_SIZE);
    if (direction > 0) {
        cpu_aim = FX(random_range(-12, 12)); // a new rally toward the CPU
        player_flash = FLASH_FRAMES;
        psg_play(SND_HIT_PLAYER);
    } else {
        cpu_flash = FLASH_FRAMES;
        psg_play(SND_HIT_CPU);
    }
}

static void score_point(bool for_player) {
    if (for_player)
        player_score++;
    else
        cpu_score++;
    draw_score();
    // The background glows in the scorer's color.
    score_flash = SCORE_FLASH_FRAMES;
    score_flash_r = for_player ? 12 : 60;
    score_flash_g = for_player ? 45 : 28;
    score_flash_b = for_player ? 75 : 6;
    if (player_score == WINNING_SCORE || cpu_score == WINNING_SCORE) {
        state = GAME_OVER;
        psg_play(player_score == WINNING_SCORE ? SND_WIN : SND_LOSE);
        show_message(player_score == WINNING_SCORE ? "YOU WIN!" : "CPU WINS", "PRESS START");
        draw_score();
        return;
    }
    psg_play(for_player ? SND_POINT_PLAYER : SND_POINT_CPU);
    start_serve(for_player ? 1 : -1); // toward whoever conceded
}

static void update_play(void) {
    control_player();
    control_cpu();
    FIXED vel_y_before = vel_y[ball];
    sys_movement();
    sys_physics();
    if ((vel_y_before < 0) != (vel_y[ball] < 0) && vel_y_before != 0)
        psg_play(SND_WALL); // physics bounced it off the top or bottom
    hit_paddle(player, 1);
    hit_paddle(cpu, -1);
    if (pos_x[ball] < -FX(BALL_SIZE))
        score_point(false);
    else if (pos_x[ball] > FX(SCREEN_W))
        score_point(true);
}

// Remembers the ball's position for the trail.
static void record_trail(void) {
    trail_head = (trail_head + 1) % TRAIL_LENGTH;
    trail_x[trail_head] = fx_to_int(pos_x[ball]);
    trail_y[trail_head] = fx_to_int(pos_y[ball]);
    if (trail_count < TRAIL_LENGTH)
        trail_count++;
}

// Draws the trail behind the ball: shrinking, dimming dots where it was 2, 4
// and 6 frames ago. Drawn after sys_render, so they come after the ball in
// sprite order and appear behind it.
static void draw_trail(void) {
    static const u16 trail_sprites[3] = {SPR_TRAIL1, SPR_TRAIL2, SPR_TRAIL3};
    for (int k = 0; k < 3; k++) {
        int age = 2 * (k + 1);
        if (age >= trail_count)
            break;
        int at = (trail_head - age + TRAIL_LENGTH) % TRAIL_LENGTH;
        sprite_draw(trail_sprites[k], 0, trail_x[at], trail_y[at], SPRITE_ABOVE_HUD);
    }
}

// Hit flashes on the paddles and the background glow after a point, fading
// out over a few frames.
static void update_flashes(void) {
    spr_id[player] = player_flash > 0 ? SPR_PADDLE_FLASH : SPR_PADDLE_PLAYER;
    spr_id[cpu] = cpu_flash > 0 ? SPR_PADDLE_FLASH : SPR_PADDLE_CPU;
    if (player_flash > 0)
        player_flash--;
    if (cpu_flash > 0)
        cpu_flash--;

    int glow = score_flash; // fades from SCORE_FLASH_FRAMES to 0
    screen_set_backdrop(COLOR_RGB(BACKDROP_R + score_flash_r * glow / SCORE_FLASH_FRAMES,
                                  BACKDROP_G + score_flash_g * glow / SCORE_FLASH_FRAMES,
                                  BACKDROP_B + score_flash_b * glow / SCORE_FLASH_FRAMES));
    if (score_flash > 0)
        score_flash--;
}

int main(void) {
    serval_init();
    serval_splash();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&pong_group);
    psg_table_set(sound_table, SOUND_COUNT);
    screen_set_backdrop(COLOR_RGB(BACKDROP_R, BACKDROP_G, BACKDROP_B));
    physics_set_bounds(0, FIELD_TOP, SCREEN_W, SCREEN_H);
    physics_set_open_edges(PHYSICS_EDGE_LEFT | PHYSICS_EDGE_RIGHT);

    ball = create(SPR_BALL, BALL_SIZE, BALL_SIZE, C_VEL);
    spr_flags[ball] = SPRITE_ABOVE_HUD; // over the net, which is text
    player = create(SPR_PADDLE_PLAYER, PADDLE_W, PADDLE_H, 0);
    cpu = create(SPR_PADDLE_CPU, PADDLE_W, PADDLE_H, 0);
    if (ball == MAX_ENT || player == MAX_ENT || cpu == MAX_ENT)
        return 1; // can't happen: all 128 entities are free at startup
    pos_x[player] = FX(PLAYER_X);
    pos_x[cpu] = FX(CPU_X);
    center_paddles();
    start_serve(1);

    state = TITLE;
    show_message("PRESS START", "UP/DOWN:MOVE  START:PAUSE");

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
                psg_play(SND_PAUSE);
                text_print_centered(10, "PAUSED");
            } else if (--serve_timer == 0) {
                state = PLAY;
                psg_play(SND_SERVE);
                vel_x[ball] = serve_direction * ball_speed;
                vel_y[ball] = FX(random_range(-1, 1)) / 2;
                cpu_aim = FX(random_range(-12, 12));
            }
            break;
        case PLAY:
            if (start) {
                state = PAUSED;
                psg_play(SND_PAUSE);
                text_print_centered(10, "PAUSED");
            } else {
                update_play();
            }
            break;
        case PAUSED:
            if (start) {
                psg_play(SND_PAUSE);
                state = vel_x[ball] ? PLAY : SERVE;
                text_print_line(0, 10, "");
                draw_net();
            }
            break;
        }

        if (state == PLAY)
            record_trail();
        update_flashes();
        if (state != TITLE && state != GAME_OVER) { // ball and paddles only during a game
            sys_render();
            if (state == PLAY)
                draw_trail();
        }
        frame_end();
    }
}
