// Game states, the HUD, the camera, scoring and the goal.

#include "game.h"

typedef enum {
    TITLE,
    STAGE_CARD, // "STAGE 1" and the lives left, before the level starts
    PLAYING,
    PAUSED,
    DYING,
    GOAL_SLIDE, // down the pole, with the banner
    GOAL_WALK,  // to the den
    GOAL_TALLY, // time left becomes points
    STAGE_CLEAR,
    GAME_OVER
} State;

#define SKY COLOR_RGB(100, 168, 252)
#define START_LIVES 3
#define TIME_START 300
#define FRAMES_PER_TICK 24 // the countdown ticks a little faster than seconds
#define HURRY_TIME 100
#define HURRY_TEMPO 180 // the level's tune speeds up from 150 when time runs low
#define GEMS_PER_LIFE 20
#define CARD_FRAMES 150
#define CLEAR_FRAMES 300
#define GAME_OVER_FRAMES 300
// The camera scrolls once the serval is this far from the screen's left edge,
// and keeps it between these heights on screen (update_camera).
#define CAMERA_LEAD 104
#define CAMERA_HEAD 24
#define CAMERA_FEET 136
#define CAMERA_REST_Y (11 * TILE - CAMERA_FEET) // standing on the ground
#define FADE_STEP 2     // brightness per frame: 8 frames from black to normal
#define GLINT_PERIOD 64 // frames between glints sweeping over the bonus blocks
#define GLINT_STEP 4    // frames each step of the glint shows

static State state;
static int state_timer;
static int score, gems, lives, time_left, time_ticks;
static bool checkpoint_reached;
static int banner_y, banner_bottom; // world y of the goal banner while it slides
static int goal_bonus;
static int brightness = SCREEN_BRIGHTNESS_MIN; // the title fades in from black
static bool fading_out;
static void (*next_screen)(void); // shown once the fade out reaches black
int cam_x, cam_y;

// --- Scoring -----------------------------------------------------------------

void add_score(int points) {
    score = int_min(score + points, 999999);
}

void add_life(void) {
    lives = int_min(lives + 1, 9);
    psg_play(SND_ONE_UP);
}

void add_gem(void) {
    add_score(200);
    if (++gems == GEMS_PER_LIFE) {
        gems = 0;
        add_life();
    } else {
        psg_play(SND_GEM);
    }
}

// --- Screens and fades -------------------------------------------------------

// Fades to black, then calls show() to set up the next screen, which fades in.
static void fade_to(void (*show)(void)) {
    next_screen = show;
    fading_out = true;
}

// Steps the fade, once per frame right after frame_begin() (so the whole
// frame has the new brightness). Returns true while fading out: the game
// waits meanwhile.
static bool update_fade(void) {
    if (fading_out) {
        brightness = int_max(brightness - FADE_STEP, SCREEN_BRIGHTNESS_MIN);
        if (brightness == SCREEN_BRIGHTNESS_MIN) {
            fading_out = false;
            next_screen();
        }
    } else {
        brightness = int_min(brightness + FADE_STEP, 0);
    }
    screen_set_brightness(brightness);
    return fading_out;
}

// --- HUD and animated tiles ---------------------------------------------------

// One row at the top: score, gems, stage, time and lives. The icons are
// sprites drawn above the text layer.
static void draw_hud(void) {
    text_print_line(0, 0,
                    text_format(" %06d  x%02d  STAGE 1  %03d  x%d", score, gems, time_left, lives));
    sprite_draw(SPR_HUD_GEM, 0, 8 * 8, 0, SPRITE_ABOVE_HUD);
    sprite_draw(SPR_HUD_CLOCK, 0, 22 * 8, 0, SPRITE_ABOVE_HUD);
    sprite_draw(SPR_HUD_SERVAL, 0, 27 * 8, 0, SPRITE_ABOVE_HUD);
}

// Every GLINT_PERIOD frames a glint sweeps across the bonus blocks: their
// tiles are replaced, step by step, ending on the plain block. Every block
// showing those tiles changes at once.
static void animate_bonus_blocks(void) {
    u32 t = frame_count() % GLINT_PERIOD;
    if (t % GLINT_STEP == 0 && t / GLINT_STEP < BONUS_GLINT_FRAMES)
        tileset_set_tiles(TILE_BONUS, bonus_glint_tiles[t / GLINT_STEP], BONUS_TILE_COUNT);
}

static void draw_banner(void) {
    int y = state >= GOAL_SLIDE && state <= STAGE_CLEAR ? banner_y : (pole_top_my + 1) * TILE;
    sprite_draw(SPR_BANNER, 0, pole_mx * TILE - 9 - cam_x, y - cam_y, 0);
}

// --- Camera ------------------------------------------------------------------

// Follows the serval to the right but never back left. Vertically it rests
// with the ground near the bottom of the screen, and moves when the serval's
// feet go below CAMERA_FEET or its head above CAMERA_HEAD (screen y).
// camera_set() clamps the view to the playfield; reading it back keeps cam_x
// and cam_y in sync with it.
static void update_camera(void) {
    int x = fx_to_int(pos_x[player]);
    int top = fx_to_int(pos_y[player]);
    int feet = top + body_h[player];
    int y = cam_y;
    if (feet - y > CAMERA_FEET)
        y = feet - CAMERA_FEET;
    if (top - y < CAMERA_HEAD)
        y = top - CAMERA_HEAD;
    camera_set(int_max(cam_x, x - CAMERA_LEAD), y);
    cam_x = camera_x();
    cam_y = camera_y();
}

// --- States ------------------------------------------------------------------

static void show_title(void) {
    state = TITLE;
    time_left = TIME_START;
    state_timer = 0;
    ecs_reset();
    screen_set_backdrop(SKY);
    level_show();
    cam_x = 0;
    cam_y = CAMERA_REST_Y;
    camera_set(cam_x, cam_y);
    player_big = false;
    player_spawn(start_mx * TILE + 2, (start_my + 1) * TILE);
    text_clear();
    text_print_centered(5, "S E R V A L   D A S H");
    text_print_centered(13, "A:JUMP  B:RUN  START:PAUSE");
    text_print_centered(15, "A FIRST LEVEL, CLASSIC STYLE");
}

// A black screen with the stage and the lives left, before (re)starting.
static void show_stage_card(void) {
    state = STAGE_CARD;
    state_timer = CARD_FRAMES;
    ecs_reset();
    psg_music_stop();
    map_unload(1);
    map_unload(2);
    map_unload(3);
    screen_set_backdrop(COLOR_RGB(0, 0, 0));
    text_clear();
    text_print_centered(8, "STAGE 1");
    text_print_centered(11, text_format("x %d", lives));
}

static void start_level(void) {
    state = PLAYING;
    text_clear();
    screen_set_backdrop(SKY);
    level_show();
    int mx = checkpoint_reached ? checkpoint_mx : start_mx;
    int my = checkpoint_reached ? checkpoint_my : start_my;
    player_spawn(mx * TILE + 2, (my + 1) * TILE);
    cam_x = int_max(0, mx * TILE - 40);
    cam_y = CAMERA_REST_Y;
    camera_set(cam_x, cam_y);
    cam_x = camera_x();
    objects_reset(cam_x / TILE);
    time_left = TIME_START;
    time_ticks = 0;
    psg_music_play(&level_song);
}

static void start_game(void) {
    // The level plays the same for the same input on every platform:
    // random_entropy() depends only on the buttons pressed so far and when.
    random_seed(random_entropy());
    score = 0;
    gems = 0;
    lives = START_LIVES;
    checkpoint_reached = false;
    player_big = false;
    psg_play(SND_START);
    fade_to(show_stage_card);
}

static void game_over(void) {
    state = GAME_OVER;
    state_timer = GAME_OVER_FRAMES;
    ecs_reset();
    map_unload(1);
    map_unload(2);
    map_unload(3);
    screen_set_backdrop(COLOR_RGB(0, 0, 0));
    text_clear();
    text_print_centered(9, "GAME OVER");
    psg_play(SND_GAME_OVER);
}

void start_goal(void) {
    state = GOAL_SLIDE;
    // The serval grabs the pole, on its left side.
    ent_mask[player] &= ~C_MAPBODY;
    vel_x[player] = vel_y[player] = 0;
    pos_x[player] = FX(pole_mx * TILE + 6 - body_w[player]);
    // Points for how high it caught the pole.
    int height = pole_base_my * TILE - (fx_to_int(pos_y[player]) + body_h[player]);
    goal_bonus = height > 112  ? 5000
                 : height > 80 ? 2000
                 : height > 48 ? 800
                 : height > 16 ? 400
                               : 100;
    add_score(goal_bonus);
    text_print_centered(4, text_format("+%d", goal_bonus));
    banner_y = (pole_top_my + 1) * TILE;
    banner_bottom = pole_base_my * TILE - TILE;
    psg_music_stop();
    psg_play(SND_POLE);
}

static void update_time(void) {
    if (++time_ticks < FRAMES_PER_TICK)
        return;
    time_ticks = 0;
    if (time_left > 0 && --time_left == HURRY_TIME) {
        psg_play(SND_HURRY);
        psg_music_set_tempo(HURRY_TEMPO); // faster from where it is
    }
    if (time_left == 0)
        player_die(false);
}

static void update_playing(void) {
    if (button_pressed(BUTTON_START)) {
        state = PAUSED;
        psg_music_pause(); // silent, and holding its place
        psg_play(SND_PAUSE);
        text_print_centered(9, "PAUSED");
        return;
    }
    if (player_mode == PLAYER_NORMAL) {
        player_control();
        objects_spawn_ahead();
        objects_update();
        sys_map_movement(); // the serval, enemies, the fish, gems
        sys_movement();     // everything else...
        sys_physics();      // ...and gravity for the debris
        player_after_move();
        objects_after_move();
        sys_animate();
        update_time();
    } else {
        player_update_mode(); // growing or shrinking: everything else waits
    }
    if (player_mode == PLAYER_DYING) {
        state = DYING;
        return;
    }
    if (!checkpoint_reached && player_center_x() >= checkpoint_mx * TILE)
        checkpoint_reached = true;
    // Touching the pole, or its base (a serval that jumped short).
    if (fx_to_int(pos_x[player]) + body_w[player] >= pole_mx * TILE + 6 ||
        (fx_to_int(pos_x[player]) + body_w[player] >= pole_mx * TILE &&
         (body_contact[player] & MAP_CONTACT_RIGHT)))
        start_goal();
    update_camera();
}

static void update_goal_slide(void) {
    update_camera();
    int bottom = pole_base_my * TILE; // the top of the pole's base
    int feet = fx_to_int(pos_y[player]) + body_h[player];
    bool serval_down = feet >= bottom;
    if (!serval_down)
        pos_y[player] = FX(int_min(feet + 2, bottom) - body_h[player]);
    bool banner_down = banner_y >= banner_bottom;
    if (!banner_down)
        banner_y += 2;
    if (serval_down && banner_down && ++state_timer > 20) {
        // Around the pole and off toward the den.
        state = GOAL_WALK;
        state_timer = 0;
        pos_x[player] = FX(pole_mx * TILE + 10);
        ent_mask[player] |= C_MAPBODY;
        text_print_line(0, 4, "");
        psg_music_play(&goal_song);
    }
}

static void update_goal_walk(void) {
    player_auto_walk();
    sys_map_movement();
    sys_movement();
    sys_physics();
    objects_after_move();
    sys_animate();
    update_camera();
    if (player_center_x() >= den_door_x) {
        player_hidden = true;
        state = GOAL_TALLY;
        state_timer = 0;
    }
}

static void update_goal_tally(void) {
    if (time_left > 0) {
        int n = int_min(time_left, 2);
        time_left -= n;
        add_score(50 * n);
        if (state_timer++ % 3 == 0)
            psg_play(SND_TALLY);
    } else if (++state_timer > 60) {
        state = STAGE_CLEAR;
        state_timer = CLEAR_FRAMES;
        text_print_centered(7, "STAGE CLEAR!");
        text_print_centered(9, text_format("SCORE %d", score));
        psg_play(SND_ONE_UP);
    }
}

void game_init(void) {
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black while loading; the title fades in
    lives = START_LIVES;
    psg_table_set(sound_table, SOUND_COUNT);
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    tileset_load(&tileset);
    physics_set_gravity(0, GRAVITY);
    // Bodies that aren't map bodies (brick debris) fall through the level:
    // sys_physics' bounds have no walls.
    physics_set_open_edges(PHYSICS_EDGE_LEFT | PHYSICS_EDGE_RIGHT | PHYSICS_EDGE_TOP |
                           PHYSICS_EDGE_BOTTOM);
    text_set_shadow(true); // white text stays readable over the clouds
    level_build();
    show_title();
}

static void update_state(void) {
    bool start = button_pressed(BUTTON_START);
    switch (state) {
    case TITLE:
        if ((++state_timer / 30) % 2)
            text_print_centered(9, "PRESS START");
        else
            text_print_line(0, 9, "");
        if (start)
            start_game();
        break;
    case STAGE_CARD:
        if (--state_timer == 0)
            fade_to(start_level);
        break;
    case PLAYING:
        update_playing();
        break;
    case PAUSED:
        if (start) {
            state = PLAYING;
            psg_play(SND_PAUSE);
            psg_music_resume(); // where it stopped (square 1 once the tick ends)
            text_print_line(0, 9, "");
        }
        break;
    case DYING:
        player_update_mode();
        if (player_dying_done()) {
            lives--;
            fade_to(lives > 0 ? show_stage_card : game_over);
        }
        break;
    case GOAL_SLIDE:
        update_goal_slide();
        break;
    case GOAL_WALK:
        update_goal_walk();
        break;
    case GOAL_TALLY:
        update_goal_tally();
        break;
    case STAGE_CLEAR:
    case GAME_OVER:
        if (--state_timer == 0 || start)
            fade_to(show_title);
        break;
    }
}

void game_frame(void) {
    if (!update_fade()) // fading out: the game waits
        update_state();
    if (state != PAUSED)
        animate_bonus_blocks();

    // Drawing: the HUD icons, the serval and everything else, the banner.
    if (state == STAGE_CARD) {
        sprite_draw(SPR_HUD_SERVAL, 0, 12 * 8, 11 * 8, 0);
    } else if (state != GAME_OVER) {
        draw_hud();
        player_update_sprite(state == GOAL_SLIDE);
        sys_render();
        draw_banner();
    }
}
