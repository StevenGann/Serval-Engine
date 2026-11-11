// Acacia Climb on screen: see climb.h.
//
// The trunk is a background layer (background 2, 16 x 10 metatiles that wrap)
// and the sky another (background 3, a gradient the height of the screen
// with its clouds drifting); the camera scrolls the trunk with
// map_set_scroll(), 160 pixels being one lap of the picture. The boughs and
// the nests are sprites, not tiles: they sway, snap and fall, which is a
// move of a sprite and not a rewrite of the map, and the sprites are only
// the few rows on screen. A bough is its pieces (root, middle, tip; flipped
// when it is on the left), a dry one in dry wood, with a crack over it that
// widens in the frames before it breaks.

#include "climb.h"

#include "art.h"
#include "climb_rules.h"
#include "clock.h"
#include "den.h"
#include "sound.h"

#include <serval/serval.h>

#define FALLING_MAX 4
#define LAND_FRAMES 6
#define FADE_STEP 2
#define HUD_ROW 0
#define HINT_ROW 19
#define HINT_FRAMES 360

static u16* best_score;

typedef struct {
    bool active, left, dry;
    s16 x0;    // the bough's left end
    u8 len;    // in pixels
    s32 y, vy; // the top, world pixels, and Q8 per frame
    u8 age;
} Falling;

typedef struct {
    Falling fall[FALLING_MAX];
    u16 lock; // buttons held since before they were wanted
    int shown_rows, shown_best;
    int stage;
    int land; // frames of the landing crouch left
    bool face_left;
    bool moving;
    u32 started; // the frame the round began
    u32 hint;    // frames the hint has been up (0: not yet)
} View;

static View v;

void climb_attach(u16* best) {
    best_score = best;
}

// --- Input -----------------------------------------------------------------------------------

static void refresh_lock(void) {
    for (u16 bit = 1; bit <= BUTTON_L; bit <<= 1)
        if (!button_down(bit))
            v.lock &= (u16)~bit;
}

static bool pressed(u16 b) {
    return button_pressed(b) && !(v.lock & b);
}

static u16 held(void) {
    u16 keys = 0;
    static const u16 wanted[3] = {BUTTON_LEFT, BUTTON_RIGHT, BUTTON_A};
    for (int i = 0; i < 3; i++)
        if (button_down(wanted[i]) && !(v.lock & wanted[i]))
            keys |= wanted[i];
    return keys;
}

// --- Drawing ---------------------------------------------------------------------------------

static void draw_hud(const ClimbState* s) {
    int best = *best_score > s->rows ? *best_score : s->rows;
    if ((int)s->rows != v.shown_rows) {
        v.shown_rows = s->rows;
        text_print(1, HUD_ROW, text_format("HEIGHT %u M ", (u32)s->rows));
    }
    if (best != v.shown_best) {
        v.shown_best = best;
        text_print(19, HUD_ROW, text_format("BEST %u M ", (u32)best));
    }
}

// A bough from its pieces: `crack` 0 none, 1 hairline, 2 gap.
static void draw_bough(int x0, int len, int top, bool left, bool dry, int crack, int shake) {
    int n = len / 16;
    u16 flags = (u16)((left ? SPRITE_FLIP_H : 0) | (dry ? SPRITE_PALETTE(PAL_TREE_DRY) : 0));
    // The crack first: the first sprite drawn is in front.
    if (crack)
        sprite_draw(SPR_BRANCH, (u8)(crack == 1 ? BR_CRACK0 : BR_CRACK1), x0 + 16 * (n / 2) - 1,
                    top - 3, 0);
    for (int i = 0; i < n; i++) {
        int root_end = left ? n - 1 : 0, tip_end = left ? 0 : n - 1;
        int frame = i == root_end ? BR_ROOT : i == tip_end ? BR_TIP : BR_MID;
        sprite_draw(SPR_BRANCH, (u8)frame, x0 + 16 * i + (i & 1 ? shake : -shake), top - 3, flags);
    }
}

static void draw_rows(const ClimbState* s) {
    // Rows whose top is within (-16, 176) on screen.
    int cam = (int)s->cam;
    int r_hi = (-cam + 16) / CLIMB_ROW_H;
    int r_lo = -cam - 176 <= 0 ? 0 : (-cam - 176 + CLIMB_ROW_H - 1) / CLIMB_ROW_H;
    if (-cam + 16 < 0)
        return;
    u32 f = s->frame;
    for (int r = r_lo; r <= r_hi; r++) {
        ClimbRow row;
        climb_row(s->seed, r, &row);
        int top = -r * CLIMB_ROW_H - cam;
        for (int i = 0; i < row.count; i++) {
            const ClimbBranch* b = &row.b[i];
            if (climb_broken(s, r, i))
                continue;
            int off = b->flags & CLIMB_SWAYS ? climb_sway(r, i, f) : 0;
            int left = climb_snap_left(s, r, i);
            int crack = left == 0 ? 0 : left > 14 ? 1 : 2;
            draw_bough(b->x0 + off, b->x1 - b->x0, top, b->inner_right,
                       (b->flags & CLIMB_SNAPS) != 0, crack, left && left <= 14 ? (int)(f & 1) : 0);
        }
    }
    // The nests hang behind their boughs (drawn after them).
    for (int r = r_lo < 1 ? 1 : r_lo; r <= r_hi; r++) {
        ClimbRow row;
        climb_row(s->seed, r, &row);
        int top = -r * CLIMB_ROW_H - cam;
        for (int i = 0; i < row.count; i++)
            if ((row.b[i].flags & CLIMB_NESTS) && !climb_broken(s, r, i))
                sprite_draw(SPR_NEST, (u8)((f / 24) & 1), row.b[i].nest_x - 8, top + 5, 0);
    }
}

static void draw_falling(const ClimbState* s) {
    for (int k = 0; k < FALLING_MAX; k++) {
        Falling* fl = &v.fall[k];
        if (!fl->active)
            continue;
        int top = (int)(fl->y / 256) - (int)s->cam;
        int n = fl->len / 16;
        u16 flags =
            (u16)((fl->left ? SPRITE_FLIP_H : 0) | (fl->dry ? SPRITE_PALETTE(PAL_TREE_DRY) : 0));
        for (int i = 0; i < n; i++) {
            // The pieces part as they fall.
            int root_end = fl->left ? n - 1 : 0, tip_end = fl->left ? 0 : n - 1;
            int frame = i == root_end ? BR_ROOT : i == tip_end ? BR_TIP : BR_MID;
            int dx = (i - n / 2) * fl->age / 12, dy = (i & 1) * fl->age / 6;
            sprite_draw(SPR_BRANCH, (u8)frame, fl->x0 + 16 * i + dx, top - 3 + dy, flags);
        }
    }
}

static void falling_step(const ClimbState* s) {
    for (int k = 0; k < FALLING_MAX; k++) {
        Falling* fl = &v.fall[k];
        if (!fl->active)
            continue;
        fl->vy += 80;
        fl->y += fl->vy;
        if (fl->age < 255)
            fl->age++;
        if (fl->y / 256 - s->cam > 180)
            fl->active = false;
    }
}

static void falling_start(const ClimbState* s, int row, int idx) {
    ClimbRow r;
    climb_row(s->seed, row, &r);
    const ClimbBranch* b = &r.b[idx];
    for (int k = 0; k < FALLING_MAX; k++)
        if (!v.fall[k].active) {
            v.fall[k] = (Falling){.active = true,
                                  .left = b->inner_right,
                                  .dry = true,
                                  .x0 = b->x0,
                                  .len = (u8)(b->x1 - b->x0),
                                  .y = -row * CLIMB_ROW_H * 256};
            return;
        }
}

static void draw_serval(const ClimbState* s) {
    int fx = (int)(s->x / 256), fy = (int)(s->y / 256) - (int)s->cam;
    int frame;
    if (!s->standing)
        frame = PF_JUMP;
    else if (v.land)
        frame = PF_LAND;
    else if (v.moving)
        frame = PF_WALK0 + (int)((s->frame / 5) % 4);
    else
        frame = PF_STAND;
    sprite_draw((u16)(SPR_KITTEN + v.stage), (u8)frame, fx - 16, fy - 31,
                v.face_left ? SPRITE_FLIP_H : 0);
}

static void draw_scene(const ClimbState* s, bool serval) {
    map_set_scroll(DEN_BG, 0, (int)s->cam);
    map_set_scroll(SKY_BG, (int)(frame_count() / 12), 0);
    if (serval)
        draw_serval(s);
    draw_rows(s);
    draw_falling(s);
}

// --- Screens ---------------------------------------------------------------------------------

static void fade(const ClimbState* s, int from, int to, bool serval) {
    int step = from < to ? FADE_STEP : -FADE_STEP;
    for (int b = from;; b += step) {
        if (b * step > to * step)
            b = to;
        frame_begin();
        screen_set_brightness(b);
        draw_scene(s, serval);
        frame_end();
        if (b == to)
            break;
    }
}

enum { PAUSE_RESUME, PAUSE_QUIT };

// The pause menu: true to quit the round.
static bool pause_menu(const ClimbState* s) {
    int sel = PAUSE_RESUME;
    sound_tap();
    v.lock = BUTTON_ANY;
    bool quit = false, done = false;
    bool redraw = true;
    while (!done) {
        frame_begin();
        refresh_lock();
        if (pressed(BUTTON_UP) || pressed(BUTTON_DOWN)) {
            sel = !sel;
            redraw = true;
        }
        if (pressed(BUTTON_START) || pressed(BUTTON_B))
            done = true;
        else if (pressed(BUTTON_A)) {
            done = true;
            quit = sel == PAUSE_QUIT;
        }
        if (redraw) {
            text_set_style(TEXT_HIGHLIGHT);
            text_print_centered(7, "PAUSED");
            text_set_style(sel == PAUSE_RESUME ? TEXT_HIGHLIGHT : 3);
            text_print_centered_in(8, 14, 9, sel == PAUSE_RESUME ? "[RESUME]" : " RESUME ");
            text_set_style(sel == PAUSE_QUIT ? TEXT_HIGHLIGHT : 3);
            text_print_centered_in(8, 14, 11, sel == PAUSE_QUIT ? "[QUIT]" : " QUIT ");
            text_set_style(TEXT_NORMAL);
            redraw = false;
        }
        art_draw_panel(64, 40, 7, 4); // first drawn is in front
        draw_scene(s, true);
        frame_end();
    }
    text_clear_area(6, 6, 18, 7);
    v.lock = BUTTON_ANY; // the button that closed the menu is not the game's
    v.shown_rows = v.shown_best = -1;
    return quit;
}

static void results_card(const ClimbState* s, bool top, int best, bool new_best, u8 fun,
                         u32 worst) {
    text_clear_area(0, HINT_ROW, TEXT_COLS, 1);
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(5, top ? "TOP OF THE TREE!" : "ROUND OVER");
    text_set_style(TEXT_NORMAL);
    text_print_centered(7, text_format("HEIGHT %u M", (u32)s->rows));
    text_print_centered(9, text_format(new_best ? "BEST %d M  NEW!" : "BEST %d M", best));
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(11, text_format("FUN +%u", (u32)fun));
    text_set_style(TEXT_NORMAL);
    text_print_centered(13, "A OK");
#ifdef SERVAL_DEBUG
    text_print_centered(15, text_format("WORST %u.%u%%", worst / 10, worst % 10));
#else
    (void)worst;
#endif
    v.lock = BUTTON_ANY;
    for (;;) {
        frame_begin();
        refresh_lock();
        falling_step(s);
        art_draw_panel(48, 28, 9, 7);
        draw_scene(s, false);
        frame_end();
        if (pressed(BUTTON_A) || pressed(BUTTON_START))
            break;
    }
    sound_mew();
}

// --- The game --------------------------------------------------------------------------------

bool climb_play(Pet* pet, u32 now) {
    static u16 no_best;
    if (!best_score)
        best_score = &no_best;
    art_load_climb(); // the acacia's pictures, the screen black
    text_set_shadow(true);
    text_set_color(COLOR_RGB(248, 236, 208), COLOR_RGB(30, 20, 16));
    text_clear();
    int tod = art_time_of_day(clock_hour(now));
    art_set_scene(tod, false);
    art_set_pet(pet->coat, pet->sickness, tod, false);

    ClimbState s;
    climb_start(&s, now ^ frame_count() * 2654435761u);
    v = (View){0};
    v.stage = (int)pet_stage(pet, pet->last_tick);
    v.lock = BUTTON_ANY;
    v.shown_rows = v.shown_best = -1;
    button_repeat_reset();
    text_print_centered(HINT_ROW, "STEER  A JUMP  START PAUSE");
    draw_hud(&s);
    fade(&s, SCREEN_BRIGHTNESS_MIN, 0, true);

    u32 worst = 0;
    bool quit = false;
    ClimbResult r = CLIMB_PLAYING;
    while (r == CLIMB_PLAYING && !quit) {
        frame_begin();
        refresh_lock();
        u32 load = frame_cpu_permille();
        if (load > worst)
            worst = load;
        if (pressed(BUTTON_START)) {
            quit = pause_menu(&s);
            continue;
        }
        u16 keys = held();
        v.moving = (keys & (BUTTON_LEFT | BUTTON_RIGHT)) != 0;
        if (keys & BUTTON_LEFT)
            v.face_left = true;
        else if (keys & BUTTON_RIGHT)
            v.face_left = false;
        r = climb_step(&s, keys);
        if (s.events & CLIMB_EV_JUMP)
            sound_jump();
        if (s.events & CLIMB_EV_LAND) {
            sound_tap();
            v.land = LAND_FRAMES;
        } else if (v.land) {
            v.land--;
        }
        if (s.events & CLIMB_EV_SNAP) {
            sound_snap();
            falling_start(&s, s.ev_row, s.ev_idx);
        }
        if (s.events & CLIMB_EV_NEST)
            sound_knock();
        if (s.events & CLIMB_EV_OVER)
            sound_miss();
        falling_step(&s);
        if (v.hint < HINT_FRAMES && ++v.hint == HINT_FRAMES)
            text_clear_area(0, HINT_ROW, TEXT_COLS, 1);
        draw_hud(&s);
        draw_scene(&s, true);
        frame_end();
    }

    // The round counts, whether the serval fell or the player quit.
    u8 fun = climb_fun(s.rows);
    bool new_best = s.rows > *best_score;
    if (new_best)
        *best_score = s.rows;
    pet_played(pet, now, fun);
    results_card(&s, r == CLIMB_TOP, *best_score, new_best, fun, worst);

    fade(&s, 0, SCREEN_BRIGHTNESS_MIN, false);
    text_clear();
    map_unload(DEN_BG);
    map_unload(SKY_BG);
    frame_begin();
    frame_end(); // behind the black screen
    return true;
}
