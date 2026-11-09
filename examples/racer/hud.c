// The race's HUD: text on background 0 (the engine's text layer, above the
// road and the sprites, never bent by the road's raster effect), with a
// shadow so it reads over the bright sky, its labels in STYLE_LABEL and the
// seconds left in red for the last ten; and the countdown's numbers, GO,
// FINISH and TIME UP as big sprites. The numbers pop in from bigger than
// they are, and GO flies at the player as it goes: sprite_draw_ex() in a
// few steps of scale, so they share a matrix. Values are printed only when
// they change: printing writes VRAM straight away.

#include "hud.h"

#include "art.h"
#include "screens.h"

#define MESSAGE_ROW 5     // messages, over the sky
#define PAUSED_ROW 9      // PAUSED, over the far road
#define READOUT_ROW 18    // the SELECT readout: the bottom two rows
#define MESSAGE_FRAMES 96 // a message stays up this long
#define GO_FRAMES 60      // GO stays up this long after the countdown
#define COUNT_Y 20        // the countdown's numbers: their top, over the sky
#define WORD_Y 20         // FINISH or TIME UP, where it lands
#define LOW_TIME 10       // the seconds left turn red from here
#define POP_FRAMES 8      // a number or GO shrinks to its size in this many frames
#define GO_LEAVE 8        // ... and GO grows away (to twice its size) in its last 8

static int shown_lap, shown_time, shown_speed;
static u32 shown_race_time;
static int message_frames; // frames left of the message; 0 none
static bool shown_paused;
static bool readout;
static u32 cpu_peak, line_peak, cut, lost; // the readout's, since it came on

// The readout prints every READOUT_FRAMES frames the highest numbers of the
// frames since it last printed.
#define READOUT_FRAMES 16
typedef struct {
    u32 cpu, line, drawn, matrices, frames;
    bool printed; // it printed in the last frame: leave that frame out
} Window;
static Window window;

// A big number's or GO's scale `shown` frames after it came up: twice its
// size, shrinking to its size in POP_FRAMES frames, in eighths.
static FIXED pop_scale(int shown) {
    return shown >= POP_FRAMES ? FX_ONE : FX_ONE + (POP_FRAMES - shown) * FX_ONE / 8;
}

const char* hud_time(u32 t) {
    return text_format("%u'%02u\"%02u", t / 3600, t / 60 % 60, t % 60 * 100 / 60);
}

void hud_reset(void) {
    text_clear();
    text_set_style(STYLE_LABEL);
    text_print(1, 0, "LAP");
    text_print_centered_in(11, 8, 0, "TIME");
    text_print(25, 0, "KM/H");
    text_set_style(TEXT_NORMAL);
    shown_lap = shown_time = shown_speed = -1;
    shown_race_time = (u32)-1;
    message_frames = 0;
    shown_paused = false;
}

static void message(const char* first, const char* second) {
    text_clear_area(0, MESSAGE_ROW, TEXT_COLS, 2);
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(MESSAGE_ROW, first);
    if (second)
        text_print_centered(MESSAGE_ROW + 1, second);
    text_set_style(TEXT_NORMAL);
}

void hud_message_clear(void) {
    text_clear_area(0, MESSAGE_ROW, TEXT_COLS, 2);
    message_frames = 0;
}

void hud_message(const char* first, const char* second) {
    message(first, second);
    message_frames = MESSAGE_FRAMES;
}

void hud_step(void) {
    if (message_frames > 0 && --message_frames == 0)
        text_clear_area(0, MESSAGE_ROW, TEXT_COLS, 2);
}

void hud_draw(const Race* r, u32 since_go, bool paused) {
    // The top two rows: LAP and the race's time on the left, the time left
    // in the middle, the speed on the right.
    int lap = int_min(r->lap + 1, RACE_LAPS);
    if (lap != shown_lap)
        text_print(5, 0, text_format("%d/%d", shown_lap = lap, RACE_LAPS));
    if (r->race_time != shown_race_time)
        text_print(1, 1, hud_time(shown_race_time = r->race_time));
    u32 left = r->countdown ? r->track->start_time : r->time_left;
    int seconds = (int)((left + 59) / 60);
    if (seconds != shown_time) {
        text_set_style(seconds <= LOW_TIME && !r->countdown ? STYLE_WARNING : TEXT_NORMAL);
        text_print_centered_in(11, 8, 1, text_format("%d", shown_time = seconds));
        text_set_style(TEXT_NORMAL);
    }
    int kmh = race_speed_kmh(r);
    if (kmh != shown_speed)
        text_print(21, 0, text_format("%3d", shown_speed = kmh));
    if (paused != shown_paused) {
        shown_paused = paused;
        if (paused)
            text_print_centered(PAUSED_ROW, "PAUSED");
        else
            text_clear_area(0, PAUSED_ROW, TEXT_COLS, 1);
    }
    // The countdown, big: 3, 2, 1, then GO. Each pops in at twice its size
    // and shrinks to it; GO, as it goes, grows away.
    if (r->countdown) {
        int number = (int)(r->countdown + 59) / 60;
        int shown = number * 60 - (int)r->countdown; // frames it has been up
        FIXED scale = pop_scale(shown);
        sprite_draw_ex(SPR_DIGIT, (u8)(number - 1), SCREEN_W / 2 - 16, COUNT_Y, 0, scale, scale, 0);
    } else if (since_go < GO_FRAMES && !r->over) {
        int going = GO_FRAMES - (int)since_go; // frames left of GO
        FIXED scale = going <= GO_LEAVE ? FX_ONE + (GO_LEAVE + 1 - going) * FX_ONE / 8
                                        : pop_scale((int)since_go);
        sprite_draw_ex(SPR_GO, 0, SCREEN_W / 2 - 32, COUNT_Y, 0, scale, scale, 0);
    }
}

void hud_word_draw(bool finished, int y) {
    int first = (finished ? WORD_FINISH : WORD_TIME_UP) * WORD_PIECES;
    for (int p = 0; p < WORD_PIECES; p++)
        sprite_draw(SPR_WORDS, (u8)(first + p), SCREEN_W / 2 - WORD_PIECES * 32 + p * 64, y, 0);
}

int hud_word_drop(u32 frames) {
    // In sixteenths of a pixel: falling from above the screen, bouncing a
    // little lower each time it lands, at rest within a second.
    int y = -32 * 16, vy = 0;
    for (u32 i = 0; i < frames && i < 60; i++) {
        vy += 10;
        y += vy;
        if (y >= WORD_Y * 16) {
            y = WORD_Y * 16;
            vy = -vy * 2 / 5;
        }
    }
    return y / 16;
}

void hud_readout_toggle(void) {
    readout = !readout;
    sprite_stats_scanlines(readout); // its cost shows in the CPU load
    cpu_peak = line_peak = cut = lost = 0;
    window = (Window){0, 0, 0, 0, 0, true};
    text_clear_area(0, READOUT_ROW, TEXT_COLS, 2);
}

void hud_readout_off(void) {
    if (readout)
        hud_readout_toggle();
}

void hud_readout_draw(void) {
    if (!readout)
        return;
    // The last frame's numbers, into the window: unless the readout printed
    // in that frame (printing is slow, and would measure itself).
    u32 p = frame_cpu_permille();
    SpriteStats st = sprite_stats();
    cut += st.cut_short;
    lost += st.dropped + st.untransformed;
    if (!window.printed) {
        window.cpu = int_max((int)window.cpu, (int)p);
        window.line = int_max((int)window.line, st.busiest_line);
        window.drawn = int_max((int)window.drawn, st.drawn);
        window.matrices = int_max((int)window.matrices, st.matrices);
    }
    cpu_peak = int_max((int)cpu_peak, (int)window.cpu);
    line_peak = int_max((int)line_peak, (int)window.line);
    window.printed = false;
    if (++window.frames < READOUT_FRAMES)
        return;
    // Every READOUT_FRAMES frames, the highest of them: the CPU load
    // (percent of a frame, with the per-line count's own cost) and the
    // highest since the readout came on; hardware sprites and matrices
    // used; the busiest line's sprite time (cycles of 1,210), and its
    // highest; then, since it came on, sprites that lost a line and draws
    // lost to the limits (OAM full, matrices out): both should stay 0.
    text_print_line(0, READOUT_ROW,
                    text_format("CPU %2u.%u PEAK %2u.%u SPR %3u M%2u", window.cpu / 10,
                                window.cpu % 10, cpu_peak / 10, cpu_peak % 10, window.drawn,
                                window.matrices));
    text_print_line(0, READOUT_ROW + 1,
                    text_format("LINE %4u/%4u CUT %u LOST %u", window.line, line_peak, cut, lost));
    window = (Window){0, 0, 0, 0, 0, true};
}
