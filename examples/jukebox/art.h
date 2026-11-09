#ifndef JUKEBOX_ART_H
#define JUKEBOX_ART_H

// The jukebox's pictures, drawn at boot by art_build() (no image files): the
// cabinet on background 2 (a marquee across the top, the turntable deck, the
// control panel, the four effect pads), the backdrop's gradient, and the
// sprites (the title's letters, the record and its tonearm, the meters, the
// cursor, the particles), with their palettes and the colour stages show.c
// steps through for the beat's pulse and the pads' flashes.

#include "jukebox.h"

// --- Layout (screen pixels) ---------------------------------------------------------------

#define TITLE_X 37 // the title's first letter; each is LETTER_ADVANCE on
#define TITLE_Y 3
#define LETTER_ADVANCE 12

#define PANEL_TOP 21 // the deck and the control panel
#define PANEL_FOOT 109
#define DECK_LEFT 1
#define DECK_RIGHT 74
#define MENU_LEFT 77
#define MENU_RIGHT 238
#define DIVIDER_Y 84 // between the menu and the now-playing lines

#define RECORD_X 36 // the record's middle
#define RECORD_Y 66
#define ARM_X 48 // the tonearm sprite's top left
#define ARM_Y 24

// The meters right of the menu's names: ten lamps each (VOLUME, EFFECTS),
// LED_STEP apart, and the SPEED slider's groove between them.
#define METER_X 148
#define LED_STEP 5
#define LEDS 10
#define SPEED_X(percent) (METER_X + ((percent) - 50) * 49 / 150) // 50-200%

// The beat lamps on the first now-playing line: one a beat of the bar.
#define LAMP_X 184 // the first lamp's middle
#define LAMP_STEP 12
#define LAMPS 4
#define NOW_ROW 11 // the now-playing lines' first text row

// The effect pads: A, B, L and R, left to right.
enum { PAD_COIN, PAD_LASER, PAD_BOOM, PAD_HUM, PADS };
#define PAD_TOP 114
#define PAD_FOOT 141
#define PAD_X(i) (8 + (i) * 58)
#define PAD_W 50
#define PAD_CAP_X(i) (PAD_X(i) + 24) // the button cap's middle
#define PAD_CAP_Y 124

// --- Backgrounds ----------------------------------------------------------------------------

#define CABINET_BG 2 // behind the sprites; text is in front of both

// Background palette banks: the cabinet's, one a pad, and one a meter
// (VOLUME's, EFFECTS'), whose colours 5-14 are its ten lamps: lighting them
// is a palette write, no sprites.
enum { BANK_CABINET, BANK_PAD, BANK_METER = BANK_PAD + PADS, BG_BANKS = BANK_METER + 2 };

// The beat's pulse: the cabinet's gold trim, the marquee's bulbs and the
// backdrop's gradient brighten on each beat, in PULSE_LEVELS stages.
#define PULSE_LEVELS 8

// A pad's flash, in FLASH_LEVELS stages from rest (0) to a hit.
#define FLASH_LEVELS 8

// --- Sprites --------------------------------------------------------------------------------

enum {
    SPR_LETTER, // 16x16: a title letter a frame (title_letters)
    SPR_RECORD, // 64x64: the record, its origin its middle
    SPR_GLARE,  // 64x64: the light on it, which does not turn
    SPR_ARM,    // 32x64: the tonearm, ARM_REST and ARM_PLAY
    SPR_CURSOR, // 8x8: a quaver
    SPR_KNOB,   // 8x8: the speed slider's knob, its origin its middle
    SPR_LAMP,   // 8x8: a beat lamp, LAMP_BRIGHT and LAMP_DIM, origin its middle
    SPR_COIN,   // 16x16: COIN_FRAMES of a coin spinning, origin its middle
    SPR_BEAM,   // 32x8: the laser's beam, origin its middle
    SPR_SPARK,  // 8x8: a spark, big and small, origin its middle
    SPR_WAVE,   // 8x8: a ripple of the hum, origin its middle
    SPRITE_COUNT,
};

enum { ARM_REST, ARM_PLAY };
enum { LAMP_BRIGHT, LAMP_DIM };
#define COIN_FRAMES 4

// The title's letters, in the order of SPR_LETTER's frames.
extern const char title_letters[];

// Sprite palettes.
enum {
    PAL_TITLE,
    PAL_RECORD, // the label's colours: one palette a tune, THEME's, CALM's, OFF's
    PAL_METAL = PAL_RECORD + TUNE_COUNT, // the tonearm, the glare, the knob, the cursor
    PAL_LAMP,                            // the beat lamps
    PAL_LAMP_DOWN,                       // the bar's first beat
    PAL_COIN,                            // one a pad, in PAD_* order: the particles
    OBJ_PALETTES = PAL_COIN + PADS,
};

// Builds every picture into EWRAM. Call once at boot, before art_load().
void art_build(void);

// Loads the tiles, the palettes, the cabinet and the sprites into VRAM. Call
// once, with the screen dark.
void art_load(void);

// The backdrop's gradient at a stage of the pulse: SCREEN_H colours, for
// raster_backdrop().
const Color* art_backdrop(int level);

// The cabinet's trim at a stage of the beat's pulse (0 at rest), the
// marquee's lit bulb (0-3, the chase), and a pad at a stage of its flash (0
// at rest): palette writes, shown at the next VBlank.
void art_pulse(int level);
void art_bulbs(int lit, int level);
void art_pad_flash(int pad, int level);

// Lights the first `lit` of a meter's lamps (meter 0 VOLUME's, 1 EFFECTS').
void art_meter(int meter, int lit);

#endif // JUKEBOX_ART_H
