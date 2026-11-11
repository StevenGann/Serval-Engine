#ifndef LIFE_RULES_H
#define LIFE_RULES_H

// Life's world and its rules, portable C (no hardware; tested on the host in
// tests/life_tests.c). The world is LIFE_W x LIFE_H cells on a torus: the
// left edge's neighbours are on the right edge, the top's at the bottom.
//
// A cell is one byte, and that byte is both its state and the color number
// the pixel surface shows it in (main.c draws the grid into the surface's
// top-left LIFE_W x LIFE_H pixels as it is):
//   0                      empty (transparent: the backdrop shows)
//   1 .. LIFE_TRAIL        dead lately: a fading trail, one step a
//                          generation, LIFE_TRAIL just after dying
//   LIFE_BORN .. LIFE_OLDEST
//                          alive, LIFE_BORN newborn, one more each
//                          generation it lives, up to LIFE_OLDEST
// Alive is the range LIFE_BORN-LIFE_OLDEST, which is bit 5 of the byte, so
// the step reads four cells' "alive" from a word with one shift and a mask.
//
// Rulings where the design left a choice:
//  - Alive is 32-63 and trails 1-12 (the design's sketch had alive from 1):
//    one bit tells alive, and ages and trails still read as a range.
//  - A stamp is centred on the cursor (its top-left at the cursor minus half
//    its size), clears the cells of its box that it doesn't set, and wraps
//    around the world's edges like everything else.
//  - A loaded world has lost its ages and trails: its cells come back
//    LIFE_OLDEST (settled colors) and empty.

#include <serval/platform.h>

#define LIFE_W 120
#define LIFE_H 80
#define LIFE_CELLS (LIFE_W * LIFE_H)
#define LIFE_WORDS (LIFE_CELLS / 4)
#define LIFE_ROW_WORDS (LIFE_W / 4)

#define LIFE_EMPTY 0
#define LIFE_TRAIL 12  // a cell that has just died; 0 LIFE_TRAIL generations later
#define LIFE_BORN 32   // a newborn
#define LIFE_OLDEST 63 // the age cap
#define LIFE_STATES 64 // cell values 0 to 63

// The grid: LIFE_CELLS bytes, row after row, kept in words so the step can
// read four cells at once. Cell (x, y) is byte y * LIFE_W + x of it.
typedef u32 LifeGrid[LIFE_WORDS];

static inline bool life_alive(u32 value) {
    return value >= LIFE_BORN;
}
static inline u8 life_get(const u32* grid, int x, int y) {
    return ((const u8*)grid)[y * LIFE_W + x];
}
static inline void life_set(u32* grid, int x, int y, u8 value) {
    ((u8*)grid)[y * LIFE_W + x] = value;
}
// x wrapped into 0 .. LIFE_W - 1, y into 0 .. LIFE_H - 1.
static inline int life_wrap_x(int x) {
    return ((x % LIFE_W) + LIFE_W) % LIFE_W;
}
static inline int life_wrap_y(int y) {
    return ((y % LIFE_H) + LIFE_H) % LIFE_H;
}

// --- Rules -------------------------------------------------------------------

// A rule as two masks over the number of live neighbours (0-8): bit n of
// `birth` makes an empty cell with n neighbours come alive, bit n of
// `survive` keeps a live one alive.
typedef struct {
    u16 birth, survive;
    const char* name;     // "CONWAY"
    const char* notation; // "B3/S23"
} LifeRule;

enum {
    LIFE_RULE_CONWAY,    // B3/S23
    LIFE_RULE_HIGHLIFE,  // B36/S23
    LIFE_RULE_DAY_NIGHT, // B3678/S34678
    LIFE_RULE_SEEDS,     // B2/S
    LIFE_RULE_MAZE,      // B3/S12345
    LIFE_RULE_NO_DEATH,  // B3/S012345678: life without death
    LIFE_RULE_COUNT
};
extern const LifeRule life_rules[LIFE_RULE_COUNT];

// What a cell becomes, by its value and the live cells in the 3 x 3 block
// around it, itself included (0-9): table[count * LIFE_STATES + value]. Built
// from a rule whenever the rule changes; the step reads nothing else.
#define LIFE_COUNTS 10
#define LIFE_TABLE_SIZE (LIFE_COUNTS * LIFE_STATES)
void life_table_build(u8* table, const LifeRule* rule);

// --- The step ---------------------------------------------------------------

typedef struct {
    u32 population; // live cells after the step
    u32 births;     // cells born in it
    u32 hash;       // of which cells are alive after it: the same world, the same hash
    u32 busy;       // words of four cells it had to work out (not empty and alone): its cost
} LifeStats;

#define LIFE_STATS_START ((LifeStats){0, 0, 5381u, 0})

// The surface's row length in bytes (SURFACE_W): the step writes the new
// grid's rows to a page as well, row y at page + y * LIFE_PAGE_STRIDE.
#define LIFE_PAGE_STRIDE 240

// One generation: `to` becomes the generation after `from` under `table`
// (life_table_build). `from` and `to` must be different grids. If `page` is
// not NULL, each new row is also written there (words only: it may be video
// memory). On the GBA it runs from IWRAM as ARM code.
LifeStats life_step(const u32* from, u32* to, u32* page, const u8* table);
// The same in parts: rows y0 to y1 - 1 of the next generation, adding to
// `st`, which starts as LIFE_STATS_START. Parts done in order from row 0 to
// the last give what life_step() gives (a generation over two frames: the
// grids must not change between the parts).
//
// With `from_rows` (not NULL), rows whose neighbourhood is empty are skipped
// (written empty, and only if `to_rows` says they weren't): from_rows[y] is 0
// only if row y of `from` is all empty cells (1 if it may not be), to_rows
// the same for `to`, which the step keeps up to date. life_rows_scan() sets
// them for a grid exactly; an edit must at least mark the rows it touches.
void life_step_rows(const u32* from, const u8* from_rows, u32* to, u8* to_rows, u32* page,
                    const u8* table, int y0, int y1, LifeStats* st);
void life_rows_scan(const u32* grid, u8* rows);

// Copies the grid into a page, row by row in words.
void life_draw(const u32* grid, u32* page);

// The live cells of a grid, and the same hash life_step() gives.
LifeStats life_count(const u32* grid);

// Clears the grid; fills it with a random soup, about 30% of its cells alive
// (19 in 64, newborn), seeded from random.h's sequence.
void life_clear(u32* grid);
void life_soup(u32* grid);

// --- Stamps ----------------------------------------------------------------

enum {
    LIFE_PAT_GLIDER,
    LIFE_PAT_LWSS,
    LIFE_PAT_R_PENTOMINO,
    LIFE_PAT_ACORN,
    LIFE_PAT_DIEHARD,
    LIFE_PAT_PULSAR,
    LIFE_PAT_PENTADECATHLON,
    LIFE_PAT_GOSPER_GUN,
    LIFE_PAT_COUNT
};
extern const char* const life_pattern_names[LIFE_PAT_COUNT];

#define LIFE_STAMP_MAX 40
// A pattern turned some quarter turns, as a box of cells.
typedef struct {
    u8 w, h;
    u8 cells[LIFE_STAMP_MAX][LIFE_STAMP_MAX]; // [y][x], 1 alive
} LifeStamp;

// Pattern `pattern` (LIFE_PAT_*) turned `turns` quarter turns clockwise.
void life_stamp_make(LifeStamp* out, u32 pattern, u32 turns);
// `in` a quarter turn clockwise: cell (x, y) of `out` is cell (y, h - 1 - x)
// of `in`, `out` h wide and w tall.
void life_stamp_rotate(const LifeStamp* in, LifeStamp* out);
// Where the stamp's top-left lands when centred on (cx, cy) (unwrapped).
static inline int life_stamp_left(const LifeStamp* s, int cx) {
    return cx - s->w / 2;
}
static inline int life_stamp_top(const LifeStamp* s, int cy) {
    return cy - s->h / 2;
}
// Puts the stamp into the grid centred on (cx, cy): its live cells newborn,
// the rest of its box empty, wrapping at the edges.
void life_stamp_put(u32* grid, const LifeStamp* s, int cx, int cy);

#endif // LIFE_RULES_H
