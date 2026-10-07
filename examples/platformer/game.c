// Game states, the HUD, the camera, scoring, the goal and the order of the
// stages.

#include "game.h"

// The stages, played in this order.
static const StageDef* const stages[] = {
    &stage_overworld,
};
#define STAGES ((int)(sizeof stages / sizeof stages[0]))

typedef enum {
    TITLE,
    STAGE_CARD, // "STAGE 1-1" and the lives left, before the level starts
    PLAYING,
    PAUSED,
    DYING,
    GOAL_SLIDE, // down the pole, with the banner
    GOAL_WALK,  // to the den
    GOAL_TALLY, // time left becomes points
    STAGE_CLEAR,
    GAME_OVER,
    ENDING // after the last stage (its ending hooks)
} State;

#define START_LIVES 3
#define FRAMES_PER_TICK 24 // the countdown ticks a little faster than seconds
#define HURRY_TIME 100
#define GEMS_PER_LIFE 20
#define CARD_FRAMES 150
#define CLEAR_FRAMES 300
#define GAME_OVER_FRAMES 300
// The camera scrolls once the serval is this far from the screen's left edge,
// and keeps it between these heights on screen (update_camera).
#define CAMERA_LEAD 104
#define CAMERA_HEAD 24
#define CAMERA_FEET 136
#define CAMERA_REST_Y ((start_my + 1) * TILE - CAMERA_FEET) // standing at the start
#define FADE_STEP 2     // brightness per frame: 8 frames from black to normal
#define GLINT_PERIOD 64 // frames between glints sweeping over the bonus blocks
#define GLINT_STEP 4    // frames each step of the glint shows

const StageDef* stage;
static int stage_index;      // in stages[]
static u32 stage_group_mark; // sprite groups loaded after this are the stage's
static State state;
static int state_timer;
static int score, gems, lives, time_left, time_ticks;
static bool checkpoint_reached;
static int banner_y, banner_bottom; // world y of the goal banner while it slides
static int goal_bonus;
static int brightness = SCREEN_BRIGHTNESS_MIN; // the title fades in from black
static bool fading_out;
static void (*next_screen)(void); // shown once the fade out reaches black
static int title_stage;           // the stage START begins with, picked with SELECT
int cam_x, cam_y;

// --- Saved progress ------------------------------------------------------------

// How far the player has come and the best score, in one small struct in one
// save slot: the cartridge's Flash on the GBA (SAVE FLASH64K in
// examples/CMakeLists.txt), the browser's localStorage on the web. Only
// fixed-size fields: no pointers, which would mean nothing after a power-off.
typedef struct {
    u8 reached;   // stages reached so far, 1 to STAGES: the title can start any of them
    u8 unused[3]; // keeps best_score 4 bytes in; saved as 0
    u32 best_score;
} Progress;

// PROGRESS_VERSION is the layout of Progress: raise it when the struct
// changes, and save_read() reports a save in the old layout as
// SAVE_OTHER_VERSION instead of reading it as garbage.
#define PROGRESS_SLOT 0
#define PROGRESS_VERSION 1

static Progress progress;

static void progress_load(void) {
    if (save_read(PROGRESS_SLOT, &progress, sizeof progress, PROGRESS_VERSION) != SAVE_OK ||
        progress.reached < 1 || progress.reached > STAGES)
        // A first boot (SAVE_EMPTY), a damaged save (SAVE_CORRUPT) or another
        // layout: only the first stage, no best score. Nothing is written
        // until there is progress to save.
        progress = (Progress){.reached = 1};
}

// Records reaching stage_index and the score so far, saving only if either
// is new: a Flash write takes a few frames (an erase and about 40
// microseconds a byte), so it happens on black screens (the stage card, game
// over, the ending), never during play.
static void progress_update(void) {
    bool changed = false;
    if (stage_index + 1 > progress.reached) {
        progress.reached = (u8)(stage_index + 1);
        changed = true;
    }
    if ((u32)score > progress.best_score) {
        progress.best_score = (u32)score;
        changed = true;
    }
    if (changed)
        save_write(PROGRESS_SLOT, &progress, sizeof progress, PROGRESS_VERSION);
}

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

// One row at the top, all 30 columns: score, gems, stage (on the title, the
// one START begins with), time and lives. The icons are sprites drawn above
// the text layer.
static void draw_hud(void) {
    const char* name = stages[state == TITLE ? title_stage : stage_index]->name;
    text_print_line(
        0, 0, text_format("%06d  x%02d STAGE %s  %03d  x%d", score, gems, name, time_left, lives));
    sprite_draw(SPR_HUD_GEM, 0, 7 * 8, 0, SPRITE_ABOVE_HUD);
    sprite_draw(SPR_HUD_CLOCK, 0, 22 * 8, 0, SPRITE_ABOVE_HUD);
    sprite_draw(SPR_HUD_SERVAL, 0, 27 * 8, 0, SPRITE_ABOVE_HUD);
}

// Every GLINT_PERIOD frames a glint sweeps across the bonus blocks: their
// tiles are replaced, step by step, ending on the plain block. Every block
// showing those tiles changes at once.
static void animate_bonus_blocks(void) {
    u32 t = frame_count() % GLINT_PERIOD;
    if (t % GLINT_STEP == 0 && t / GLINT_STEP < BONUS_GLINT_FRAMES)
        tileset_set_tiles(stage->bonus_tile, bonus_glint_tiles[t / GLINT_STEP], BONUS_TILE_COUNT);
}

static void draw_banner(void) {
    if (pole_mx < 0)
        return; // a stage without a pole
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

// --- Stages ------------------------------------------------------------------

// Makes stages[index] the stage: its tileset, its sprite group (in place of
// the last stage's, after the global group's mark) and its level, built into
// the map buffers. With the screen black: it writes VRAM at once, and the
// buffers may be on screen until the maps are unloaded.
static void load_stage(int index) {
    stage_index = index;
    stage = stages[index];
    sprite_groups_release(stage_group_mark);
    sprite_group_load(stage->sprites);
    tileset_load(stage->tileset);
    level_build();
}

// --- States ------------------------------------------------------------------

static void show_title(void) {
    state = TITLE;
    state_timer = 0;
    ecs_reset();
    map_unload(1);
    map_unload(2);
    map_unload(3);
    load_stage(0); // the title shows the first stage's start
    time_left = stage->time;
    screen_set_backdrop(stage->backdrop);
    level_show();
    cam_x = 0;
    cam_y = CAMERA_REST_Y;
    camera_set(cam_x, cam_y);
    player_big = false;
    player_spawn(start_mx * TILE + 2, (start_my + 1) * TILE);
    text_clear();
    text_print_centered(5, "S E R V A L   D A S H");
    text_print_centered(13, "A:JUMP  B:RUN  START:PAUSE");
    text_print_centered(15, text_format("BEST %06d", (int)progress.best_score));
    if (title_stage >= progress.reached)
        title_stage = 0;
    if (progress.reached > 1)
        text_print_centered(17, text_format("SELECT: STAGE %s", stages[title_stage]->name));
}

// SELECT on the title: the next stage reached, back to the first after the
// last.
static void select_stage(void) {
    if (progress.reached < 2)
        return;
    title_stage = (title_stage + 1) % progress.reached;
    text_print_centered(17, text_format("SELECT: STAGE %s", stages[title_stage]->name));
    psg_play(SND_PAUSE);
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
    load_stage(stage_index); // also rebuilds the level after a lost life
    text_clear();
    text_print_centered(8, text_format("STAGE %s", stage->name));
    text_print_centered(11, text_format("x %d", lives));
    progress_update(); // a stage reached for the first time, a best score
}

static void start_level(void) {
    state = PLAYING;
    text_clear();
    screen_set_backdrop(stage->backdrop);
    level_show();
    int mx = checkpoint_reached ? checkpoint_mx : start_mx;
    int my = checkpoint_reached ? checkpoint_my : start_my;
    player_spawn(mx * TILE + 2, (my + 1) * TILE);
    cam_x = int_max(0, mx * TILE - 40);
    cam_y = CAMERA_REST_Y;
    camera_set(cam_x, cam_y);
    cam_x = camera_x();
    objects_reset(cam_x / TILE);
    time_left = stage->time;
    time_ticks = 0;
    if (stage->start)
        stage->start();
    psg_music_play(stage->song);
}

static void start_game(void) {
    // The level plays the same for the same input on every platform:
    // random_entropy() depends only on the buttons pressed so far and when.
    random_seed(random_entropy());
    score = 0;
    gems = 0;
    lives = START_LIVES;
    stage_index = title_stage;
    checkpoint_reached = false;
    player_big = false;
    psg_play(SND_START);
    fade_to(show_stage_card);
}

// The ending, after the last stage: its hooks show it on a black screen.
static void show_ending(void) {
    state = ENDING;
    ecs_reset();
    psg_music_stop();
    map_unload(1);
    map_unload(2);
    map_unload(3);
    screen_set_backdrop(COLOR_RGB(0, 0, 0));
    text_clear();
    progress_update();
    if (stage->ending_start)
        stage->ending_start();
}

// After "STAGE CLEAR!": on to the next stage, keeping the score, gems, lives
// and the serval's size; after the last one, its ending.
static void next_stage(void) {
    checkpoint_reached = false;
    if (stage_index + 1 < STAGES) {
        stage_index++;
        show_stage_card();
    } else {
        show_ending();
    }
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
    progress_update();
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
        psg_music_set_tempo(stage->hurry_tempo); // faster from where it is
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
    int right = fx_to_int(pos_x[player]) + body_w[player];
    if (pole_mx >= 0) {
        // Touching the pole, or its base (a serval that jumped short).
        if (right >= pole_mx * TILE + 6 ||
            (right >= pole_mx * TILE && (body_contact[player] & MAP_CONTACT_RIGHT)))
            start_goal();
    } else if (player_center_x() >= exit_x + TILE) {
        goal_start_exit(); // a stage without a pole: into its exit
    }
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
        pos_x[player] = FX(pole_mx * TILE + 10);
        ent_mask[player] |= C_MAPBODY;
        text_print_line(0, 4, "");
        goal_start_exit();
    }
}

void goal_start_exit(void) {
    state = GOAL_WALK;
    state_timer = 0;
    psg_music_play(&goal_song); // in place of the stage's tune
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
    // Sprites in layers: the global group (the serval, gems, the HUD...)
    // stays loaded, and each stage's group is loaded after this mark,
    // released to it when the next stage loads (load_stage).
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&global_group);
    stage_group_mark = sprite_groups_mark();
    physics_set_gravity(0, GRAVITY);
    // Bodies that aren't map bodies (brick debris) fall through the level:
    // sys_physics' bounds have no walls.
    physics_set_open_edges(PHYSICS_EDGE_LEFT | PHYSICS_EDGE_RIGHT | PHYSICS_EDGE_TOP |
                           PHYSICS_EDGE_BOTTOM);
    text_set_shadow(true); // white text stays readable over the clouds
    progress_load();
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
        else if (button_pressed(BUTTON_SELECT))
            select_stage();
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
        if (--state_timer == 0 || start)
            fade_to(stage_index + 1 < STAGES || stage->ending_start ? next_stage : show_title);
        break;
    case GAME_OVER:
        if (--state_timer == 0 || start)
            fade_to(show_title);
        break;
    case ENDING:
        if (!stage->ending_update || stage->ending_update())
            fade_to(show_title);
        break;
    }
}

void game_frame(void) {
    if (!update_fade()) // fading out: the game waits
        update_state();
    if (state == TITLE || (state >= PLAYING && state <= STAGE_CLEAR && state != PAUSED)) {
        animate_bonus_blocks();
        if (state != TITLE && stage->effects)
            stage->effects();
    }

    // Drawing: the HUD icons, the serval and everything else, the banner.
    if (state == STAGE_CARD) {
        sprite_draw(SPR_HUD_SERVAL, 0, 12 * 8, 11 * 8, 0);
    } else if (state != GAME_OVER && state != ENDING) {
        draw_hud();
        player_update_sprite(state == GOAL_SLIDE);
        sys_render();
        if (stage->draw)
            stage->draw();
        draw_banner();
    }
}
