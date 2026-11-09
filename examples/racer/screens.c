// The title and the results: text on the engine's 8x8 font with a shadow,
// labels in STYLE_LABEL, over the road scene (game.c keeps it moving on the
// title, and holds the last frame of the race behind the results, dimmed),
// and the words bigger than the font allows: SAVANNA RUN in sprites (art.c
// blows up a 5x7 font), five 64x32 pieces side by side in two rows; FINISH
// or TIME UP over the results, three pieces (hud.c).

#include "screens.h"

#include "art.h"
#include "hud.h"
#include "records.h"

#define LOGO_Y 0       // SAVANNA's pieces' top; RUN's are LOGO_Y + 32
#define START_ROW 10   // PRESS START
#define RECORDS_ROW 13 // BEST RACE, and BEST LAP two rows under it
#define ERASED_ROW 17  // the erase's confirmation
#define ERASED_FRAMES 150
#define BLINK 64 // PRESS START: on for 40 frames of 64
#define BLINK_ON 40

static int erased_frames;
static bool shown_blink;

// A record, or dashes for none.
static const char* record(u32 frames) {
    return frames ? hud_time(frames) : "--'--\"--";
}

// A label in `style` at (col, row), and a value after it.
static void labelled_in(int style, int col, int row, const char* label, int value_col,
                        const char* value) {
    text_set_style(style);
    text_print(col, row, label);
    text_set_style(TEXT_NORMAL);
    text_print(value_col, row, value);
}

static void labelled(int col, int row, const char* label, int value_col, const char* value) {
    labelled_in(STYLE_LABEL, col, row, label, value_col, value);
}

// The records as they stand, from `row` (BEST LAP two rows under BEST RACE),
// their labels in `label_style`.
static void records_rows(int label_style, int col, int row) {
    const Records* rec = records_get();
    labelled_in(label_style, col, row, "BEST RACE", col + 10, record(rec->best_race));
    labelled_in(label_style, col, row + 2, "BEST LAP", col + 10, record(rec->best_lap));
}

// On the title, the labels in white: orange would be lost on the sunlit
// grass.
static void title_records(void) {
    text_clear_area(0, RECORDS_ROW, TEXT_COLS, 3);
    records_rows(TEXT_NORMAL, 6, RECORDS_ROW);
}

void title_show(void) {
    text_clear();
    title_records();
    erased_frames = 0;
    shown_blink = false;
}

void title_erased(void) {
    title_records();
    text_set_style(records_save_failed() ? STYLE_WARNING : TEXT_HIGHLIGHT);
    text_print_line(0, ERASED_ROW, "");
    text_print_centered(ERASED_ROW, records_save_failed() ? "SAVE FAILED" : "RECORDS ERASED");
    text_set_style(TEXT_NORMAL);
    erased_frames = ERASED_FRAMES;
}

void title_draw(u32 frames) {
    for (int f = LOGO_SA; f <= LOGO_NA; f++)
        sprite_draw(SPR_LOGO, (u8)f, SCREEN_W / 2 - 96 + (f - LOGO_SA) * 64, LOGO_Y, 0);
    for (int f = LOGO_RU; f <= LOGO_UN; f++)
        sprite_draw(SPR_LOGO, (u8)f, SCREEN_W / 2 - 64 + (f - LOGO_RU) * 64, LOGO_Y + 32, 0);
    bool on = frames % BLINK < BLINK_ON;
    if (on != shown_blink) {
        shown_blink = on;
        if (on) {
            text_set_style(TEXT_HIGHLIGHT);
            text_print_centered(START_ROW, "PRESS START");
            text_set_style(TEXT_NORMAL);
        } else
            text_clear_area(0, START_ROW, TEXT_COLS, 1);
    }
    if (erased_frames > 0 && --erased_frames == 0)
        text_clear_area(0, ERASED_ROW, TEXT_COLS, 1);
}

// --- Results -------------------------------------------------------------------------

// FINISH or TIME UP takes the top four rows (hud_word_draw(), drawn by
// game.c). A blank row between any two lines: each line's shadow falls on
// the row under it, onto the capitals of a line there.
#define TOTAL_ROW 5   // the race time, or the distance reached
#define LAPS_ROW 7    // the three laps, every other row
#define FAILED_ROW 13 // SAVE FAILED
#define BESTS_ROW 15  // the records as they stand now, BEST LAP two rows under
#define BACK_ROW 19   // PRESS A
#define LABEL_COL 1   // RACE, LAP n, BEST ...
#define VALUE_COL 11  // the times (as records_rows() puts them)
#define MARK_COL 19   // BEST, NEW RECORD (ten letters, to the last column)

static void mark(int row, const char* s) {
    text_set_style(TEXT_HIGHLIGHT);
    text_print(MARK_COL, row, s);
    text_set_style(TEXT_NORMAL);
}

void results_show(const Race* r, u32 fell) {
    text_clear();
    if (r->finished) {
        labelled(LABEL_COL, TOTAL_ROW, "RACE", VALUE_COL, hud_time(r->race_time));
        if (fell & RECORD_RACE)
            mark(TOTAL_ROW, "NEW RECORD");
    } else {
        u32 km = race_hundredths_km(r->travelled);
        labelled(LABEL_COL, TOTAL_ROW, "DISTANCE", VALUE_COL,
                 text_format("%u.%02u KM", km / 100, km % 100));
    }
    // The laps; the race's fastest completed one marked.
    int best = -1;
    for (int i = 0; i < r->lap && i < RACE_LAPS; i++)
        if (best < 0 || r->lap_times[i] < r->lap_times[best])
            best = i;
    for (int i = 0; i < RACE_LAPS; i++) {
        u32 t = i < r->lap ? r->lap_times[i] : 0;
        labelled(LABEL_COL, LAPS_ROW + 2 * i, text_format("LAP %d", i + 1), VALUE_COL, record(t));
        if (i == best)
            mark(LAPS_ROW + 2 * i, fell & RECORD_LAP ? "NEW RECORD" : "BEST");
    }
    if (fell && records_save_failed()) {
        text_set_style(STYLE_WARNING);
        text_print_centered(FAILED_ROW, "SAVE FAILED");
        text_set_style(TEXT_NORMAL);
    }
    records_rows(STYLE_LABEL, LABEL_COL, BESTS_ROW);
    shown_blink = false;
}

void results_draw(u32 frames) {
    bool on = frames % BLINK < BLINK_ON;
    if (on != shown_blink) {
        shown_blink = on;
        if (on)
            text_print_centered(BACK_ROW, "PRESS A");
        else
            text_clear_area(0, BACK_ROW, TEXT_COLS, 1);
    }
}
