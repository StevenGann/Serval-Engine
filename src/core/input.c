// button_repeat(): held-button repeat for menus, and the secret sequence
// (button_secret_set). Portable, so both are unit tested natively; the frame
// loop feeds them the polled buttons.

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

// The secret sequence: the classic cheat code. In ROM.
static const u16 secret_sequence[] = {BUTTON_UP,   BUTTON_UP,    BUTTON_DOWN, BUTTON_DOWN,
                                      BUTTON_LEFT, BUTTON_RIGHT, BUTTON_LEFT, BUTTON_RIGHT,
                                      BUTTON_B,    BUTTON_A};
#define SECRET_LENGTH (sizeof secret_sequence / sizeof secret_sequence[0])

static void (*secret_hook)(void); // NULL: not watching for the sequence
static u8 secret_matched;         // the last presses were its first secret_matched

// Engine-internal, for serval_init(): forgets everything, settings included,
// and the secret sequence's hook and progress. Not button_repeat_reset(), the
// public call below, which only silences the buttons held now and keeps the
// settings.
void serval_repeat_reset(void) {
    delay = DEFAULT_DELAY;
    interval = DEFAULT_INTERVAL;
    held = 0;
    due = 0;
    muted = 0;
    secret_hook = NULL;
    secret_matched = 0;
}

// A frame with new presses (BUTTON_* bits), while a hook is set: rare, so
// out of line and compiled for size.
//
// The progress becomes the longest start of the sequence that the presses
// made so far end with, this frame's included: one more for the right
// button. A wrong button keeps what still matches. For this sequence that is
// nothing, except for Up: a third Up after Up, Up still leaves Up, Up, and an
// Up later on is the sequence's first. Two new presses on one frame match no
// button of it, so the sequence starts over. No time limit: presses may be
// any time apart.
static __attribute__((noinline, cold)) void secret_press(u32 pressed) {
    u32 n = 0; // the new progress
    if ((pressed & (pressed - 1)) == 0) {
        // One button: try each length from one more than now down. The last
        // n presses are the sequence's [secret_matched - n + 1, secret_matched)
        // then this one; they match if they equal its first n.
        for (n = secret_matched + 1u; n > 0; n--) {
            if (secret_sequence[n - 1] != pressed)
                continue;
            u32 k = 0;
            while (k < n - 1 && secret_sequence[k] == secret_sequence[secret_matched + 1 - n + k])
                k++;
            if (k == n - 1)
                break;
        }
    }
    if (n < SECRET_LENGTH) {
        secret_matched = (u8)n;
        return;
    }
    secret_matched = 0; // complete: the next press starts it again
    secret_hook();
}

// A frame with buttons held: their repeats, and the presses for the secret
// sequence. Out of line, so the frames with none, most of them, don't pay
// for the registers it saves.
static __attribute__((noinline)) void count_held(u32 buttons) {
    muted &= buttons;              // a release ends a button's silence
    u32 pressed = buttons & ~held; // buttons that went down this frame
    u32 fire = pressed;            // presses fire at once
    for (u32 still = buttons & held, i = 0; still; still >>= 1, i++) {
        if (!(still & 1))
            continue;
        if (--wait[i] == 0) {
            fire |= 1u << i;
            wait[i] = interval;
        }
    }
    for (u32 p = pressed, i = 0; p; p >>= 1, i++) {
        if (p & 1)
            wait[i] = delay;
    }
    held = buttons;
    due = fire & ~muted;
    // Last, so that button_repeat() answers for this frame when the hook runs
    // (core.h). Frames that press nothing new pay this one test.
    if (pressed && secret_hook)
        secret_press(pressed);
}

void serval_repeat_frame(u32 buttons) {
    buttons &= BUTTON_ANY;
    if ((buttons | held) == 0) { // most frames: nothing held, nothing to count
        due = 0;
        return;
    }
    count_held(buttons);
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

// The hidden extra (core.h). Another hook, or NULL, starts the sequence over;
// the hook already set changes nothing.
void button_secret_set(void (*on_entered)(void)) {
    if (on_entered != secret_hook)
        secret_matched = 0;
    secret_hook = on_entered;
}
