// life: the Game of Life example's rules (examples/life/life_rules.c) and
// save (life_save.c), natively. The word-at-a-time step is checked against a
// plain cell-by-cell one written here from the rules' definition.

#include "test.h"

#include <string.h>

#include <serval/random.h>

#include "../examples/life/life_rules.h"
#include "../examples/life/life_save.h"

static LifeGrid grid_a, grid_b;
static u8 table[LIFE_TABLE_SIZE];

static void use_rule(u32 rule) {
    life_table_build(table, &life_rules[rule]);
}

// Places cells from rows of '#' and '.' with the top-left at (x, y), wrapping.
static void put(u32* grid, int x, int y, const char* const* rows, int count) {
    for (int r = 0; r < count; r++)
        for (int c = 0; rows[r][c]; c++)
            if (rows[r][c] == '#')
                life_set(grid, life_wrap_x(x + c), life_wrap_y(y + r), LIFE_BORN);
}

// `n` generations from grid_a; the result is left in grid_a.
static LifeStats run(int n) {
    LifeStats st = LIFE_STATS_START;
    for (int i = 0; i < n; i++) {
        st = life_step(grid_a, grid_b, NULL, table);
        memcpy(grid_a, grid_b, sizeof grid_a);
    }
    return st;
}

static bool alive_at(const u32* grid, int x, int y) {
    return life_alive(life_get(grid, life_wrap_x(x), life_wrap_y(y)));
}

// The live cells of `grid` are exactly those of `rows` placed at (x, y).
static bool only(const u32* grid, int x, int y, const char* const* rows, int count) {
    static LifeGrid want;
    life_clear(want);
    put(want, x, y, rows, count);
    for (int yy = 0; yy < LIFE_H; yy++)
        for (int xx = 0; xx < LIFE_W; xx++)
            if (life_alive(life_get(grid, xx, yy)) != life_alive(life_get(want, xx, yy)))
                return false;
    return true;
}

static const char* const blinker_h[] = {"###"};
static const char* const blinker_v[] = {"#", "#", "#"};
static const char* const block[] = {"##", "##"};
static const char* const glider[] = {".#.", "..#", "###"};

static void test_blinker_period_2(void) {
    use_rule(LIFE_RULE_CONWAY);
    life_clear(grid_a);
    put(grid_a, 10, 10, blinker_h, 1);
    LifeStats st = run(1);
    CHECK(only(grid_a, 11, 9, blinker_v, 3));
    CHECK(st.population == 3 && st.births == 2);
    run(1);
    CHECK(only(grid_a, 10, 10, blinker_h, 1));
}

static void test_block_still(void) {
    use_rule(LIFE_RULE_CONWAY);
    life_clear(grid_a);
    put(grid_a, 50, 40, block, 2);
    LifeStats before = life_count(grid_a);
    LifeStats st = run(1);
    CHECK(st.population == 4 && st.births == 0);
    CHECK(st.hash == before.hash);
    CHECK(only(grid_a, 50, 40, block, 2));
}

static void test_glider_moves(void) {
    use_rule(LIFE_RULE_CONWAY);
    life_clear(grid_a);
    put(grid_a, 20, 30, glider, 3);
    run(4);
    CHECK(only(grid_a, 21, 31, glider, 3));
    // Across the corner: from the bottom-right edge to the top-left.
    life_clear(grid_a);
    put(grid_a, LIFE_W - 2, LIFE_H - 2, glider, 3);
    run(4);
    CHECK(only(grid_a, LIFE_W - 1, LIFE_H - 1, glider, 3));
    run(8);
    CHECK(only(grid_a, 1, 1, glider, 3));
    CHECK(life_count(grid_a).population == 5);
}

static void test_highlife_b6(void) {
    // An empty cell with six live neighbours: born in HighLife, not in Conway.
    static const char* const six[] = {"###", "...", "###"};
    use_rule(LIFE_RULE_CONWAY);
    life_clear(grid_a);
    put(grid_a, 60, 40, six, 3);
    run(1);
    CHECK(!alive_at(grid_a, 61, 41));
    use_rule(LIFE_RULE_HIGHLIFE);
    life_clear(grid_a);
    put(grid_a, 60, 40, six, 3);
    run(1);
    CHECK(alive_at(grid_a, 61, 41));
}

static void test_seeds(void) {
    // B2/S: every live cell dies; a domino's two births on each side.
    static const char* const domino[] = {"##"};
    use_rule(LIFE_RULE_SEEDS);
    life_clear(grid_a);
    put(grid_a, 30, 30, domino, 1);
    LifeStats st = run(1);
    CHECK(!alive_at(grid_a, 30, 30) && !alive_at(grid_a, 31, 30));
    CHECK(alive_at(grid_a, 30, 29) && alive_at(grid_a, 31, 29));
    CHECK(alive_at(grid_a, 30, 31) && alive_at(grid_a, 31, 31));
    CHECK(st.population == 4 && st.births == 4);
}

static void test_ages_cap(void) {
    use_rule(LIFE_RULE_CONWAY);
    life_clear(grid_a);
    put(grid_a, 0, 0, block, 2); // across nothing: the corner
    run(1);
    CHECK(life_get(grid_a, 0, 0) == LIFE_BORN + 1);
    run(LIFE_OLDEST - LIFE_BORN - 1);
    CHECK(life_get(grid_a, 1, 1) == LIFE_OLDEST);
    run(100);
    CHECK(life_get(grid_a, 0, 0) == LIFE_OLDEST && life_get(grid_a, 1, 0) == LIFE_OLDEST);
    CHECK(life_get(grid_a, 0, 1) == LIFE_OLDEST && life_get(grid_a, 1, 1) == LIFE_OLDEST);
}

static void test_trails_fade(void) {
    use_rule(LIFE_RULE_CONWAY);
    life_clear(grid_a);
    life_set(grid_a, 70, 20, LIFE_BORN);
    LifeStats st = run(1);
    CHECK(st.population == 0);
    CHECK(life_get(grid_a, 70, 20) == LIFE_TRAIL);
    run(1);
    CHECK(life_get(grid_a, 70, 20) == LIFE_TRAIL - 1);
    run(LIFE_TRAIL - 2);
    CHECK(life_get(grid_a, 70, 20) == 1);
    run(1);
    CHECK(life_get(grid_a, 70, 20) == LIFE_EMPTY);
    run(5);
    CHECK(life_get(grid_a, 70, 20) == LIFE_EMPTY);
    // A trail is empty to the rules: three trails around a cell bear nothing.
    life_clear(grid_a);
    life_set(grid_a, 5, 5, LIFE_TRAIL);
    life_set(grid_a, 6, 5, LIFE_TRAIL);
    life_set(grid_a, 7, 5, LIFE_TRAIL);
    run(1);
    CHECK(life_count(grid_a).population == 0);
}

// The step against cell-by-cell counting, on soups, for every rule, and the
// page it writes against the grid.
static void test_step_matches_reference(void) {
    static LifeGrid want;
    static u32 page[LIFE_PAGE_STRIDE / 4 * LIFE_H];
    random_seed(77);
    for (u32 rule = 0; rule < LIFE_RULE_COUNT; rule++) {
        use_rule(rule);
        const LifeRule* r = &life_rules[rule];
        life_soup(grid_a);
        for (int gen = 0; gen < 6; gen++) {
            u32 pop = 0, births = 0;
            for (int y = 0; y < LIFE_H; y++) {
                for (int x = 0; x < LIFE_W; x++) {
                    int n = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                            if ((dx || dy) && alive_at(grid_a, x + dx, y + dy))
                                n++;
                    u8 v = life_get(grid_a, x, y), next;
                    if (life_alive(v))
                        next = r->survive >> n & 1 ? (u8)(v < LIFE_OLDEST ? v + 1 : v)
                                                   : (u8)LIFE_TRAIL;
                    else if (r->birth >> n & 1)
                        next = LIFE_BORN;
                    else
                        next = v ? (u8)(v - 1) : (u8)0;
                    life_set(want, x, y, next);
                    pop += life_alive(next);
                    births += !life_alive(v) && life_alive(next);
                }
            }
            memset(page, 0xAA, sizeof page);
            LifeStats st = life_step(grid_a, grid_b, page, table);
            CHECK(memcmp(grid_b, want, sizeof want) == 0);
            CHECK(st.population == pop && st.births == births);
            CHECK(st.hash == life_count(grid_b).hash);
            bool same = true;
            for (int y = 0; y < LIFE_H; y++) {
                const u8* row = (const u8*)page + y * LIFE_PAGE_STRIDE;
                same = same && memcmp(row, (const u8*)grid_b + y * LIFE_W, LIFE_W) == 0;
                same = same && row[LIFE_W] == 0xAA; // the rest of the page untouched
            }
            CHECK(same);
            memcpy(grid_a, grid_b, sizeof grid_a);
        }
    }
}

static void test_step_in_parts(void) {
    static LifeGrid whole;
    random_seed(31);
    use_rule(LIFE_RULE_CONWAY);
    life_soup(grid_a);
    LifeStats a = life_step(grid_a, whole, NULL, table);
    LifeStats b = LIFE_STATS_START;
    life_step_rows(grid_a, NULL, grid_b, NULL, NULL, table, 0, 37, &b);
    life_step_rows(grid_a, NULL, grid_b, NULL, NULL, table, 37, LIFE_H, &b);
    CHECK(memcmp(whole, grid_b, sizeof whole) == 0);
    CHECK(a.population == b.population && a.births == b.births && a.hash == b.hash);
    CHECK(a.busy == b.busy && a.busy > 0 && a.busy <= LIFE_WORDS);
    // An empty world: nothing to work out.
    life_clear(grid_a);
    CHECK(life_step(grid_a, grid_b, NULL, table).busy == 0);
}

// Skipping the rows with nothing near: the same generations, the same
// stats, for a world with empty bands (and the rows marks kept right).
static void test_step_skips_empty_rows(void) {
    static LifeGrid plain_a, plain_b, skip_a, skip_b;
    static u8 rows_a[LIFE_H], rows_b[LIFE_H];
    static u32 page[LIFE_PAGE_STRIDE / 4 * LIFE_H];
    use_rule(LIFE_RULE_CONWAY);
    life_clear(plain_a);
    static LifeStamp s;
    life_stamp_make(&s, LIFE_PAT_GOSPER_GUN, 0);
    life_stamp_put(plain_a, &s, 40, 20);
    life_stamp_make(&s, LIFE_PAT_ACORN, 0);
    life_stamp_put(plain_a, &s, 80, 60);
    life_set(plain_a, 3, 79, LIFE_BORN); // a lone cell on the last row: wraps to the first
    memcpy(skip_a, plain_a, sizeof plain_a);
    life_rows_scan(skip_a, rows_a);
    memset(rows_b, 1, sizeof rows_b);
    memset(skip_b, 0x21, sizeof skip_b); // garbage the step must clear
    bool same = true;
    for (int gen = 0; gen < 120; gen++) {
        memset(page, 0x55, sizeof page);
        LifeStats p = life_step(plain_a, plain_b, NULL, table);
        LifeStats q = LIFE_STATS_START;
        life_step_rows(skip_a, rows_a, skip_b, rows_b, page, table, 0, 40, &q);
        life_step_rows(skip_a, rows_a, skip_b, rows_b, page, table, 40, LIFE_H, &q);
        same = same && memcmp(plain_b, skip_b, sizeof plain_b) == 0;
        same = same && p.population == q.population && p.births == q.births && p.hash == q.hash;
        same = same && q.busy <= p.busy;
        for (int y = 0; y < LIFE_H; y++) {
            same = same && memcmp((const u8*)page + y * LIFE_PAGE_STRIDE,
                                  (const u8*)skip_b + y * LIFE_W, LIFE_W) == 0;
            u32 any = 0;
            for (int w = 0; w < LIFE_ROW_WORDS; w++)
                any |= skip_b[y * LIFE_ROW_WORDS + w];
            same = same && (any == 0 || rows_b[y]); // never marked empty wrongly
        }
        memcpy(plain_a, plain_b, sizeof plain_a);
        memcpy(skip_a, skip_b, sizeof skip_a);
        u8 t[LIFE_H];
        memcpy(t, rows_a, sizeof t);
        memcpy(rows_a, rows_b, sizeof t);
        memcpy(rows_b, t, sizeof t);
    }
    CHECK(same);
}

static void test_draw_and_hash(void) {
    static u32 page[LIFE_PAGE_STRIDE / 4 * LIFE_H];
    random_seed(5);
    life_soup(grid_a);
    life_draw(grid_a, page);
    CHECK(memcmp((const u8*)page + 7 * LIFE_PAGE_STRIDE, (const u8*)grid_a + 7 * LIFE_W, LIFE_W) ==
          0);
    // The hash tells worlds apart by where their cells are.
    life_clear(grid_a);
    life_set(grid_a, 3, 3, LIFE_BORN);
    u32 h1 = life_count(grid_a).hash;
    life_clear(grid_a);
    life_set(grid_a, 4, 3, LIFE_BORN);
    CHECK(life_count(grid_a).hash != h1);
    // A soup is about 30% alive.
    life_soup(grid_a);
    u32 pop = life_count(grid_a).population;
    CHECK(pop > LIFE_CELLS * 25 / 100 && pop < LIFE_CELLS * 35 / 100);
}

static bool stamps_equal(const LifeStamp* a, const LifeStamp* b) {
    if (a->w != b->w || a->h != b->h)
        return false;
    for (int y = 0; y < a->h; y++)
        for (int x = 0; x < a->w; x++)
            if (a->cells[y][x] != b->cells[y][x])
                return false;
    return true;
}

static void test_stamp_rotation(void) {
    static LifeStamp s, t, u;
    for (u32 p = 0; p < LIFE_PAT_COUNT; p++) {
        life_stamp_make(&s, p, 0);
        CHECK(s.w > 0 && s.h > 0 && s.w <= LIFE_STAMP_MAX && s.h <= LIFE_STAMP_MAX);
        CHECK(life_pattern_names[p] != NULL);
        t = s;
        for (int i = 0; i < 4; i++) {
            life_stamp_rotate(&t, &u);
            t = u;
        }
        CHECK(stamps_equal(&s, &t));
        life_stamp_make(&t, p, 1);
        life_stamp_rotate(&s, &u);
        CHECK(stamps_equal(&t, &u));
        CHECK(t.w == s.h && t.h == s.w);
    }
    // The glider turned once clockwise.
    life_stamp_make(&s, LIFE_PAT_GLIDER, 1);
    static const char* const turned[] = {"#..", "#.#", "##."};
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++)
            CHECK(s.cells[y][x] == (turned[y][x] == '#'));
    // Pattern sizes: the gun is 36 x 9.
    life_stamp_make(&s, LIFE_PAT_GOSPER_GUN, 0);
    CHECK(s.w == 36 && s.h == 9);
}

static void test_stamp_put(void) {
    static LifeStamp s;
    use_rule(LIFE_RULE_CONWAY);
    life_clear(grid_a);
    life_set(grid_a, 40, 40, LIFE_OLDEST); // inside the glider's box, dead in it: cleared
    life_stamp_make(&s, LIFE_PAT_GLIDER, 0);
    life_stamp_put(grid_a, &s, 41, 41);
    CHECK(only(grid_a, 40, 40, glider, 3));
    CHECK(life_get(grid_a, 41, 41) == LIFE_EMPTY && life_get(grid_a, 42, 41) == LIFE_BORN);
    // Wrapping at the corner.
    life_clear(grid_a);
    life_stamp_put(grid_a, &s, 0, 0);
    CHECK(only(grid_a, -1, -1, glider, 3));
    // The gun fires a glider within 30 generations: more cells than it has.
    life_clear(grid_a);
    life_stamp_make(&s, LIFE_PAT_GOSPER_GUN, 0);
    life_stamp_put(grid_a, &s, 60, 40);
    CHECK(life_count(grid_a).population == 36);
    run(30);
    CHECK(life_count(grid_a).population == 36 + 5);
}

static void test_save_round_trip(void) {
    static u8 bytes[LIFE_SAVE_BYTES];
    static LifeGrid back;
    random_seed(9);
    life_soup(grid_a);
    life_set(grid_a, 1, 1, 5); // a trail: saved as empty
    LifeSettings s = {LIFE_RULE_MAZE, 3, 6, 1, LIFE_PAT_ACORN, 2, 123456};
    life_save_pack(bytes, &s, grid_a);
    LifeSettings t;
    CHECK(life_save_unpack(bytes, &t, back));
    CHECK(t.rule == s.rule && t.theme == s.theme && t.speed == s.speed && t.sound == s.sound);
    CHECK(t.stamp == s.stamp && t.turns == s.turns && t.generation == s.generation);
    bool same = true;
    for (int y = 0; y < LIFE_H; y++)
        for (int x = 0; x < LIFE_W; x++) {
            u8 v = life_get(back, x, y);
            same = same && (life_alive(life_get(grid_a, x, y)) ? v == LIFE_OLDEST : v == 0);
        }
    CHECK(same);
    CHECK(bytes[8] == (123456 & 255) && bytes[9] == (123456 >> 8 & 255));
}

static void test_save_refuses(void) {
    static u8 bytes[LIFE_SAVE_BYTES];
    static LifeGrid back;
    life_clear(grid_a);
    LifeSettings s = {0, 0, 0, 0, 0, 0, 0};
    life_save_pack(bytes, &s, grid_a);
    static const struct {
        u8 at, value;
    } bad[] = {{0, LIFE_RULE_COUNT},
               {1, LIFE_THEMES},
               {2, LIFE_SPEEDS},
               {3, 1},
               {4, 2},
               {5, LIFE_PAT_COUNT},
               {6, 4},
               {7, 1}};
    for (u32 i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        static u8 broken[LIFE_SAVE_BYTES];
        memcpy(broken, bytes, sizeof broken);
        broken[bad[i].at] = bad[i].value;
        LifeSettings t = {9, 9, 9, 9, 9, 9, 9};
        life_set(back, 0, 0, 7);
        CHECK(!life_save_unpack(broken, &t, back));
        CHECK(t.rule == 9 && life_get(back, 0, 0) == 7); // left alone
    }
    LifeSettings t;
    CHECK(life_save_unpack(bytes, &t, back));
}

TEST_SUITE(life_tests, "life", {"life_blinker_period_2", test_blinker_period_2},
           {"life_block_still", test_block_still}, {"life_glider_moves", test_glider_moves},
           {"life_highlife_b6", test_highlife_b6}, {"life_seeds", test_seeds},
           {"life_ages_cap", test_ages_cap}, {"life_trails_fade", test_trails_fade},
           {"life_step_matches_reference", test_step_matches_reference},
           {"life_step_in_parts", test_step_in_parts},
           {"life_step_skips_empty_rows", test_step_skips_empty_rows},
           {"life_draw_and_hash", test_draw_and_hash}, {"life_stamp_rotation", test_stamp_rotation},
           {"life_stamp_put", test_stamp_put}, {"life_save_round_trip", test_save_round_trip},
           {"life_save_refuses", test_save_refuses});
