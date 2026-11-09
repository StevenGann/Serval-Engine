#ifndef RHYTHM_ART_H
#define RHYTHM_ART_H

// The pictures, built at boot by art_build() (no image files): the play
// screen's field (lanes, pads, the stage under a night sky) and its top
// panel, and the sprites (the notes, the serval, the words, the digits),
// with their palettes and the colour tables play.c steps through for the
// beat's pulse and the pads' flashes.

#include <serval/serval.h>

// --- Layout -----------------------------------------------------------------------------

// Four lanes, 32 pixels wide, in two pairs: LEFT and RIGHT (the left thumb)
// on the left of the screen, B and A (the right thumb) on the right, the
// stage between them. lane_x[] are their middles.
#define LANES 4
#define LANE_W 32
extern const u8 lane_x[LANES];

#define PAD_Y 132      // the pads' middle: a note is on time as its middle crosses it
#define FIELD_TOP 22   // the panel's foot: notes come out from under it
#define STAGE_X 120    // the stage's middle
#define STAGE_FOOT 150 // where the serval stands

// The progress groove in the panel: its left end, length and middle line.
#define PROGRESS_X 10
#define PROGRESS_W 220
#define PROGRESS_Y 12

// --- Backgrounds --------------------------------------------------------------------------

#define PANEL_BG 1 // the top panel: background 1, in front of the sprites
#define FIELD_BG 2 // the field: background 2, behind them

// Background palette banks: the field's, one for each lane's pad (its
// colours 1-7 the field's, so the floor around a pad matches), the panel's.
enum { BANK_FIELD, BANK_PAD, BANK_PANEL = BANK_PAD + LANES, BG_BANKS };

// The beat's pulse: colours 0-11 of the field's bank (and 1-7 of each pad's
// bank), in PULSE_LEVELS stages from rest (0) to the beat's peak.
#define PULSE_LEVELS 8
#define PULSE_COLORS 12
#define FIELD_SHARED 7 // colours 1-7: in the pads' banks too
extern Color field_pulse[PULSE_LEVELS][PULSE_COLORS];

// A pad's flash: colours 8-13 of its bank, in FLASH_LEVELS stages from rest
// (0) to a hit (FLASH_LEVELS - 1).
#define FLASH_LEVELS 8
#define PAD_COLOR0 8
#define PAD_COLORS 6
extern Color pad_flash[LANES][FLASH_LEVELS][PAD_COLORS];

// --- Sprites ------------------------------------------------------------------------------

enum {
    SPR_NOTE,   // 16x16: a paw print, its origin its middle; drawn in its lane's palette
    SPR_BURST,  // 32x32: BURST_FRAMES of a ring of sparks opening, origin its middle
    SPR_SERVAL, // 64x64: DANCE_FRAMES, origin between its feet
    SPR_WORD,   // 32x16: WORD_PIECES frames a word, left to right (draw_word() in play.c)
    SPR_DIGIT,  // 16x16: 0-9, each 12 pixels wide with its outline
    SPR_MARKER, // 8x8: the progress marker, origin its middle
    SPR_BIG,    // 32x32: a big letter a frame (big_letters), origin its middle
    SPRITE_COUNT,
};

#define BURST_FRAMES 3
#define DIGIT_ADVANCE 12

// The serval's dance: standing, bobbing down, its left paw raised (drawn
// flipped, the right).
enum { DANCE_IDLE, DANCE_BOB, DANCE_PAW, DANCE_FRAMES };

// The words: the judgements, then the count-in's beats 1 to 4, each 128x16
// in WORD_PIECES pieces of 32x16 (frames WORD_PIECES * w to
// WORD_PIECES * w + 3, left to right), centred: draw them side by side from
// STAGE_X - 64.
enum { WORD_PERFECT, WORD_GOOD, WORD_MISS, WORD_BEAT1, WORD_COUNT = WORD_BEAT1 + 4 };
#define WORD_PIECES 4

// Sprite palettes: one a lane (the notes and bursts, by SPRITE_PALETTE), the
// serval's, and the words' (by SPRITE_PALETTE).
enum {
    PAL_LANE,
    PAL_SERVAL = PAL_LANE + LANES,
    PAL_GOLD,  // PERFECT
    PAL_MINT,  // GOOD
    PAL_GREY,  // MISS
    PAL_WHITE, // the count-in, the combo, the marker
    OBJ_PALETTES,
};

// The big letters' frames, in order: the title's and the grades'.
extern const char big_letters[];

// The frame of big letter c (0 for one it lacks).
int art_big_frame(char c);

// Builds every picture into EWRAM. Call once at boot, before art_load().
void art_build(void);

// Loads the tiles, the palettes and the sprites into VRAM. Call once, with
// the screen black.
void art_load(void);

// Shows the play screen's two backgrounds; art_hide_play() takes them off
// (both, or either shown alone).
void art_show_play(void);
void art_hide_play(void);

// Shows the field alone (the title's stage), or the panel alone.
void art_show_field(void);
void art_show_panel(void);

// The field's colours at a stage of the beat's pulse (0 at rest), and a
// lane's pad at a stage of its flash (0 at rest): palette writes, for the
// next VBlank.
void art_pulse(int level);
void art_pad_flash(int lane, int level);

// The field's colours and the pads' at rest, mixed toward the backdrop by
// amount (0-256): the stage dimmed behind a menu's text. art_pulse(0) and
// art_pad_flash(lane, 0) undo it.
void art_dim(u32 amount);

#endif // RHYTHM_ART_H
