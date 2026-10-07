// The VM's engine calls that only the GBA build has (src/core/vm_internal.h):
// sound, music, text (strings and numbers), buttons and brightness. The web
// build compiles it too.

#include "../core/vm_internal.h"
#include "serval/audio.h"
#include "serval/core.h"
#include "serval/screen.h"
#include "serval/text.h"

// text_print_number: the value in decimal at (col, row); with a width of 1 or
// more, right-aligned in that many columns with blank cells in front (so a
// number that got shorter leaves nothing behind), or in full if it is wider.
// Positions are worked out in 64 bits: col and width are any cells.
static void print_number(s32 col, s32 row, s32 value, s32 width) {
    const char* digits = text_format("%d", value);
    s32 length = 0;
    while (digits[length])
        length++;
    if (width <= length) {
        text_print(col, row, digits);
        return;
    }
    int64_t start = (int64_t)col + (width - length); // where the digits go
    // The blank cells, clipped to the screen (text_clear_area clips too, but
    // takes ints).
    int64_t lo = col < 0 ? 0 : col, hi = start < TEXT_COLS ? start : TEXT_COLS;
    if (lo < hi)
        text_clear_area((int)lo, row, (int)(hi - lo), 1);
    if (start < TEXT_COLS && start > -(int64_t)length) // some digit is on the screen
        text_print((int)start, row, digits);
}

s32 serval_vm_platform_call(u32 fn, const s32* args, const void* ptr) {
    switch (fn) {
    case VM_SYS_PSG_PLAY:
        // An ID no u16 holds becomes 0xFFFF, which no sound table reaches
        // (psg_play warns), rather than another sound's ID once truncated.
        psg_play(args[0] >= 0 && args[0] <= 0xFFFF ? (u16)args[0] : 0xFFFF);
        return 0;
    case VM_SYS_PSG_MUSIC_PLAY:
        psg_music_play(ptr);
        return 0;
    case VM_SYS_PSG_MUSIC_STOP:
        psg_music_stop();
        return 0;
    case VM_SYS_PSG_MUSIC_PAUSE:
        psg_music_pause();
        return 0;
    case VM_SYS_PSG_MUSIC_RESUME:
        psg_music_resume();
        return 0;
    case VM_SYS_TEXT_PRINT:
        text_print(args[0], args[1], ptr);
        return 0;
    case VM_SYS_TEXT_PRINT_NUMBER:
        print_number(args[0], args[1], args[2], args[3]);
        return 0;
    case VM_SYS_BUTTON_DOWN:
        return button_down((u16)args[0]);
    case VM_SYS_BUTTON_PRESSED:
        return button_pressed((u16)args[0]);
    case VM_SYS_SCREEN_SET_BRIGHTNESS:
        screen_set_brightness(args[0]);
        return 0;
    default:
        return 0;
    }
}
