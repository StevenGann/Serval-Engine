// Checkers for the search: see checkers_eval.h.

#include "checkers_eval.h"

_Static_assert(sizeof(CheckersPosition) <= SEARCH_POS_BYTES,
               "the search's copy must hold a CheckersPosition");

// --- The evaluation -------------------------------------------------------------

// Tables from Dark's view (Dark's men start on 0-11 and move toward 28-31).
// Light's piece on square s reads entry 31 - s: the board turned half round.
#define MAN 100
#define KING 150
#define BACK_GUARD 10 // a man on its own back row while the other side has men

// A man's worth by how far it has come: a row at a time, more near crowning.
static const s8 man_row[8] = {0, 2, 4, 7, 11, 16, 22, 0};
// Squares by centrality: the four in the middle, then the ring around them.
static const s8 centre[32] = {
    0, 0, 0, 0, //
    0, 2, 2, 0, //
    2, 4, 4, 2, //
    2, 8, 8, 2, // 13, 14 (as printed 14, 15)
    2, 8, 8, 2, // 17, 18 (18, 19)
    2, 4, 4, 2, //
    0, 2, 2, 0, //
    0, 0, 0, 0, //
};

int ck_eval(const CheckersPosition* p) {
    int score[2] = {0, 0}, men[2] = {0, 0}, guards[2] = {0, 0};
    for (int s = 0; s < 32; s++) {
        u8 pc = p->board[s];
        if (pc == CK_EMPTY)
            continue;
        int c = CK_COLOR(pc) >> 2, t = c ? 31 - s : s;
        if (CK_KIND(pc) == CK_MAN) {
            men[c]++;
            score[c] += MAN + man_row[t >> 2] + centre[t];
            if (t < 4)
                guards[c]++;
        } else {
            score[c] += KING + centre[t];
        }
    }
    for (int c = 0; c < 2; c++)
        if (men[c ^ 1])
            score[c] += BACK_GUARD * guards[c];
    int s = score[0] - score[1];
    return ck_side(p) == CK_DARK ? s : -s;
}

// --- The search's table -----------------------------------------------------------

static int game_gen(void* pos, u32* out, bool noisy_only) {
    return noisy_only ? ck_gen_jumps(pos, out) : ck_gen_legal(pos, out);
}

static void game_make(void* pos, u32 m) {
    ck_make(pos, m);
}

static void game_unmake(void* pos) {
    ck_unmake(pos);
}

static int game_eval(const void* pos) {
    return ck_eval(pos);
}

static ChessKey game_key(const void* pos) {
    return ck_key(pos);
}

static int game_no_moves(const void* pos, int ply) {
    (void)pos;
    return -SEARCH_MATE + ply; // a side that cannot move has lost
}

// The forty-move rule, any repetition since the last capture or man move
// (once is enough inside the search), and the game's length cap.
static bool game_is_draw(void* pos) {
    const CheckersPosition* p = pos;
    int reach = p->clock < p->ply ? p->clock : p->ply;
    if (p->clock >= 80 || p->ply >= GAME_PLY_MAX)
        return true;
    ChessKey key = p->keys[p->ply];
    for (int back = 4; back <= reach; back += 2)
        if (p->keys[p->ply - back] == key)
            return true;
    return false;
}

// Jumps first, longer ones before shorter; then a man's move that crowns; then
// men's moves by how far they go, kings' last.
static int game_order(const void* pos, u32 m) {
    const CheckersPosition* p = pos;
    if (ck_move_is_jump(m))
        return SEARCH_ORDER_NOISY + ck_move_steps(m) * 10;
    u8 pc = p->board[CK_MOVE_FROM(m)];
    if (CK_KIND(pc) == CK_KING)
        return 100;
    int to = ck_move_to(m), t = CK_COLOR(pc) ? 31 - to : to;
    return t >= 28 ? 500 : 100 + man_row[t >> 2] + centre[t];
}

static int game_ply(const void* pos) {
    return ck_ply(pos);
}

const SearchGame checkers_search_game = {
    .pos_size = sizeof(CheckersPosition),
    .gen = game_gen,
    .make = game_make,
    .unmake = game_unmake,
    .eval = game_eval,
    .key = game_key,
    .no_moves_score = game_no_moves,
    .is_draw = game_is_draw,
    .order = game_order,
    .quiesce_in_check_all = false,
    .in_check = 0,
    .noisy_forced = true,
    .ply = game_ply,
    .frame_work = 890, // measured: HUNTER and SERVAL from a crowded middlegame, release build
};

// Measured on the GBA (release build, games played on from the middlegame
// B:W17,18,19,21-28,30:B1-3,5-7,9-14, about 1,600 nodes a second): CUB
// answers in about 0.3 s, at most 0.7 s, with its random margin and weak
// moves; HUNTER in about 2.4 s, at most 3 s (depth 5-10); SERVAL stops at
// 11,500 nodes, about 7.3 s, at most 7.8 s (8.2 in a gba-ci build): depth 12
// in that middlegame, 8-13 after.
static const SearchLevel levels[LEVEL_COUNT] = {
    [LEVEL_CUB] = {3, 1500, 40, 150, 0, 0},
    [LEVEL_HUNTER] = {10, 4000, 0, 0, 0, 0},
    [LEVEL_SERVAL] = {14, 11500, 0, 0, 8, 15},
};

const SearchLevel* checkers_level(int level) {
    return &levels[level >= 0 && level < LEVEL_COUNT ? level : LEVEL_CUB];
}
