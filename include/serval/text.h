#ifndef SERVAL_TEXT_H
#define SERVAL_TEXT_H

// Minimal text for HUDs and debugging: a fixed 8x8 font on a grid of
// TEXT_COLS x TEXT_ROWS characters, drawn on background layer 0 (the HUD layer,
// docs/tilemaps.md). The full text system (variable-width fonts, dialogue
// boxes) is planned separately; see docs/runtime-systems.md.

#include "serval/platform.h"

#define TEXT_COLS 30
#define TEXT_ROWS 20

// Writes a string starting at a character cell. Clipped at the right edge;
// characters outside printable ASCII are shown as '?'. The first text call
// sets up the font and turns layer 0 on.
void text_print(int col, int row, const char* s);

// Clears every character cell.
void text_clear(void);

// printf-style formatting without a C library. Supports %d %u %x %s %c %%,
// an optional '0' flag and a field width (e.g. "%5d", "%03u"). %d, %u and %x
// take any 32-bit integer: int, unsigned, s32 or u32 (on the GBA, u32 is an
// unsigned long, which printf's %u would reject). Returns one of four rotating
// static buffers of TEXT_FORMAT_MAX characters, so a few results can be used
// together; longer output is truncated.
//
// Not checked by the compiler like printf, since its integer rules differ.
#define TEXT_FORMAT_MAX 128
const char* text_format(const char* fmt, ...);

#endif // SERVAL_TEXT_H
