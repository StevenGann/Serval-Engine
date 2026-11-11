// Yarn Bat on screen: see yarn.h.
//
// The game is played in the den itself: its pictures stay on, the rock at
// the sides are the walls, the roof is the ceiling and holds the score, the
// floor is where the pet runs; only the care bar is gone. The pet is the paw
// (it walks the floor with its own stage's frames, its own size sets its
// reach, and it swipes its arm up when it bats), and the yarn
// ball and its thread are sprites. The thread is the ball's last positions, one
// a frame, a dot each, waving more the older it is; it grows with the bats,
// so the yarn is seen to unravel, and the ball shrinks a little as it goes.

#include "yarn.h"

#include "art.h"
#include "clock.h"
#include "den.h"
#include "sound.h"
#include "yarn_rules.h"

#include <serval/serval.h>

#define TRAIL_MAX 72
#define TRAIL_BASE 10   // dots of thread from the start
#define TRAIL_PER_BAT 3 // and three more for each bat
#define HIT_FRAMES 12   // the paw's swing, and the spark
#define FEET_Y 138      // where the pet's feet are on the den's floor
#define SHOULDER_Y 131  // where its raised arm starts (the kitten's; 4 higher for the others)
#define FADE_STEP 2
#define HUD_ROW 0
#define HINT_ROW 19

static u16* best_score;

static const s8 wave[12] = {0, 1, 1, 2, 1, 1, 0, -1, -1, -2, -1, -1};

typedef struct {
    s16 x[TRAIL_MAX], y[TRAIL_MAX]; // ring, newest at `head`
    u8 head, count;
    u8 hit;   // frames left of the paw's swing
    u16 lock; // buttons held since before they were wanted
    int shown_bats, shown_best;
    int stage;
    bool moving, left, lights_off;
} View;

static View v;

void yarn_attach(u16* best) {
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
    if (button_down(BUTTON_LEFT) && !(v.lock & BUTTON_LEFT))
        keys |= BUTTON_LEFT;
    if (button_down(BUTTON_RIGHT) && !(v.lock & BUTTON_RIGHT))
        keys |= BUTTON_RIGHT;
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
    v.head = v.head == TRAIL_MAX - 1 ? 0 : (u8)(v.head + 1);
    v.x[v.head] = (s16)(s->x >> 8);
    v.y[v.head] = (s16)(s->y >> 8);
    if (v.count < TRAIL_MAX)
        v.count++;
}

static int arm_frame(void) {
    int e = HIT_FRAMES - v.hit; // frames since the bat
    return e < 2 ? 1 : e < 7 ? 2 : e < 10 ? 1 : 0;
}

static void draw_court(const YarnState* s, bool paw_visible) {
    u32 f = frame_count();
    int bx = (int)(s->x >> 8), by = (int)(s->y >> 8);
    // Front to back: the ball, the arm and the spark, the pet, the thread, the lamp.
    int size = s->bats >= 25 ? 2 : s->bats >= 10 ? 1 : 0;
    sprite_draw(SPR_YARN, (u8)(size * YARN_TURNS + (((bx + by) >> 2) & 3)), bx - 4, by - 4, 0);
    if (paw_visible) {
        int body = v.stage == STAGE_KITTEN ? 16 : 32;
        // The pet stands as in the den: feet on the floor, flipped to face
        // left. The paw is its raised front paw, at paw_x: the pet is drawn
        // back from it, so that the ball meets the paw, not the pet's middle.
        int px = s->paw_x, top = FEET_Y - body + 2;
        int dir = v.left ? -1 : 1, back = v.stage == STAGE_KITTEN ? 4 : 8;
        int shoulder = SHOULDER_Y - (v.stage == STAGE_KITTEN ? 0 : 4);
        if (v.hit) {
            // The arm swings up from the chest (drawn pointing right, turned
            // a quarter turn up: its root at the shoulder, its tip up).
            sprite_draw_rotated(SPR_PAW, (u8)arm_frame(), px - 8, shoulder - 12, ANGLE_DEG(270), 0);
            sprite_draw(SPR_SPARK, (u8)((HIT_FRAMES - v.hit) * SPARK_FRAMES / HIT_FRAMES), px - 4,
                        s->paw_y - 12, 0);
        }
        int frame = v.hit ? PF_HAPPY : v.moving ? PF_WALK0 + (int)(f / 6 % 4) : PF_STAND;
        sprite_draw((u16)(SPR_KITTEN + v.stage), (u8)frame, px - dir * back - body / 2, top,
                    v.left ? SPRITE_FLIP_H : 0);
    }
    int shown = trail_wanted(s);
    if (shown > v.count)
        shown = v.count;
    // No divisions in this loop (Thumb code has none): the ring index, the
    // wave's phase and the amplitude step along.
    int k = v.head, phase = (int)(f >> 1) % 12, amp = 1, next_amp = 10;
    for (int i = 1; i < shown; i++) { // 0 is under the ball
        k = k == 0 ? TRAIL_MAX - 1 : k - 1;
        phase = phase == 11 ? 0 : phase + 1;
        if (i == next_amp) {
            amp += amp < 3;
            next_amp += 10;
        }
        sprite_draw(SPR_THREAD,
                    (u8)(i < 5    ? 0
                         : i < 24 ? 1
                                  : 2),
                    v.x[k] - 4, v.y[k] - 4 + wave[phase] * amp / 2, 0);
    }
    sprite_draw(SPR_LAMP, v.lights_off ? 2 : (u8)((f / 14) & 1), LAMP_X, LAMP_Y, 0);
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
        art_draw_panel(64, 40, 7, 4);
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
        art_draw_panel(48, 28, 9, 7);
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
    den_resume(); // the den's pictures, the screen black; the care bar stays off
    int tod = art_time_of_day(clock_hour(now));
    art_set_scene(tod, pet->lights_off);
    art_set_pet(pet->coat, pet->sickness, tod, pet->lights_off);
    map_set_scroll(SKY_BG, 0, 0);
    text_print_centered(HINT_ROW, "LEFT RIGHT MOVE   START PAUSE");

    YarnState s;
    yarn_start(&s, now ^ frame_count() * 2654435761u);
    v = (View){0};
    v.stage = (int)pet_stage(pet, pet->last_tick);
    v.lights_off = pet->lights_off;
    yarn_set_reach(&s,
                   v.stage == STAGE_KITTEN       ? 16
                   : v.stage == STAGE_ADOLESCENT ? 26
                                                 : 30,
                   v.stage == STAGE_KITTEN ? YARN_PAW_Y : YARN_PAW_Y - 12);
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
        v.moving = (keys & (BUTTON_LEFT | BUTTON_RIGHT)) != 0;
        if (keys & BUTTON_LEFT)
            v.left = true;
        else if (keys & BUTTON_RIGHT)
            v.left = false;
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
    text_clear();
    frame_begin();
    frame_end(); // behind the black screen
    return true;
}
