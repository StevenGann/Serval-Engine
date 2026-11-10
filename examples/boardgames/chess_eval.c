// Chess for the search: see chess_eval.h.

#include "chess_eval.h"

_Static_assert(sizeof(Position) <= SEARCH_POS_BYTES, "the search's copy must hold a Position");

// --- Piece-square tables --------------------------------------------------------

// White's view, written as the board is printed: the first row is rank 8.
// A white piece on square sq (a1 = 0) reads entry sq ^ 56, a black one entry
// sq, so the tables serve both colours mirrored.
static const s8 pst_pawn[64] = {
    0,  0,  0,  0,   0,   0,  0,  0,  //
    60, 60, 60, 64,  64,  60, 60, 60, //
    22, 24, 28, 36,  36,  28, 24, 22, //
    6,  8,  12, 26,  26,  12, 8,  6,  //
    2,  4,  8,  22,  22,  6,  4,  2,  //
    4,  2,  2,  6,   6,   -2, 2,  4,  //
    4,  8,  8,  -12, -12, 8,  8,  4,  //
    0,  0,  0,  0,   0,   0,  0,  0,  //
};
static const s8 pst_knight[64] = {
    -50, -36, -28, -24, -24, -28, -36, -50, //
    -34, -14, 0,   4,   4,   0,   -14, -34, //
    -26, 4,   12,  16,  16,  12,  4,   -26, //
    -24, 6,   16,  22,  22,  16,  6,   -24, //
    -24, 2,   14,  20,  20,  14,  2,   -24, //
    -28, 2,   10,  12,  12,  10,  2,   -28, //
    -36, -16, -2,  4,   4,   -2,  -16, -36, //
    -48, -30, -26, -22, -22, -26, -30, -48, //
};
static const s8 pst_bishop[64] = {
    -18, -10, -10, -10, -10, -10, -10, -18, //
    -10, 2,   0,   0,   0,   0,   2,   -10, //
    -10, 0,   6,   10,  10,  6,   0,   -10, //
    -10, 6,   6,   12,  12,  6,   6,   -10, //
    -10, 2,   12,  12,  12,  12,  2,   -10, //
    -10, 10,  10,  10,  10,  10,  10,  -10, //
    -10, 8,   2,   2,   2,   2,   8,   -10, //
    -18, -10, -12, -10, -10, -12, -10, -18, //
};
static const s8 pst_rook[64] = {
    4,  6,  6,  8,  8,  6,  6,  4,  //
    10, 14, 14, 14, 14, 14, 14, 10, //
    -4, 0,  2,  2,  2,  2,  0,  -4, //
    -6, 0,  0,  2,  2,  0,  0,  -6, //
    -6, 0,  0,  2,  2,  0,  0,  -6, //
    -6, 0,  0,  2,  2,  0,  0,  -6, //
    -8, -2, 0,  0,  0,  0,  -2, -8, //
    -2, 0,  2,  8,  8,  4,  0,  -2, //
};
static const s8 pst_queen[64] = {
    -18, -10, -8, -4, -4, -8, -10, -18, //
    -10, 0,   2,  2,  2,  2,  0,   -10, //
    -8,  2,   6,  6,  6,  6,  2,   -8,  //
    -4,  0,   6,  8,  8,  6,  0,   -4,  //
    -2,  0,   6,  8,  8,  6,  0,   -4,  //
    -8,  4,   6,  6,  6,  6,  2,   -8,  //
    -10, 0,   4,  0,  0,  0,  0,   -10, //
    -18, -10, -8, -2, -6, -8, -10, -18, //
};
// The king hides in the middlegame and comes to the centre in the endgame.
static const s8 pst_king_mid[64] = {
    -32, -42, -42, -52, -52, -42, -42, -32, //
    -32, -42, -42, -52, -52, -42, -42, -32, //
    -32, -42, -42, -52, -52, -42, -42, -32, //
    -30, -40, -40, -50, -50, -40, -40, -30, //
    -22, -30, -30, -40, -40, -30, -30, -22, //
    -12, -22, -22, -24, -24, -22, -22, -12, //
    16,  16,  -4,  -6,  -6,  -4,  16,  16,  //
    18,  28,  12,  -2,  0,   8,   30,  20,  //
};
static const s8 pst_king_end[64] = {
    -48, -36, -28, -20, -20, -28, -36, -48, //
    -30, -18, -8,  0,   0,   -8,  -18, -30, //
    -28, -8,  18,  28,  28,  18,  -8,  -28, //
    -28, -8,  28,  38,  38,  28,  -8,  -28, //
    -28, -8,  28,  38,  38,  28,  -8,  -28, //
    -28, -8,  18,  28,  28,  18,  -8,  -28, //
    -30, -28, 0,   0,   0,   0,   -28, -30, //
    -48, -32, -28, -28, -28, -28, -32, -48, //
};

static const s8* const pst[7] = {0, pst_pawn, pst_knight, pst_bishop, pst_rook, pst_queen, 0};
static const s16 piece_value[7] = {0, 100, 320, 330, 500, 900, 0};
// The game phase each piece is worth: 24 with all of them on the board.
static const u8 phase_weight[7] = {0, 0, 1, 1, 2, 4, 0};
#define PHASE_MAX 24

// Pawn structure.
#define DOUBLED 14  // each pawn beyond the first on a file
#define ISOLATED 12 // a pawn with no pawn of its side on a file beside it
// A passed pawn by how far it has come (its rank from its side's view).
static const u8 passed_bonus[8] = {0, 8, 12, 20, 34, 56, 90, 0};

// --- The evaluation -------------------------------------------------------------

int chess_eval(const Position* p) {
    int score[2] = {0, 0}; // [0] White, [1] Black
    int phase = 0;
    int king_sq[2] = {0, 0};
    // Per file (index file + 1, with an empty file each side): pawn counts,
    // White's lowest pawn rank and Black's highest.
    u8 pawns[2][10] = {{0}, {0}};
    s8 white_low[10], black_high[10];
    u8 pawn_sq[2][16];
    int pawn_count[2] = {0, 0};
    for (int f = 0; f < 10; f++) {
        white_low[f] = 8;
        black_high[f] = -1;
    }

    for (int rank = 0; rank < 8; rank++) {
        const u8* row = &p->board[rank * 16];
        for (int file = 0; file < 8; file++) {
            Piece pc = row[file];
            if (pc == EMPTY)
                continue;
            int kind = PIECE_KIND(pc), c = PIECE_COLOR(pc) >> 3;
            int sq = rank * 8 + file, idx = c ? sq : sq ^ 56;
            if (kind == KING) {
                king_sq[c] = idx;
                continue;
            }
            score[c] += piece_value[kind] + pst[kind][idx];
            phase += phase_weight[kind];
            if (kind == PAWN) {
                pawns[c][file + 1]++;
                if (pawn_count[c] < 16)
                    pawn_sq[c][pawn_count[c]++] = (u8)sq;
                if (c == 0 && rank < white_low[file + 1])
                    white_low[file + 1] = (s8)rank;
                if (c == 1 && rank > black_high[file + 1])
                    black_high[file + 1] = (s8)rank;
            }
        }
    }

    // Pawn structure.
    for (int c = 0; c < 2; c++) {
        for (int f = 1; f <= 8; f++)
            if (pawns[c][f] > 1)
                score[c] -= DOUBLED * (pawns[c][f] - 1);
        for (int i = 0; i < pawn_count[c]; i++) {
            int sq = pawn_sq[c][i], f = SQ_FILE(sq) + 1, rank = SQ_RANK(sq);
            if (pawns[c][f - 1] == 0 && pawns[c][f + 1] == 0)
                score[c] -= ISOLATED;
            bool passed;
            if (c == 0)
                passed =
                    black_high[f - 1] <= rank && black_high[f] <= rank && black_high[f + 1] <= rank;
            else
                passed =
                    white_low[f - 1] >= rank && white_low[f] >= rank && white_low[f + 1] >= rank;
            if (passed)
                score[c] += passed_bonus[c ? 7 - rank : rank];
        }
    }

    // The kings, tapered by the material left.
    if (phase > PHASE_MAX)
        phase = PHASE_MAX;
    for (int c = 0; c < 2; c++)
        score[c] +=
            (pst_king_mid[king_sq[c]] * phase + pst_king_end[king_sq[c]] * (PHASE_MAX - phase)) /
            PHASE_MAX;

    int s = score[0] - score[1];
    return pos_side(p) == WHITE ? s : -s;
}

// --- The search's table -----------------------------------------------------------

static Move scratch[MAX_MOVES] SERVAL_EWRAM_BSS;

static int game_gen(void* pos, u32* out, bool noisy_only) {
    Position* p = pos;
    int n = noisy_only ? pos_gen_captures(p, scratch) : pos_gen_legal(p, scratch);
    for (int i = 0; i < n; i++)
        out[i] = scratch[i];
    return n;
}

static void game_make(void* pos, u32 m) {
    pos_make(pos, (Move)m);
}

static void game_unmake(void* pos) {
    pos_unmake(pos);
}

static int game_eval(const void* pos) {
    return chess_eval(pos);
}

static ChessKey game_key(const void* pos) {
    return pos_key(pos);
}

static int game_no_moves(const void* pos, int ply) {
    return pos_in_check(pos) ? -SEARCH_MATE + ply : 0;
}

// The fifty-move rule, any repetition since the last capture or pawn move
// (once is enough inside the search: the side repeating could do so again),
// and the game's length cap.
static bool game_is_draw(void* pos) {
    const Position* p = pos;
    int reach = p->halfmove < p->ply ? p->halfmove : p->ply;
    if (p->halfmove >= 100 || p->ply >= GAME_PLY_MAX)
        return true;
    ChessKey key = p->keys[p->ply];
    for (int back = 4; back <= reach; back += 2)
        if (p->keys[p->ply - back] == key)
            return true;
    return false;
}

// Captures by most valuable victim, then least valuable attacker; a queen's
// promotion with them; quiet moves by how much the piece-square table likes
// the move.
static int game_order(const void* pos, u32 mv) {
    const Position* p = pos;
    Move m = (Move)mv;
    int from = MOVE_FROM(m), to = MOVE_TO(m), flags = MOVE_FLAGS(m);
    Piece pc = pos_piece(p, from);
    int kind = PIECE_KIND(pc);
    int v = 0;
    if (flags & MF_PROMO) {
        if (MOVE_PROMO_KIND(m) != QUEEN)
            return 0;
        v = SEARCH_ORDER_NOISY + 80;
    }
    if (flags & MF_CAPTURE) {
        int victim = flags == MF_EP ? PAWN : PIECE_KIND(pos_piece(p, to));
        return (v ? v : SEARCH_ORDER_NOISY) + victim * 10 - kind;
    }
    if (v)
        return v;
    if (kind == KING)
        return 100;
    int flip = PIECE_COLOR(pc) ? 0 : 56;
    return 100 + pst[kind][to ^ flip] - pst[kind][from ^ flip];
}

static bool game_in_check(const void* pos) {
    return pos_in_check(pos);
}

static int game_ply(const void* pos) {
    return pos_ply(pos);
}

const SearchGame chess_search_game = {
    .pos_size = sizeof(Position),
    .gen = game_gen,
    .make = game_make,
    .unmake = game_unmake,
    .eval = game_eval,
    .key = game_key,
    .no_moves_score = game_no_moves,
    .is_draw = game_is_draw,
    .order = game_order,
    .quiesce_in_check_all = true,
    .in_check = game_in_check,
    .noisy_forced = false,
    .ply = game_ply,
};

// Placeholders until the strengths are measured on the GBA (Task 7).
static const SearchLevel levels[LEVEL_COUNT] = {
    [LEVEL_CUB] = {2, 2000, 60, 150, 0, 0},
    [LEVEL_HUNTER] = {5, 40000, 0, 0, 0, 0},
    [LEVEL_SERVAL] = {8, 100000, 0, 0, 8, 15},
};

const SearchLevel* chess_level(int level) {
    return &levels[level >= 0 && level < LEVEL_COUNT ? level : LEVEL_CUB];
}
