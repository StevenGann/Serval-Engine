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

// Like text_print_centered within `width` columns starting at `col` (say a
// field beside a HUD panel): blanks those cells of the row and writes the
// string in their middle. Nothing outside the columns changes; a string wider
// than `width` loses characters at both ends.
void text_print_centered_in(int col, int width, int row, const char* s);

// Clears every character cell.
void text_clear(void);

// Clears a rectangle of `width` x `height` character cells from (col, row).
// Cells off the screen are skipped.
void text_clear_area(int col, int row, int width, int height);

// Text styles: TEXT_STYLES pairs of text and shadow colors, so a few lines can
// stand out (the entry just made in a high-score table, a selected menu item)
// while the rest keeps its color. The style set with text_set_style applies
// to what is printed afterwards; a style's colors apply to all text already
// shown in it.
#define TEXT_STYLES 4
#define TEXT_NORMAL 0    // white text, black shadow (text_set_color's)
#define TEXT_HIGHLIGHT 1 // yellow text, black shadow by default

// The style of text printed from now on (0 to TEXT_STYLES - 1). Default:
// TEXT_NORMAL. Styles 2 and 3 default to light red and grey text.
void text_set_style(int style);

// The text and shadow colors of a style, for all text shown in it.
void text_set_style_color(int style, Color text, Color shadow);

// The text color and the shadow color of TEXT_NORMAL, the style all text has
// unless text_set_style chose another, including what is already shown.
// Default: white text, black shadow.
void text_set_color(Color text, Color shadow);

// Turns the font's drop shadow on or off: a copy of each glyph one pixel to
// the right and one down, in the shadow color, behind the glyph (within its
// 8x8 cell), so light text stays readable over light art. For all text on the
// layer, in every style. Default: off.
void text_set_shadow(bool on);

// printf-style formatting without a C library. Supports %d %i %u %x %s %c %%,
// the '-' (left-align) and '0' (zero-pad, numbers only) flags, a field width
// (e.g. "%5d", "%03u", "%-10s"; capped at TEXT_FORMAT_MAX) and, for %s only, a
// precision: "%.3s" prints at most 3 characters and reads no further, so a
// char[3] without a terminating zero prints as is ("%.*s" takes the count as
// an int argument before the string). %d, %i, %u and %x take any 32-bit
// integer: int, unsigned, s32 or u32 (on the GBA, u32 is an unsigned long,
// which printf's %u would reject). The h and l length modifiers are accepted
// (%ld reads a long); %lld prints only the low 32 bits. Other conversions
// (such as %f) are printed as written, and a precision on anything but %s is
// ignored, with a warning in debug builds. Returns one of four rotating
// static buffers of TEXT_FORMAT_MAX characters, so a few results can be used
// together; longer output is truncated.
//
// Not checked by the compiler like printf, since its integer rules differ. A
// NULL %s prints "(null)"; on the GBA, debug builds also catch a %s argument
// that isn't a pointer at all (usually a number), printing "(?)" and warning.
#define TEXT_FORMAT_MAX 128
const char* text_format(const char* fmt, ...);

#endif // SERVAL_TEXT_H
