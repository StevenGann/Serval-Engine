// Game states, scrolling, the HUD panel, fades and the stage's flow: waves,
// the warning, the boss, stage clear, game over and the high-score table.

#include "game.h"

#include "scores.h"

typedef enum {
    TITLE, // attract mode: the title and the high-score table in turn
    PLAYING,
    PAUSED,
    GAME_OVER,
    CLEAR, // the boss is down: bonus tally
    ENTRY, // initials for a new high score
} State;

typedef enum { STAGE_WAVES, STAGE_WARNING, STAGE_BOSS } StagePhase;

#define BACKDROP COLOR_RGB(6, 4, 16)
#define FADE_STEP 2         // brightness per frame: 8 frames from black to normal
#define PAGE_FRAMES 300     // attract pages alternate every 5 seconds
#define INTRO_FRAMES 180    // "STAGE 1" over the start of the stage
#define WARNING_LEAD 360    // the warning starts this many pixels before the boss
#define FIRST_EXTEND 50000  // an extra ship at 50,000 points...
#define EXTEND_EVERY 100000 // ...and every 100,000 after
#define MAX_LIVES 9

u8 kind[MAX_ENT];
s16 hp[MAX_ENT];
u16 timer[MAX_ENT];
u8 flash[MAX_ENT];
u8 drops[MAX_ENT];
int shot_count, enemy_count, ebullet_count, fx_count, item_count;
int cam_y;
int score;
int stage_frame;
bool player_alive;

static State state;
static StagePhase stage_phase;
static int state_timer;
static bool scores_page; // TITLE: showing the table instead of the title
static int highlight;    // the table entry to mark (the newest), or -1
static int next_extend;
static bool cleared; // this run beat the boss
static int bonus;    // CLEAR: points still to count into the score
static int fade = SCREEN_BRIGHTNESS_MIN;
static int flash_level; // a white flash fading out (stage clear)
static bool fading_out;
static void (*next_screen)(void);
#ifdef SERVAL_DEBUG
static bool show_cpu;
static u32 cpu_peak;
#endif

// --- Entities ----------------------------------------------------------------

u32 spawn(u32 components, u16 sprite, int w, int h, FIXED cx, FIXED cy) {
    Entity e = entity_create(C_POS | C_SPR | components);
    if (e == ENTITY_NONE)
        return MAX_ENT;
    u32 i = entity_index(e);
    pos_x[i] = cx - FX(w) / 2;
    pos_y[i] = cy - FX(h) / 2;
    body_w[i] = (u8)w;
    body_h[i] = (u8)h;
    spr_id[i] = sprite;
    // Flying things stay on the screen while the camera climbs the map.
    spr_flags[i] = components & C_GROUND ? 0 : SPRITE_SCREEN;
    return i;
}

FIXED center_x(u32 i) {
    return pos_x[i] + FX(body_w[i]) / 2;
}

FIXED center_y(u32 i) {
    FIXED top = ent_has(i, C_GROUND) ? pos_y[i] - FX(cam_y) : pos_y[i];
    return top + FX(body_h[i]) / 2;
}

int screen_y(u32 i) {
    return fx_to_int(pos_y[i]) - (ent_has(i, C_GROUND) ? cam_y : 0);
}

void destroy_all(u32 components) {
    ECS_FOR_EACH(i, components) {
        entity_destroy(entity_at(i));
    }
}

void add_score(int points) {
    score = int_min(score + points, 9999999);
    if (score >= next_extend) {
        next_extend += EXTEND_EVERY;
        lives = int_min(lives + 1, MAX_LIVES);
        psg_play(SND_EXTEND);
    }
}

// --- Scrolling and gathering -------------------------------------------------------

SlotList shots, enemies, ebullets, items, effects;

// Each kind's slots, listed at the start of the frame. ecs_gather scans the
// pool in IWRAM, about a quarter of what an ECS_FOR_EACH costs from ROM.
static void gather(void) {
    shot_count = shots.count = (int)ecs_gather(C_SHOT, shots.slot);
    enemy_count = enemies.count = (int)ecs_gather(C_ENEMY, enemies.slot);
    ebullet_count = ebullets.count = (int)ecs_gather(C_EBULLET, ebullets.slot);
    item_count = items.count = (int)ecs_gather(C_ITEM, items.slot);
    fx_count = effects.count = (int)ecs_gather(C_FX, effects.slot);
}

static int drift; // pixels the stars have scrolled on by themselves (the boss fight)

// The camera climbs the stage a pixel per frame, scrolling the map and the
// turrets on it down the screen; flying things have SPRITE_SCREEN, so they
// stay put. At the top of the map, where the boss waits, the camera stops
// and the starfield scrolls on by itself for as long as the fight lasts:
// map_set_scroll moves it half a pixel per frame (its scroll_factor's speed),
// counting on rather than wrapping, so the stars never jump.
static void scroll(void) {
    if (cam_y > 0) {
        camera_set(0, --cam_y);
    } else {
        drift++;
        map_set_scroll(3, 0, -((drift + 1) / 2)); // rounded down, as for the camera
    }
    gather();
}

// --- Text in the field and on the panel --------------------------------------------

// Text in the field: centered in, or cleared from, its 22 columns, leaving
// the panel's half of the row alone.
#define FIELD_COLS PANEL_COL

static int shown_score = -1, shown_best = -1, shown_lives = -1, shown_bombs = -1, shown_power = -1;

static void panel_labels(void) {
    text_print(PANEL_COL + 1, 1, "SCORE");
    text_print(PANEL_COL + 1, 4, "HI");
    text_print(PANEL_COL + 1, 7, "SHIPS");
    text_print(PANEL_COL + 1, 10, "BOMBS");
    text_print(PANEL_COL + 1, 13, "POWER");
    shown_score = shown_best = shown_lives = shown_bombs = shown_power = -1;
}

// Values are rewritten only when they change.
static void draw_panel(void) {
    int best = int_max(score, scores_best());
    if (score != shown_score)
        text_print(PANEL_COL, 2, text_format("%8d", shown_score = score));
    if (best != shown_best)
        text_print(PANEL_COL, 5, text_format("%8d", shown_best = best));
    if (lives != shown_lives)
        text_print(PANEL_COL + 3, 8, text_format("x%d", shown_lives = lives));
    if (bombs != shown_bombs)
        text_print(PANEL_COL + 3, 11, text_format("x%d", shown_bombs = bombs));
    if (power != shown_power) {
        shown_power = power;
        text_print(PANEL_COL + 1, 14,
                   power >= MAX_POWER ? "MAX   " : text_format("LV %d  ", power));
    }
    // The icons: sprites above the panel (background 1), below its text.
    sprite_draw(SPR_ICON_SHIP, 0, (PANEL_COL + 1) * 8, 8 * 8, SPRITE_ABOVE_FOREGROUND);
    sprite_draw(SPR_ICON_BOMB, 0, (PANEL_COL + 1) * 8, 11 * 8, SPRITE_ABOVE_FOREGROUND);
#ifdef SERVAL_DEBUG
    // SELECT shows the CPU load (this frame and the highest since the stage
    // or the boss fight began, in percent of a frame) and the live entities.
    u32 p = frame_cpu_permille();
    cpu_peak = p > cpu_peak ? p : cpu_peak;
    if (button_pressed(BUTTON_SELECT)) {
        show_cpu = !show_cpu;
        text_clear_area(PANEL_COL, 16, 8, 3);
    }
    if (show_cpu) {
        text_print(PANEL_COL, 16, text_format("C %3u.%u", p / 10, p % 10));
        text_print(PANEL_COL, 17, text_format("P %3u.%u", cpu_peak / 10, cpu_peak % 10));
        text_print(PANEL_COL, 18, text_format("E %3u", ecs_count(0)));
    }
#endif
}

// --- Fades ---------------------------------------------------------------------

// Fades to black, then calls show() to set up the next screen, which fades in.
static void fade_to(void (*show)(void)) {
    next_screen = show;
    fading_out = true;
}

// Once per frame, right after frame_begin(), so a whole frame has the new
// brightness. A fade wins over the white flashes of bombs and stage clear.
static void update_brightness(void) {
    if (fading_out) {
        fade = int_max(fade - FADE_STEP, SCREEN_BRIGHTNESS_MIN);
        if (fade == SCREEN_BRIGHTNESS_MIN) {
            fading_out = false;
            next_screen();
        }
    } else {
        fade = int_min(fade + FADE_STEP, 0);
    }
    if (flash_level > 0)
        flash_level--;
    screen_set_brightness(fade < 0 ? fade : int_max(flash_level, bomb_glow / 2));
}

// --- Screens -------------------------------------------------------------------

static void show_title_page(void) {
    text_clear();
    if (scores_page) {
        text_print_centered(1, "PRESS START");
        scores_draw(highlight);
    } else {
        text_print_centered(4, "S T A R   V E L D T");
        text_print_centered(6, "A SERVAL ENGINE SHOOTER");
        text_print_centered(13, "D-PAD:MOVE A:FIRE B:BOMB");
        text_print_centered(15, "R:FOCUS  START:PAUSE");
        text_print_centered(19, text_format("HI %d", scores_best()));
    }
}

static void enter_title(bool table, int frames) {
    state = TITLE;
    scores_page = table;
    state_timer = frames;
    ecs_reset();
    stage_hide();
    map_set_scroll(3, 0, 0); // the stars follow the camera again
    map_load(&stars_layer);
    psg_music_stop();
    show_title_page();
}

static void show_title(void) {
    enter_title(false, PAGE_FRAMES);
}

static void show_new_scores(void) {
    enter_title(true, 2 * PAGE_FRAMES);
}

static void begin_stage(void) {
    state = PLAYING;
    stage_phase = STAGE_WAVES;
    stage_frame = 0;
    ecs_reset();
    enemies_reset();
    boss_reset();
    cam_y = stage_start_y;
    camera_set(0, cam_y); // before loading the layers, so they are drawn there
    drift = 0;
    map_set_scroll(3, 0, 0);
    stage_show();
    text_clear();
    panel_labels();
    text_print_centered_in(0, FIELD_COLS, 7, "STAGE 1");
    text_print_centered_in(0, FIELD_COLS, 9, "THE STAR VELDT");
    player_spawn();
    psg_music_play(&stage_song);
#ifdef SERVAL_DEBUG
    cpu_peak = 0;
#endif
}

static void start_game(void) {
    // Every game differs, and the same input plays the same game on the GBA
    // and the web: random_entropy() depends only on the buttons pressed so far.
    random_seed(random_entropy());
    score = 0;
    next_extend = FIRST_EXTEND;
    cleared = false;
    player_reset();
    psg_play(SND_START);
    fade_to(begin_stage);
}

static void show_entry(void) {
    state = ENTRY;
    ecs_reset();
    stage_hide();
    map_set_scroll(3, 0, 0);
    map_load(&stars_layer);
    psg_music_stop();
    entry_begin(score, cleared);
    psg_play(SND_EXTEND);
}

// After game over or stage clear: the initials screen if the score made the
// table, otherwise the title.
static void after_game(void) {
    highlight = scores_rank(score);
    if (highlight >= 0)
        show_entry();
    else
        show_title();
}

static void game_over(void) {
    state = GAME_OVER;
    state_timer = 0;
    psg_music_stop();
    psg_play(SND_GAME_OVER);
    text_print_centered_in(0, FIELD_COLS, 8, "GAME OVER");
}

static void start_clear(void) {
    state = CLEAR;
    state_timer = 0;
    cleared = true;
    flash_level = 16; // a white flash, fading
    invulnerable = 10000;
    bonus = lives * 10000 + bombs * 2000;
    psg_music_play(&clear_song);
    text_print_centered_in(0, FIELD_COLS, 6, "STAGE CLEAR!");
}

// --- Each frame ------------------------------------------------------------------

// Everything that moves: in play, and behind GAME OVER and STAGE CLEAR.
static void update_world(void) {
    scroll(); // and gather()
    stage_frame++;
    stage_spawn_ground();
    if (stage_phase == STAGE_WAVES)
        enemies_spawn_waves();
    player_update();
    enemies_update();
    boss_update();
    sys_path();
    sys_movement();
    sys_animate();
    enemies_collide();
    enemies_cull();
    stage_animate();
}

// The stage's flow: the intro text, the warning, the boss.
static void update_stage(void) {
    if (stage_frame == INTRO_FRAMES) {
        text_clear_area(0, 7, FIELD_COLS, 1);
        text_clear_area(0, 9, FIELD_COLS, 1);
    }
    switch (stage_phase) {
    case STAGE_WAVES:
        if (cam_y <= WARNING_LEAD) {
            stage_phase = STAGE_WARNING;
            state_timer = 0;
            psg_music_stop();
            psg_play(SND_WARNING);
        }
        break;
    case STAGE_WARNING:
        state_timer++;
        if (state_timer % 32 == 0)
            text_print_centered_in(0, FIELD_COLS, 8, "WARNING");
        else if (state_timer % 32 == 20)
            text_clear_area(0, 8, FIELD_COLS, 1);
        if (state_timer == 40)
            text_print_centered_in(0, FIELD_COLS, 10, "THE HIVE LANTERN");
        if (state_timer == 150)
            psg_play(SND_WARNING);
        if (cam_y == 0) { // the top of the map
            stage_phase = STAGE_BOSS;
            text_clear_area(0, 8, FIELD_COLS, 1);
            text_clear_area(0, 10, FIELD_COLS, 1);
            boss_start();
#ifdef SERVAL_DEBUG
            cpu_peak = 0;
#endif
        }
        break;
    case STAGE_BOSS:
        if (boss_defeated())
            start_clear();
        break;
    }
}

static void update_playing(bool start) {
    if (start) {
        state = PAUSED;
        psg_music_pause(); // silent, and holding its place
        psg_play(SND_PAUSE);
        text_print_centered_in(0, FIELD_COLS, 12, "PAUSED");
        return;
    }
    update_world();
    update_stage();
    if (!player_alive && player_respawn_due()) {
        if (lives > 0)
            player_spawn();
        else
            game_over();
    }
}

static void update_clear(bool start) {
    update_world();
    state_timer++;
    if (state_timer == 60)
        text_print_centered_in(0, FIELD_COLS, 8, "BONUS");
    if (state_timer >= 60 && bonus > 0) {
        int n = int_min(bonus, 500);
        bonus -= n;
        add_score(n);
        if (state_timer % 4 == 0)
            psg_play(SND_TALLY);
    }
    if (state_timer >= 60)
        text_print_centered_in(0, FIELD_COLS, 9, text_format("%d", bonus));
    if (state_timer >= 480 || (bonus == 0 && start))
        fade_to(after_game);
}

static void update_entry(void) {
    camera_set(0, --cam_y); // the stars drift on
    switch (entry_update()) {
    case ENTRY_LETTER:
        psg_play(SND_LETTER);
        break;
    case ENTRY_MOVE:
        psg_play(SND_MOVE);
        break;
    case ENTRY_DONE:
        psg_play(SND_CONFIRM);
        fade_to(show_new_scores);
        break;
    case ENTRY_NONE:
        break;
    }
}

static void update_title(bool start) {
    camera_set(0, --cam_y); // the stars drift by
    if (start) {
        highlight = -1;
        start_game();
        return;
    }
    if (!scores_page) {
        if ((state_timer / 30) % 2)
            text_print_centered(10, "PRESS START");
        else
            text_print_line(0, 10, "");
    }
    if (--state_timer == 0) {
        scores_page = !scores_page;
        state_timer = PAGE_FRAMES;
        if (!scores_page)
            highlight = -1; // the newest entry is marked only the first time
        show_title_page();
    }
}

void game_init(void) {
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black while loading; the title fades in
    screen_set_backdrop(BACKDROP);
    art_build();
    stage_build();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    tileset_load(&tileset);
    psg_table_set(sound_table, SOUND_COUNT);
    text_set_shadow(true); // readable over stars and explosions
    scores_load();
    highlight = -1;
    show_title();
}

void game_frame(void) {
    update_brightness();
    bool start = button_pressed(BUTTON_START);
    if (!fading_out) {
        switch (state) {
        case TITLE:
            update_title(start);
            break;
        case PLAYING:
            update_playing(start);
            break;
        case PAUSED:
            if (start) {
                state = PLAYING;
                psg_play(SND_PAUSE);
                psg_music_resume(); // where it stopped
                text_clear_area(0, 12, FIELD_COLS, 1);
            }
            break;
        case GAME_OVER:
            update_world();
            if (++state_timer >= 300 || (state_timer > 60 && start))
                fade_to(after_game);
            break;
        case CLEAR:
            update_clear(start);
            break;
        case ENTRY:
            update_entry();
            break;
        }
    }

    // Drawing: sprites drawn by hand first (in front), then every entity,
    // higher spr_depth in front (game.h lists the depths).
    if (state == TITLE && !scores_page)
        sprite_draw(SPR_SHIP, (u8)(frame_count() / 3 % 2), 120, 138, 0);
    if (state == PLAYING || state == PAUSED || state == GAME_OVER || state == CLEAR) {
        draw_panel();
        player_draw();
    }
    sys_render_by_depth();
}
