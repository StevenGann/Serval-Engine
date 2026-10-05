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

// Like text_print, then blanks the rest of the row, so text that got shorter
// (say a score going from 10 to 9) leaves nothing behind. Use it for lines
// that are redrawn with changing content, such as a HUD.
void text_print_line(int col, int row, const char* s);

// Clears every character cell.
void text_clear(void);

// printf-style formatting without a C library. Supports %d %u %x %s %c %%,
// the '-' (left-align) and '0' (zero-pad) flags, and a field width (e.g.
// "%5d", "%03u", "%-10s"). %d, %u and %x
// take any 32-bit integer: int, unsigned, s32 or u32 (on the GBA, u32 is an
// unsigned long, which printf's %u would reject). Returns one of four rotating
// static buffers of TEXT_FORMAT_MAX characters, so a few results can be used
// together; longer output is truncated.
//
// Not checked by the compiler like printf, since its integer rules differ. A
// NULL %s prints "(null)"; debug builds also catch a %s argument that isn't a
// pointer at all (usually a number), printing "(?)" and warning.
#define TEXT_FORMAT_MAX 128
const char* text_format(const char* fmt, ...);

#endif // SERVAL_TEXT_H
