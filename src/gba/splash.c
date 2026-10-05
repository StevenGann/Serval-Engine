#include "serval/audio.h"
#include "serval/core.h"
#include "serval/text.h"

#include <tonc.h>

#include "internal.h"
#include "screen_internal.h"

// The "Made with Serval Engine" splash. Timings in frames at ~59.73 fps.
#define FADE_FRAMES 30        // 500 ms in, and again out
#define HOLD_BEFORE_JINGLE 30 // 500 ms
#define HOLD_AFTER_JINGLE 90  // 1500 ms
#define JINGLE_FIRST_NOTE 4   // frames of the short first note

// Text on background 0: "made with" through palette bank 14 (grey), "Serval
// Engine" through the text layer's own bank 15 (white). (A logo comes later.)
#define GREY_BANK 14
#define WHITE_BANK 15

// A coin-pickup style jingle: a short B5, then a long E6 that fades away.
static const PsgSound jingle_first = {
    .channel = PSG_SQUARE1, .frequency = 988, .frames = JINGLE_FIRST_NOTE, .volume = 12};
static const PsgSound jingle_second = {
    .channel = PSG_SQUARE1, .frequency = 1319, .fade = -3, .volume = 12};

// Fades background 0 (the text) toward black: 0 = fully visible, 16 = black.
static void set_darkness(u32 level) {
    REG_BLDY = (u16)level;
}

// Runs one frame of the splash; returns true if a button was pressed.
static bool splash_frame(void) {
    frame_begin();
    bool pressed = button_pressed(BUTTON_ANY);
    frame_end();
    return pressed;
}

void serval_splash(void) {
    // Borrow the backdrop, two palette entries, the blend registers (the
    // game's brightness), the text shadow and background 0 (control register
    // and display bit); put them back at the end.
    bool text_was_active = serval_text_active();
    u16 old_bg0cnt = REG_BG0CNT;
    u16 old_bg0_shown = REG_DISPCNT & DCNT_BG0;
    u16 old_backdrop = pal_bg_mem[0];
    u16 old_grey = pal_bg_bank[GREY_BANK][1];
    u16 old_white = pal_bg_bank[WHITE_BANK][1];
    u16 old_bldcnt = REG_BLDCNT; // (BLDY is write-only: screen.c keeps the game's level)
    bool old_shadow = serval_text_shadow();

    pal_bg_mem[0] = RGB15(0, 0, 0);
    text_set_shadow(false);
    text_clear(); // also sets the text layer up if the game hadn't
    pal_bg_bank[GREY_BANK][1] = RGB15(16, 16, 16);
    pal_bg_bank[WHITE_BANK][1] = RGB15(31, 31, 31);
    REG_BLDCNT = BLD_BG0 | BLD_BLACK;
    set_darkness(16);
    serval_text_print_bank((TEXT_COLS - 9) / 2, 8, "made with", GREY_BANK);
    serval_text_print_bank((TEXT_COLS - 13) / 2, 10, "Serval Engine", WHITE_BANK);

    // Fade in (no skipping yet).
    for (u32 f = 1; f <= FADE_FRAMES; f++) {
        set_darkness(16 - f * 16 / FADE_FRAMES);
        splash_frame();
    }

    // Hold, jingle, hold, fade out; any button skips the rest.
    bool skipped = false;
    for (u32 f = 0; f < HOLD_BEFORE_JINGLE && !skipped; f++)
        skipped = splash_frame();
    if (!skipped) {
        serval_psg_play_sound(&jingle_first);
        for (u32 f = 0; f < HOLD_AFTER_JINGLE && !skipped; f++) {
            if (f == JINGLE_FIRST_NOTE)
                serval_psg_play_sound(&jingle_second);
            skipped = splash_frame();
        }
    }
    for (u32 f = 1; f <= FADE_FRAMES && !skipped; f++) {
        set_darkness(f * 16 / FADE_FRAMES);
        skipped = splash_frame();
    }

    // Clear the text and put everything back.
    serval_psg_silence(PSG_SQUARE1);
    text_clear();
    if (!text_was_active)
        serval_text_deactivate();
    REG_BG0CNT = old_bg0cnt;
    REG_DISPCNT = (u16)((REG_DISPCNT & ~DCNT_BG0) | old_bg0_shown);
    pal_bg_bank[GREY_BANK][1] = old_grey;
    pal_bg_bank[WHITE_BANK][1] = old_white;
    text_set_shadow(old_shadow);
    REG_BLDCNT = old_bldcnt;
    int level = serval_screen_brightness();
    REG_BLDY = (u16)(level < 0 ? -level : level);
    pal_bg_mem[0] = old_backdrop;
}
