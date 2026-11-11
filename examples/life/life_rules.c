// Life's rules and its step (life_rules.h). Portable C: the host tests run
// exactly this code; on the GBA life_step() runs from IWRAM as ARM code.

#include "life_rules.h"

#include <serval/random.h>

#define B(n) (1u << (n))

const LifeRule life_rules[LIFE_RULE_COUNT] = {
    [LIFE_RULE_CONWAY] = {B(3), B(2) | B(3), "CONWAY", "B3/S23"},
    [LIFE_RULE_HIGHLIFE] = {B(3) | B(6), B(2) | B(3), "HIGHLIFE", "B36/S23"},
    [LIFE_RULE_DAY_NIGHT] = {B(3) | B(6) | B(7) | B(8), B(3) | B(4) | B(6) | B(7) | B(8),
                             "DAY & NIGHT", "B3678/S34678"},
    [LIFE_RULE_SEEDS] = {B(2), 0, "SEEDS", "B2/S"},
    [LIFE_RULE_MAZE] = {B(3), B(1) | B(2) | B(3) | B(4) | B(5), "MAZE", "B3/S12345"},
    [LIFE_RULE_NO_DEATH] = {B(3), 0x1FF, "NO DEATH", "B3/S012345678"},
};

void life_table_build(u8* table, const LifeRule* rule) {
    for (u32 v = 0; v < LIFE_STATES; v++) {
        for (u32 count = 0; count < 16; count++) {
            u8 next;
            if (life_alive(v)) {
                u32 n = count - 1; // the count includes the cell itself
                if (count >= 1 && count <= 9 && (rule->survive >> n & 1))
                    next = (u8)(v < LIFE_OLDEST ? v + 1 : v);
                else
                    next = LIFE_TRAIL;
            } else if (count <= 8 && (rule->birth >> count & 1)) {
                next = LIFE_BORN;
            } else {
                next = (u8)(v ? v - 1 : 0);
            }
            table[v * 16 + count] = next;
        }
    }
}

// Four cells' "alive" as four bytes of 0 or 1: bit 5 of each (values are
// below 64).
static inline u32 alive4(u32 word) {
    return word >> 5 & 0x01010101u;
}

// The number of 1 bytes in a word of 0/1 bytes.
static inline u32 ones4(u32 bytes) {
    return bytes * 0x01010101u >> 24;
}

static inline u32 hash_mix(u32 hash, u32 alive) {
    return (hash ^ alive) * 16777619u;
}

// The step, a row at a time. For each word of four cells: the live cells of
// the three rows above, on and below it summed column by column (a byte per
// column, at most 3), then each column summed with its left and right
// neighbours (at most 9, so no byte carries into the next), which is each
// cell's 3 x 3 count with itself included; the table then gives each cell's
// next value from its own and that count.
SERVAL_IWRAM_CODE LifeStats life_step(const u32* from, u32* to, u32* page, const u8* table) {
    u32 rows[3][LIFE_ROW_WORDS]; // the alive bytes of the rows above, on and below
    u32 sums[LIFE_ROW_WORDS];
    u32* above = rows[0];
    u32* here = rows[1];
    u32* below = rows[2];
    const u32* last = from + (LIFE_H - 1) * LIFE_ROW_WORDS;
    for (int w = 0; w < LIFE_ROW_WORDS; w++) {
        above[w] = alive4(last[w]);
        here[w] = alive4(from[w]);
    }
    LifeStats st = {0, 0, 2166136261u};
    for (int y = 0; y < LIFE_H; y++) {
        const u32* cur = from + y * LIFE_ROW_WORDS;
        const u32* next = from + (y + 1 < LIFE_H ? y + 1 : 0) * LIFE_ROW_WORDS;
        for (int w = 0; w < LIFE_ROW_WORDS; w++) {
            below[w] = alive4(next[w]);
            sums[w] = above[w] + here[w] + below[w];
        }
        u32* out_row = to + y * LIFE_ROW_WORDS;
        u32* page_row = page ? page + y * (LIFE_PAGE_STRIDE / 4) : 0;
        u32 left_sum = sums[LIFE_ROW_WORDS - 1];
        for (int w = 0; w < LIFE_ROW_WORDS; w++) {
            u32 s = sums[w];
            u32 right_sum = sums[w + 1 < LIFE_ROW_WORDS ? w + 1 : 0];
            // The column to the left of each byte, and to the right (a
            // little-endian word: byte 0 is the leftmost cell).
            u32 count = s + (s << 8 | left_sum >> 24) + (s >> 8 | right_sum << 24);
            u32 old = cur[w];
            u32 out;
            if ((old | count) == 0) {
                out = 0;
            } else {
                out = table[(old & 63) << 4 | (count & 15)];
                out |= (u32)table[(old >> 8 & 63) << 4 | (count >> 8 & 15)] << 8;
                out |= (u32)table[(old >> 16 & 63) << 4 | (count >> 16 & 15)] << 16;
                out |= (u32)table[(old >> 24 & 63) << 4 | (count >> 24 & 15)] << 24;
                u32 now = alive4(out);
                if (now) {
                    st.population += ones4(now);
                    st.births += ones4(now & ~here[w]);
                }
            }
            st.hash = hash_mix(st.hash, alive4(out));
            out_row[w] = out;
            if (page_row)
                page_row[w] = out;
            left_sum = s;
        }
        u32* t = above;
        above = here;
        here = below;
        below = t;
    }
    return st;
}

void life_draw(const u32* grid, u32* page) {
    for (int y = 0; y < LIFE_H; y++) {
        const u32* src = grid + y * LIFE_ROW_WORDS;
        u32* dst = page + y * (LIFE_PAGE_STRIDE / 4);
        for (int w = 0; w < LIFE_ROW_WORDS; w++)
            dst[w] = src[w];
    }
}

LifeStats life_count(const u32* grid) {
    LifeStats st = {0, 0, 2166136261u};
    for (int i = 0; i < LIFE_WORDS; i++) {
        u32 a = alive4(grid[i]);
        st.population += ones4(a);
        st.hash = hash_mix(st.hash, a);
    }
    return st;
}

void life_clear(u32* grid) {
    for (int i = 0; i < LIFE_WORDS; i++)
        grid[i] = 0;
}

void life_soup(u32* grid, u32 percent) {
    u8* cells = (u8*)grid;
    u32 threshold = percent * 65536u / 100u;
    for (int i = 0; i < LIFE_CELLS; i++)
        cells[i] = (random_u32() >> 16) < threshold ? LIFE_BORN : LIFE_EMPTY;
}

// --- Stamps ----------------------------------------------------------------

const char* const life_pattern_names[LIFE_PAT_COUNT] = {
    [LIFE_PAT_GLIDER] = "GLIDER",
    [LIFE_PAT_LWSS] = "SPACESHIP",
    [LIFE_PAT_R_PENTOMINO] = "R-PENTOMINO",
    [LIFE_PAT_ACORN] = "ACORN",
    [LIFE_PAT_DIEHARD] = "DIEHARD",
    [LIFE_PAT_PULSAR] = "PULSAR",
    [LIFE_PAT_PENTADECATHLON] = "PENTADECATHLON",
    [LIFE_PAT_GOSPER_GUN] = "GLIDER GUN",
};

static const char* const glider[] = {".#.", "..#", "###"};
static const char* const lwss[] = {".#..#", "#....", "#...#", "####."};
static const char* const r_pentomino[] = {".##", "##.", ".#."};
static const char* const acorn[] = {".#.....", "...#...", "##..###"};
static const char* const diehard[] = {"......#.", "##......", ".#...###"};
static const char* const pulsar[] = {
    "..###...###..", ".............", "#....#.#....#", "#....#.#....#", "#....#.#....#",
    "..###...###..", ".............", "..###...###..", "#....#.#....#", "#....#.#....#",
    "#....#.#....#", ".............", "..###...###..",
};
// A row of ten whose third and eighth cells have moved to above and below.
static const char* const pentadecathlon[] = {"..#....#..", "##.####.##", "..#....#.."};
static const char* const gosper_gun[] = {
    "........................#...........", "......................#.#...........",
    "............##......##............##", "...........#...#....##............##",
    "##........#.....#...##..............", "##........#...#.##....#.#...........",
    "..........#.....#.......#...........", "...........#...#....................",
    "............##......................",
};

static const struct {
    const char* const* rows;
    u8 h;
} patterns[LIFE_PAT_COUNT] = {
    [LIFE_PAT_GLIDER] = {glider, 3},
    [LIFE_PAT_LWSS] = {lwss, 4},
    [LIFE_PAT_R_PENTOMINO] = {r_pentomino, 3},
    [LIFE_PAT_ACORN] = {acorn, 3},
    [LIFE_PAT_DIEHARD] = {diehard, 3},
    [LIFE_PAT_PULSAR] = {pulsar, 13},
    [LIFE_PAT_PENTADECATHLON] = {pentadecathlon, 3},
    [LIFE_PAT_GOSPER_GUN] = {gosper_gun, 9},
};

void life_stamp_rotate(const LifeStamp* in, LifeStamp* out) {
    out->w = in->h;
    out->h = in->w;
    for (int y = 0; y < out->h; y++)
        for (int x = 0; x < out->w; x++)
            out->cells[y][x] = in->cells[in->h - 1 - x][y];
}

void life_stamp_make(LifeStamp* out, u32 pattern, u32 turns) {
    if (pattern >= LIFE_PAT_COUNT)
        pattern = LIFE_PAT_GLIDER;
    const char* const* rows = patterns[pattern].rows;
    out->h = patterns[pattern].h;
    out->w = 0;
    while (rows[0][out->w])
        out->w++;
    for (int y = 0; y < out->h; y++)
        for (int x = 0; x < out->w; x++)
            out->cells[y][x] = rows[y][x] == '#';
    static LifeStamp spare;
    for (u32 t = 0; t < (turns & 3); t++) {
        life_stamp_rotate(out, &spare);
        *out = spare;
    }
}

void life_stamp_put(u32* grid, const LifeStamp* s, int cx, int cy) {
    int left = life_stamp_left(s, cx), top = life_stamp_top(s, cy);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++)
            life_set(grid, life_wrap_x(left + x), life_wrap_y(top + y),
                     s->cells[y][x] ? LIFE_BORN : LIFE_EMPTY);
}
