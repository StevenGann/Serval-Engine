// Yarn Bat on screen: see yarn.h.
//
// The court is the screen without its background maps (art_hide()): a
// backdrop color per scanline (raster_backdrop(): dark above the ceiling and
// below the floor, sand between), the wall a column of sprites, the pet at
// the left as the paw, and the yarn ball, with its thread, as sprites. The
// thread is the ball's last positions, one every two frames, a dot each; it
// grows with the bats, so the yarn is seen to unravel.

#include "yarn.h"

#include "art.h"
#include "clock.h"
#include "den.h"
#include "sound.h"
#include "yarn_rules.h"

#include <serval/serval.h>

#define TRAIL_MAX 48
#define TRAIL_BASE 6      // dots of thread from the start
#define TRAIL_PER_BAT 2   // and two more for each bat
#define TRAIL_SPACING 2   // frames between dots
#define HIT_FRAMES 10     // the paw's swing, and the spark
#define FADE_STEP 2
#define HUD_ROW 0
#define HINT_ROW 19

static u16* best_score;

static Color court[SCREEN_H] SERVAL_EWRAM_BSS;

typedef struct {
    s16 x[TRAIL_MAX], y[TRAIL_MAX]; // ring, newest at `head`
    u8 head, count, tick;
    u8 hit;                         // frames left of the paw's swing
    u16 lock;                       // buttons held since before they were wanted
    int shown_bats, shown_best;
    int stage;
    bool moving;
} View;

static View v;

void yarn_attach(u16* best) {
    best_score = best;
}

// --- The court -------------------------------------------------------------------------------

static void build_court(void) {
    for (int y = 0; y < SCREEN_H; y++) {
        Color c;
        if (y < YARN_CEIL_Y - 2 || y >= YARN_FLOOR_Y + 2)
            c = COLOR_RGB(36, 24, 20); // above the ceiling, below the floor
        else if (y < YARN_CEIL_Y || y >= YARN_FLOOR_Y)
            c = COLOR_RGB(214, 178, 128); // their edges
        else {
            // sand, a little darker toward the foot, in broad bands
            int band = (y - YARN_CEIL_Y) / 16;
            c = COLOR_RGB(190 - band * 5, 148 - band * 5, 100 - band * 4);
        }
        court[y] = c;
    }
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
    if (button_down(BUTTON_UP) && !(v.lock & BUTTON_UP))
        keys |= BUTTON_UP;
    if (button_down(BUTTON_DOWN) && !(v.lock & BUTTON_DOWN))
        keys |= BUTTON_DOWN;
    return keys;
}

// --- Drawing ---------------------------------------------------------------------------------

static void draw_hud(const YarnState* s) {
    int best = *best_score > s->bats ? *best_score : s->bats;
    if ((int)s->bats != v.shown_bats) {
        v.shown_bats = s->bats;
        text_print(1, HUD_ROW, text_format("BATS %-3u", (u32)s->bats));
    }
    if (best != v.shown_best) {
        v.shown_best = best;
        text_print(20, HUD_ROW, text_format("BEST %-3u", (u32)best));
    }
}

static int trail_wanted(const YarnState* s) {
    int n = TRAIL_BASE + TRAIL_PER_BAT * (int)s->bats;
    return n > TRAIL_MAX ? TRAIL_MAX : n;
}

static void trail_update(const YarnState* s) {
    if (s->wait)
        return;
    if (v.tick++ % TRAIL_SPACING)
        return;
    v.head = (u8)((v.head + 1) % TRAIL_MAX);
    v.x[v.head] = (s16)(s->x >> 8);
    v.y[v.head] = (s16)(s->y >> 8);
    if (v.count < TRAIL_MAX)
        v.count++;
}

static void draw_court(const YarnState* s, bool paw_visible) {
    u32 f = frame_count();
    int bx = (int)(s->x >> 8), by = (int)(s->y >> 8);
    // Front to back: the ball, the paw and its spark, the thread, the wall.
    sprite_draw(SPR_YARN, (u8)(((bx + by) >> 2) & 3), bx - 4, by - 4, 0);
    if (paw_visible) {
        int size = v.stage == STAGE_KITTEN ? 16 : 32;
        int frame = v.hit ? PF_HAPPY : v.moving ? PF_WALK0 + (int)(f / 5 % 4) : PF_STAND;
        sprite_draw((u16)(SPR_KITTEN + v.stage), (u8)frame, YARN_PAW_X - size + 2,
                    s->paw_y - size / 2 + 2, 0);
        if (v.hit)
            sprite_draw(SPR_SPARK, (u8)((HIT_FRAMES - v.hit) * SPARK_FRAMES / HIT_FRAMES),
                        YARN_PAW_X - 2, s->paw_y - 4, 0);
    }
    int shown = trail_wanted(s);
    if (shown > v.count)
        shown = v.count;
    for (int i = 1; i < shown; i++) { // 0 is under the ball
        int k = (v.head + TRAIL_MAX - i) % TRAIL_MAX;
        int frame = i < 5 ? 0 : i < 14 ? 1 : 2;
        sprite_draw(SPR_THREAD, (u8)frame, v.x[k] - 4, v.y[k] - 4, 0);
    }
    for (int i = 0; i < (YARN_FLOOR_Y - YARN_CEIL_Y) / 16; i++)
        sprite_draw(SPR_WALL, 0, YARN_WALL_X, YARN_CEIL_Y + i * 16, 0);
}

// A box of 16 x 16 pieces behind text: columns and rows of them from (x, y).
static void draw_panel(int x, int y, int cols, int rows) {
    for (int j = 0; j < rows; j++)
        for (int i = 0; i < cols; i++)
            sprite_draw(SPR_PANEL,
                        (u8)((j == 0 ? 0 : j == rows - 1 ? 2 : 1) * 3 +
                             (i == 0 ? 0 : i == cols - 1 ? 2 : 1)),
                        x + i * 16, y + j * 16, 0);
}

// --- Screens ---------------------------------------------------------------------------------

static void fade(const YarnState* s, int from, int to, bool paw) {
    int step = from < to ? FADE_STEP : -FADE_STEP;
    for (int b = from;; b += step) {
        if (b * step > to * step)
            b = to;
        frame_begin();
        screen_set_brightness(b);
        draw_court(s, paw);
        frame_end();
        if (b == to)
            break;
    }
}

enum { PAUSE_RESUME, PAUSE_QUIT };

// The pause menu: true to quit the round.
static bool pause_menu(const YarnState* s) {
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
        draw_court(s, true);
        draw_panel(64, 40, 7, 4);
        frame_end();
    }
    text_clear_area(6, 6, 18, 7);
    v.lock = BUTTON_ANY; // the button that closed the menu is not the game's
    v.shown_bats = v.shown_best = -1;
    return quit;
}

static void results_card(const YarnState* s, int best, bool new_best, u8 fun, u32 worst) {
    text_clear_area(0, HINT_ROW, TEXT_COLS, 1);
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(5, "ROUND OVER");
    text_set_style(TEXT_NORMAL);
    text_print_centered(7, text_format("BATS %u", (u32)s->bats));
    text_print_centered(9, text_format(new_best ? "BEST %d  NEW!" : "BEST %d", best));
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
        draw_court(s, false);
        draw_panel(48, 28, 9, 7);
        frame_end();
        if (pressed(BUTTON_A) || pressed(BUTTON_START))
            break;
    }
    sound_mew();
}

// --- The game --------------------------------------------------------------------------------

bool yarn_play(Pet* pet, u32 now) {
    static u16 no_best;
    if (!best_score)
        best_score = &no_best;
    art_hide();
    art_set_pet(pet->coat, 0, TOD_DAY, false);
    build_court();
    raster_backdrop(court);
    text_clear();
    text_set_color(COLOR_RGB(248, 236, 208), COLOR_RGB(20, 14, 12));
    text_print_centered(HINT_ROW, "UP DOWN MOVE   START PAUSE");

    YarnState s;
    yarn_start(&s, now ^ frame_count() * 2654435761u);
    v = (View){0};
    v.stage = (int)pet_stage(pet, pet->last_tick);
    v.lock = BUTTON_ANY;
    v.shown_bats = v.shown_best = -1;
    button_repeat_reset();
    draw_hud(&s);
    fade(&s, SCREEN_BRIGHTNESS_MIN, 0, true);

    u32 worst = 0;
    bool quit = false;
    YarnResult r = YARN_PLAYING;
    while (r == YARN_PLAYING && !quit) {
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
        v.moving = (keys & (BUTTON_UP | BUTTON_DOWN)) != 0;
        r = yarn_step(&s, keys);
        if (s.events & YARN_EV_BAT) {
            v.hit = HIT_FRAMES;
            sound_bat();
        } else if (s.events & YARN_EV_WALL) {
            sound_tap();
        }
        if (s.events & YARN_EV_MISS)
            sound_miss();
        if (v.hit)
            v.hit--;
        trail_update(&s);
        draw_hud(&s);
        draw_court(&s, true);
        frame_end();
    }

    // The round counts, whether the ball got past or the player quit.
    u8 fun = yarn_fun(s.bats);
    bool new_best = s.bats > *best_score;
    if (new_best)
        *best_score = s.bats;
    pet_played(pet, now, fun);
    results_card(&s, *best_score, new_best, fun, worst);

    fade(&s, 0, SCREEN_BRIGHTNESS_MIN, false);
    raster_clear();
    text_clear();
    frame_begin();
    frame_end(); // the clear takes effect, behind the black screen
    return true;
}
