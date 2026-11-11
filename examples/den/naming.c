// The naming page: see naming.h.

#include "naming.h"

#include "litter.h"
#include "sound.h"

#include <serval/serval.h>

#define COLS 6
#define CELLS 28 // A-Z, DEL, END
#define CELL_DEL 26
#define CELL_END 27
#define NAME_MAX 8

static u16 lock;

static bool pressed(u16 b) {
    return button_pressed(b) && !(lock & b);
}

static void refresh_lock(void) {
    for (u16 bit = 1; bit <= BUTTON_L; bit <<= 1)
        if (!button_down(bit))
            lock &= (u16)~bit;
}

static void draw_name(const char* name, int len) {
    char buf[NAME_MAX + 1];
    int i = 0;
    for (; i < len; i++)
        buf[i] = name[i];
    for (; i < NAME_MAX; i++)
        buf[i] = '_';
    buf[NAME_MAX] = 0;
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(4, buf);
    text_set_style(TEXT_NORMAL);
}

static void draw_grid(int sel) {
    for (int c = 0; c < CELLS; c++) {
        int col = c % COLS * 5, row = 7 + c / COLS * 2;
        bool on = c == sel;
        text_set_style(on ? TEXT_HIGHLIGHT : TEXT_NORMAL);
        if (c < 26)
            text_print(col + 1, row, text_format(on ? "[%c]" : " %c ", 'A' + c));
        else
            text_print(col, row,
                       c == CELL_DEL ? (on ? "[DEL]" : " DEL ") : (on ? "[END]" : " END "));
    }
    text_set_style(TEXT_NORMAL);
}

void naming_run(char out[9], const char* title, const char* suggestion) {
    char name[NAME_MAX + 1] = {0};
    if (!suggestion)
        suggestion = litter_names[random_range(0, LITTER_NAME_COUNT - 1)];
    int len = 0;
    while (suggestion[len] && len < NAME_MAX) {
        name[len] = suggestion[len];
        len++;
    }
    int sel = CELL_END;
    lock = BUTTON_ANY;
    button_repeat_reset();

    text_clear();
    text_set_style(TEXT_NORMAL);
    text_print_centered(2, title);
    text_print_centered(18, "A PICK  B DELETE  START DONE");
    draw_name(name, len);
    draw_grid(sel);

    for (;;) {
        frame_begin();
        refresh_lock();
        bool changed = false, done = false;
        if (!(lock & BUTTON_RIGHT) && button_repeat(BUTTON_RIGHT)) {
            sel = (sel + 1) % CELLS;
            changed = true;
        } else if (!(lock & BUTTON_LEFT) && button_repeat(BUTTON_LEFT)) {
            sel = (sel + CELLS - 1) % CELLS;
            changed = true;
        } else if (!(lock & BUTTON_DOWN) && button_repeat(BUTTON_DOWN)) {
            sel = sel + COLS < CELLS ? sel + COLS : sel % COLS;
            changed = true;
        } else if (!(lock & BUTTON_UP) && button_repeat(BUTTON_UP)) {
            sel = sel >= COLS ? sel - COLS
                              : (sel + 4 * COLS < CELLS ? sel + 4 * COLS : sel + 3 * COLS);
            changed = true;
        }
        if (pressed(BUTTON_B)) {
            if (len > 0)
                name[--len] = 0;
            else
                sound_no();
            changed = true;
        } else if (pressed(BUTTON_START)) {
            done = true;
        } else if (pressed(BUTTON_A)) {
            if (sel < 26) {
                if (len < NAME_MAX) {
                    name[len++] = (char)('A' + sel);
                    name[len] = 0;
                } else
                    sound_no();
            } else if (sel == CELL_DEL) {
                if (len > 0)
                    name[--len] = 0;
                else
                    sound_no();
            } else {
                done = true;
            }
            changed = true;
        }
        if (done && len == 0) {
            sound_no();
            done = false;
        }
        if (changed) {
            draw_name(name, len);
            draw_grid(sel);
        }
        if (done) {
            sound_mew();
            text_clear(); // inside the frame
            frame_end();
            break;
        }
        frame_end();
    }
    for (int i = 0; i < 9; i++) // all of it: nothing of an earlier name stays after the NUL
        out[i] = i < len ? name[i] : 0;
}
