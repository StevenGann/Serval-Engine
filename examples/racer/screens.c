// The title and the results: text on the engine's 8x8 font with a shadow,
// over the road scene (game.c keeps it moving on the title and holds the
// last frame of the race behind the results), and the logo, which is bigger
// than the font allows: SAVANNA RUN in sprites (art.c blows up a 5x7
// font), five 64x32 pieces side by side in two rows.

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

static void title_records(void) {
    const Records* rec = records_get();
    text_print_line(6, RECORDS_ROW, text_format("BEST RACE %s", record(rec->best_race)));
    text_print_line(6, RECORDS_ROW + 2, text_format("BEST LAP  %s", record(rec->best_lap)));
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
        if (on)
            text_print_centered(START_ROW, "PRESS START");
        else
            text_clear_area(0, START_ROW, TEXT_COLS, 1);
    }
    if (erased_frames > 0 && --erased_frames == 0)
        text_clear_area(0, ERASED_ROW, TEXT_COLS, 1);
}

// --- Results -------------------------------------------------------------------------

// A blank row between any two lines: each line's shadow falls on the row
// under it, onto the capitals of a line there.
#define RESULT_ROW 1  // FINISH or TIME UP
#define TOTAL_ROW 3   // the race time, or the distance reached
#define LAPS_ROW 5    // the three laps, every other row
#define FAILED_ROW 11 // SAVE FAILED
#define BESTS_ROW 13  // the records as they stand now, BEST LAP two rows under
#define BACK_ROW 17   // PRESS A
#define LABEL_COL 2   // RACE, LAP n, BEST ...
#define MARK_COL 19   // BEST, NEW RECORD (ten letters, to the last column)

static void mark(int row, const char* s) {
    text_set_style(TEXT_HIGHLIGHT);
    text_print(MARK_COL, row, s);
    text_set_style(TEXT_NORMAL);
}

void results_show(const Race* r, u32 fell) {
    text_clear();
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(RESULT_ROW, r->finished ? "FINISH" : "TIME UP");
    text_set_style(TEXT_NORMAL);
    if (r->finished) {
        text_print(LABEL_COL, TOTAL_ROW, text_format("RACE   %s", hud_time(r->race_time)));
        if (fell & RECORD_RACE)
            mark(TOTAL_ROW, "NEW RECORD");
    } else {
        u32 km = race_hundredths_km(r->travelled);
        text_print(LABEL_COL, TOTAL_ROW, text_format("DISTANCE %u.%02u KM", km / 100, km % 100));
    }
    // The laps; the race's fastest completed one marked.
    int best = -1;
    for (int i = 0; i < r->lap && i < RACE_LAPS; i++)
        if (best < 0 || r->lap_times[i] < r->lap_times[best])
            best = i;
    for (int i = 0; i < RACE_LAPS; i++) {
        u32 t = i < r->lap ? r->lap_times[i] : 0;
        text_print(LABEL_COL, LAPS_ROW + 2 * i, text_format("LAP %d  %s", i + 1, record(t)));
        if (i == best)
            mark(LAPS_ROW + 2 * i, fell & RECORD_LAP ? "NEW RECORD" : "BEST");
    }
    if (fell && records_save_failed()) {
        text_set_style(STYLE_WARNING);
        text_print_centered(FAILED_ROW, "SAVE FAILED");
        text_set_style(TEXT_NORMAL);
    }
    const Records* rec = records_get();
    text_print(LABEL_COL, BESTS_ROW, text_format("BEST RACE %s", record(rec->best_race)));
    text_print(LABEL_COL, BESTS_ROW + 2, text_format("BEST LAP  %s", record(rec->best_lap)));
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
