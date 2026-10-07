#ifndef SERVAL_GBA_SPLASH_ART_H
#define SERVAL_GBA_SPLASH_ART_H

// Engine-internal: the splash screen's logo (splash_art.c), drawn at boot
// from ASCII pictures into charblock 1 and shown on the text layer's map.
// Not part of the public API.
//
// The logo is the mark-and-wordmark design (the chosen serval's head beside
// "SERVAL ENGINE" in chunky letters) in four variations of that head while
// the final one is picked (docs/open-questions.md); the splash cycles them
// with L and R.

#include <tonc_types.h>

// Where the logos live: charblock 1 (BG0 keeps charblock 0 as its base, and
// a 10-bit tile index reaches both), up to SERVAL_SPLASH_ART_TILES drawn
// tiles per style (tiles with nothing drawn take no VRAM), and one BG
// palette bank per style from SERVAL_SPLASH_ART_FIRST_BANK.
#define SERVAL_SPLASH_ART_CHARBLOCK 1
#define SERVAL_SPLASH_ART_TILES 128
#define SERVAL_SPLASH_ART_FIRST_BANK 10
// The rows of screenblock 31 the logo and its text take (the whole screen).
#define SERVAL_SPLASH_ART_ROWS 20

// Draws every style's tiles into charblock 1 and its colors into its palette
// bank. Writes VRAM and palette RAM directly: call with background 0 faded
// to black.
void serval_splash_art_load(void);

// Rewrites rows 0 to SERVAL_SPLASH_ART_ROWS - 1 of screenblock 31 with one
// style's logo and its "made with" line, printed through palette bank
// `text_bank` with the text layer's font. Writes VRAM: call in VBlank (or
// with the layer faded out).
void serval_splash_art_show(u32 style, u32 text_bank);

#endif // SERVAL_GBA_SPLASH_ART_H
