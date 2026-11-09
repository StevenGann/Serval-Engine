// Screens and states: the title and the score table, levels with their
// intro, pause, a lost ball, level clear and game over, the HUD, the
// backdrop's gradient and its flashes, and the fades between screens.

#include "game.h"

typedef enum {
    TITLE,     // the attract mode: the title and the score table in turn
    PLAYING,   // including the wait for A with the ball on the paddle
    PAUSED,    //
    BALL_LOST, // a moment before the next ball, or game over
    CLEARED,   // the level's fanfare, then a fade to the next level
    GAME_OVER, // then a fade to the score table
} State;

typedef enum { PAGE_TITLE, PAGE_SCORES } Page;

#define START_LIVES 3
#define MAX_LIVES 5
#define CLEAR_BONUS 1000
#define FADE_STEP 2 // brightness per frame: 8 frames between black and normal
#define PAGE_FRAMES 300
#define NEW_SCORE_FRAMES 600 // the score table after a game that made it
#define LOST_FRAMES 75
#define CLEAR_FRAMES 150
#define GAME_OVER_FRAMES 180
// Messages: two lines on rows 13 and 15, or one on row 14 (PAUSED, between
// the level's two), on a band from y = 100 to 131.
#define MESSAGE_ROW 13 // below the lowest bricks, above the paddle
#define PAUSE_ROW 14
#define BANNER_Y 100
#define SCORES_ROW 3      // "BEST SCORES"; the scores from 2 rows below
#define LOGO_Y 10         // the title's "PAW"; "BREAKER" under it
#define ARCH_Y 70         // the bricks on the title...
#define ARCH_Y_SCORES 104 // ...and on the score table
#define FLASH_FADE 16     // per frame, of 256: a flash of the backdrop lasts 16 frames

static State state;
static Page page;
static int timer, page_timer;
static int score, lives, level;
static int new_rank = -1; // place in the table of the last game's score
static bool hud_dirty;
static bool intro_shown; // the level's name is on screen until the first launch
static int brightness = SCREEN_BRIGHTNESS_MIN;
static bool fading_out;
static void (*next_screen)(void);
static int style; // the level style shown: its pattern and gradient
static int flash; // how far the backdrop is mixed toward flash_color (0-256)
static Color flash_color;
static Color backdrop[SCREEN_H] SERVAL_EWRAM_BSS; // raster_backdrop's table, a color per line

// --- Backdrop ----------------------------------------------------------------

// Fills the backdrop table with the style's gradient, mixed toward the
// flash's color while one is on. The colors go up in half steps of the
// hardware's 5-bit channels, alternate lines rounding the half up or down,
// so the bands between the few shades of a dark gradient blur together.
static void build_backdrop(void) {
    Color top = backdrop_gradients[style][0], bottom = backdrop_gradients[style][1];
    if (flash > 0) {
        top = color_mix(top, flash_color, (u32)flash);
        bottom = color_mix(bottom, flash_color, (u32)flash);
    }
    int channel_top[3], step[3];
    for (int c = 0; c < 3; c++) {
        int a = (top >> (5 * c)) & 31, b = (bottom >> (5 * c)) & 31;
        channel_top[c] = a << 17; // in 1/65536ths of half a step
        step[c] = ((b - a) << 17) / (SCREEN_H - 1);
    }
    for (int y = 0; y < SCREEN_H; y++) {
        Color color = 0;
        for (int c = 0; c < 3; c++) {
            int halves = (channel_top[c] + step[c] * y) >> 16;
            color |= (Color)(((halves + (y & 1)) >> 1) << (5 * c));
        }
        backdrop[y] = color;
    }
}

// Lights the backdrop up in a color, fading back over FLASH_FADE frames.
static void flash_backdrop(Color color) {
    flash_color = color;
    flash = 200;
    build_backdrop();
}

static void update_flash(void) {
    if (flash > 0) {
        flash = int_max(flash - FLASH_FADE, 0);
        build_backdrop();
    }
}

// --- Fades -------------------------------------------------------------------

// Fades to black, then calls show() to set up the next screen, which fades in.
static void fade_to(void (*show)(void)) {
    next_screen = show;
    fading_out = true;
}

// Steps the fade once per frame, right after frame_begin(), so the whole
// frame has the new brightness. Returns true while fading out (the game
// waits meanwhile).
static bool update_fade(void) {
    if (fading_out) {
        brightness = int_max(brightness - FADE_STEP, SCREEN_BRIGHTNESS_MIN);
        if (brightness == SCREEN_BRIGHTNESS_MIN) {
            fading_out = false;
            next_screen(); // loads the next screen while it is black
        }
    } else {
        brightness = int_min(brightness + FADE_STEP, 0);
    }
    screen_set_brightness(brightness);
    return fading_out;
}

// --- Score, lives, HUD -------------------------------------------------------

void add_score(int points) {
    score = int_min(score + points, 999999);
    hud_dirty = true;
}

void add_life(void) {
    lives = int_min(lives + 1, MAX_LIVES);
    hud_dirty = true;
    psg_play(SND_LIFE_UP);
}

// The top row: score, best score and level as text (the labels in the
// HUD_LABEL style), lives as paw prints (sprites above the text layer).
#define HUD_LABEL 2
static void draw_hud(void) {
    if (hud_dirty) {
        hud_dirty = false;
        text_print_line(0, 0, text_format(" %06d", score));
        text_print(12, 0, text_format("%06d", int_max(score, scores_best())));
        text_print(22, 0, text_format("%d", level + 1));
        text_set_style(HUD_LABEL);
        text_print(9, 0, "HI");
        text_print(20, 0, "LV");
        text_set_style(TEXT_NORMAL);
    }
    for (int i = 0; i < lives; i++)
        sprite_draw(SPR_PAW, 0, SCREEN_W - 8 - 9 * (MAX_LIVES - i), 0, SPRITE_ABOVE_HUD);
}

// --- Screens -----------------------------------------------------------------

// The frame and a level style's pattern behind it (loaded at black).
static void show_background(int s) {
    style = s % LEVEL_STYLES;
    flash = 0;
    map_load(&frame_layer);
    map_load(&pattern_layers[style]);
    build_backdrop();
    raster_backdrop(backdrop);
}

static void show_page(Page p, int frames) {
    page = p;
    page_timer = frames;
    text_clear();
    text_print_centered(0, text_format("HI %06d", scores_best()));
    if (p == PAGE_TITLE) { // the logo is sprites (draw_title_art)
        text_print_centered(7, "A SERVAL BRICK BREAKER");
        text_print_centered(14, "LEFT/RIGHT:MOVE  A:LAUNCH");
        text_print_centered(15, "START:PAUSE");
    } else {
        scores_draw(SCORES_ROW, new_rank);
    }
}

static void show_title(void) {
    state = TITLE;
    ecs_reset();
    psg_music_stop();
    show_background(0);
    show_page(new_rank >= 0 ? PAGE_SCORES : PAGE_TITLE,
              new_rank >= 0 ? NEW_SCORE_FRAMES : PAGE_FRAMES);
}

// The title's logo: "PAW" in big letters over "BREAKER", each letter riding
// a slow wave (a sine of the time and its place in the line).
static void draw_logo(void) {
    static const u8 paw[] = {SPR_LOGO_BIG_P, SPR_LOGO_BIG_A, SPR_LOGO_BIG_W};
    static const u8 breaker[] = {SPR_LOGO_B, SPR_LOGO_R, SPR_LOGO_E, SPR_LOGO_A,
                                 SPR_LOGO_K, SPR_LOGO_E, SPR_LOGO_R};
    u16 t = (u16)(frame_count() * 0x0300);
    for (int i = 0; i < 3; i++) {
        int bob = fx_to_int(fx_sin((u16)(t + i * 0x2000)) * 2);
        sprite_draw(paw[i], 0, SCREEN_W / 2 - 46 + i * 30, LOGO_Y + bob, 0);
    }
    for (int i = 0; i < 7; i++) {
        int bob = fx_to_int(fx_sin((u16)(t + 0x6000 + i * 0x1800)) * 2);
        sprite_draw(breaker[i], 0, SCREEN_W / 2 - 56 + i * 16, LOGO_Y + 28 + bob, 0);
    }
}

// The score table's medals, by the three best scores' place numbers.
static void draw_medals(void) {
    static const u8 medal[3] = {PAL_GOLD, PAL_SILVER, PAL_BRONZE};
    for (int i = 0; i < 3; i++)
        sprite_draw(SPR_MEDAL, 0, 28, (SCORES_ROW + 2 + i) * 8, SPRITE_PALETTE(medal[i]));
}

// Decoration on the title and the score table: the logo (title page) or the
// medals, an arch of bricks, the paddle and a ball.
static void draw_title_art(void) {
    static const u8 arch[2][10] = {
        {PAL_PURPLE, PAL_BLUE, PAL_GREEN, PAL_YELLOW, PAL_ORANGE, PAL_RED, PAL_ORANGE, PAL_YELLOW,
         PAL_GREEN, PAL_BLUE},
        {PAL_SILVER, PAL_RED, PAL_RED, PAL_GOLD, PAL_GOLD, PAL_GOLD, PAL_GOLD, PAL_RED, PAL_RED,
         PAL_SILVER},
    };
    if (page == PAGE_TITLE)
        draw_logo();
    else
        draw_medals();
    int arch_y = page == PAGE_TITLE ? ARCH_Y : ARCH_Y_SCORES;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 10; c++) {
            u8 color = arch[r][c];
            u8 frame = color == PAL_SILVER ? BRICK_FRAME_SILVER
                       : color == PAL_GOLD ? BRICK_FRAME_GOLD
                                           : BRICK_FRAME_PLAIN;
            sprite_draw(SPR_BRICK, frame, 40 + c * BRICK_W, arch_y + r * BRICK_H,
                        SPRITE_PALETTE(color));
        }
    }
    // The ball bobs above the paddle.
    int bob = (int)(frame_count() % 16);
    int y = PADDLE_Y - BALL_SIZE - (bob < 8 ? bob : 16 - bob);
    sprite_draw(SPR_BALL, 0, 117, y, 0);
    sprite_draw(SPR_PADDLE, 0, 104, PADDLE_Y, 0);
}

static void show_level(void) {
    state = PLAYING;
    show_background(level);
    text_clear();
    hud_dirty = true;
    play_start_level(level);
    text_print_centered(MESSAGE_ROW,
                        text_format("LEVEL %d: %s", level + 1, levels[level % LEVEL_COUNT].name));
    text_print_centered(MESSAGE_ROW + 2, "A: LAUNCH");
    intro_shown = true;
    psg_music_play(&level_song);
}

static void start_game(void) {
    // random_entropy() depends only on the buttons pressed so far and when:
    // the same input plays the same game on the GBA and the web.
    random_seed(random_entropy());
    score = 0;
    lives = START_LIVES;
    level = 0;
    new_rank = -1;
    psg_play(SND_START);
    fade_to(show_level);
}

static void next_level(void) {
    level++;
    show_level();
}

static void clear_messages(void) {
    for (int row = MESSAGE_ROW; row <= MESSAGE_ROW + 2; row++)
        text_print_line(0, row, "");
}

// --- States ------------------------------------------------------------------

static void update_title(void) {
    // For demos: hold L and R and press SELECT to erase the saved table.
    if (button_down(BUTTON_L) && button_down(BUTTON_R) && button_pressed(BUTTON_SELECT)) {
        scores_reset();
        new_rank = -1;
        psg_play(SND_RESET);
        show_page(PAGE_SCORES, PAGE_FRAMES);
        text_print_centered(12, "SCORES RESET");
        return;
    }
    if (button_pressed(BUTTON_START)) {
        start_game();
        return;
    }
    if (--page_timer == 0) {
        new_rank = -1; // the new score's moment is over
        show_page(page == PAGE_TITLE ? PAGE_SCORES : PAGE_TITLE, PAGE_FRAMES);
    }
    if (page == PAGE_TITLE) {
        if ((page_timer / 30) % 2)
            text_print_centered(12, "PRESS START");
        else
            text_print_line(0, 12, "");
    }
}

static void update_playing(void) {
    if (button_pressed(BUTTON_START)) {
        state = PAUSED;
        psg_music_pause(); // silent, and holding its place
        psg_play(SND_PAUSE);
        text_print_centered(PAUSE_ROW, "PAUSED");
        return;
    }
    switch (play_update()) {
    case PLAY_ON:
        break;
    case PLAY_BALL_LOST:
        state = BALL_LOST;
        timer = LOST_FRAMES;
        lives--;
        hud_dirty = true;
        psg_play(SND_BALL_LOST);
        flash_backdrop(COLOR_RGB(150, 20, 40));
        break;
    case PLAY_CLEARED:
        state = CLEARED;
        timer = CLEAR_FRAMES;
        add_score(CLEAR_BONUS);
        psg_music_play(&clear_song);
        flash_backdrop(COLOR_RGB(255, 230, 160));
        text_print_centered(MESSAGE_ROW, "LEVEL CLEAR!");
        text_print_centered(MESSAGE_ROW + 2, text_format("BONUS %d", CLEAR_BONUS));
        break;
    }
    if (intro_shown && !play_holding_ball() && state == PLAYING) {
        intro_shown = false;
        clear_messages();
    }
}

static void game_over(void) {
    state = GAME_OVER;
    timer = GAME_OVER_FRAMES;
    psg_music_stop();
    psg_play(SND_GAME_OVER);
    // Saved now, at a pause: the table shows it after the fade.
    new_rank = scores_add(score, level + 1);
    if (new_rank >= 0) {
        text_print_centered(MESSAGE_ROW, "GAME OVER");
        text_print_centered(MESSAGE_ROW + 2, text_format("NEW BEST SCORE: #%d", new_rank + 1));
    } else {
        text_print_centered(MESSAGE_ROW + 1, "GAME OVER");
    }
}

static void update_state(void) {
    bool start = button_pressed(BUTTON_START);
    switch (state) {
    case TITLE:
        update_title();
        break;
    case PLAYING:
        update_playing();
        break;
    case PAUSED:
        if (start) {
            state = PLAYING;
            psg_play(SND_PAUSE);
            psg_music_resume(); // where it stopped
            text_print_line(0, PAUSE_ROW, "");
        }
        break;
    case BALL_LOST:
        if (--timer > 0)
            break;
        if (lives > 0) {
            state = PLAYING;
            play_serve();
        } else {
            game_over();
        }
        break;
    case CLEARED:
        if (--timer == 0)
            fade_to(next_level);
        break;
    case GAME_OVER:
        if (--timer == 0 || start)
            fade_to(show_title);
        break;
    }
}

void game_init(void) {
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black while loading; the title fades in
    psg_table_set(sound_table, SOUND_COUNT);
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    tileset_load(&tileset);
    // The balls bounce off the frame's inside; the bottom is open, so a
    // missed ball falls out. Gravity pulls the capsules down; the balls have
    // none of it (body_gravity) and fly straight. body_contact tells which
    // wall a ball touched, and when anything left through the bottom.
    physics_set_bounds(FIELD_LEFT, FIELD_TOP, FIELD_RIGHT, SCREEN_H);
    physics_set_open_edges(PHYSICS_EDGE_BOTTOM);
    physics_set_gravity(0, FX_ONE / 16);
    physics_set_contacts(true);
    text_set_shadow(true); // white text stays readable over the pattern
    text_set_style_color(HUD_LABEL, COLOR_RGB(255, 200, 90), COLOR_RGB(0, 0, 0));
    // Only SPRITE_BLEND sprites (top 0) blend: the message banner and the
    // balls' trails, half and half with whatever background is behind them.
    screen_set_blend(0, LAYER_PLAYFIELD | LAYER_BACKGROUND | LAYER_BACKDROP, 8, 8);
    scores_load();
    show_title();
}

// The band behind the messages, while there are any: the level's name, pause,
// level clear and game over. Drawn after the game's sprites, so behind them,
// and semi-transparent: four 64x32 sprites across the field.
static void draw_banner(void) {
    static const u8 xs[4] = {FIELD_LEFT, FIELD_LEFT + 64, FIELD_LEFT + 128, FIELD_RIGHT - 64};
    bool messages = intro_shown || state == PAUSED || state == CLEARED || state == GAME_OVER;
    if (!messages)
        return;
    for (int i = 0; i < 4; i++)
        sprite_draw(SPR_BANNER, 0, xs[i], BANNER_Y, SPRITE_BLEND);
}

void game_frame(void) {
    if (!update_fade()) // fading out: the game waits
        update_state();
    update_flash();
    if (state == TITLE) {
        draw_title_art();
    } else {
        draw_hud();
        play_draw();
        draw_banner();
    }
}
