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

// Starts a frame: polls input and clears the sprite draw list.
void frame_begin(void);

// Ends a frame: waits for VBlank, then flushes the shadow OAM to hardware.
void frame_end(void);

// True while the button (or any of several OR'd buttons) is held.
bool button_down(u16 buttons);

// True only on the frame the button went down.
bool button_pressed(u16 buttons);

#endif // SERVAL_CORE_H
