// button_repeat(): held-button repeat for menus. Portable, so the timing is
// unit tested natively; the frame loop feeds it the polled buttons.

#include "input_internal.h"
#include "serval/core.h"
#include "warn.h"

#define BUTTON_COUNT 10 // BUTTON_A to BUTTON_L: bits 0-9
#define DEFAULT_DELAY 20
#define DEFAULT_INTERVAL 4

static u16 delay = DEFAULT_DELAY, interval = DEFAULT_INTERVAL;
static u32 held;               // buttons held last frame
static u32 due;                // buttons whose repeat fires this frame
static u32 muted;              // held at button_repeat_reset(): silent until released
static u16 wait[BUTTON_COUNT]; // frames until a held button's next repeat

// Engine-internal, for serval_init(): forgets everything, settings included.
// Not button_repeat_reset(), the public call below, which only silences the
// buttons held now and keeps the settings.
void serval_repeat_reset(void) {
    delay = DEFAULT_DELAY;
    interval = DEFAULT_INTERVAL;
    held = 0;
    due = 0;
    muted = 0;
}

void serval_repeat_frame(u32 buttons) {
    buttons &= BUTTON_ANY;
    if ((buttons | held) == 0) { // most frames: nothing held, nothing to count
        due = 0;
        return;
    }
    muted &= buttons;           // a release ends a button's silence
    u32 fire = buttons & ~held; // presses fire at once
    for (u32 still = buttons & held, i = 0; still; still >>= 1, i++) {
        if (!(still & 1))
            continue;
        if (--wait[i] == 0) {
            fire |= 1u << i;
            wait[i] = interval;
        }
    }
    for (u32 pressed = buttons & ~held, i = 0; pressed; pressed >>= 1, i++) {
        if (pressed & 1)
            wait[i] = delay;
    }
    held = buttons;
    due = fire & ~muted;
}

bool button_repeat(u16 buttons) {
    return (due & buttons) != 0;
}

// The public call: the buttons held now (polled by this frame's
// frame_begin()) stay silent, from this frame on, until released. Their
// repeat waits count on, but fire nothing; a new press restarts the wait, as
// for any press.
void button_repeat_reset(void) {
    muted = held;
    due &= ~muted;
}

void button_repeat_set(int new_delay, int new_interval) {
    if (new_delay < 1 || new_delay > 0xFFFF || new_interval < 1 || new_interval > 0xFFFF) {
#ifdef SERVAL_DEBUG
        static bool warned;
        if (!warned) {
            warned = true;
            SERVAL_WARN("button_repeat_set: delay %d and interval %d must be 1 to 65535 frames; "
                        "ignored",
                        new_delay, new_interval);
        }
#endif
        return;
    }
    delay = (u16)new_delay;
    interval = (u16)new_interval;
}
