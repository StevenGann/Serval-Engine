// The VM's engine calls that only the GBA build has (src/core/vm_internal.h):
// sound, music, text, buttons and brightness. The web build compiles it too.

#include "../core/vm_internal.h"
#include "serval/audio.h"
#include "serval/core.h"
#include "serval/screen.h"
#include "serval/text.h"

s32 serval_vm_platform_call(u32 fn, const s32* args, const void* ptr) {
    switch (fn) {
    case VM_SYS_PSG_PLAY:
        // An ID no u16 holds becomes 0xFFFF, which no sound table reaches
        // (psg_play warns), rather than another sound's ID once truncated.
        psg_play(args[0] >= 0 && args[0] <= 0xFFFF ? (u16)args[0] : 0xFFFF);
        return 0;
    case VM_SYS_MUSIC_PLAY:
        psg_music_play(ptr);
        return 0;
    case VM_SYS_MUSIC_STOP:
        psg_music_stop();
        return 0;
    case VM_SYS_MUSIC_PAUSE:
        psg_music_pause();
        return 0;
    case VM_SYS_MUSIC_RESUME:
        psg_music_resume();
        return 0;
    case VM_SYS_TEXT_PRINT:
        text_print(args[0], args[1], ptr);
        return 0;
    case VM_SYS_BUTTON_DOWN:
        return button_down((u16)args[0]);
    case VM_SYS_BUTTON_PRESSED:
        return button_pressed((u16)args[0]);
    case VM_SYS_BRIGHTNESS:
        screen_set_brightness(args[0]);
        return 0;
    default:
        return 0;
    }
}
