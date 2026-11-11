// High Pounce on screen: see pounce.h.
//
// The savanna is a background layer (hills, a plain and tall grass, drawn in
// the savanna's palette so that the hour colours it) over the same
// full-height sky as Acacia Climb, whose clouds drift. In front of the serval
// stands a row of grass tufts (sprites) that sway a little; a rustle shows
// as the tuft at its place shaking hard for a moment, with a burst of noise
// as loud as the serval is near. Nothing else shows where the mice and the
// birds are: the grass of an idle rustle is the grass of any other. The serval,
// a mouse under its paw, the birds in the air, feathers and the arrow of the
// ears are sprites too.

#include "pounce.h"

#include "art.h"
#include "clock.h"
#include "den.h"
#include "pounce_rules.h"
#include "sound.h"

#include <serval/serval.h>

#define GROUND_Y 146 // the serval's feet
#define TUFTS 15     // the row of grass across the screen
#define FEATHERS 10
#define FEATHER_FRAMES_LIVE 44
#define CATCH_FRAMES 36
#define RIPPLE_FRAMES 16
#define FADE_STEP 2
#define HUD_ROW 0
#define HINT_ROW 19
#define HINT_FRAMES 420

static u16* best_score;

typedef struct {
    bool active, seed; // a seed head (from a miss) or a feather
    s32 x, y, vx, vy;  // Q8
    u8 age;
} Feather;

typedef struct {
    Feather feather[FEATHERS];
    u16 lock; // buttons held since before they were wanted
    int shown_points, shown_best, shown_time;
    int stage;
    int catch_t, catch_x;   // frames left of the catch pose, and where
    int ripple_t, ripple_x; // frames left of the grass rippling after a miss, and where
    u32 hint;               // frames the hint has been up (0: not yet)
    u32 seed;
    bool moving;
} View;

static View v;

void pounce_attach(u16* best) {
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
    static const u16 wanted[4] = {BUTTON_LEFT, BUTTON_RIGHT, BUTTON_B, BUTTON_A};
    for (int i = 0; i < 4; i++)
        if (button_down(wanted[i]) && !(v.lock & wanted[i]))
            keys |= wanted[i];
    return keys;
}

static int vrand(int n) {
    v.seed = v.seed * 1664525u + 1013904223u;
    return (int)((v.seed >> 16) * (u32)n >> 16);
}

// --- Drawing ---------------------------------------------------------------------------------

static void draw_hud(const PounceState* s) {
    int best = *best_score > s->points ? *best_score : s->points;
    if ((int)s->points != v.shown_points) {
        v.shown_points = s->points;
        text_print(1, HUD_ROW, text_format("SCORE %u  ", (u32)s->points));
    }
    int left = pounce_seconds_left(s);
    if (left != v.shown_time) {
        v.shown_time = left;
        text_print(12, HUD_ROW, text_format("TIME %u ", (u32)left));
    }
    if (best != v.shown_best) {
        v.shown_best = best;
        text_print(21, HUD_ROW, text_format("BEST %u ", (u32)best));
    }
}

static int tuft_x(int i) {
    return i * 16;
}

// The ambient tuft's frame: a slow sway, each its own phase.
static int sway_frame(int i, u32 f) {
    static const u8 seq[4] = {0, 1, 0, 2};
    return seq[((f >> 4) + (u32)i) & 3];
}

static int shake_frame(u32 f) {
    static const u8 seq[4] = {3, 1, 3, 2};
    return seq[(f / 3) & 3];
}

static void draw_serval(const PounceState* s) {
    int fx = pounce_x(s), fy = GROUND_Y - pounce_height(s);
    int frame;
    if (s->leap)
        frame = PF_JUMP;
    else if (v.catch_t > 0 && !v.moving)
        frame = PF_LAND;
    else if (s->recover)
        frame = PF_LAND;
    else if (s->crouch)
        frame = v.moving && (s->frame / 10) % 4 == 3 ? PF_LAND : PF_CROUCH;
    else if (v.moving)
        frame = PF_WALK0 + (int)((s->frame / 6) % 4);
    else
        frame = (s->frame / 70) % 3 == 2 && (s->frame % 70) < 14 ? PF_STAND_TAIL : PF_STAND;
    sprite_draw((u16)(SPR_KITTEN + v.stage), (u8)frame, fx - 16, fy - 31,
                s->face < 0 ? SPRITE_FLIP_H : 0);
}

static void draw_cue(const PounceState* s) {
    int dx;
    int left = pounce_heard(s, &dx);
    if (!left)
        return;
    if (left < 16 && (left & 2))
        return; // blinks out
    int fx = pounce_x(s), fy = GROUND_Y - pounce_height(s);
    int near = dx < 0 ? -dx : dx;
    int bob = (int)((s->frame / 5) & 1);
    // Close, the arrow sits right over the head; far, it leans away from it.
    int off = (dx < 0 ? -1 : 1) * (near < 16 ? 6 : 12);
    sprite_draw(SPR_CUE, (u8)(dx < 0 ? 0 : 1), fx - 4 + off, fy - 42 - bob, 0);
}

static void draw_mouse(void) {
    if (v.catch_t <= 0)
        return;
    int t = CATCH_FRAMES - v.catch_t;
    // squirming, then still
    sprite_draw(SPR_MOUSE, (u8)(t < 20 ? (t / 4) % 3 : 0), v.catch_x - 8, GROUND_Y - 7, 0);
    if (t < 12)
        sprite_draw(SPR_SPARK, (u8)((t / 4) % SPARK_FRAMES), v.catch_x - 4, GROUND_Y - 22 - t / 2,
                    SPRITE_PALETTE(PAL_ITEMS));
}

static void draw_feathers(void) {
    for (int k = 0; k < FEATHERS; k++) {
        Feather* f = &v.feather[k];
        if (!f->active)
            continue;
        if (f->seed)
            sprite_draw(SPR_SEED, (u8)((f->age / 5) & 1), (int)(f->x / 256) - 4,
                        (int)(f->y / 256) - 4, 0);
        else
            sprite_draw(SPR_FEATHER, (u8)((f->age / 6) % FEATHER_FRAMES), (int)(f->x / 256) - 4,
                        (int)(f->y / 256) - 4, 0);
    }
}

static void feathers_step(void) {
    for (int k = 0; k < FEATHERS; k++) {
        Feather* f = &v.feather[k];
        if (!f->active)
            continue;
        f->x += f->vx;
        f->y += f->vy;
        f->vx -= f->vx / 24;
        f->vy += 14; // falls
        if (f->vy > 160)
            f->vy = 160;
        if (++f->age >= FEATHER_FRAMES_LIVE)
            f->active = false;
    }
}

static void feathers_burst(int x, int h) {
    for (int k = 0; k < 6; k++) {
        v.feather[k] = (Feather){.active = true,
                                 .x = x * 256,
                                 .y = (GROUND_Y - 10 - h) * 256,
                                 .vx = (vrand(5) - 2) * 150 + (vrand(100) - 50),
                                 .vy = -(vrand(200) + 120),
                                 .age = (u8)vrand(6)};
    }
}

// Seed heads thrown up by a miss (they use the pool's last four places).
static void seeds_burst(int x) {
    for (int k = 6; k < FEATHERS; k++) {
        v.feather[k] = (Feather){.active = true,
                                 .seed = true,
                                 .x = (x + vrand(41) - 20) * 256,
                                 .y = (GROUND_Y - 6) * 256,
                                 .vx = (vrand(9) - 4) * 90,
                                 .vy = -(vrand(300) + 420),
                                 .age = (u8)vrand(6)};
    }
}

static void draw_birds(const PounceState* s) {
    for (int i = 0; i < POUNCE_SLOTS; i++) {
        const PounceRustle* r = &s->r[i];
        if (r->state != PR_FLY)
            continue;
        static const u8 beat[4] = {0, 1, 2, 1};
        sprite_draw(SPR_BIRD, beat[(s->frame / 4) & 3], (int)(r->x / 256) - 8,
                    GROUND_Y - 22 - (int)(r->h / 256), r->vx < 0 ? SPRITE_FLIP_H : 0);
    }
}

static void draw_tufts(const PounceState* s) {
    // The shaking ones at the rustles first (in front), at their own places.
    for (int i = 0; i < POUNCE_SLOTS; i++) {
        const PounceRustle* r = &s->r[i];
        if (r->state == PR_ACTIVE && r->burst) {
            // Two tufts high, jittering: taller than the grass around it.
            int jitter = (s->frame & 2) ? 2 : -2, x = (int)(r->x / 256) - 8;
            sprite_draw(SPR_STALK, (u8)shake_frame(s->frame), x + jitter, GROUND_Y - 6, 0);
            sprite_draw(SPR_STALK, (u8)shake_frame(s->frame + 1), x - jitter, GROUND_Y - 18, 0);
        }
    }
    // The rustles a miss scared whip the grass as they flee.
    for (int i = 0; i < POUNCE_SLOTS; i++) {
        const PounceRustle* r = &s->r[i];
        if (r->state == PR_SCARED && r->timer > POUNCE_SCARE_FRAMES - RIPPLE_FRAMES) {
            int jitter = (s->frame & 1) ? 3 : -3, x = (int)(r->x / 256) - 8;
            sprite_draw(SPR_STALK, (u8)shake_frame(s->frame), x + jitter, GROUND_Y - 6, 0);
            sprite_draw(SPR_STALK, (u8)shake_frame(s->frame + 2), x - jitter, GROUND_Y - 18, 0);
            sprite_draw(SPR_STALK, (u8)shake_frame(s->frame + 1), x + jitter, GROUND_Y - 30, 0);
        }
    }
    for (int i = 0; i < TUFTS; i++) {
        int x = tuft_x(i);
        bool ripple = v.ripple_t > 0 && x + 8 > v.ripple_x - POUNCE_SCARE_R &&
                      x + 8 < v.ripple_x + POUNCE_SCARE_R;
        // The grass round a miss jumps and shakes.
        int hop = ripple ? (v.ripple_t > 8 ? 7 : 3) : 0;
        sprite_draw(SPR_STALK,
                    (u8)(ripple ? shake_frame(s->frame + (u32)i) : sway_frame(i, s->frame)), x,
                    GROUND_Y - 8 + (i & 1) * 3 - hop, 0);
    }
}

static void draw_scene(const PounceState* s, bool serval) {
    map_set_scroll(SKY_BG, (int)(frame_count() / 12), 0);
    if (serval)
        draw_cue(s);
    draw_mouse();
    draw_feathers();
    draw_birds(s);
    draw_tufts(s);
    if (serval)
        draw_serval(s);
}

// --- Screens ---------------------------------------------------------------------------------

static void fade(const PounceState* s, int from, int to, bool serval) {
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
static bool pause_menu(const PounceState* s) {
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
    v.shown_points = v.shown_best = v.shown_time = -1;
    return quit;
}

static void results_card(const PounceState* s, bool timeup, int best, bool new_best, u8 fun,
                         u32 worst) {
    text_clear_area(0, HINT_ROW, TEXT_COLS, 1);
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(4, timeup ? "TIME UP!" : "ROUND OVER");
    text_set_style(TEXT_NORMAL);
    text_print_centered(6, text_format("SCORE %u", (u32)s->points));
    text_print_centered(8, text_format(new_best ? "BEST %d  NEW!" : "BEST %d", best));
    text_print_centered(10, text_format("MICE %u  BIRDS %u", (u32)s->mice, (u32)s->birds));
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(12, text_format("FUN +%u", (u32)fun));
    text_set_style(TEXT_NORMAL);
    text_print_centered(14, "A OK");
#ifdef SERVAL_DEBUG
    text_print_centered(16, text_format("WORST %u.%u%%", worst / 10, worst % 10));
#else
    (void)worst;
#endif
    v.lock = BUTTON_ANY;
    for (;;) {
        frame_begin();
        refresh_lock();
        feathers_step();
        art_draw_panel(48, 28, 9, 7);
        draw_scene(s, false);
        frame_end();
        if (pressed(BUTTON_A) || pressed(BUTTON_START))
            break;
    }
    sound_mew();
}

// --- The game --------------------------------------------------------------------------------

// The loudest burst this frame, by how near its rustle is.
static void play_bursts(const PounceState* s) {
    int loudest = 0;
    for (int i = 0; i < POUNCE_SLOTS; i++)
        if (s->bursts & (1u << i)) {
            int d = (int)(s->r[i].x / 256) - pounce_x(s);
            int l = pounce_hear(s, d);
            if (l > loudest)
                loudest = l;
        }
    if (loudest)
        sound_rustle(loudest);
}

bool pounce_play(Pet* pet, u32 now) {
    static u16 no_best;
    if (!best_score)
        best_score = &no_best;
    art_load_pounce(); // the savanna's pictures, the screen black
    text_set_shadow(true);
    text_set_color(COLOR_RGB(248, 236, 208), COLOR_RGB(30, 20, 16));
    text_clear();
    int tod = art_time_of_day(clock_hour(now));
    art_set_scene(tod, false);
    art_set_pet(pet->coat, pet->sickness, tod, false);

    PounceState s;
    pounce_start(&s, now ^ frame_count() * 2654435761u);
    v = (View){0};
    v.seed = now ^ 0x51ED270Bu;
    v.stage = (int)pet_stage(pet, pet->last_tick);
    v.lock = BUTTON_ANY;
    v.shown_points = v.shown_best = v.shown_time = -1;
    button_repeat_reset();
    text_print_centered(HINT_ROW, "B CREEP A POUNCE START PAUSE");
    draw_hud(&s);
    fade(&s, SCREEN_BRIGHTNESS_MIN, 0, true);

    u32 worst = 0;
    bool quit = false;
    PounceResult r = POUNCE_PLAYING;
    while (r == POUNCE_PLAYING && !quit) {
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
        v.moving = (keys & (BUTTON_LEFT | BUTTON_RIGHT)) != 0 && !s.leap && !s.recover;
        r = pounce_step(&s, keys);
        play_bursts(&s);
        if (s.events & POUNCE_EV_LEAP) {
            sound_jump();
            v.catch_t = 0; // it has left its catch
        }
        if (s.events & POUNCE_EV_FLUSH)
            sound_flutter();
        if (s.events & POUNCE_EV_SWAT) {
            sound_bat();
            feathers_burst(s.ev_x, s.ev_h);
        }
        if (s.events & POUNCE_EV_CATCH) {
            sound_mew();
            v.catch_t = CATCH_FRAMES;
            v.catch_x = s.ev_x;
        } else if (v.catch_t > 0) {
            v.catch_t--;
        }
        if (s.events & POUNCE_EV_MISS) {
            sound_miss();
            v.ripple_t = RIPPLE_FRAMES;
            v.ripple_x = s.ev_x;
            seeds_burst(s.ev_x);
        } else if (v.ripple_t > 0) {
            v.ripple_t--;
        }
        if (s.events & POUNCE_EV_LAND && !(s.events & (POUNCE_EV_CATCH | POUNCE_EV_MISS)))
            sound_tap();
        feathers_step();
        if (v.hint < HINT_FRAMES && ++v.hint == HINT_FRAMES)
            text_clear_area(0, HINT_ROW, TEXT_COLS, 1);
        draw_hud(&s);
        draw_scene(&s, true);
        frame_end();
    }

    // The round counts, whether the time ran out or the player quit.
    u8 fun = pounce_fun(s.points);
    bool new_best = s.points > *best_score;
    if (new_best)
        *best_score = s.points;
    pet_played(pet, now, fun);
    results_card(&s, r == POUNCE_OVER, *best_score, new_best, fun, worst);

    fade(&s, 0, SCREEN_BRIGHTNESS_MIN, false);
    text_clear();
    map_unload(DEN_BG);
    map_unload(SKY_BG);
    frame_begin();
    frame_end(); // behind the black screen
    return true;
}
