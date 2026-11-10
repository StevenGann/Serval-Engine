#ifndef BOARDGAMES_ART_H
#define BOARDGAMES_ART_H

// The pictures, built at boot by art_build() (no image files): the table
// (the board in its walnut frame on green felt, and the side panel) as one
// map on background 2, and the sprites (both games' pieces in ivory and
// ebony, their small icons, the cursor, the move targets, the effects and
// the logo), with their palettes.

#include <serval/serval.h>

// --- The table ----------------------------------------------------------------------------

#define BOARD_BG 2 // the playfield: map_set_cell() changes its squares and labels

// The map is MAP_W x MAP_H metatiles. The board's squares are metatiles 1-8
// across and down; the map is shown MAP_SCROLL_X pixels to the left, so
// the squares start at screen x 14 and the frame and the panel leave equal
// margins.
#define TABLE_W 16
#define TABLE_H 10
#define MAP_SCROLL_X 2
#define BOARD_X 14 // screen position of the board's top-left square
#define BOARD_Y 16
#define SQUARE 16

// The panel: from screen x PANEL_X to the right edge, its text in columns
// PANEL_COL to 29. Its dividers run under text rows 3, 6, 9 and 16.
#define PANEL_X 156
#define PANEL_COL 20
#define PANEL_COLS 10

// Background palette banks. The squares' tiles use colours 1-SQUARE_COLORS
// of their bank, and a square's state is its bank: the same tiles in
// BANK_LAST are the last move's tint, and so on. The frame, the felt and
// the panel are BANK_TABLE.
enum { BANK_SQUARES, BANK_LAST, BANK_SELECTED, BANK_ALERT, BANK_TABLE, BG_BANKS };
#define SQUARE_COLORS 8

// A square's state (its bank), most important last: view.c shows the
// highest that applies.
enum { SQ_PLAIN, SQ_LAST, SQ_SELECTED, SQ_ALERT, SQ_STATES };

// The checkers board's numbers, 1-32 small in a corner of each dark square,
// as the board is seen: none (chess), White's side (Light's) at the bottom,
// or flipped.
enum { NUM_NONE, NUM_UPRIGHT, NUM_FLIPPED, NUM_VARIANTS };

// Metatiles of the table's map. MT_CELL is what the map holds at load, cell
// by cell (the squares plain, the frame unlabelled); the others are what
// map_set_cell() puts on a square (col, row as seen, 0-7 from the top-left)
// or a frame label (rank labels down the left, file labels along the
// bottom; flip 1 when the board is seen from Black's side).
#define MT_CELL(cx, cy) ((cy) * TABLE_W + (cx))
#define MT_SQUARES (TABLE_W * TABLE_H)
#define MT_SQUARE(state, numbers, col, row)                                                        \
    (MT_SQUARES + (((state) * NUM_VARIANTS + (numbers)) * 64) + (row) * 8 + (col))
#define MT_LABELS (MT_SQUARES + SQ_STATES * NUM_VARIANTS * 64)
#define MT_RANK(flip, row) (MT_LABELS + (flip) * 8 + (row))
#define MT_FILE(flip, col) (MT_LABELS + 16 + (flip) * 8 + (col))
#define MT_COUNT (MT_LABELS + 32)

extern Tileset table_tileset;
extern const MapLayer table_layer;
extern u16 bg_tiles_used; // tiles the table took, of MAP_MAX_TILES

// The king in check's square flashes: BANK_ALERT's colours at ALERT_LEVELS
// stages, mixed at boot, from a light red tint (0) to a strong one.
#define ALERT_LEVELS 8
extern Color alert_colors[ALERT_LEVELS][SQUARE_COLORS];

// --- Sprites ------------------------------------------------------------------------------

enum {
    SPR_PIECE,   // 16x16: PIECE_FRAMES, drawn in PAL_IVORY or PAL_EBONY
    SPR_ICON,    // 8x8: the same pieces small, for the panel, then ICON_CHIP
    SPR_CURSOR,  // 16x16: corner brackets, two frames (out, in)
    SPR_TARGET,  // 16x16: TARGET_DOT (a move), TARGET_RING (a capture); blended
    SPR_MARK,    // 16x16: a ring round a piece that must capture, two frames (bright, dim)
    SPR_PUFF,    // 32x32: PUFF_FRAMES of a captured piece's dust, over its square's middle
    SPR_SPARKLE, // 16x16: SPARKLE_FRAMES of a promotion's or a crowning's sparkle
    SPR_LOGO,    // 64x32: LOGO_FRAMES pieces of PAWS & KINGS, LOGO_W x LOGO_H
    SPRITE_COUNT,
};

// The pieces' frames: chess's kinds in order (frame PAWN - 1 ... KING - 1),
// then checkers' man and king.
enum {
    FRAME_PAWN,
    FRAME_KNIGHT, // a serval's head
    FRAME_BISHOP,
    FRAME_ROOK,
    FRAME_QUEEN,
    FRAME_KING,
    FRAME_MAN,     // a disc with a paw print
    FRAME_CROWNED, // a checkers king: the disc with a crown
    PIECE_FRAMES,
};

// The icons' last frame: a round chip in the side's colour, beside each
// move in the panel.
#define ICON_CHIP PIECE_FRAMES
#define ICON_FRAMES (PIECE_FRAMES + 1)

enum { TARGET_DOT, TARGET_RING, TARGET_FRAMES };
#define PUFF_FRAMES 4
#define PUFF_SIDE 32
#define SPARKLE_FRAMES 3

// The logo: two lines, PAWS & over KINGS, in four 64x32 pieces: frame 0 the
// top-left, 1 the top-right, 2 and 3 under them.
#define LOGO_FRAMES 4
#define LOGO_W 128
#define LOGO_H 64

// Sprite palettes (SPRITE_PALETTE(n) picks one for a draw).
enum {
    PAL_IVORY,  // White's chess pieces, Light's checkers
    PAL_EBONY,  // Black's chess pieces, Dark's checkers
    PAL_UI,     // the cursor and the marks
    PAL_TARGET, // the dots and rings, drawn SPRITE_BLEND
    PAL_PUFF,   // the puff and the sparkle
    PAL_LOGO,
    OBJ_PALETTES,
};

extern const SpriteAsset* sprite_table[SPRITE_COUNT];
extern const SpriteGroup sprite_group;
extern u16 obj_tiles_used; // sprite tiles the pictures took, of 1,024

// Builds every picture into EWRAM. Call once at boot.
void art_build(void);

// Loads the tiles, the palettes and the sprites into VRAM and shows the
// table (background 2). Call with the screen black (view_init() does).
void art_load(void);

// Dims the whole picture but the text and the logo, behind a menu or a result, or puts
// its colours back (palette writes, in VBlank). view.c's alert flash writes
// BANK_ALERT's square colours itself: call view_set_dim(), not this.
void art_set_dim(bool on);

#endif // BOARDGAMES_ART_H
