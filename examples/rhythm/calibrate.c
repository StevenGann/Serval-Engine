// The calibration: the click module's steady beat (120 BPM), tapped along
// to with any lane button, 16 taps. Each tap is timed exactly as a press in
// a song is (play.c: music_ticks() less PLAY_INPUT_MS, here with no
// calibration) and its error is taken against the nearest click
// (calibration_tap_ms()), in milliseconds; the offset is their median
// (calibration_median_ms()), so a stray tap hardly moves it. Then LEFT and
// RIGHT adjust it, A saves it and B leaves the saved one as it was.
//
// It covers latencies up to +-200 ms (CALIBRATION_MAX_MS): a tap is matched
// to the nearest click, and the clicks are half a second apart (120 BPM), so
// a latency beyond 250 ms would be matched to the click before and measure
// as early.
//
// A, LEFT and RIGHT tap and adjust, so the result settles first: after the
// 16th tap the screen shows it and takes no input until every button has
// been let go for SETTLE_FRAMES, so a player still tapping along can't
// save, cancel or move it unawares. B is not a tap: it leaves, keeping the
// saved offset, at any time while tapping. START stops the tapping early:
// with at least MIN_TAPS taps the offset is their median, with fewer it
// stays the saved one (TOO FEW TAPS).
//
// The offset is how much later than the music the player's presses come:
// a song subtracts it from every press (docs/audio.md#position). The taps
// make no sound, so that only the clicks are heard; the serval lifts a paw
// on each.
//
// The panel's groove is the timing line, -200 ms at its left end, +200 at
// its right: a white diamond at 0, a rose one for each tap of the round, an
// amber one for the offset.

#include "calibrate.h"

#include "art.h"
#include "play.h"
#include "records.h"
#include "screens.h"
#include "songs.h"
#include "sound.h"

#define TAPS 16
#define ADJUST_STEP 4    // ms a repeat of LEFT or RIGHT held; a press is 1
#define MIN_TAPS 4       // START takes the median of this many taps or more
#define SETTLE_FRAMES 30 // with no button down, after the 16th tap, before adjusting

enum { TAPPING, SETTLING, ADJUSTING };

#define LINE_X 120 // the groove's middle, 0 ms; 11 pixels each 20 ms either way
#define TITLE_ROW 0
#define LABEL_ROW 3
#define HINT_ROW 5
#define TAPS_ROW 8
#define LAST_ROW 9
#define OFFSET_ROW 11
#define STATE_ROW 12
#define HELP_ROW 16
#define SERVAL_X 220
#define SERVAL_FOOT 150
#define PAW_FRAMES 8

static const u16 tap_buttons = BUTTON_LEFT | BUTTON_RIGHT | BUTTON_A;

static struct {
    u8 phase, taps;
    s16 errors[TAPS];
    s16 offset;
    u8 paw, side; // frames left of the serval's paw up, and which
    u8 settle;    // SETTLING: frames left with no button down
} cal;

// Signed milliseconds: "+12 MS", "-3 MS", "0 MS".
static const char* ms_text(int ms) {
    return text_format("%s%d MS", ms > 0 ? "+" : "", ms);
}

static int line_x(int ms) {
    return LINE_X + int_clamp(ms, -CALIBRATION_MAX_MS, CALIBRATION_MAX_MS) * 11 / 20;
}

static void show_offset(void) {
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(OFFSET_ROW, text_format("OFFSET %s", ms_text(cal.offset)));
    text_set_style(TEXT_NORMAL);
}

static void show_taps(void) {
    text_print_centered(TAPS_ROW, text_format("TAPS %2u / %u", cal.taps, TAPS));
}

static void show_phase(const char* state) {
    text_print_centered(STATE_ROW, state);
    text_clear_area(0, HINT_ROW, TEXT_COLS, 2);
    text_clear_area(0, HELP_ROW, TEXT_COLS, 4);
    if (cal.phase == TAPPING) {
        text_print_centered(HINT_ROW, "TAP ALONG TO THE CLICKS");
        text_print_centered(HINT_ROW + 1, "WITH A, LEFT OR RIGHT");
        text_print_centered(HELP_ROW + 1, "START: STOP TAPPING");
        text_print_centered(HELP_ROW + 2, "B: BACK");
    } else if (cal.phase == SETTLING) {
        text_print_centered(HELP_ROW + 1, "LET GO OF THE BUTTONS");
    } else {
        text_print_centered(HELP_ROW, "LEFT RIGHT: ADJUST");
        text_print_centered(HELP_ROW + 1, "A: SAVE   B: CANCEL");
        text_print_centered(HELP_ROW + 2, "START: TAP AGAIN");
    }
}

static void start_tapping(void) {
    cal.phase = TAPPING;
    cal.taps = 0;
    show_taps();
    text_print_line(0, LAST_ROW, "");
    show_phase("");
}

void calibrate_show(void) {
    cal.offset = records_get()->calibration_ms;
    cal.paw = 0;
    text_clear();
    art_hide_play();
    art_show_panel();
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(TITLE_ROW, "CALIBRATION");
    text_set_style(TEXT_NORMAL);
    text_print(1, LABEL_ROW, "EARLY");
    text_print(25, LABEL_ROW, "LATE");
    show_offset();
    start_tapping();
    button_repeat_reset();
    music_play(module_calibrate, true);
}

static void tap(void) {
    cal.paw = PAW_FRAMES;
    cal.side ^= 1;
    MusicPosition pos = music_position();
    FIXED press = pos.ticks - ms_to_ticks(PLAY_INPUT_MS, pos.tick_samples);
    s16 error;
    if (!calibration_tap_ms(press, BEAT_TICKS << 8, pos.tick_samples, &error))
        return; // before the first click
    cal.errors[cal.taps++] = error;
    show_taps();
    text_print_centered(LAST_ROW, text_format("LAST %s", ms_text(error)));
    if (cal.taps == TAPS) {
        cal.offset = (s16)int_clamp(calibration_median_ms(cal.errors, TAPS), -CALIBRATION_MAX_MS,
                                    CALIBRATION_MAX_MS);
        cal.phase = SETTLING;
        cal.settle = SETTLE_FRAMES;
        sound_play(SND_SELECT);
        show_offset();
        show_phase("MEASURED");
    }
}

static void draw(void) {
    sprite_draw(SPR_MARKER, 0, LINE_X, PROGRESS_Y, SPRITE_ABOVE_FOREGROUND);
    sprite_draw(SPR_MARKER, 0, line_x(cal.offset), PROGRESS_Y,
                SPRITE_ABOVE_FOREGROUND | SPRITE_PALETTE(PAL_LANE + 1));
    for (int i = 0; i < cal.taps; i++)
        sprite_draw(SPR_MARKER, 0, line_x(cal.errors[i]), PROGRESS_Y,
                    SPRITE_ABOVE_FOREGROUND | SPRITE_PALETTE(PAL_LANE));
    if (cal.paw) {
        cal.paw--;
        sprite_draw(SPR_SERVAL, DANCE_PAW, SERVAL_X, SERVAL_FOOT, cal.side ? SPRITE_FLIP_H : 0);
    } else {
        serval_dance(-1, SERVAL_X, SERVAL_FOOT);
    }
}

CalibrateResult calibrate_frame(bool input) {
    CalibrateResult result = CALIBRATE_RUNNING;
    if (input && cal.phase == TAPPING) {
        if (button_pressed(BUTTON_B)) {
            sound_play(SND_SELECT);
            result = CALIBRATE_CANCELLED;
        } else if (button_pressed(BUTTON_START)) {
            cal.phase = ADJUSTING;
            sound_play(SND_MOVE);
            if (cal.taps >= MIN_TAPS) {
                cal.offset = (s16)int_clamp(calibration_median_ms(cal.errors, cal.taps),
                                            -CALIBRATION_MAX_MS, CALIBRATION_MAX_MS);
                show_offset();
                show_phase("MEASURED");
            } else {
                show_phase("");
                text_set_style(TEXT_WARNING);
                text_print_centered(STATE_ROW, "TOO FEW TAPS");
                text_set_style(TEXT_NORMAL);
            }
        } else if (button_pressed(tap_buttons)) {
            tap();
        }
    } else if (cal.phase == SETTLING) {
        // Every button let go, then SETTLE_FRAMES more: any button down
        // starts the wait again.
        if (button_down(BUTTON_ANY))
            cal.settle = SETTLE_FRAMES;
        else if (--cal.settle == 0) {
            cal.phase = ADJUSTING;
            button_repeat_reset();
            show_phase("MEASURED");
        }
    } else if (input) {
        int step = 0;
        if (button_repeat(BUTTON_LEFT))
            step = button_pressed(BUTTON_LEFT) ? -1 : -ADJUST_STEP;
        else if (button_repeat(BUTTON_RIGHT))
            step = button_pressed(BUTTON_RIGHT) ? 1 : ADJUST_STEP;
        if (step) {
            int v = int_clamp(cal.offset + step, -CALIBRATION_MAX_MS, CALIBRATION_MAX_MS);
            if (v != cal.offset) {
                cal.offset = (s16)v;
                sound_play(SND_MOVE);
                show_offset();
                show_phase("");
            }
        }
        if (button_pressed(BUTTON_A)) {
            records_save_calibration(cal.offset);
            sound_play(SND_SELECT);
            result = CALIBRATE_SAVED;
        } else if (button_pressed(BUTTON_B)) {
            sound_play(SND_SELECT);
            result = CALIBRATE_CANCELLED;
        } else if (button_pressed(BUTTON_START)) {
            sound_play(SND_MOVE);
            start_tapping();
        }
    }
    draw();
    return result;
}
