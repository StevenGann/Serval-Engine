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
    for (u32 count = 0; count < LIFE_COUNTS; count++) {
        for (u32 v = 0; v < LIFE_STATES; v++) {
            u8 next;
            if (life_alive(v)) {
                // The count includes the cell itself.
                if (count >= 1 && (rule->survive >> (count - 1) & 1))
                    next = (u8)(v < LIFE_OLDEST ? v + 1 : v);
                else
                    next = LIFE_TRAIL;
            } else if (rule->birth >> count & 1) {
                next = LIFE_BORN;
            } else {
                next = (u8)(v ? v - 1 : 0);
            }
            table[count * LIFE_STATES + v] = next;
        }
    }
}

// Four cells' "alive" as four bytes of 0 or 1: bit 5 of each (values are
// below 64).
static inline u32 alive4(u32 word) {
    return word >> 5 & 0x01010101u;
}

// The sum of a word's four bytes, if it is below 256.
static inline u32 bytes_sum(u32 bytes) {
    return bytes * 0x01010101u >> 24;
}

// The hash: (h + a) * 33 for each word's alive bytes a in turn, so where a
// cell is counts as much as whether it is alive (an add and a shifted add a
// word).
static inline u32 hash_mix(u32 hash, u32 alive) {
    hash += alive;
    return hash + (hash << 5);
}

// The step, a row at a time. For each word of four cells: the live cells of
// the three rows above, on and below it summed column by column (a byte per
// column, at most 3), then each column summed with its left and right
// neighbours (at most 9, so no byte carries into the next), which is each
// cell's 3 x 3 count with itself included; the table then gives each cell's
// next value from its own and that count, two cells' table indices built at
// once in a word's two halves.
//
// Each row of the grid is read once, into a ring of three rows on the stack
// (IWRAM), with the row's column sums. A column's sum is taken
// from the cells as they are: bit 5 (alive) masked out of each of the three
// words, added (at most 0x60 a byte) and shifted down. The population and
// births are summed a byte per column and added up once a row.
#define ALIVE_BITS 0x20202020u
static inline u32 column_sums(u32 a, u32 b, u32 c) {
    return ((a & ALIVE_BITS) + (b & ALIVE_BITS) + (c & ALIVE_BITS)) >> 5;
}

// (h + 0) * 33, thirty times: an empty row's words.
#define HASH_EMPTY_ROW 0xCCC4CFC1u

SERVAL_IWRAM_CODE void life_step_rows(const u32* from, const u8* from_rows, u32* to, u8* to_rows,
                                      u32* page, const u8* table, int y0, int y1, LifeStats* st) {
    u32 ring[3][LIFE_ROW_WORDS];
    u32 sums[LIFE_ROW_WORDS + 1]; // and the first again, after the last
    u32* above = ring[0];
    u32* here = ring[1];
    u32* below = ring[2];
    const u32* first = from + y0 * LIFE_ROW_WORDS;
    const u32* before = from + (y0 ? y0 - 1 : LIFE_H - 1) * LIFE_ROW_WORDS;
    for (int w = 0; w < LIFE_ROW_WORDS; w++) {
        above[w] = before[w];
        here[w] = first[w];
    }
    u32 hash = st->hash;
    for (int y = y0; y < y1; y++) {
        int next_y = y + 1 < LIFE_H ? y + 1 : 0;
        u32* out_row = to + y * LIFE_ROW_WORDS;
        u32* page_row = page ? page + y * (LIFE_PAGE_STRIDE / 4) : out_row;
        if (from_rows && !from_rows[y ? y - 1 : LIFE_H - 1] && !from_rows[y] &&
            !from_rows[next_y]) {
            // Nothing in this row or beside it: it stays empty. The ring's
            // next two rows are empty too.
            if (to_rows[y]) {
                for (int w = 0; w < LIFE_ROW_WORDS; w++)
                    out_row[w] = 0;
                to_rows[y] = 0;
            }
            for (int w = 0; w < LIFE_ROW_WORDS; w++) {
                page_row[w] = 0;
                here[w] = 0;
                below[w] = 0;
            }
            hash *= HASH_EMPTY_ROW;
        } else {
            const u32* src = from + next_y * LIFE_ROW_WORDS;
            for (int w = 0; w < LIFE_ROW_WORDS; w++) {
                u32 cells = src[w];
                below[w] = cells;
                sums[w] = column_sums(above[w], here[w], cells);
            }
            sums[LIFE_ROW_WORDS] = sums[0];
            // The row's left and right neighbours wrap around.
            u32 left_sum = sums[LIFE_ROW_WORDS - 1];
            u32 sum = sums[0];
            u32 born = 0, living = 0, busy = 0;
            for (int w = 0; w < LIFE_ROW_WORDS; w++) {
                u32 right_sum = sums[w + 1];
                // The column to the left of each byte, and to the right (a
                // little-endian word: byte 0 is the leftmost cell).
                u32 count = sum + (sum << 8 | left_sum >> 24) + (sum >> 8 | right_sum << 24);
                u32 old = here[w];
                u32 out = 0;
                if (old | count) {
                    // Cells 0 and 2 in one word, 1 and 3 in the other:
                    // count * 64 + value, at most 639 (values are below 64
                    // and counts below 10, so one mask does for both).
                    u32 even = (old & 0x00FF00FFu) | (count & 0x00FF00FFu) << 6;
                    u32 odd = (old >> 8 & 0x00FF00FFu) | (count >> 8 & 0x00FF00FFu) << 6;
                    out = (u32)table[even & 0xFFFF] | (u32)table[odd & 0xFFFF] << 8 |
                          (u32)table[even >> 16] << 16 | (u32)table[odd >> 16] << 24;
                    u32 now = alive4(out);
                    living += now;
                    born += now & ~alive4(old);
                    hash += now;
                    busy++;
                }
                hash = hash_mix(hash, 0);
                out_row[w] = out;
                page_row[w] = out;
                left_sum = sum;
                sum = right_sum;
            }
            st->population += bytes_sum(living); // each byte at most 30
            st->births += bytes_sum(born);
            st->busy += busy;
            // Busy words that came out empty (trails gone) leave the row
            // marked: a row marked but empty costs only the time.
            if (to_rows)
                to_rows[y] = busy != 0;
        }
        u32* t = above;
        above = here;
        here = below;
        below = t;
    }
    st->hash = hash;
}

LifeStats life_step(const u32* from, u32* to, u32* page, const u8* table) {
    LifeStats st = LIFE_STATS_START;
    life_step_rows(from, NULL, to, NULL, page, table, 0, LIFE_H, &st);
    return st;
}

void life_rows_scan(const u32* grid, u8* rows) {
    for (int y = 0; y < LIFE_H; y++) {
        u32 any = 0;
        for (int w = 0; w < LIFE_ROW_WORDS; w++)
            any |= grid[y * LIFE_ROW_WORDS + w];
        rows[y] = any != 0;
    }
}

void life_draw(const u32* grid, u32* page) {
    for (int y = 0; y < LIFE_H; y++) {
        const u32* src = grid + y * LIFE_ROW_WORDS;
        u32* dst = page + y * (LIFE_PAGE_STRIDE / 4);
        for (int w = 0; w < LIFE_ROW_WORDS; w++)
            dst[w] = src[w];
    }
}

// In IWRAM too: every stamp, clear and soup counts the world.
SERVAL_IWRAM_CODE LifeStats life_count(const u32* grid) {
    LifeStats st = LIFE_STATS_START;
    for (int y = 0; y < LIFE_H; y++) {
        u32 living = 0;
        for (int w = 0; w < LIFE_ROW_WORDS; w++) {
            u32 a = alive4(*grid++);
            living += a;
            st.hash = hash_mix(st.hash, a);
        }
        st.population += bytes_sum(living);
    }
    return st;
}

void life_clear(u32* grid) {
    for (int i = 0; i < LIFE_WORDS; i++)
        grid[i] = 0;
}

void life_soup(u32* grid) {
    // random.h's generator costs too much for 9,600 cells: it seeds a
    // xorshift, whose every number makes four cells, each alive if two of
    // its byte's bits are both set or four others are (1/4 + 3/4 * 1/16).
    u32 x = random_u32() | 1u;
    for (int i = 0; i < LIFE_WORDS; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        u32 alive = (x & x >> 1) | (x >> 2 & x >> 3 & x >> 4 & x >> 5);
        grid[i] = (alive & 0x01010101u) * LIFE_BORN;
    }
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
    static LifeStamp spare SERVAL_EWRAM_BSS;
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
