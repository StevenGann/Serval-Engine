// Serval Den's life events on screen: see scenes.h.
//
// Palettes: the pet keeps PAL_PET; the visitor, the kittens, the album's
// portrait and the new kitten take PAL_COAT1-5 (art_set_coat()), so the
// litter's six coats (the parents and up to four kittens) are on the screen
// together. The growing serval's white flash is PAL_COAT1 made all white.
// The scenes' state is in EWRAM: IWRAM is better left to the games.

#include "scenes.h"

#include "album.h"
#include "art.h"
#include "clock.h"
#include "den.h"
#include "litter.h"
#include "menu.h"
#include "naming.h"
#include "sound.h"

#include <serval/serval.h>

#define FADE_STEP 2
#define PROMPT_ROW 19
#define CREAM COLOR_RGB(248, 236, 208)
#define SHADOW COLOR_RGB(30, 20, 16)

static const char* const game_names[3] = {"YARN BAT", "ACACIA CLIMB", "HIGH POUNCE"};

// --- Frames, input and fades -------------------------------------------------------------------

static u16 lock;

static void refresh_lock(void) {
    for (u16 bit = 1; bit <= BUTTON_L; bit <<= 1)
        if (!button_down(bit))
            lock &= (u16)~bit;
}

static void lock_input(void) {
    lock = BUTTON_ANY;
    button_repeat_reset();
}

static bool pressed(u16 b) {
    return button_pressed(b) && !(lock & b);
}

static bool repeated(u16 b) {
    return !(lock & b) && button_repeat(b);
}

// Ends this frame and begins the next (the scenes run inside the den's frame).
static void tick(void) {
    frame_end();
    frame_begin();
    refresh_lock();
}

typedef void (*DrawFn)(int k);

static void fade(int from, int to, DrawFn draw, int k) {
    int step = from < to ? FADE_STEP : -FADE_STEP;
    for (int b = from;; b += step) {
        if (b * step > to * step)
            b = to;
        screen_set_brightness(b);
        if (draw)
            draw(k);
        tick();
        if (b == to)
            break;
    }
}

// Text on the plain dark page (art_hide(), or the title's black).
static void dark_text(void) {
    text_set_shadow(true);
    text_set_color(CREAM, SHADOW);
    text_set_style(TEXT_NORMAL);
    text_clear();
}

// The den again behind the black screen: its pictures, the serval placed
// (anew for a new pet), the bar ready. The caller stores the save, then fades
// in (menu_fade_in()).
static void back_to_den(Pet* p, bool new_pet) {
    sound_tune_stop();
    if (new_pet)
        den_enter();
    else
        den_resume();
    den_update(p, p->last_tick); // places the serval; no time passes
    menu_resume();
}

// A blinking prompt.
static void prompt(int k, const char* s) {
    if (k % 60 == 0)
        text_print_centered(PROMPT_ROW, s);
    else if (k % 60 == 40)
        text_clear_area(0, PROMPT_ROW, TEXT_COLS, 1);
}

static int str_len(const char* s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void copy_name(char out[9], const char* s) {
    int i = 0;
    for (; i < 8 && s[i]; i++)
        out[i] = s[i];
    out[i] = 0;
}

// The serval at a stage, its paws' middle at (x, y).
static void draw_serval(int stage, int frame, int x, int y, bool left, int palette) {
    int size = stage == STAGE_KITTEN ? 16 : 32;
    u16 flags = (u16)((left ? SPRITE_FLIP_H : 0) | (palette >= 0 ? SPRITE_PALETTE(palette) : 0));
    sprite_draw((u16)(SPR_KITTEN + stage), (u8)frame, x - size / 2, y - size + 2, flags);
}

// The idle frames: stands, blinks now and then (each serval at its own
// `phase`), flicks its tail.
static int idle_frame(int k, int phase) {
    int t = (k + phase) % 200;
    return t < 8 ? PF_BLINK : t >= 100 && t < 116 ? PF_STAND_TAIL : PF_STAND;
}

static int walk_frame(int k) {
    return PF_WALK0 + k / 8 % 4;
}

// Hearts rising from (x, y), one every 20 frames.
static void draw_hearts(int x, int y, int k) {
    for (int i = 0; i < 3; i++) {
        int age = (k - i * 20) % 60;
        if (age >= 0)
            sprite_draw(SPR_HEART, age > 40, x - 6 + i * 6 + ((age / 8) & 1), y - age / 3, 0);
    }
}

static void album_add_pet(const char* name, Coat coat, u8 generation, u8 end, u32 age_minutes,
                          const u16* best) {
    AlbumEntry e = {0};
    copy_name(e.name, name);
    e.coat = coat;
    e.generation = generation;
    e.end = end;
    e.age_minutes = age_minutes;
    for (int i = 0; i < 3; i++)
        e.best[i] = best ? best[i] : 0;
    album_add(&e);
}

static u8 next_generation(u8 g) {
    return g < 255 ? (u8)(g + 1) : g;
}

// --- Cards -------------------------------------------------------------------------------------

// Inside a frame: the lines, then A.
static void card(const char* const* lines, int count) {
    dark_text();
    int row0 = 8 - count;
    for (int i = 0; i < count; i++) {
        text_set_style(i == 0 ? TEXT_HIGHLIGHT : TEXT_NORMAL);
        text_print_centered(row0 + i * 2, lines[i]);
    }
    text_set_style(TEXT_NORMAL);
    lock_input();
    for (int k = 0;; k++) {
        if (k >= 20)
            prompt(k - 20, "PRESS A");
        if (k >= 20 && (pressed(BUTTON_A) || pressed(BUTTON_START)))
            break;
        tick();
    }
    text_clear();
}

void scenes_card(const char* const* lines, int count) {
    frame_begin();
    card(lines, count);
    frame_end();
}

static bool ask_jump(u32 days) {
    dark_text();
    text_print_centered(5, "THE CLOCK JUMPED AHEAD");
    text_print_centered(6, text_format("BY %u DAYS. COUNT IT?", days));
    text_print_centered(9, "YES: THE DAYS ARE LIVED");
    text_print_centered(10, "NO: THEY ARE NOT COUNTED");
    bool yes = false, dirty = true;
    lock_input();
    for (int k = 0;; k++) {
        if (repeated(BUTTON_LEFT) || repeated(BUTTON_RIGHT) || repeated(BUTTON_UP) ||
            repeated(BUTTON_DOWN)) {
            yes = !yes;
            dirty = true;
        }
        if (dirty) {
            dirty = false;
            text_set_style(yes ? TEXT_HIGHLIGHT : TEXT_NORMAL);
            text_print(9, 13, yes ? ">YES" : " YES");
            text_set_style(yes ? TEXT_NORMAL : TEXT_HIGHLIGHT);
            text_print(17, 13, yes ? " NO" : ">NO");
            text_set_style(TEXT_NORMAL);
        }
        if (k >= 20 && pressed(BUTTON_A))
            break;
        tick();
    }
    sound_mew();
    text_clear();
    return yes;
}

bool scenes_ask_jump(u32 days) {
    frame_begin();
    bool yes = ask_jump(days);
    frame_end();
    return yes;
}

static int summary_lines(const Pet* p, const Away* a, const char* ptrs[EVENT_LINES]) {
    static char lines[EVENT_LINES][EVENT_LINE_MAX] SERVAL_EWRAM_BSS;
    int n = events_summary(p, a, lines);
    for (int i = 0; i < n; i++)
        ptrs[i] = lines[i];
    return n;
}

void scenes_summary(const Pet* p, const Away* a) {
    const char* ptrs[EVENT_LINES];
    int n = summary_lines(p, a, ptrs);
    if (n > 0)
        scenes_card(ptrs, n);
}

bool scenes_catch_up_in_play(DenSave* s, u32 now) {
    Pet* p = &s->pet;
    menu_fade_out(p, !menu_covers_den());
    art_hide();
    dark_text();
    Away a;
    events_live(p, now, clock_hour, &a); // up to about a second, behind the black screen
    const char* ptrs[EVENT_LINES];
    int n = summary_lines(p, &a, ptrs);
    if (n > 0) {
        fade(SCREEN_BRIGHTNESS_MIN, 0, 0, 0);
        card(ptrs, n);
        fade(0, SCREEN_BRIGHTNESS_MIN, 0, 0);
    }
    back_to_den(p, false);
    return true;
}

bool scenes_jump_in_play(DenSave* s, u32 now) {
    Pet* p = &s->pet;
    u32 days = (now - p->last_tick) / 86400;
    menu_fade_out(p, !menu_covers_den());
    art_hide();
    dark_text();
    fade(SCREEN_BRIGHTNESS_MIN, 0, 0, 0);
    if (ask_jump(days)) {
        Away a;
        events_live(p, now, clock_hour, &a);
        const char* ptrs[EVENT_LINES];
        int n = summary_lines(p, &a, ptrs);
        if (n > 0)
            card(ptrs, n);
    } else {
        pet_rebase(p, clock_decline_jump(p->last_tick));
    }
    fade(0, SCREEN_BRIGHTNESS_MIN, 0, 0);
    back_to_den(p, false);
    return true;
}

// --- Growing up --------------------------------------------------------------------------------

#define GROW_START 40 // the sparkles gather, then it grows
#define GROW_SWAP 110 // the new stage's picture
#define GROW_TEXT 160 // the words
#define GROW_SPARKS 6

static struct {
    const Pet* p;
    int from, to;
    int x, y;
    bool left;
} g SERVAL_EWRAM_BSS;

static void sparkles(int cx, int cy, int r, int k) {
    for (int i = 0; i < GROW_SPARKS; i++) {
        u16 a = (u16)(k * 600 + i * (65536 / GROW_SPARKS));
        int x = cx + fx_to_int(fx_cos(a) * r) - 4, y = cy + fx_to_int(fx_sin(a) * r * 3 / 4) - 4;
        sprite_draw(SPR_SPARK, (u8)((k / 6 + i) % SPARK_FRAMES), x, y, 0);
    }
}

static void grow_draw(int k) {
    bool grown = k >= GROW_SWAP;
    int stage = grown ? g.to : g.from;
    int size = stage == STAGE_KITTEN ? 16 : 32;
    FIXED s = FX_ONE;
    if (k >= GROW_START && !grown) {
        FIXED target = g.from == STAGE_KITTEN ? FX(2) : FX_ONE + FX_ONE / 6;
        s = FX_ONE + (target - FX_ONE) * (k - GROW_START) / (GROW_SWAP - GROW_START);
    }
    // White flashes quicken as it grows; white around the change.
    bool white =
        (k >= GROW_START && !grown && (k - GROW_START) % (12 - (k - GROW_START) / 10) < 3) ||
        (k >= GROW_SWAP - 6 && k < GROW_SWAP + 6);
    int frame = !grown ? (k < GROW_START ? idle_frame(k, 0) : PF_STAND)
                       : (k < GROW_SWAP + 60 ? PF_HAPPY : idle_frame(k, 0));
    int cy = g.y + 2 - size * s / FX_ONE / 2; // the feet stay on the floor
    // Sparkles in front: circling closer as it grows, a burst at the change.
    if (k >= GROW_START / 2 && k < GROW_SWAP)
        sparkles(g.x, cy, 36 - (k - GROW_START / 2) * 12 / (GROW_SWAP - GROW_START / 2), k);
    else if (k >= GROW_SWAP && k < GROW_SWAP + 40)
        sparkles(g.x, cy, 14 + (k - GROW_SWAP), k);
    u16 flags = (u16)((g.left ? SPRITE_FLIP_H : 0) | (white ? SPRITE_PALETTE(PAL_COAT1) : 0));
    u16 id = (u16)(SPR_KITTEN + stage);
    if (s == FX_ONE)
        sprite_draw(id, (u8)frame, g.x - size / 2, cy - size / 2, flags);
    else
        sprite_draw_ex(id, (u8)frame, g.x - size / 2, cy - size / 2, 0, s, s, flags);
    if (k >= GROW_TEXT)
        art_draw_panel(16, 12, 13, 4 + (g.to - g.from > 1));
    den_draw_scene(g.p, clock_now(), false);
}

bool scenes_grow_up(DenSave* s) {
    Pet* p = &s->pet;
    g.p = p;
    g.from = s->stage_seen;
    g.to = (int)pet_stage(p, p->last_tick);
    den_pet_spot(&g.x, &g.y);
    g.left = den_pet_left();
    // Room for the grown serval and the sparkles.
    g.x = g.x < 48 ? 48 : g.x > 192 ? 192 : g.x;
    g.y = g.y < FLOOR_TOP + 8 ? FLOOR_TOP + 8 : g.y > 140 ? 140 : g.y;
    text_clear(); // the HUD and the bar's label
    art_flash_coat(PAL_COAT1);
    lock_input();
    int k = 0;
    for (;; k++) {
        if (k == GROW_START)
            sound_fanfare_grow();
        if (k == GROW_TEXT) {
            static const char* const now_a[3] = {"", "NOW AN ADOLESCENT", "NOW AN ADULT"};
            text_set_style(TEXT_HIGHLIGHT);
            text_print_centered(3, text_format("%s GREW UP!", p->name));
            text_set_style(TEXT_NORMAL);
            text_print_centered(5, now_a[g.to]);
            int row = 7;
            for (int st = g.from + 1; st <= g.to; st++)
                text_print_centered(row++, text_format("NEW GAME: %s", game_names[st]));
        }
        if (k >= GROW_TEXT + 30)
            prompt(k - GROW_TEXT - 30, "PRESS A");
        if (k >= GROW_TEXT + 30 && (pressed(BUTTON_A) || pressed(BUTTON_START)))
            break;
        grow_draw(k);
        tick();
    }
    s->stage_seen = (u8)g.to;
    fade(0, SCREEN_BRIGHTNESS_MIN, grow_draw, k);
    back_to_den(p, false);
    return true;
}

// --- The litter --------------------------------------------------------------------------------

#define PICK_Y 138    // the kittens' paws
#define PARENTS_Y 122 // the parents', behind them
#define VISITOR_STOP 146

enum { L_WALK_IN, L_COURT, L_PICK, L_LEAVE };

static struct {
    Pet* p;
    Coat visitor;
    u8 visitor_name;
    Litter l;
    int phase, sel;
    int px, py, vx, vy; // paws, Q4
    int kx[LITTER_MAX]; // the kittens' x, Q4
    int leave_k;
} L SERVAL_EWRAM_BSS;

static int kitten_x(int i) {
    return 120 + (2 * i - (L.l.count - 1)) * 30;
}

// Moves (x, y) (Q4) toward (tx, ty) by `speed`; true once there.
static bool step_to(int* x, int* y, int tx, int ty, int speed) {
    int dx = tx - *x, dy = ty - *y;
    if (int_abs(dx) <= speed && int_abs(dy) <= speed) {
        *x = tx;
        *y = ty;
        return true;
    }
    *x += dx > speed ? speed : dx < -speed ? -speed : dx;
    *y += dy > speed ? speed : dy < -speed ? -speed : dy;
    return false;
}

static void print_names(void) {
    text_clear_area(0, 18, TEXT_COLS, 1);
    for (int i = 0; i < L.l.count; i++) {
        const char* n = litter_names[L.l.names[i]];
        int col = (kitten_x(i) + 4) / 8 - str_len(n) / 2;
        text_set_style(i == L.sel ? TEXT_HIGHLIGHT : TEXT_NORMAL);
        text_print(col < 0 ? 0 : col, 18, n);
    }
    text_set_style(TEXT_NORMAL);
}

static void litter_draw(int k) {
    int adult = STAGE_ADULT;
    int px = L.px / 16, py = L.py / 16, vx = L.vx / 16, vy = L.vy / 16;
    if (L.phase == L_WALK_IN || L.phase == L_COURT) {
        bool court = L.phase == L_COURT;
        if (court)
            draw_hearts((px + vx) / 2, py - 30, k);
        bool pet_walks = !court && (L.px != 100 * 16 || L.py != 134 * 16);
        draw_serval(adult,
                    court       ? (k / 30 % 2 ? PF_HAPPY : PF_STAND_TAIL)
                    : pet_walks ? walk_frame(k)
                                : idle_frame(k, 0),
                    px, py, false, -1);
        draw_serval(adult, court ? (k / 30 % 2 ? PF_STAND_TAIL : PF_HAPPY) : walk_frame(k), vx, vy,
                    true, PAL_COAT1);
    } else {
        // Front to back: the kittens, then the parents.
        for (int i = 0; i < L.l.count; i++) {
            int x = L.kx[i] / 16, y = PICK_Y;
            bool chosen = i == L.sel;
            int frame;
            if (L.phase == L_PICK)
                frame = chosen ? ((k / 20) % 3 == 0 ? PF_HAPPY : PF_STAND) : idle_frame(k, i * 47);
            else
                frame = chosen ? (L.leave_k < 60 ? PF_HAPPY : PF_SIT) : walk_frame(k + i * 3);
            if (chosen && L.phase == L_PICK) {
                y -= (k / 6) % 4 == 1 ? 2 : 0; // a hop
                sprite_draw(SPR_HEART, 0, x - 4, y - 24 - ((k / 16) & 1), 0);
            }
            draw_serval(STAGE_KITTEN, frame, x, y, L.phase == L_PICK && x > 120 && !chosen,
                        PAL_COAT2 + i);
        }
        bool leaving = L.phase == L_LEAVE;
        draw_serval(adult, leaving ? walk_frame(k) : idle_frame(k, 0), px, py, false, -1);
        draw_serval(adult, leaving ? walk_frame(k + 4) : idle_frame(k, 90), vx, vy, !leaving,
                    PAL_COAT1);
    }
    den_draw_scene(L.p, clock_now(), false);
}

bool scenes_litter(DenSave* s) {
    Pet* p = &s->pet;
    L.p = p;
    // The same visitor and litter for this pet every time, so that switching
    // off before the choice is stored replays them rather than re-rolling.
    random_seed(p->born ^ 0x117E5u);
    L.visitor = litter_visitor_coat();
    u8 avoid = 255;
    for (int i = 0; i < LITTER_NAME_COUNT; i++) {
        const char* n = litter_names[i];
        int j = 0;
        while (n[j] && n[j] == p->name[j])
            j++;
        if (!n[j] && !p->name[j])
            avoid = (u8)i;
    }
    litter_make(&L.l, p->coat, L.visitor, avoid);
    for (;;) {
        int v = random_range(0, LITTER_NAME_COUNT - 1);
        bool used = v == avoid;
        for (int i = 0; i < L.l.count; i++)
            used = used || L.l.names[i] == v;
        if (!used) {
            L.visitor_name = (u8)v;
            break;
        }
    }
    random_seed(clock_now() ^ random_entropy()); // the den's chance again
    int tod = art_time_of_day(clock_hour(clock_now()));
    art_set_coat(PAL_COAT1, L.visitor, 0, tod, false);
    for (int i = 0; i < L.l.count; i++)
        art_set_coat(PAL_COAT2 + i, L.l.coats[i], 0, tod, false);
    const char* vname = litter_names[L.visitor_name];

    // A visitor walks in from the savanna, and the pet goes to meet it.
    den_pet_spot(&L.px, &L.py);
    L.px *= 16;
    L.py *= 16;
    L.vx = 260 * 16;
    L.vy = 134 * 16;
    L.phase = L_WALK_IN;
    text_clear();
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(2, "A VISITOR!");
    text_set_style(TEXT_NORMAL);
    sound_mew();
    lock_input();
    int k = 0;
    for (;; k++) {
        bool pet_there = step_to(&L.px, &L.py, 100 * 16, 134 * 16, 16);
        bool there = step_to(&L.vx, &L.vy, VISITOR_STOP * 16, 134 * 16, 14);
        litter_draw(k);
        tick();
        if (pet_there && there && k >= 60)
            break;
    }
    // They court: purring, hearts.
    L.phase = L_COURT;
    text_clear_area(0, 2, TEXT_COLS, 1);
    text_print_centered(2, text_format("%s MEETS %s", p->name, vname));
    for (k = 0; k < 180; k++) {
        if (k == 0 || k == 80)
            sound_purr();
        litter_draw(k);
        tick();
    }
    fade(0, SCREEN_BRIGHTNESS_MIN, litter_draw, k);

    // The litter: choose one.
    L.phase = L_PICK;
    L.px = 72 * 16;
    L.py = PARENTS_Y * 16;
    L.vx = 168 * 16;
    L.vy = PARENTS_Y * 16;
    L.sel = 0;
    for (int i = 0; i < L.l.count; i++)
        L.kx[i] = kitten_x(i) * 16;
    text_clear();
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(1, text_format("%u KITTENS!", (u32)L.l.count));
    text_set_style(TEXT_NORMAL);
    text_print_centered(2, "CHOOSE ONE TO RAISE");
    text_print_centered(3, "LEFT RIGHT CHOOSE   A TAKE");
    print_names();
    sound_fanfare_litter();
    fade(SCREEN_BRIGHTNESS_MIN, 0, litter_draw, 0);
    lock_input();
    for (k = 0;; k++) {
        int was = L.sel;
        if (repeated(BUTTON_RIGHT))
            L.sel = (L.sel + 1) % L.l.count;
        else if (repeated(BUTTON_LEFT))
            L.sel = (L.sel + L.l.count - 1) % L.l.count;
        if (L.sel != was) {
            print_names();
            sound_mew();
        }
        if (k >= 30 && pressed(BUTTON_A))
            break;
        litter_draw(k);
        tick();
    }

    // The parents and the other kittens leave for the savanna.
    L.phase = L_LEAVE;
    L.leave_k = 0;
    sound_mew();
    text_clear();
    text_print_centered(1, text_format("%s AND %s", p->name, vname));
    text_print_centered(2, "LEAVE FOR THE SAVANNA");
    const char* chosen = litter_names[L.l.names[L.sel]];
    int col = (kitten_x(L.sel) + 4) / 8 - str_len(chosen) / 2;
    text_set_style(TEXT_HIGHLIGHT);
    text_print(col < 0 ? 0 : col, 18, chosen);
    text_set_style(TEXT_NORMAL);
    for (k = 0; k < 240; k++, L.leave_k++) {
        L.px += 14;
        L.vx += 14;
        for (int i = 0; i < L.l.count; i++)
            if (i != L.sel)
                L.kx[i] += 11;
        litter_draw(k);
        tick();
    }
    fade(0, SCREEN_BRIGHTNESS_MIN, litter_draw, k);

    // The new pet, named.
    art_hide();
    dark_text();
    fade(SCREEN_BRIGHTNESS_MIN, 0, 0, 0);
    char name[9];
    frame_end();
    naming_run(name, "NAME YOUR KITTEN", chosen);
    frame_begin();
    u32 age = pet_age_minutes(p, p->last_tick);
    album_add_pet(vname, L.visitor, p->generation, END_LITTER, 0, 0); // age 0: a visitor
    album_add_pet(p->name, p->coat, p->generation, END_LITTER, age, s->best);
    pet_new(p, name, L.l.coats[L.sel], next_generation(p->generation), clock_now());
    s->stage_seen = STAGE_KITTEN;
    fade(0, SCREEN_BRIGHTNESS_MIN, 0, 0);
    back_to_den(p, true);
    return true;
}

// --- The tombstone -----------------------------------------------------------------------------

#define TOMB_Y 128 // the stone's foot
#define NEW_KITTEN_Y 112

static struct {
    const Pet* p;
    Coat coat; // the new kitten's
    int x;     // the new kitten's, Q4
} t SERVAL_EWRAM_BSS;

// The flowers laid round the stone: x, frame.
static const u8 flowers[6][2] = {{92, 0}, {101, 1}, {108, 0}, {128, 1}, {136, 0}, {145, 1}};

static void tomb_draw(int k) {
    for (int i = 0; i < 6; i++)
        sprite_draw(SPR_FLOWER, flowers[i][1], flowers[i][0], TOMB_Y - 6 + (i & 1), 0);
    // A firefly or two from the jar, drifting up over the stone.
    for (int i = 0; i < 2; i++) {
        int q = (k + i * 90) % 180;
        if (q < 150)
            sprite_draw(SPR_SPARK, (u8)(q / 50), 102 + i * 30 + ((q / 20) & 1) * 2 - (q / 30),
                        TOMB_Y - 30 - q / 8, 0);
    }
    sprite_draw_ex(SPR_TOMB, 0, 112, TOMB_Y - 23, 0, FX(2), FX(2), 0);
    art_draw_panel(48, 12, 9, 4);
    den_draw_scene(t.p, clock_now(), t.p->lights_off);
}

static void arrival_draw(int k) {
    int x = t.x / 16;
    bool walking = x < 120;
    draw_serval(STAGE_KITTEN,
                walking             ? walk_frame(k)
                : (k / 20) % 3 == 0 ? PF_HAPPY
                                    : PF_STAND,
                x, NEW_KITTEN_Y, false, PAL_COAT1);
}

bool scenes_tombstone(DenSave* s) {
    Pet* p = &s->pet;
    t.p = p;
    menu_fade_out(p, true);
    text_clear();
    u32 lived = p->died_at > p->born ? (p->died_at - p->born) / 60 : 0;
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(3, p->name);
    text_set_style(TEXT_NORMAL);
    u32 days = lived / 1440;
    // At least a day: death comes 36 h after the last care.
    text_print_centered(5, text_format("LIVED %u DAY%s", days, days == 1 ? "" : "S"));
    text_print_centered(7, text_format("GENERATION %u", (u32)p->generation));
    sound_tune_tomb();
    fade(SCREEN_BRIGHTNESS_MIN, 0, tomb_draw, 0);
    lock_input();
    int k = 0;
    for (;; k++) {
        if (k >= 90)
            prompt(k - 90, "PRESS A");
        if (k >= 90 && (pressed(BUTTON_A) || pressed(BUTTON_START)))
            break;
        tomb_draw(k);
        tick();
    }
    sound_tune_stop();
    fade(0, SCREEN_BRIGHTNESS_MIN, tomb_draw, k);

    // A new kitten arrives.
    art_hide();
    dark_text();
    t.coat = litter_visitor_coat();
    t.x = -16 * 16;
    art_set_coat(PAL_COAT1, t.coat, 0, TOD_DAY, false);
    text_print_centered(8, "A NEW KITTEN ARRIVES");
    fade(SCREEN_BRIGHTNESS_MIN, 0, arrival_draw, 0);
    lock_input();
    for (k = 0;; k++) {
        if (t.x < 120 * 16) {
            t.x += 12;
            if (t.x >= 120 * 16)
                sound_mew();
        } else if (k >= 60) {
            prompt(k, "PRESS A");
            if (pressed(BUTTON_A) || pressed(BUTTON_START))
                break;
        }
        arrival_draw(k);
        tick();
    }
    fade(0, SCREEN_BRIGHTNESS_MIN, arrival_draw, k);
    dark_text();
    fade(SCREEN_BRIGHTNESS_MIN, 0, 0, 0);
    char name[9];
    frame_end();
    naming_run(name, "NAME YOUR KITTEN", 0);
    frame_begin();
    album_add_pet(p->name, p->coat, p->generation, END_TOMBSTONE, lived, s->best);
    pet_new(p, name, t.coat, next_generation(p->generation), clock_now());
    s->stage_seen = STAGE_KITTEN;
    fade(0, SCREEN_BRIGHTNESS_MIN, 0, 0);
    back_to_den(p, true);
    return true;
}
