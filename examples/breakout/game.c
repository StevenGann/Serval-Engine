// Screens and states: the title and the score table, levels with their
// intro, pause, a lost ball, level clear and game over, the HUD, and the
// fades between screens.

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
#define MESSAGE_ROW 13 // below the lowest bricks, above the paddle
#define PAUSE_ROW 16

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

// The top row: score, best score and level as text, lives as paw prints
// (sprites above the text layer).
static void draw_hud(void) {
    if (hud_dirty) {
        hud_dirty = false;
        text_print_line(
            0, 0,
            text_format(" %06d  HI %06d  L%d", score, int_max(score, scores_best()), level + 1));
    }
    for (int i = 0; i < lives; i++)
        sprite_draw(SPR_PAW, 0, SCREEN_W - 8 - 9 * (MAX_LIVES - i), 0, SPRITE_ABOVE_HUD);
}

// --- Screens -----------------------------------------------------------------

// The frame and a level style's pattern behind it (loaded at black).
static void show_background(int style) {
    map_load(&frame_layer);
    map_load(&pattern_layers[style % LEVEL_STYLES]);
}

static void show_page(Page p, int frames) {
    page = p;
    page_timer = frames;
    text_clear();
    text_print_centered(0, text_format("HI %06d", scores_best()));
    if (p == PAGE_TITLE) {
        text_print_centered(4, "P A W   B R E A K E R");
        text_print_centered(6, "A SERVAL BRICK BREAKER");
        text_print_centered(14, "LEFT/RIGHT:MOVE  A:LAUNCH");
        text_print_centered(15, "START:PAUSE");
    } else {
        scores_draw(4, new_rank);
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

// Decoration on the title page: an arch of bricks (title page only), the
// paddle and a ball.
static void draw_title_art(void) {
    static const u8 arch[2][10] = {
        {PAL_PURPLE, PAL_BLUE, PAL_GREEN, PAL_YELLOW, PAL_ORANGE, PAL_RED, PAL_ORANGE, PAL_YELLOW,
         PAL_GREEN, PAL_BLUE},
        {PAL_SILVER, PAL_RED, PAL_RED, PAL_GOLD, PAL_GOLD, PAL_GOLD, PAL_GOLD, PAL_RED, PAL_RED,
         PAL_SILVER},
    };
    for (int r = 0; r < 2 && page == PAGE_TITLE; r++) {
        for (int c = 0; c < 10; c++) {
            u8 color = arch[r][c];
            u8 frame = color == PAL_SILVER ? BRICK_FRAME_SILVER
                       : color == PAL_GOLD ? BRICK_FRAME_GOLD
                                           : BRICK_FRAME_PLAIN;
            sprite_draw(SPR_BRICK, frame, 40 + c * BRICK_W, 72 + r * BRICK_H,
                        SPRITE_PALETTE(color));
        }
    }
    // The ball bobs above the paddle.
    int bob = (int)(frame_count() % 16);
    int y = PADDLE_Y - BALL_SIZE - (bob < 8 ? bob : 16 - bob);
    sprite_draw(SPR_BALL, 0, 117, y, 0);
    sprite_draw(SPR_PADDLE_LEFT, 0, 104, PADDLE_Y, 0);
    sprite_draw(SPR_PADDLE_RIGHT, 0, 120, PADDLE_Y, 0);
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
    for (int row = MESSAGE_ROW; row <= PAUSE_ROW; row++)
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
        break;
    case PLAY_CLEARED:
        state = CLEARED;
        timer = CLEAR_FRAMES;
        add_score(CLEAR_BONUS);
        psg_music_play(&clear_song);
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
    text_print_centered(MESSAGE_ROW, "GAME OVER");
    // Saved now, at a pause: the table shows it after the fade.
    new_rank = scores_add(score, level + 1);
    if (new_rank >= 0)
        text_print_centered(MESSAGE_ROW + 2, text_format("NEW BEST SCORE: #%d", new_rank + 1));
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
    scores_load();
    show_title();
}

void game_frame(void) {
    if (!update_fade()) // fading out: the game waits
        update_state();
    if (state == TITLE) {
        draw_title_art();
    } else {
        draw_hud();
        play_draw();
    }
}
