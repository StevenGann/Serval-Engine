// pong: single-player Pong against a simple computer opponent.
//
// Demonstrates:
//   - The engine's splash screen (serval_splash)
//   - A complete small game: title screen, serve, play, pause, game over
//   - Engine physics for the ball (sys_physics with open left and right edges,
//     so it bounces off the top and bottom but scores by leaving the screen;
//     physics_set_contacts and body_contact report the bounces and, with
//     BODY_CONTACT_EXIT and a side, the goals)
//   - Collisions with body_overlap: paddles are bodies without velocity, which
//     the game moves itself
//   - Graphics drawn at boot (art.c): shaded sprites, a chunky font, and a
//     court background cut into tiles (tileset_load, map_load)
//   - Several sprites sharing tiles with different palettes (the paddles and
//     their hit flash; sparks, glyphs and the trail drawn with SPRITE_PALETTE)
//   - Juice: additive glows and a trail (SPRITE_BLEND, screen_set_blend),
//     sparks, a screen shake (camera_set), a score that pops (sprite_draw_ex),
//     a shaded and flashing backdrop (raster_backdrop)
//   - Sound effects and jingles on the tone generators (audio.h: psg_play)
//   - A simple AI, fixed-point math (fx_mul, fx_sin) and int_clamp
//   - Seeding random numbers from the player's timing (random_entropy)
//   - Text for labels and prompts, in colored styles with a drop shadow
//
// What to expect when booting the ROM:
//   - First the Serval Engine splash: "made with" and the engine's logo (a
//     serval's head beside "SERVAL ENGINE" in chunky gold and cream letters)
//     fade in on black, a coin-like jingle plays, and they fade out (about 3
//     seconds; any button skips it once the logo is in).
//   - The title: a big bobbing "PONG" logo (P and O in blue, N and G in
//     orange) over the court, a navy floor shaded lighter toward the middle,
//     with rails top and bottom, a dashed net, a center circle and a blue
//     (left) and an orange (right) goal line. Behind the logo two paddles keep
//     a demo rally going, with sparks at each hit. "PRESS START" blinks under
//     the logo; "FIRST TO 7 WINS" and the controls ("UP/DOWN:MOVE
//     START:PAUSE") are below. The art is drawn at boot, which adds about a
//     third of a second of black before the splash.
//   - After START (a rising four-note chime): "YOU" (blue) and "CPU"
//     (orange) on the dark band at the top, big see-through score digits
//     in each side's color, your glossy blue paddle on the left and the
//     computer's orange one on the right, each with a soft glow, and a white
//     ball in the center.
//   - The ball waits a moment while a ring closes in on it, then serves with
//     a short tick. In flight it leaves a glowing trail in the color of
//     whoever hit it last. It bounces off the top and bottom rails (a low
//     blip and a puff of white sparks) and off the paddles (a beep: higher
//     for yours, and a spray of sparks in the paddle's color); the farther
//     from a paddle's center it hits, the steeper it flies back. Each hit
//     makes it a little faster and makes that paddle flash white.
//   - Up and Down move your paddle. The computer follows the ball, a bit slower
//     than you and not perfectly, so it can be beaten.
//   - A ball past a paddle's face scores for the other side as soon as it is
//     off the screen (the paddle's ends don't catch a ball that is already
//     past it): a burst of sparks at that edge, the screen shakes, the floor
//     glows blue (your point, with a rising tone) or orange (the computer's,
//     with a falling tone), the scorer's digit pops in big and bright, and the
//     ball is served again toward whoever conceded.
//   - First to 7 wins: "YOU WIN!" in big blue letters with a fanfare and
//     fireworks of sparks, or "CPU WINS" in orange with a sad descending
//     tune. The final score stays up; START (blinking) plays again.
//   - START pauses ("PAUSED" in gold, the floor dims) and resumes during a
//     game, with a tick.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, START = Enter.)
//
// Uses only Serval Engine's API; no third-party headers.

#include "pong.h"

// --- Sounds ------------------------------------------------------------------

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

typedef enum { TITLE, SERVE, PLAY, PAUSED, GAME_OVER } State;

static State state;
static u32 ball, player, cpu; // entity slot indices
static FIXED ball_speed;
static int player_score, cpu_score;
static int serve_timer, serve_direction; // direction: -1 toward the player, 1 toward the CPU
static FIXED cpu_aim;                    // where on its paddle the CPU tries to hit the ball

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

// --- Effects -----------------------------------------------------------------
//
// Nothing here changes the game: it only shows what happened. The effects
// draw their randomness from noise(), not random_range(), so they leave the
// game's random numbers (the serves, the computer's aim) as they were.

#define FLASH_FRAMES 6        // a paddle glows white this long after a hit
#define SCORE_FLASH_FRAMES 24 // the floor glows this long after a point
#define SCORE_POP_FRAMES 16   // the scorer's digit shrinks from twice its size
#define SHAKE_FRAMES 14       // the screen shakes this long after a point
#define TRAIL_LENGTH 7        // ball positions remembered: now and 6 frames back
#define MAX_SPARKS 40
#define SPARK_LIFE 26

static int player_flash, cpu_flash;                      // frames left of a paddle's hit flash
static int score_flash;                                  // frames left of the floor's glow
static int glow_r, glow_g, glow_b;                       // its color
static int score_pop[2];                                 // frames left: yours, the computer's
static int shake;                                        // frames left of the shake
static int trail_x[TRAIL_LENGTH], trail_y[TRAIL_LENGTH]; // recent ball positions
static int trail_head, trail_count;
static int trail_palette; // whoever hit the ball last: their color

static u32 noise_state = 1;

// A number from lo to hi (inclusive), for effects only.
static int noise(int lo, int hi) {
    noise_state = noise_state * 1664525u + 1013904223u;
    // Scales the top 16 bits to the range: a multiply, where % would divide.
    return lo + (int)(((noise_state >> 16) * (u32)(hi - lo + 1)) >> 16);
}

// Sparks: small dots that fly out, slow down and shrink. A fixed pool, drawn
// with sprite_draw (they are not entities: they take no part in the game).
typedef struct {
    FIXED x, y, vx, vy, gravity;
    u8 life, palette;
} Spark;

static Spark sparks[MAX_SPARKS] SERVAL_EWRAM_BSS;

// Throws `count` sparks from (x, y) in palette `palette`: toward dir_x and
// dir_y (-1 or 1), or either way along an axis whose dir is 0. With
// `gravity`, they fall as they fade.
static void spawn_sparks(int x, int y, int dir_x, int dir_y, int count, int palette,
                         FIXED gravity) {
    for (int i = 0; i < MAX_SPARKS && count > 0; i++) {
        Spark* s = &sparks[i];
        if (s->life)
            continue;
        s->x = FX(x);
        s->y = FX(y);
        s->vx = dir_x ? dir_x * noise(FX_ONE / 2, FX(3)) : noise(-FX(2), FX(2));
        s->vy = dir_y ? dir_y * noise(FX_ONE / 2, FX(2)) : noise(-FX(2), FX(2));
        s->gravity = gravity;
        s->life = (u8)noise(SPARK_LIFE / 2, SPARK_LIFE);
        s->palette = (u8)palette;
        count--;
    }
}

// Fireworks for a win: a burst at a random spot, falling as it fades.
static void spawn_firework(void) {
    static const u8 colors[3] = {PAL_PLAYER, PAL_GOLD, PAL_CPU};
    spawn_sparks(noise(40, SCREEN_W - 40), noise(24, 64), 0, 0, 14, colors[noise(0, 2)],
                 FX_ONE / 16);
}

static void update_sparks(void) {
    for (int i = 0; i < MAX_SPARKS; i++) {
        Spark* s = &sparks[i];
        if (!s->life)
            continue;
        s->x += s->vx;
        s->y += s->vy;
        s->vx -= s->vx / 12; // drag
        s->vy += s->gravity - s->vy / 12;
        s->life--;
    }
}

static void clear_sparks(void) {
    for (int i = 0; i < MAX_SPARKS; i++)
        sparks[i].life = 0;
}

// The camera shakes the court and the entities (sys_render draws them at
// their position minus the camera); sprite_draw takes screen positions, so
// the effects drawn with it subtract the camera themselves.
static void draw_shaken(u16 sprite, int frame, int x, int y, u16 flags) {
    sprite_draw(sprite, (u8)frame, x - camera_x(), y - camera_y(), flags);
}

static void draw_sparks(void) {
    for (int i = 0; i < MAX_SPARKS; i++) {
        const Spark* s = &sparks[i];
        if (s->life)
            draw_shaken(SPR_SPARK,
                        s->life > 14  ? 0
                        : s->life > 6 ? 1
                                      : 2,
                        fx_to_int(s->x) - 4, fx_to_int(s->y) - 4,
                        SPRITE_ABOVE_HUD | SPRITE_PALETTE(s->palette));
    }
}

// The floor: the backdrop, one color per screen line (raster_backdrop), navy
// that lightens toward the middle of the court, plus the glow of a point
// fading out, or dimmed while the game is paused.
static Color backdrop[SCREEN_H] SERVAL_EWRAM_BSS;

// Recomputed only when it changes: 160 colors each frame would cost far more
// than the rest of the game.
static void shade_backdrop(void) {
    static int shown_glow = -1, shown_dim = -1;
    int glow = score_flash;    // fades from SCORE_FLASH_FRAMES to 0
    int dim = state == PAUSED; // a shift: halves the colors
    if (glow == shown_glow && dim == shown_dim)
        return;
    shown_glow = glow;
    shown_dim = dim;
    // The glow's share of each channel (the GBA has no divider: divide once).
    int add_r = glow_r * glow / SCORE_FLASH_FRAMES, add_g = glow_g * glow / SCORE_FLASH_FRAMES,
        add_b = glow_b * glow / SCORE_FLASH_FRAMES;
    for (int y = 0; y < SCREEN_H; y++) {
        int light = int_max(0, 72 - int_abs(y - 84)); // 0 at the rails, 72 in the middle
        // light / 8, about light / 6 and about light / 3, by multiplying and
        // shifting: a division by 6 or 3 would call the slow divide routine.
        int r = (6 + (light >> 3) + add_r) >> dim;
        int g = (6 + ((light * 43) >> 8) + add_g) >> dim;
        int b = (18 + ((light * 85) >> 8) + add_b) >> dim;
        backdrop[y] = COLOR_RGB(int_min(r, 255), int_min(g, 255), int_min(b, 255));
    }
}

// Remembers the ball's position for the trail.
static void record_trail(void) {
    trail_head = (trail_head + 1) % TRAIL_LENGTH;
    trail_x[trail_head] = fx_to_int(pos_x[ball]);
    trail_y[trail_head] = fx_to_int(pos_y[ball]);
    if (trail_count < TRAIL_LENGTH)
        trail_count++;
}

// Draws the trail behind the ball: shrinking dots where it was 2, 4 and 6
// frames ago, added onto the floor (SPRITE_BLEND) in the color of whoever hit
// it last. Drawn after sys_render, so they come after the ball in sprite
// order and appear behind it.
static void draw_trail(void) {
    for (int k = 0; k < 3; k++) {
        int age = 2 * (k + 1);
        if (age >= trail_count)
            break;
        int at = (trail_head - age + TRAIL_LENGTH) % TRAIL_LENGTH;
        draw_shaken(SPR_TRAIL, k, trail_x[at], trail_y[at],
                    SPRITE_BLEND | SPRITE_PALETTE(trail_palette));
    }
}

// The paddles' halos, white while a paddle flashes.
static void draw_glows(void) {
    int pal_player = player_flash > 0 ? PAL_FLASH : PAL_PLAYER;
    int pal_cpu = cpu_flash > 0 ? PAL_FLASH : PAL_CPU;
    draw_shaken(SPR_GLOW, 0, PLAYER_X - 4, fx_to_int(pos_y[player]),
                SPRITE_BLEND | SPRITE_PALETTE(pal_player));
    draw_shaken(SPR_GLOW, 0, CPU_X - 4, fx_to_int(pos_y[cpu]),
                SPRITE_BLEND | SPRITE_PALETTE(pal_cpu));
}

// The score: a big digit in each half, see-through, or bright and shrinking
// from twice its size just after it changed.
static void draw_score(void) {
    for (int side = 0; side < 2; side++) {
        int score = side ? cpu_score : player_score;
        u16 flags = SPRITE_PALETTE(side ? PAL_CPU : PAL_PLAYER);
        int x = side ? SCREEN_W * 3 / 4 - 16 : SCREEN_W / 4 - 16, y = 13;
        int pop = score_pop[side];
        if (pop > 0) {
            FIXED scale = FX_ONE + FX_ONE * pop / SCORE_POP_FRAMES;
            sprite_draw_ex(SPR_BIG, (u8)glyph((char)('0' + score)), x - camera_x(), y - camera_y(),
                           0, scale, scale, flags);
        } else {
            draw_shaken(SPR_BIG, glyph((char)('0' + score)), x, y, flags | SPRITE_BLEND);
        }
    }
}

// A word in SPR_BIG or SPR_SMALL glyphs, `step` pixels apart, centered on the
// screen with its top at y, each letter bobbing on a wave when `wave` is set.
static void draw_word(const char* word, u16 sprite, int step, int y, int palette, bool wave) {
    int count = 0;
    while (word[count])
        count++;
    int inset = sprite == SPR_BIG ? 4 : 2; // the glyph's left edge in its sprite
    int x = SCREEN_W / 2 - (count * step - (step - (sprite == SPR_BIG ? 22 : 12))) / 2 - inset;
    for (int i = 0; i < count; i++, x += step) {
        int g = glyph(word[i]);
        if (g < 0)
            continue; // a space
        int dy = wave ? fx_to_int(fx_sin((u16)(frame_count() * 1024 + i * ANGLE_DEG(45))) * 3) : 0;
        sprite_draw(sprite, (u8)g, x, y + dy, SPRITE_ABOVE_HUD | SPRITE_PALETTE(palette));
    }
}

// The title's logo: "PONG", its first half in your color, the rest in the
// computer's.
static void draw_logo(void) {
    static const char logo[] = "PONG";
    for (int i = 0; i < 4; i++) {
        int x = SCREEN_W / 2 - 50 - 4 + i * 26;
        int dy = fx_to_int(fx_sin((u16)(frame_count() * 768 + i * ANGLE_DEG(50))) * 3);
        sprite_draw(SPR_BIG, (u8)glyph(logo[i]), x, 22 + dy,
                    SPRITE_ABOVE_HUD | SPRITE_PALETTE(i < 2 ? PAL_PLAYER : PAL_CPU));
    }
}

// The title's demo rally: a ball between two paddles that never miss, drawn
// with sprite_draw (the game's entities stay hidden until START).
static int demo_x = 60, demo_y = 90, demo_vx = 2, demo_vy = 1;
static int demo_left = 70, demo_right = 70; // the paddles' tops

static void update_demo(void) {
    demo_x += demo_vx;
    demo_y += demo_vy;
    if (demo_y < FIELD_TOP || demo_y > SCREEN_H - BALL_SIZE) {
        demo_vy = -demo_vy;
        demo_y += 2 * demo_vy;
    }
    bool left = demo_x<PLAYER_X + PADDLE_W, right = demo_x> CPU_X - BALL_SIZE;
    if (left || right) {
        demo_vx = -demo_vx;
        demo_x += 2 * demo_vx;
        demo_vy = noise(0, 1) ? 1 : 2;
        demo_vy = noise(0, 1) ? demo_vy : -demo_vy;
        spawn_sparks(left ? PLAYER_X + PADDLE_W : CPU_X, demo_y + BALL_SIZE / 2, left ? 1 : -1, 0,
                     5, left ? PAL_PLAYER : PAL_CPU, 0);
    }
    int target = demo_y + BALL_SIZE / 2 - PADDLE_H / 2;
    demo_left += int_clamp(target - demo_left, -3, 3);
    demo_right += int_clamp(target - demo_right, -3, 3);
}

static void draw_demo(void) {
    sprite_draw(SPR_BALL, 0, demo_x, demo_y, 0);
    sprite_draw(SPR_PADDLE_PLAYER, 0, PLAYER_X, demo_left, 0);
    sprite_draw(SPR_PADDLE_CPU, 0, CPU_X, demo_right, 0);
    sprite_draw(SPR_GLOW, 0, PLAYER_X - 4, demo_left, SPRITE_BLEND | SPRITE_PALETTE(PAL_PLAYER));
    sprite_draw(SPR_GLOW, 0, CPU_X - 4, demo_right, SPRITE_BLEND | SPRITE_PALETTE(PAL_CPU));
}

// Hit flashes on the paddles, the floor's glow after a point, the shake and
// the sparks, each fading out over a few frames.
static void update_effects(void) {
    spr_id[player] = player_flash > 0 ? SPR_PADDLE_FLASH : SPR_PADDLE_PLAYER;
    spr_id[cpu] = cpu_flash > 0 ? SPR_PADDLE_FLASH : SPR_PADDLE_CPU;
    if (player_flash > 0)
        player_flash--;
    if (cpu_flash > 0)
        cpu_flash--;
    for (int side = 0; side < 2; side++)
        if (score_pop[side] > 0)
            score_pop[side]--;

    shade_backdrop();
    if (score_flash > 0)
        score_flash--;

    int amount = (shake + 3) / 4; // pixels: 4, 3, 2, 1, then still
    camera_set(shake ? noise(-amount, amount) : 0, shake ? noise(-amount, amount) : 0);
    if (shake > 0)
        shake--;

    update_sparks();
}

// --- Rules -------------------------------------------------------------------

// The HUD's labels on the dark band at the top; the digits are sprites.
static void draw_hud(void) {
    text_clear();
    text_set_style(2);
    text_print(6, 0, "YOU");
    text_set_style(3);
    text_print(21, 0, "CPU");
    text_set_style(TEXT_NORMAL);
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
    trail_palette = PAL_BALL;
}

static void start_game(void) {
    psg_play(SND_START);
    player_score = cpu_score = 0;
    center_paddles();
    clear_sparks();
    draw_hud();
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
    int face = fx_to_int(direction > 0 ? pos_x[ball] : pos_x[ball] + FX(BALL_SIZE));
    int palette = direction > 0 ? PAL_PLAYER : PAL_CPU;
    spawn_sparks(face, fx_to_int(center_y(ball)), direction, 0, 6, palette, 0);
    trail_palette = palette;
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
    // Sparks where the ball left, a shake, the floor glowing in the scorer's
    // color and their digit popping.
    spawn_sparks(for_player ? SCREEN_W - 2 : 2, int_clamp(fx_to_int(center_y(ball)), 16, 150),
                 for_player ? -1 : 1, 0, 16, for_player ? PAL_PLAYER : PAL_CPU, 0);
    shake = SHAKE_FRAMES;
    score_flash = SCORE_FLASH_FRAMES;
    glow_r = for_player ? 12 : 60;
    glow_g = for_player ? 45 : 28;
    glow_b = for_player ? 75 : 6;
    score_pop[for_player ? 0 : 1] = SCORE_POP_FRAMES;
    if (player_score == WINNING_SCORE || cpu_score == WINNING_SCORE) {
        state = GAME_OVER;
        psg_play(player_score == WINNING_SCORE ? SND_WIN : SND_LOSE);
        return;
    }
    psg_play(for_player ? SND_POINT_PLAYER : SND_POINT_CPU);
    start_serve(for_player ? 1 : -1); // toward whoever conceded
}

static void update_play(void) {
    control_player();
    control_cpu();
    sys_movement();
    sys_physics();
    // body_contact: the walls the ball touched in sys_physics (contacts are
    // on, see main), so a bounce off the top or bottom gets its sound.
    if (body_contact[ball] & (BODY_SIDE_TOP | BODY_SIDE_BOTTOM)) {
        psg_play(SND_WALL);
        bool top = body_contact[ball] & BODY_SIDE_TOP;
        spawn_sparks(fx_to_int(pos_x[ball]) + BALL_SIZE / 2, top ? FIELD_TOP : SCREEN_H - 2, 0,
                     top ? 1 : -1, 3, PAL_BALL, 0);
    }
    hit_paddle(player, 1);
    hit_paddle(cpu, -1);
    // A point on the frame the ball is entirely off screen: sys_physics sets
    // BODY_CONTACT_EXIT, with the side of the open edge the ball left through.
    if (body_contact[ball] & BODY_CONTACT_EXIT)
        score_point((body_contact[ball] & BODY_SIDE_RIGHT) != 0); // past the CPU: yours
}

// --- Drawing -----------------------------------------------------------------

// "PRESS START" on `row`, blinking: on for 40 frames, off for 20.
static void blink_prompt(int row) {
    u32 t = frame_count() % 60;
    if (t == 0)
        text_print_centered(row, "PRESS START");
    else if (t == 40)
        text_print_line(0, row, "");
}

// Sprites drawn first are in front: sparks and words, then the entities
// (sys_render), then what lies behind them on the floor.
static void draw_frame(void) {
    draw_sparks();
    switch (state) {
    case TITLE:
        draw_logo();
        draw_demo();
        break;
    case GAME_OVER: {
        bool won = player_score == WINNING_SCORE;
        draw_word(won ? "YOU WIN!" : "CPU WINS", SPR_BIG, 26, 54, won ? PAL_PLAYER : PAL_CPU, won);
        draw_score();
        break;
    }
    default:
        if (state == PAUSED)
            draw_word("PAUSED", SPR_SMALL, 14, 76, PAL_GOLD, true);
        sys_render();
        if (state == PLAY)
            draw_trail();
        if (state == SERVE && serve_timer <= 24) // a ring closing in on the ball
            draw_shaken(SPR_RING, 2 - (serve_timer - 1) / 8, fx_to_int(pos_x[ball]) - 4,
                        fx_to_int(pos_y[ball]) - 4, SPRITE_BLEND);
        draw_glows();
        draw_score();
        break;
    }
}

int main(void) {
    serval_init();
    art_build(); // about half a second of work: done before the splash fades in
    serval_splash();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&pong_group);
    tileset_load(&court_tileset);
    map_load(&court_layer);
    psg_table_set(sound_table, SOUND_COUNT);
    // SPRITE_BLEND sprites (glows, trail, ring, score) add 11/16 of their color
    // onto the court and the floor behind them.
    screen_set_blend(0, LAYER_BACKGROUND | LAYER_BACKDROP, 11, 16);
    raster_backdrop(backdrop);
    text_set_shadow(true);
    text_set_color(COLOR_RGB(200, 210, 240), COLOR_RGB(0, 0, 16));
    text_set_style_color(2, COLOR_RGB(120, 210, 255), COLOR_RGB(0, 0, 16));
    text_set_style_color(3, COLOR_RGB(255, 170, 90), COLOR_RGB(0, 0, 16));
    physics_set_bounds(0, FIELD_TOP, SCREEN_W, SCREEN_H);
    physics_set_open_edges(PHYSICS_EDGE_LEFT | PHYSICS_EDGE_RIGHT);
    physics_set_contacts(true); // body_contact: the walls the ball bounced off, and goals

    ball = create(SPR_BALL, BALL_SIZE, BALL_SIZE, C_VEL);
    spr_flags[ball] = SPRITE_ABOVE_HUD; // over the glows and the text
    player = create(SPR_PADDLE_PLAYER, PADDLE_W, PADDLE_H, 0);
    cpu = create(SPR_PADDLE_CPU, PADDLE_W, PADDLE_H, 0);
    if (ball == MAX_ENT || player == MAX_ENT || cpu == MAX_ENT)
        return 1; // can't happen: all 128 entities are free at startup
    pos_x[player] = FX(PLAYER_X);
    pos_x[cpu] = FX(CPU_X);
    center_paddles();
    start_serve(1);

    state = TITLE;
    text_print_centered(15, "FIRST TO 7 WINS");
    text_print_centered(17, "UP/DOWN:MOVE  START:PAUSE");

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
            } else {
                blink_prompt(state == TITLE ? 9 : 12);
                if (state == TITLE)
                    update_demo();
                else if (player_score == WINNING_SCORE && frame_count() % 14 == 0)
                    spawn_firework();
            }
            break;
        case SERVE:
            control_player();
            control_cpu();
            if (start) {
                state = PAUSED;
                psg_play(SND_PAUSE);
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
            } else {
                update_play();
            }
            break;
        case PAUSED:
            if (start) {
                psg_play(SND_PAUSE);
                state = vel_x[ball] ? PLAY : SERVE;
            }
            break;
        }

        if (state == PLAY)
            record_trail();
        update_effects();
        draw_frame();
        frame_end();
    }
}
