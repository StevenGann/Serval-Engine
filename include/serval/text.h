#ifndef SERVAL_TEXT_H
#define SERVAL_TEXT_H

// Minimal text for HUDs and debugging: a fixed 8x8 font on a grid of
// TEXT_COLS x TEXT_ROWS characters, drawn on background layer 0 (the HUD layer,
// docs/tilemaps.md). The full text system (variable-width fonts, dialogue
// boxes) is planned separately; see docs/runtime-systems.md.

#include "serval/platform.h"
#include "serval/screen.h"

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

// Like text_print_line on the whole row, centered: blanks the row and writes
// the string in its middle (one column left of center if the leftover space
// is odd). A string wider than TEXT_COLS loses characters at both ends.
void text_print_centered(int row, const char* s);

// Clears every character cell.
void text_clear(void);

// The text color and the shadow color, for all text on the layer, including
// what is already shown. Default: white text, black shadow.
void text_set_color(Color text, Color shadow);

// Turns the font's drop shadow on or off: a copy of each glyph one pixel to
// the right and one down, in the shadow color, behind the glyph (within its
// 8x8 cell), so light text stays readable over light art. For all text on the
// layer. Default: off.
void text_set_shadow(bool on);

// printf-style formatting without a C library. Supports %d %i %u %x %s %c %%,
// the '-' (left-align) and '0' (zero-pad, numbers only) flags, and a field
// width (e.g. "%5d", "%03u", "%-10s"; capped at TEXT_FORMAT_MAX). %d, %i, %u
// and %x take any 32-bit integer: int, unsigned, s32 or u32 (on the GBA, u32
// is an unsigned long, which printf's %u would reject). The h and l length
// modifiers are accepted (%ld reads a long); %lld prints only the low 32 bits.
// Other conversions (such as %f) are printed as written, with a warning in
// debug builds. Returns one of four rotating static buffers of
// TEXT_FORMAT_MAX characters, so a few results can be used together; longer
// output is truncated.
//
// Not checked by the compiler like printf, since its integer rules differ. A
// NULL %s prints "(null)"; on the GBA, debug builds also catch a %s argument
// that isn't a pointer at all (usually a number), printing "(?)" and warning.
#define TEXT_FORMAT_MAX 128
const char* text_format(const char* fmt, ...);

#endif // SERVAL_TEXT_H
