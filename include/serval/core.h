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

// Shows the "Made with Serval Engine" splash screen, then returns: about three
// seconds of fading in, a jingle, and fading out on a black screen. Pressing
// any button once it has faded in skips the rest. Call it after serval_init(),
// before loading your game's graphics. Leaves the screen black, and puts back
// what it borrowed (backdrop color, text layer, palette entries, sound
// channel).
void serval_splash(void);

// Starts a frame: polls input and clears the sprite draw list.
void frame_begin(void);

// Ends a frame: waits for VBlank, then flushes the shadow OAM to hardware and
// advances sound effects (notes and lengths).
void frame_end(void);

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

#endif // SERVAL_CORE_H
