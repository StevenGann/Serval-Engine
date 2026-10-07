#ifndef SERVAL_CORE_H
#define SERVAL_CORE_H

// raylib-style core API: frame timing and input. See docs/core-api.md.

#include "serval/platform.h"

// Buttons, combinable with |.
#define BUTTON_A 0x0001
#define BUTTON_B 0x0002
#define BUTTON_SELECT 0x0004
#define BUTTON_START 0x0008
#define BUTTON_RIGHT 0x0010
#define BUTTON_LEFT 0x0020
#define BUTTON_UP 0x0040
#define BUTTON_DOWN 0x0080
#define BUTTON_R 0x0100
#define BUTTON_L 0x0200
#define BUTTON_ANY 0x03FF

// Initializes interrupts, the display (sprites on), and the engine's
// subsystems. Call once at the start of main().
void serval_init(void);

// Shows the "made with Serval Engine" splash screen, then returns: about three
// seconds of the logo fading in on a black backdrop, a jingle, and fading out.
// Pressing any button but L and R once it has faded in skips the rest. (While
// the logo is being chosen, four candidate styles are built in and R and L
// show the next and previous one; a switch restarts the hold. See
// docs/core-api.md.) Call it after serval_init(), before loading your game's
// graphics. Puts back the backdrop color (so the screen then shows the game's
// backdrop, black by default), background 0's control register and on/off
// state, the blend control register and the brightness
// (screen_set_brightness), the palette entries it uses (one in BG bank 14 and
// colors 1-15 of banks 10-13), whether the text layer was set up and the text
// shadow setting; silences square channel 1. Not restored: the text layer's
// map (text_print output is cleared), charblock 1 (the logo's tiles), and if
// the text layer wasn't set up, charblock 0's first 96 tiles (overwritten by
// the font) and background 0's scroll (reset to 0).
void serval_splash(void);

// Starts a frame: polls input and clears the sprite draw list.
void frame_begin(void);

// Ends a frame: waits for VBlank, then flushes the shadow OAM to hardware,
// copies queued map and tile changes (map.h) to VRAM, and advances sound
// effects and music by one frame.
void frame_end(void);

// Frames since serval_init(): the number of frame_end() calls, so 0 during
// the first frame. Wraps after about 2.2 years at 60 frames per second.
u32 frame_count(void);

// CPU cycles the previous frame spent between frame_begin() and frame_end()
// (the game's work, before waiting for VBlank). Measured with hardware timers
// 2 and 3, which the engine reserves.
u32 frame_cpu_cycles(void);

// frame_cpu_cycles() as thousandths of the frame budget: 500 means the previous
// frame used half the available time. Display as percent with one decimal:
// text_format("%u.%u%%", p / 10, p % 10).
u32 frame_cpu_permille(void);

// CPU cycles available per frame at 60 Hz (280,896 on the GBA). A frame whose
// frame_cpu_cycles() exceeds this misses the next refresh.
u32 frame_budget_cycles(void);

// True while the button (or any of several OR'd buttons) is held.
bool button_down(u16 buttons);

// True only on the frame the button went down.
bool button_pressed(u16 buttons);

// For menus and cursors: true on the frame a button went down, then, while it
// stays held, again after a delay and from then on at an interval (default:
// 20 frames, then every 4, about 1/3 second and 15 times a second). Each
// button counts on its own; with several OR'd buttons, true if any of them is
// due. Counted from input alone, so it is deterministic.
// Caveat: repeats count from the press, not from when a menu opens, so a
// button still held from the previous screen (the A that opened the menu)
// keeps repeating in it at once. If the menu should wait for a fresh press,
// act on button_pressed() until the button has been released once.
bool button_repeat(u16 buttons);

// The frames from a press to its first repeat, and between repeats after that
// (each 1 to 65535). A wait already under way for a held button finishes
// first. serval_init() sets the defaults.
void button_repeat_set(int delay, int interval);

#endif // SERVAL_CORE_H
