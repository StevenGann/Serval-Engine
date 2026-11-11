// The care menu: see menu.h.

#include "menu.h"

#include "album.h"
#include "art.h"
#include "clock.h"
#include "sound.h"
#include "status.h"

#include <serval/serval.h>

#define BAR_Y 143
#define BAR_X0 10
#define BAR_PITCH 29
#define LABEL_ROW 16
#define LIST_BOTTOM_ROW 13
#define MESSAGE_FRAMES 90
#define FADE_STEP 2

typedef enum { M_BAR, M_LIST, M_STATUS, M_ALBUM } Mode;
typedef enum { LIST_FEED, LIST_PLAY } ListKind;

static const char* const icon_names[ICON_COUNT] = {"FEED",     "PLAY",   "PET",    "CLEAN",
                                                   "MEDICINE", "LIGHTS", "STATUS", "ALBUM"};
static const char* const feed_names[2] = {"MEAL", "TREAT"};
static const char* const game_names[GAME_COUNT] = {"YARN BAT", "ACACIA CLIMB", "HIGH POUNCE"};
static const PetStage game_stage[GAME_COUNT] = {STAGE_KITTEN, STAGE_ADOLESCENT, STAGE_ADULT};

static MenuGameFn games[GAME_COUNT];

static struct {
    Mode mode;
    ListKind list;
    int sel, list_sel;
    int shown_label; // what the label row shows: icon index, or -1 for redraw
    int message_timer;
    int press_timer, press_icon;
    u16 lock;
    bool list_dirty; // the list's text needs drawing
    int list_stage;  // the pet's stage when it was drawn (greyed games)
    int strip_len;   // the label row's text, in characters (0: none)
} m;

void menu_set_game(int game, MenuGameFn fn) {
    if (game >= 0 && game < GAME_COUNT)
        games[game] = fn;
}

// --- Input -------------------------------------------------------------------------------------

static void lock_input(void) {
    m.lock = BUTTON_ANY;
    button_repeat_reset();
}

static void refresh_lock(void) {
    for (u16 bit = 1; bit <= BUTTON_L; bit <<= 1)
        if (!button_down(bit))
            m.lock &= (u16)~bit;
}

static bool pressed(u16 b) {
    return button_pressed(b) && !(m.lock & b);
}

static bool repeated(u16 b) {
    return !(m.lock & b) && button_repeat(b);
}

// Style 3 (greyed entries) must read on the list's dark panel.
static void set_styles(void) {
    text_set_style_color(3, COLOR_RGB(150, 140, 132), COLOR_RGB(20, 14, 12));
}

void menu_init(void) {
    set_styles();
    m.mode = M_BAR;
    m.sel = 0;
    m.shown_label = -1;
    m.message_timer = 0;
    m.press_timer = 0;
    lock_input();
}

bool menu_covers_den(void) {
    return m.mode == M_STATUS || m.mode == M_ALBUM;
}

// --- Fades -------------------------------------------------------------------------------------

static void tick(void) {
    frame_end();
    frame_begin();
}

static void page_sprites(const Pet* p) {
    if (m.mode == M_STATUS)
        status_draw(p, clock_now());
}

static void fade(const Pet* p, int from, int to, bool draw) {
    int step = from < to ? FADE_STEP : -FADE_STEP;
    for (int b = from;; b += step) {
        if (b * step > to * step)
            b = to;
        screen_set_brightness(b);
        if (draw) {
            menu_draw(p, clock_now());
            den_draw(p, clock_now());
        } else {
            page_sprites(p);
        }
        tick();
        if (b == to)
            break;
    }
}

void menu_fade_out(const Pet* p, bool draw) {
    fade(p, 0, SCREEN_BRIGHTNESS_MIN, draw);
}

void menu_fade_in(const Pet* p, bool draw) {
    fade(p, SCREEN_BRIGHTNESS_MIN, 0, draw);
}

// --- Messages and lists ----------------------------------------------------------------------

static int text_chars(const char* s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void show_message(const char* s, int style) {
    text_set_style(style);
    text_print_centered(LABEL_ROW, s);
    text_set_style(TEXT_NORMAL);
    m.strip_len = (int)text_chars(s);
    m.message_timer = MESSAGE_FRAMES;
    m.shown_label = -2;
}

static void refuse(const char* s) {
    sound_no();
    show_message(s, 2);
}

static int list_count(void) {
    return m.list == LIST_FEED ? 2 : GAME_COUNT;
}

static int list_col(void) {
    int col = m.sel * BAR_PITCH / 8;
    return col < 2 ? 2 : col;
}

static bool game_open(const Pet* p, int i) {
    return pet_stage(p, p->last_tick) >= game_stage[i];
}

static void draw_list(const Pet* p) {
    m.list_stage = (int)pet_stage(p, p->last_tick);
    int n = list_count();
    int row0 = LIST_BOTTOM_ROW - n + 1;
    for (int i = 0; i < n; i++) {
        bool on = i == m.list_sel;
        bool open = m.list == LIST_FEED || game_open(p, i);
        text_set_style(on ? TEXT_HIGHLIGHT : (open ? TEXT_NORMAL : 3));
        const char* name = m.list == LIST_FEED ? feed_names[i] : game_names[i];
        text_print(list_col(), row0 + i, text_format("%c%-12s", on ? '>' : ' ', name));
    }
    text_set_style(TEXT_NORMAL);
}

static void close_list(void) {
    text_clear_area(list_col(), LIST_BOTTOM_ROW - list_count() + 1, 15, list_count());
    m.mode = M_BAR;
}

static void open_list(ListKind k) {
    m.list = k;
    m.list_sel = 0;
    m.mode = M_LIST;
    m.list_dirty = true;
}

// --- Pages -----------------------------------------------------------------------------------

static void page_dark(void) {
    text_clear();
    status_reset();
}

static void draw_album(void) {
    text_clear();
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(1, "ALBUM");
    text_set_style(TEXT_NORMAL);
    u8 n = album_count();
    if (n == 0)
        text_print_centered(9, "NO ONE YET");
    for (u8 i = 0; i < n && i < 14; i++) {
        const AlbumEntry* e = album_get(i);
        text_print(3, 3 + i,
                   text_format("%-8s GEN %-2u %s", e->name, (u32)e->generation,
                               e->end == END_TOMBSTONE ? "TOMBSTONE" : "LITTER"));
    }
    text_print_centered(19, "B BACK");
}

static void open_page(Pet* p, Mode mode) {
    menu_fade_out(p, true);
    m.mode = mode;
    art_hide();
    page_dark();
    if (mode == M_ALBUM)
        draw_album();
    menu_fade_in(p, false);
    lock_input();
}

static void close_page(Pet* p) {
    menu_fade_out(p, false);
    m.mode = M_BAR;
    text_clear();
    den_resume();
    m.shown_label = -1;
    menu_fade_in(p, true);
    lock_input();
}

// --- Actions -----------------------------------------------------------------------------------

static const char* refusal(CareResult r) {
    switch (r) {
    case CARE_REFUSED_FULL:
        return "NOT HUNGRY";
    case CARE_REFUSED_NOT_SICK:
        return "NOT SICK";
    case CARE_REFUSED_CLEAN:
        return "ALREADY CLEAN";
    case CARE_REFUSED_ASLEEP:
        return "ASLEEP";
    default:
        return "GONE";
    }
}

static bool care(Pet* p, CareAction a, u32 now, int icon) {
    CareResult r = den_care(p, a, now);
    m.press_icon = icon;
    m.press_timer = 6;
    if (r != CARE_DONE) {
        show_message(refusal(r), 2);
        return false;
    }
    return true;
}

static bool play_game(Pet* p, int i, u32 now) {
    // The list may have stayed open while the pet fell asleep, got too tired
    // or died: ask again, with the clock as it is now.
    now = clock_now();
    if (!pet_can_play(p, now)) {
        close_list();
        refuse(p->dead ? "GONE" : p->asleep ? "SHH, ASLEEP" : "TOO TIRED");
        return false;
    }
    if (!game_open(p, i)) {
        refuse("NOT YET");
        return false;
    }
    if (!games[i]) {
        sound_no();
        show_message("COMING SOON", TEXT_HIGHLIGHT);
        return false;
    }
    close_list();
    text_clear_area(0, LABEL_ROW, TEXT_COLS, 1);
    menu_fade_out(p, true);
    text_clear();
    bool changed = games[i](p, clock_now()); // after the fade
    den_resume();
    set_styles();
    m.shown_label = -1;
    menu_fade_in(p, true);
    lock_input();
    return changed;
}

static bool activate(Pet* p, u32 now) {
    switch (m.sel) {
    case ICON_FEED:
        if (p->dead) {
            refuse("GONE");
            return false;
        }
        open_list(LIST_FEED);
        return false;
    case ICON_PLAY:
        if (!pet_can_play(p, now)) {
            refuse(p->dead ? "GONE" : p->asleep ? "SHH, ASLEEP" : "TOO TIRED");
            return false;
        }
        open_list(LIST_PLAY);
        return false;
    case ICON_PET:
        return care(p, CARE_PET, now, m.sel);
    case ICON_CLEAN:
        return care(p, CARE_CLEAN, now, m.sel);
    case ICON_MEDICINE:
        return care(p, CARE_MEDICINE, now, m.sel);
    case ICON_LIGHTS:
        return care(p, CARE_LIGHTS, now, m.sel);
    case ICON_STATUS:
        open_page(p, M_STATUS);
        return false;
    default:
        open_page(p, M_ALBUM);
        return false;
    }
}

bool menu_update(Pet* p, u32 now) {
    refresh_lock();
    bool changed = false;
    if (m.message_timer > 0 && --m.message_timer == 0)
        m.shown_label = -1;

    switch (m.mode) {
    case M_BAR:
        if (repeated(BUTTON_RIGHT))
            m.sel = (m.sel + 1) % ICON_COUNT;
        else if (repeated(BUTTON_LEFT))
            m.sel = (m.sel + ICON_COUNT - 1) % ICON_COUNT;
        else if (pressed(BUTTON_START))
            changed = care(p, CARE_LIGHTS, now, ICON_LIGHTS);
        else if (pressed(BUTTON_A))
            changed = activate(p, now);
        break;
    case M_LIST:
        if (repeated(BUTTON_DOWN)) {
            m.list_sel = (m.list_sel + 1) % list_count();
            m.list_dirty = true;
        } else if (repeated(BUTTON_UP)) {
            m.list_sel = (m.list_sel + list_count() - 1) % list_count();
            m.list_dirty = true;
        } else if (pressed(BUTTON_B)) {
            close_list();
        } else if (pressed(BUTTON_A)) {
            int i = m.list_sel;
            if (m.list == LIST_FEED) {
                close_list();
                changed = care(p, i == 0 ? CARE_MEAL : CARE_TREAT, now, ICON_FEED);
            } else {
                changed = play_game(p, i, now);
            }
        }
        if (m.mode == M_LIST && (int)pet_stage(p, p->last_tick) != m.list_stage)
            m.list_dirty = true; // grew up: more games open
        if (m.mode == M_LIST && m.list_dirty) {
            m.list_dirty = false;
            draw_list(p);
        }
        break;
    case M_STATUS:
    case M_ALBUM:
        if (pressed(BUTTON_B) || pressed(BUTTON_A))
            close_page(p);
        break;
    }
    // The label under the list: the chosen icon's name.
    if (m.mode != M_STATUS && m.mode != M_ALBUM && m.message_timer == 0 && m.shown_label != m.sel) {
        m.shown_label = m.sel;
        text_print_centered(LABEL_ROW, icon_names[m.sel]);
        m.strip_len = text_chars(icon_names[m.sel]);
    }
    return changed;
}

void menu_draw(const Pet* p, u32 now) {
    if (m.mode == M_STATUS) {
        status_draw(p, now);
        return;
    }
    if (m.mode == M_ALBUM)
        return;
    if (m.press_timer > 0)
        m.press_timer--;
    int x = BAR_X0 + m.sel * BAR_PITCH;
    sprite_draw(SPR_CURSOR, 0, x, BAR_Y - 1, 0);
    for (int i = 0; i < ICON_COUNT; i++) {
        bool off = i == ICON_PLAY && !pet_can_play(p, now);
        int y = BAR_Y - (i == m.sel ? 1 : 0) + (i == m.press_icon && m.press_timer > 0 ? 1 : 0);
        sprite_draw(SPR_ICON, (u8)i, BAR_X0 + i * BAR_PITCH, y,
                    off ? SPRITE_PALETTE(PAL_ICONS_OFF) : 0);
    }
    // Behind the label (and a message): a strip as long as its text.
    if (m.strip_len > 0 && (m.mode == M_BAR || m.mode == M_LIST)) {
        int x0 = (TEXT_COLS - m.strip_len) / 2 * 8 - 8, n = m.strip_len + 2;
        for (int i = 0; i < n; i++)
            sprite_draw(SPR_STRIP,
                        (u8)(i == 0       ? 0
                             : i == n - 1 ? 2
                                          : 1),
                        x0 + i * 8, LABEL_ROW * 8, 0);
    }
    if (m.mode == M_LIST) {
        // A box round the list, cut from nine pieces of 16 x 16.
        int n = list_count();
        int wu = m.list == LIST_FEED ? 4 : 8, hu = n == 2 ? 2 : 3;
        int x0 = list_col() * 8 - 12;
        int y0 = (LIST_BOTTOM_ROW - n + 1) * 8 - (hu * 16 - n * 8) / 2;
        art_draw_panel(x0, y0, wu, hu);
    }
}
