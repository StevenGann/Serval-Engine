#ifndef SERVAL_CORE_H
#define SERVAL_CORE_H

// raylib-style core API: frame timing and input. See docs/core-api.md.

#include "serval/platform.h"

#ifdef SERVAL_GBA
#include <tonc_memdef.h> // KEY_A, KEY_B, KEY_UP, ...
#endif

// Initializes interrupts, video state and the engine's subsystems. Call once
// at the start of main().
void serval_init(void);

// Starts a frame: polls input and clears the sprite draw list.
void frame_begin(void);

// Ends a frame: waits for VBlank, then flushes the shadow OAM to hardware.
void frame_end(void);

// True while the key (or any of several OR'd keys) is held.
bool key_down(u16 key);

// True only on the frame the key went down.
bool key_pressed(u16 key);

#endif // SERVAL_CORE_H
