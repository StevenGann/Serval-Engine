// The sprite tiles scene: runtime sprite tiles, sprite_set_tiles()
// (docs/sprites.md#runtime-tiles).
//
// Four playing cards, each one 32x64 hardware sprite whose face is composed
// at run time, as a card game deals it: the card, its corner indexes and its
// pips drawn on the canvas (art.c) into a buffer of the card's own, then
// copied to the card's VRAM by sprite_set_tiles() in VBlank at frame_end().
// A metasprite card (blackjack's) takes four hardware sprites; a composed one
// takes one. Every 40 frames the next card flips over (sprite_draw_ex,
// scale_x through 0) and shows a newly composed face; A flips all four at
// once, five copies in one frame with the counter.
//
// Under them, a frame counter: one 64x32 sprite whose eight digits are new
// every frame. Each digit's two tiles are composed once, at enter(); each
// frame builds the counter from them, a few word copies, and queues it.
//
// The sprites are loaded from one blank frame in RAM, which they share: each
// sprite ID gets its own copy in VRAM, which sprite_set_tiles() then writes.
// The scene's contract (what main.c resets, what leave() must undo) is in
// effects.h; main.c's header says what the scene looks like.

#include "effects.h"

enum { SPR_CARD, SPR_COUNTER = SPR_CARD + 4, SPRITE_COUNT };

#define CARDS 4
#define CARD_W 32
#define CARD_H 48      // drawn in the top 48 rows of a 32x64 sprite
#define FRAME_TILES 32 // tiles of a 32x64 card's frame, and of the 64x32 counter's
#define DIGITS 8
#define FLIP_FRAMES 16 // a flip: the old face narrows to nothing, the new one widens
#define DEAL_EVERY 40  // frames between one card's flip and the next's

// Colors (palette indexes) of the scene's one palette.
enum { WHITE = 1, SHADE, INK, RED, EDGE, GOLD, DARK_GOLD, PANEL, PANEL_EDGE };

static const u16 palette[16] = {
    [WHITE] = COLOR_RGB(248, 248, 240),     [SHADE] = COLOR_RGB(192, 200, 216),
    [INK] = COLOR_RGB(24, 24, 40),          [RED] = COLOR_RGB(216, 32, 48),
    [EDGE] = COLOR_RGB(56, 56, 88),         [GOLD] = COLOR_RGB(248, 208, 72),
    [DARK_GOLD] = COLOR_RGB(144, 96, 16),   [PANEL] = COLOR_RGB(24, 32, 80),
    [PANEL_EDGE] = COLOR_RGB(88, 104, 176),
};

// The VRAM the composed frames go to (a card's frame is the bigger one).
static u32 blank[FRAME_TILES * 8] SERVAL_EWRAM_BSS;

// Origins at the card's center and the counter's middle.
static const SpriteAsset card_sprite = {
    .size = SPRITE_32x64, .tiles = blank, .origin_x = CARD_W / 2, .origin_y = CARD_H / 2};
static const SpriteAsset counter_sprite = {
    .size = SPRITE_64x32, .tiles = blank, .origin_x = 32, .origin_y = 8};
static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    &card_sprite, &card_sprite, &card_sprite, &card_sprite, &counter_sprite};
static const SpriteGroup sprites = {
    .palettes = palette, .sprite_count = SPRITE_COUNT, .palette_count = 1};

// --- Pictures: '#' is a pixel, '.' is not -------------------------------------

#define GLYPH_H 7 // the corner indexes and the counter's digits

// Ranks 1-13 (A, 2-10, J, Q, K), 5 wide except the 10.
static const char* const rank_glyphs[14][GLYPH_H] = {
    [1] = {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"},
    [2] = {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"},
    [3] = {"####.", "....#", "....#", ".###.", "....#", "....#", "####."},
    [4] = {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."},
    [5] = {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."},
    [6] = {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."},
    [7] = {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."},
    [8] = {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."},
    [9] = {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."},
    [10] = {"#..##.", "#.#..#", "#.#..#", "#.#..#", "#.#..#", "#.#..#", "#..##."},
    [11] = {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."},
    [12] = {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"},
    [13] = {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"},
};

// The counter's digits.
static const char* const digit_glyphs[10][GLYPH_H] = {
    {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."},
    {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."},
    {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"},
    {"####.", "....#", "....#", ".###.", "....#", "....#", "####."},
    {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."},
    {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."},
    {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."},
    {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."},
    {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."},
    {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."},
};

enum { SPADES, HEARTS, DIAMONDS, CLUBS };

// Small pips, 5x5: the corner indexes and the number cards' layouts.
static const char* const small_pips[4][5] = {
    [SPADES] = {"..#..", ".###.", "#####", "#####", "..#.."},
    [HEARTS] = {".#.#.", "#####", "#####", ".###.", "..#.."},
    [DIAMONDS] = {"..#..", ".###.", "#####", ".###.", "..#.."},
    [CLUBS] = {"..#..", ".###.", "#####", "#.#.#", ".###."},
};

// Big pips, 9x9: aces and court cards.
static const char* const big_pips[4][9] = {
    [SPADES] = {"....#....", "...###...", "..#####..", ".#######.", "#########", "#########",
                ".##.#.##.", "....#....", "..#####.."},
    [HEARTS] = {".##...##.", "####.####", "#########", "#########", ".#######.", "..#####..",
                "...###...", "....#....", "........."},
    [DIAMONDS] = {"....#....", "...###...", "..#####..", ".#######.", "#########", ".#######.",
                  "..#####..", "...###...", "....#...."},
    [CLUBS] = {"...###...", "..#####..", "..#####..", "##.###.##", "#########", "##.#.#.##",
               "....#....", "...###...", "........."},
};

// Draws a picture of `h` rows in color c with its top-left at (x, y); turned,
// it is drawn upside down and mirrored (the bottom-right index).
static void stamp(const char* const* rows, int h, int x, int y, u32 c, bool turned) {
    for (int py = 0; py < h; py++) {
        int w = 0;
        while (rows[py][w])
            w++;
        for (int px = 0; px < w; px++) {
            if (rows[py][px] != '#')
                continue;
            if (turned)
                canvas_plot(x + w - 1 - px, y + h - 1 - py, c);
            else
                canvas_plot(x + px, y + py, c);
        }
    }
}

// --- Composing a card face ---------------------------------------------------

typedef struct {
    s8 x, y; // a small pip's center
} Pip;

// The number cards' pips: columns at 11, 16 and 21, rows from 10 to 38.
static const Pip layout_2[] = {{16, 10}, {16, 38}};
static const Pip layout_3[] = {{16, 10}, {16, 24}, {16, 38}};
static const Pip layout_4[] = {{11, 10}, {21, 10}, {11, 38}, {21, 38}};
static const Pip layout_5[] = {{11, 10}, {21, 10}, {16, 24}, {11, 38}, {21, 38}};
static const Pip layout_6[] = {{11, 10}, {21, 10}, {11, 24}, {21, 24}, {11, 38}, {21, 38}};
static const Pip layout_7[] = {{11, 10}, {21, 10}, {16, 17}, {11, 24},
                               {21, 24}, {11, 38}, {21, 38}};
static const Pip layout_8[] = {{11, 10}, {21, 10}, {16, 17}, {11, 24},
                               {21, 24}, {16, 31}, {11, 38}, {21, 38}};
static const Pip layout_9[] = {{11, 10}, {21, 10}, {11, 19}, {21, 19}, {16, 24},
                               {11, 29}, {21, 29}, {11, 38}, {21, 38}};
static const Pip layout_10[] = {{11, 10}, {21, 10}, {16, 14}, {11, 19}, {21, 19},
                                {11, 29}, {21, 29}, {16, 34}, {11, 38}, {21, 38}};
static const Pip* const layouts[11] = {
    [2] = layout_2, [3] = layout_3, [4] = layout_4, [5] = layout_5,   [6] = layout_6,
    [7] = layout_7, [8] = layout_8, [9] = layout_9, [10] = layout_10,
};

// Composes the face of `rank` (1-13) of `suit` into `out`: a card's frame,
// FRAME_TILES tiles.
static void compose_card(u32* out, int rank, int suit) {
    canvas_begin(CARD_W, 64);
    // The card: rounded corners, an edge, a shade along the right and bottom.
    for (int y = 0; y < CARD_H; y++) {
        for (int x = 0; x < CARD_W; x++) {
            int dx = int_max(0, int_max(2 - x, x - (CARD_W - 3)));
            int dy = int_max(0, int_max(2 - y, y - (CARD_H - 3)));
            int d = dx * dx + dy * dy;
            if (d > 5)
                continue; // outside the rounded corner
            bool edge = d > 2 || x == 0 || y == 0 || x == CARD_W - 1 || y == CARD_H - 1;
            bool shade = x == CARD_W - 2 || y == CARD_H - 2;
            canvas_plot(x, y, edge ? EDGE : shade ? SHADE : WHITE);
        }
    }
    u32 ink = suit == HEARTS || suit == DIAMONDS ? RED : INK;
    // The indexes: top-left, and turned at the bottom right.
    int glyph_w = rank == 10 ? 6 : 5;
    stamp(rank_glyphs[rank], GLYPH_H, 2, 3, ink, false);
    stamp(small_pips[suit], 5, 2, 12, ink, false);
    stamp(rank_glyphs[rank], GLYPH_H, CARD_W - 2 - glyph_w, CARD_H - 3 - GLYPH_H, ink, true);
    stamp(small_pips[suit], 5, CARD_W - 7, CARD_H - 17, ink, true);
    // The middle: pips, or a big pip (framed on court cards).
    if (rank >= 2 && rank <= 10) {
        for (int k = 0; k < rank; k++) {
            const Pip* p = &layouts[rank][k];
            stamp(small_pips[suit], 5, p->x - 2, p->y - 2, ink, p->y > CARD_H / 2);
        }
    } else {
        if (rank > 10) {
            for (int x = 9; x <= 22; x++) {
                canvas_plot(x, 9, GOLD);
                canvas_plot(x, CARD_H - 10, GOLD);
            }
            for (int y = 9; y <= CARD_H - 10; y++) {
                canvas_plot(9, y, GOLD);
                canvas_plot(22, y, GOLD);
            }
            stamp(rank_glyphs[rank], GLYPH_H, 14, 12, ink, false);
            stamp(rank_glyphs[rank], GLYPH_H, 13, CARD_H - 12 - GLYPH_H, ink, true);
        }
        stamp(big_pips[suit], 9, 12, CARD_H / 2 - 4, ink, false);
    }
    canvas_pack(out);
}

// --- The scene ---------------------------------------------------------------

typedef struct {
    u8 rank, suit;           // the face shown
    u8 next_rank, next_suit; // the face a flip turns to
    u8 flip;                 // frames into a flip, 0 when still
} Card;

static Card cards[CARDS];
static u32 faces[CARDS][FRAME_TILES * 8] SERVAL_EWRAM_BSS; // each card's composed face
static u32 digit_tiles[10][16] SERVAL_EWRAM_BSS;           // each digit's two tiles
static u32 counter[FRAME_TILES * 8] SERVAL_EWRAM_BSS;      // the counter's frame
static int next_deal;                                      // the card that flips next
static int deal_timer;                                     // frames until it does
static u32 shown_sprites;

// Composes card k's face and queues the copy to its VRAM.
static void show_face(int k) {
    compose_card(faces[k], cards[k].rank, cards[k].suit);
    sprite_set_tiles((u16)(SPR_CARD + k), 0, faces[k]);
}

static void start_flip(int k) {
    if (cards[k].flip)
        return;
    cards[k].next_rank = (u8)random_range(1, 13);
    cards[k].next_suit = (u8)random_range(SPADES, CLUBS);
    cards[k].flip = 1;
}

// Digit d of the counter: gold on the panel, 8x16, its glyph's rows doubled.
static void compose_digit(u32* out, int d) {
    canvas_begin(8, 16);
    for (int y = 1; y < 15; y++)
        for (int x = 0; x < 8; x++)
            canvas_plot(x, y, PANEL);
    for (int py = 0; py < GLYPH_H; py++) {
        for (int px = 0; px < 5; px++) {
            if (digit_glyphs[d][py][px] != '#')
                continue;
            for (int dy = 0; dy < 2; dy++) {
                canvas_plot(2 + px, 2 + py * 2 + dy, DARK_GOLD); // shadow
                canvas_plot(1 + px, 1 + py * 2 + dy, GOLD);
            }
        }
    }
    for (int x = 0; x < 8; x++) {
        canvas_plot(x, 0, PANEL_EDGE);
        canvas_plot(x, 15, PANEL_EDGE);
    }
    canvas_pack(out);
}

// Builds the counter's frame from the digit tiles: digit i is tiles i (top)
// and 8 + i (bottom) of the 64x32 sprite, whose tiles go row by row.
static void compose_counter(u32 value) {
    for (int i = DIGITS - 1; i >= 0; i--) {
        const u32* digit = digit_tiles[value % 10];
        value /= 10;
        for (int w = 0; w < 8; w++) {
            counter[i * 8 + w] = digit[w];
            counter[(DIGITS + i) * 8 + w] = digit[8 + w];
        }
    }
    sprite_set_tiles(SPR_COUNTER, 0, counter);
}

static void enter(void) {
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprites);
    static const u8 first[CARDS][2] = {{1, SPADES}, {13, HEARTS}, {7, DIAMONDS}, {10, CLUBS}};
    for (int k = 0; k < CARDS; k++) {
        cards[k] = (Card){.rank = first[k][0], .suit = first[k][1]};
        show_face(k);
    }
    for (int d = 0; d < 10; d++)
        compose_digit(digit_tiles[d], d);
    next_deal = 0;
    deal_timer = DEAL_EVERY;
    shown_sprites = 0;
    text_print_centered(2, "FACES COMPOSED AT RUN TIME");
    text_print_centered(3, "EACH CARD IS ONE SPRITE");
    text_print_centered(15, "NEW DIGITS EVERY FRAME");
    text_print_centered(19, "A: DEAL FOUR NEW CARDS");
}

static void update(void) {
    if (button_pressed(BUTTON_A)) {
        for (int k = 0; k < CARDS; k++)
            start_flip(k);
    } else if (--deal_timer == 0) {
        start_flip(next_deal);
        next_deal = (next_deal + 1) % CARDS;
        deal_timer = DEAL_EVERY;
    }

    for (int k = 0; k < CARDS; k++) {
        Card* c = &cards[k];
        FIXED scale = FX_ONE;
        if (c->flip) {
            // |cos| from 1 through 0 (half way: the new face) back to 1.
            scale = int_abs(fx_cos((u16)(ANGLE_DEG(180) * c->flip / FLIP_FRAMES)));
            if (c->flip == FLIP_FRAMES / 2) {
                c->rank = c->next_rank;
                c->suit = c->next_suit;
                show_face(k); // drawn at width 0 this frame: the copy is unseen
            }
            if (++c->flip > FLIP_FRAMES)
                c->flip = 0;
        }
        sprite_draw_ex((u16)(SPR_CARD + k), 0, 36 + 56 * k, 64, 0, scale, FX_ONE, 0);
    }

    compose_counter(frame_count());
    sprite_draw(SPR_COUNTER, 0, SCREEN_W / 2, 104, 0);

    // The last frame's hardware sprites: a card is one.
    u32 drawn = sprite_stats().drawn;
    if (drawn != shown_sprites) {
        shown_sprites = drawn;
        text_print_centered(17, text_format("HARDWARE SPRITES: %u", drawn));
    }
}

static void leave(void) {}

const Scene sprite_tiles_scene = {
    .name = "SPRITE TILES", .enter = enter, .update = update, .leave = leave};
