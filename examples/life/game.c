// The world on screen and the player's hands on it: the grid shown by the
// pixel surface, stepped at the chosen speed, the cursor, painting, stamps,
// the zoom and the HUD.
//
// The surface is the world: its top-left LIFE_W x LIFE_H pixels are the
// grid, a byte a cell, and the transform (surface_set_transform) stretches
// them 2x to fill the screen, or 4x around the cursor, at no cost. Each
// generation the step writes the new grid into RAM and into the page being
// drawn at once, then the page is flipped; while paused, the page is drawn
// again from the grid only when something on it changed.

#include "life.h"

const u8 speeds[LIFE_SPEEDS] = {1, 2, 4, 8, 15, 30, 60};
#define SPEED_DEFAULT 4 // 15 a second
#define SPEED_TITLE 4

static LifeGrid grids[2] SERVAL_EWRAM_BSS;
static u32 now; // grids[now] is the world
// Which rows of each grid may hold anything: the step skips the rows with
// nothing in or beside them (life_step_rows()).
static u8 rows[2][LIFE_H];
// What a cell becomes: read for each of the 9,600 cells a generation, so it
// is kept in IWRAM.
static u8 table[LIFE_TABLE_SIZE];
static LifeSettings settings;
static LifeStamp stamp SERVAL_EWRAM_BSS;

static bool running;
static u32 ticks; // speed added each frame; a generation per 60
static LifeStats stats;
static int cx, cy; // the cursor's cell

static bool painting, stamping, wants_menu;
static u8 paint_value;
static bool page_dirty; // the page must be drawn again
static bool save_dirty;

// Periods: the hashes of the last generations, newest last.
#define PERIOD_MAX 30
static u32 history[PERIOD_MAX] SERVAL_EWRAM_BSS;
static u32 history_count;
static u32 period; // 0: none found; 1: still; n: repeating every n generations

// --- The zoom -------------------------------------------------------------------
//
// The screen shows cells `ppc` pixels wide (8.8 fixed point: 2.0 or 4.0, and
// eased between them over ZOOM_FRAMES), from cell (ox, oy) at its top-left
// (8.8). At 4x it shows a 60 x 40 window, which follows the cursor, kept
// within the world.

#define ZOOM_FRAMES 12
#define WINDOW_W (LIFE_W / 2)
#define WINDOW_H (LIFE_H / 2)
#define MARGIN_X 8
#define MARGIN_Y 6
static bool zoomed;
static int zoom_t;             // 0 (2x) to ZOOM_FRAMES (4x)
static int window_x, window_y; // the 4x window's target, in cells
static s32 view_x, view_y;     // where it is, eased toward the target (8.8 cells)
static s32 ppc, ox, oy;

static u32 ease(int t) {
    // smoothstep, 0-256
    u32 x = (u32)t * 256 / ZOOM_FRAMES;
    return x * x * (768 - 2 * x) / 65536;
}

static void window_follow(bool jump) {
    if (cx < window_x + MARGIN_X)
        window_x = cx - MARGIN_X;
    if (cx > window_x + WINDOW_W - 1 - MARGIN_X)
        window_x = cx - (WINDOW_W - 1 - MARGIN_X);
    if (cy < window_y + MARGIN_Y)
        window_y = cy - MARGIN_Y;
    if (cy > window_y + WINDOW_H - 1 - MARGIN_Y)
        window_y = cy - (WINDOW_H - 1 - MARGIN_Y);
    window_x = int_clamp(window_x, 0, LIFE_W - WINDOW_W);
    window_y = int_clamp(window_y, 0, LIFE_H - WINDOW_H);
    if (jump) {
        view_x = window_x * 256;
        view_y = window_y * 256;
    } else {
        // A quarter of the way a frame, then the last few 256ths at once:
        // division toward zero alone would stop short when moving right or
        // down, the cells a pixel off the ring.
        s32 dx = window_x * 256 - view_x, dy = window_y * 256 - view_y;
        view_x += dx > -4 && dx < 4 ? dx : dx / 4;
        view_y += dy > -4 && dy < 4 ? dy : dy / 4;
    }
}

static void zoom_update(void) {
    if (zoomed && zoom_t < ZOOM_FRAMES)
        zoom_t++;
    else if (!zoomed && zoom_t > 0)
        zoom_t--;
    window_follow(false);
    u32 e = ease(zoom_t);
    ppc = 512 + (s32)(2 * e);
    ox = view_x * (s32)e / 256;
    oy = view_y * (s32)e / 256;
    FIXED scale = (65536 + ppc / 2) / ppc; // rounded: the ring stays on its cell

    surface_set_transform(ox, oy, scale, 0, 0, scale);
}

// Where a cell's top-left is on the screen.
static int screen_x(int x) {
    return (int)(((s32)x * 256 - ox) * ppc >> 16);
}
static int screen_y(int y) {
    return (int)(((s32)y * 256 - oy) * ppc >> 16);
}

// --- The page ---------------------------------------------------------------------

// Sets one pixel of the page: a halfword read, changed and written back
// (video memory takes no single bytes).
static void page_pixel(u8* page, int x, int y, u8 color) {
    u32 at = (u32)(y * SURFACE_W + x);
    u16* pair = (u16*)(page + (at & ~1u));
    *pair = (at & 1) ? (u16)((*pair & 0x00FF) | color << 8) : (u16)((*pair & 0xFF00) | color);
}

// The stamp's preview over the world: its box faintly, its cells
// see-through, a live cell it would clear in dark red.
static void draw_preview(u8* page) {
    int left = life_stamp_left(&stamp, cx), top = life_stamp_top(&stamp, cy);
    const u32* grid = grids[now];
    for (int y = 0; y < stamp.h; y++) {
        int gy = life_wrap_y(top + y);
        for (int x = 0; x < stamp.w; x++) {
            int gx = life_wrap_x(left + x);
            bool live = life_alive(life_get(grid, gx, gy));
            u8 c = stamp.cells[y][x] ? (live ? COLOR_PREVIEW_LIVE : COLOR_PREVIEW)
                                     : (live ? COLOR_PREVIEW_KILL : COLOR_PREVIEW_BOX);
            page_pixel(page, gx, gy, c);
        }
    }
}

static void page_finish(u8* page) {
    if (stamping)
        draw_preview(page);
    surface_flip();
    page_dirty = false;
}

void game_draw_world(void) {
    u8* page = surface_pixels();
    life_draw(grids[now], (u32*)page);
    page_finish(page);
}

// --- The world ----------------------------------------------------------------------
//
// A generation is stepped whole, or in two halves over two frames: the top
// rows in one, the rest and the flip at the start of the next, before any
// input is read, so nothing changes the grid between them. Halves are used
// at every speed but 60 a second (a generation every other frame or less
// often anyway), and at 60 too unless the generation is sure to fit in a
// frame with everything else: its cost, foretold from the last one's, is
// about 2,400 cycles for each row the step can't skip (a row with nothing in
// it or beside it) and 60 more for each word of four cells it worked out.
// The whole world busy, a generation takes 190,000 cycles with nothing alive
// in it and 330,000 for a fresh soup, over the 280,000 of a frame: a busy
// world runs at 30 a second in TURBO, a sparse one at 60.

#define SPLIT_ROW (LIFE_H / 2)
#define ROW_CYCLES 2400
#define WORD_CYCLES 60
#define WHOLE_CYCLES 175000 // the most a whole generation may cost

static bool half_done;    // a generation's first half is done, its second due
static u32 half_speed;    // the speed it is stepped at, for its sound
static LifeStats partial; // its stats so far

static void history_reset(void) {
    history_count = 0;
    period = 0;
}

// After an edit the busy words are unknown until a generation is stepped
// (life_count() doesn't count them): whole_fits() assumes the worst.
static bool busy_unknown;

static void edited(void) {
    stats = life_count(grids[now]);
    busy_unknown = true;
    life_rows_scan(grids[now], rows[now]);
    history_reset();
    page_dirty = true;
    save_dirty = true;
}

// The generation is in grids[now ^ 1] and on the page: it becomes the world.
static void generation_done(u8* page) {
    u32 before = stats.population;
    stats = partial;
    busy_unknown = false;
    now ^= 1;
    settings.generation++;
    u32 deaths = before + stats.births - stats.population;
    // The world repeats if it is what it was some generations ago.
    period = 0;
    for (u32 k = 1; k <= history_count && !period; k++)
        if (history[history_count - k] == stats.hash)
            period = k;
    if (history_count == PERIOD_MAX) {
        for (u32 i = 1; i < PERIOD_MAX; i++)
            history[i - 1] = history[i];
        history_count--;
    }
    history[history_count++] = stats.hash;
    page_finish(page);
    save_dirty = true;
    sound_generation(&stats, stats.births, deaths, half_speed);
}

static void step_begin(u32 speed, bool whole) {
    u8* page = surface_pixels();
    partial = LIFE_STATS_START;
    half_speed = speed;
    if (whole) {
        life_step_rows(grids[now], rows[now], grids[now ^ 1], rows[now ^ 1], (u32*)page, table, 0,
                       LIFE_H, &partial);
        generation_done(page);
    } else {
        life_step_rows(grids[now], rows[now], grids[now ^ 1], rows[now ^ 1], (u32*)page, table, 0,
                       SPLIT_ROW, &partial);
        half_done = true;
    }
}

// At the start of a frame: the second half of a generation begun in the
// last one. Returns whether it finished one.
static bool step_continue(void) {
    if (!half_done)
        return false;
    u8* page = surface_pixels();
    life_step_rows(grids[now], rows[now], grids[now ^ 1], rows[now ^ 1], (u32*)page, table,
                   SPLIT_ROW, LIFE_H, &partial);
    half_done = false;
    generation_done(page);
    return true;
}

// Whether a whole generation will fit in this frame.
static bool whole_fits(void) {
    const u8* r = rows[now];
    u32 worked = 0;
    for (int y = 0; y < LIFE_H; y++)
        worked += (r[y ? y - 1 : LIFE_H - 1] | r[y] | r[y + 1 < LIFE_H ? y + 1 : 0]) != 0;
    // Unknown, every word of the rows worked may be busy.
    u32 busy = busy_unknown ? worked * LIFE_ROW_WORDS : stats.busy;
    return worked * ROW_CYCLES + busy * WORD_CYCLES <= WHOLE_CYCLES;
}

// Steps at `speed` generations a second: a generation is due every 60 /
// speed frames, and none starts in a frame that finished one.
static void run(u32 speed, bool finished) {
    ticks += speed;
    if (ticks < 60)
        return;
    if (finished || half_done) {
        ticks = 60; // due at the next frame
        return;
    }
    ticks -= 60;
    step_begin(speed, speed >= 60 && whole_fits());
}

void game_apply_rule(void) {
    life_table_build(table, &life_rules[settings.rule]);
    history_reset();
}

void game_clear(void) {
    life_clear(grids[now]);
    settings.generation = 0;
    edited();
}

void game_soup(void) {
    random_seed(random_entropy());
    life_soup(grids[now]);
    settings.generation = 0;
    edited();
}

// The world the title shows when there is no save: a glider gun firing into
// a world with an R-pentomino and an acorn in it, which take hundreds of
// generations to settle, and a pulsar.
static void seed(void) {
    static const struct {
        u8 pattern, turns, x, y;
    } seeds[] = {
        {LIFE_PAT_GOSPER_GUN, 0, 24, 10}, {LIFE_PAT_R_PENTOMINO, 0, 82, 30},
        {LIFE_PAT_ACORN, 0, 40, 60},      {LIFE_PAT_PULSAR, 0, 98, 64},
        {LIFE_PAT_LWSS, 0, 12, 46},
    };
    life_clear(grids[now]);
    for (u32 i = 0; i < sizeof seeds / sizeof seeds[0]; i++) {
        life_stamp_make(&stamp, seeds[i].pattern, seeds[i].turns);
        life_stamp_put(grids[now], &stamp, seeds[i].x, seeds[i].y);
    }
}

LifeSettings* game_settings(void) {
    return &settings;
}

static u8 save_bytes[LIFE_SAVE_BYTES] SERVAL_EWRAM_BSS;

// The settings as last saved or loaded: the menu changes them directly.
static LifeSettings saved;

static bool settings_changed(void) {
    return settings.rule != saved.rule || settings.theme != saved.theme ||
           settings.speed != saved.speed || settings.sound != saved.sound ||
           settings.stamp != saved.stamp || settings.turns != saved.turns;
}

// About 270,000 cycles, nearly all of it save_write() (SRAM is slow): the
// frame it is called in runs late, so only at a pause and as the menu
// closes, and only when something changed.
void game_save(void) {
    if (!save_dirty && !settings_changed())
        return;
    life_save_pack(save_bytes, &settings, grids[now]);
    save_write(0, save_bytes, LIFE_SAVE_BYTES, LIFE_SAVE_VERSION);
    saved = settings;
    save_dirty = false;
}

// --- The HUD ------------------------------------------------------------------------
//
// One slim line over a strip of dark glass, at the bottom, or at the top
// while the cursor is near the bottom: run or pause, the speed, the
// generation, the population, and at the right whether the world has gone
// still or repeats. Each field is a label of its own, composed when it
// changes; while the world runs fast, the numbers at most 15 times a second.

static Label hud_state, hud_speed, hud_gen, hud_pop, hud_status;
static bool hud_top;

// "<name> ~<n>~" into `out`: what text_format() would make, in a tenth of
// the time, for numbers that change every frame.
static const char* counter(char* out, const char* name, u32 n) {
    char digits[10];
    int count = 0;
    do {
        digits[count++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    char* p = out;
    while (*name)
        *p++ = *name++;
    *p++ = ' ';
    *p++ = '~';
    while (count)
        *p++ = digits[--count];
    *p++ = '~';
    *p = 0;
    return out;
}

static void hud_draw(bool turbo) {
    // (text_format() costs about 7,000 cycles: only for what changed.)
    static int shown_speed = -1;
    int speed = running && !turbo ? speeds[settings.speed] : 0;
    label_set(&hud_state, running ? (turbo ? "} ~TURBO~" : "} RUN") : "| PAUSE");
    if (speed != shown_speed) {
        label_set(&hud_speed, speed ? text_format("~%u/S~", (u32)speed) : "");
        shown_speed = speed;
    }
    // Running, the generation at most every fourth frame and the population
    // two frames later: one composed in a frame at most (about 12,000 cycles).
    char text[24];
    u32 phase = running ? frame_count() & 3 : 0;
    if (phase == 0)
        label_set(&hud_gen, counter(text, "GEN", settings.generation));
    if (!running || phase == 2)
        label_set(&hud_pop, counter(text, "POP", stats.population));
    static u32 shown_period = 0xFFFFFFFF;
    u32 what = stats.population == 0 ? 0 : period + 1;
    if (what != shown_period) {
        shown_period = what;
        label_set(&hud_status, what == 0   ? "EMPTY"
                               : what == 2 ? "~STILL~"
                               : what > 2  ? text_format("PERIOD ~%u~", what - 1)
                                           : "");
    }

    int cursor_y = screen_y(cy);
    if (!hud_top && cursor_y > SCREEN_H - 24)
        hud_top = true;
    else if (hud_top && cursor_y < 24)
        hud_top = false;
    int band = hud_top ? 0 : SCREEN_H - 10;
    int x = 4, y = band + 2;
    label_draw(&hud_state, x, y, 0);
    x += hud_state.width + 6;
    if (hud_speed.width > 1) {
        label_draw(&hud_speed, x, y, 0);
        x += hud_speed.width + 6;
    }
    label_draw(&hud_gen, x + 4, y, 0);
    label_draw(&hud_pop, x + 4 + hud_gen.width + 10, y, 0);
    label_draw(&hud_status, SCREEN_W - 4 - hud_status.width, y, 0);
    u16 flags = SPRITE_BLEND | (hud_top ? SPRITE_FLIP_V : 0);
    for (int sx = 0; sx < SCREEN_W; sx += 32) {
        sprite_draw(SPR_STRIP, 0, sx, band + 2, flags);
        sprite_draw(SPR_STRIP, 0, sx, band, flags);
    }
}

// --- Setting up -------------------------------------------------------------------

static void defaults(void) {
    settings = (LifeSettings){.rule = LIFE_RULE_CONWAY,
                              .theme = 0,
                              .speed = SPEED_DEFAULT,
                              .sound = 1,
                              .stamp = LIFE_PAT_GLIDER};
}

void game_init(void) {
    // Label frames 0-8: the HUD's (the title's and the menu's follow).
    label_init(&hud_state, SPR_TEXT, 0, 2, "");
    label_init(&hud_speed, SPR_TEXT, 2, 1, "");
    label_init(&hud_gen, SPR_TEXT, 3, 2, "");
    label_init(&hud_pop, SPR_TEXT, 5, 2, "");
    label_init(&hud_status, SPR_TEXT, 7, 2, "");
    defaults();
    now = 0;
    if (save_read(0, save_bytes, LIFE_SAVE_BYTES, LIFE_SAVE_VERSION) != SAVE_OK ||
        !life_save_unpack(save_bytes, &settings, grids[now])) {
        defaults();
        seed();
    }
    saved = settings;
    sound_set(settings.sound);
    life_stamp_make(&stamp, settings.stamp, settings.turns);
    game_apply_rule();
    stats = life_count(grids[now]);
    busy_unknown = true;
    life_rows_scan(grids[now], rows[now]);
    life_rows_scan(grids[now ^ 1], rows[now ^ 1]);
    cx = LIFE_W / 2;
    cy = LIFE_H / 2;
    zoom_update();
    page_dirty = true;
}

void game_title_frame(void) {
    bool finished = step_continue();
    run(speeds[SPEED_TITLE], finished);
    if (page_dirty && !half_done && !finished)
        game_draw_world();
    zoom_update();
}

// The D-pad's directions: frames each has been held.
static int held[4];
static void moves_reset(void) {
    for (int i = 0; i < 4; i++)
        held[i] = 0;
}

void game_start(void) {
    running = false;
    cx = LIFE_W / 2;
    cy = LIFE_H / 2;
    ticks = 0;
    button_repeat_reset();
    moves_reset();
}

bool game_wants_menu(void) {
    return wants_menu;
}

void game_resume(void) {
    wants_menu = false;
    life_stamp_make(&stamp, settings.stamp, settings.turns);
    button_repeat_reset();
    moves_reset();
    page_dirty = true;
}

// --- Play ---------------------------------------------------------------------------

// The D-pad: a step at the press, then, held, again after 14 frames, every 4
// frames, then every 2 and at last every frame.
static bool move_due(u16 button, int i) {
    if (!button_down(button)) {
        held[i] = 0;
        return false;
    }
    // A move starts with a press: a button already held when play resumes
    // (closing the menu with a direction held) waits for the next one.
    if (held[i] == 0 && !button_pressed(button))
        return false;
    int h = held[i]++;
    if (h == 0)
        return true;
    if (h < 14)
        return false;
    int every = h < 40 ? 4 : h < 80 ? 2 : 1;
    return (h - 14) % every == 0;
}

// The cell under the cursor becomes paint_value.
static void paint(void) {
    u32* grid = grids[now];
    u8 was = life_get(grid, cx, cy);
    if (was == paint_value)
        return;
    life_set(grid, cx, cy, paint_value);
    rows[now][cy] = 1;
    stats.population += life_alive(paint_value);
    stats.population -= life_alive(was);
    history_reset();
    page_dirty = true;
    save_dirty = true;
}

static void controls(bool* turbo, bool* step_now) {
    // The cursor.
    int dx = move_due(BUTTON_RIGHT, 0) - move_due(BUTTON_LEFT, 1);
    int dy = move_due(BUTTON_DOWN, 2) - move_due(BUTTON_UP, 3);
    if (dx || dy) {
        cx = life_wrap_x(cx + dx);
        cy = life_wrap_y(cy + dy);
        if (painting)
            paint();
        if (stamping)
            page_dirty = true;
    }

    // B: the stamp, shown while held, turned by L and R, put down on release.
    if (button_pressed(BUTTON_B) && !painting) {
        stamping = true;
        page_dirty = true;
    }
    if (stamping) {
        int turn = button_pressed(BUTTON_R) - button_pressed(BUTTON_L);
        if (turn) {
            settings.turns = (u8)((settings.turns + turn) & 3);
            life_stamp_make(&stamp, settings.stamp, settings.turns);
            page_dirty = true;
            sound_play(SFX_TURN);
        }
        if (!button_down(BUTTON_B)) {
            stamping = false;
            life_stamp_put(grids[now], &stamp, cx, cy);
            edited();
            sound_play(SFX_STAMP);
        }
        return; // L, R and A wait
    }

    // A: toggles the cell; held, paints what that made onto every cell the
    // cursor enters.
    if (button_pressed(BUTTON_A)) {
        paint_value = life_alive(life_get(grids[now], cx, cy)) ? LIFE_EMPTY : LIFE_BORN;
        painting = true;
        paint();
        sound_play(paint_value ? SFX_ON : SFX_OFF);
    }
    if (!button_down(BUTTON_A))
        painting = false;

    if (button_pressed(BUTTON_L)) {
        zoomed = !zoomed;
        if (zoomed) {
            // Zoom in about the cursor: the window that keeps it where it is.
            window_x = cx / 2;
            window_y = cy / 2;
            window_follow(true);
        }
        sound_play(zoomed ? SFX_ZOOM_IN : SFX_ZOOM_OUT);
    }
    if (running) {
        *turbo = button_down(BUTTON_R);
    } else if (button_repeat(BUTTON_R)) {
        *step_now = true;
        sound_play(SFX_STEP);
    }

    if (button_pressed(BUTTON_START)) {
        running = !running;
        ticks = 0;
        sound_play(running ? SFX_RUN : SFX_PAUSE);
        if (!running)
            game_save();
    }
    if (button_pressed(BUTTON_SELECT) && !painting)
        wants_menu = true;
}

void game_frame(void) {
    bool finished = step_continue();
    bool turbo = false, step_now = false;
    controls(&turbo, &step_now);
    if (!wants_menu) {
        if (running)
            run(turbo ? 60 : speeds[settings.speed], finished);
        else if (step_now && !finished && !half_done)
            step_begin(1, false);
    }
    if (page_dirty && !half_done)
        game_draw_world();
    zoom_update();

    // The cursor's ring, a pixel outside the cell: 4 x 4 at 2x, 6 x 6 at 4x.
    // While the stamp shows, its preview takes the ring's place.
    if (stamping)
        art_preview_pulse();
    else
        sprite_draw(SPR_CURSOR, ppc >= 768, screen_x(cx) - 1, screen_y(cy) - 1, 0);
    hud_draw(turbo);
}
