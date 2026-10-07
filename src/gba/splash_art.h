#ifndef SERVAL_GBA_SPLASH_ART_H
#define SERVAL_GBA_SPLASH_ART_H

// Engine-internal: the splash screen's logo (splash_art.c), drawn at boot
// from ASCII pictures into charblock 1 and shown on the text layer's map.
// Not part of the public API.

#include <tonc_types.h>

// Where the logo lives: the first tiles of charblock 1 (BG0 keeps charblock 0
// as its base, and a 10-bit tile index reaches both; a tile with nothing
// drawn takes no VRAM) and one BG palette bank.
#define SERVAL_SPLASH_ART_CHARBLOCK 1
#define SERVAL_SPLASH_ART_BANK 13

// Draws the logo: its tiles into charblock 1, its colors into its palette
// bank, and the logo with its "made with" line (printed through palette bank
// `text_bank` with the text layer's font) onto screenblock 31, the text
// layer's map. Writes VRAM and palette RAM directly: call after text_clear(),
// with background 0 faded to black.
void serval_splash_art_draw(u32 text_bank);

#endif // SERVAL_GBA_SPLASH_ART_H
