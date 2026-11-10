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
const s8 chess_king_mid[64] = {
    -32, -42, -42, -52, -52, -42, -42, -32, //
    -32, -42, -42, -52, -52, -42, -42, -32, //
    -32, -42, -42, -52, -52, -42, -42, -32, //
    -30, -40, -40, -50, -50, -40, -40, -30, //
    -22, -30, -30, -40, -40, -30, -30, -22, //
    -12, -22, -22, -24, -24, -22, -22, -12, //
    16,  16,  -4,  -6,  -6,  -4,  16,  16,  //
    18,  28,  12,  -2,  0,   8,   30,  20,  //
};
const s8 chess_king_end[64] = {
    -48, -36, -28, -20, -20, -28, -36, -48, //
    -30, -18, -8,  0,   0,   -8,  -18, -30, //
    -28, -8,  18,  28,  28,  18,  -8,  -28, //
    -28, -8,  28,  38,  38,  28,  -8,  -28, //
    -28, -8,  28,  38,  38,  28,  -8,  -28, //
    -28, -8,  18,  28,  28,  18,  -8,  -28, //
    -30, -28, 0,   0,   0,   0,   -28, -30, //
    -48, -32, -28, -28, -28, -28, -32, -48, //
};

const s8* const chess_pst[7] = {0, pst_pawn, pst_knight, pst_bishop, pst_rook, pst_queen, 0};
const s16 chess_piece_value[7] = {0, 100, 320, 330, 500, 900, 0};
// The game phase each piece is worth: 24 with all of them on the board.
const u8 chess_phase_weight[7] = {0, 0, 1, 1, 2, 4, 0};
#define PHASE_MAX 24
// A king's square, 0..63, from the 0x88 one the position keeps.
#define TO64_KING(s) (((s) + ((s) & 7)) >> 1)

// Pawn structure.
#define DOUBLED 14  // each pawn beyond the first on a file
#define ISOLATED 12 // a pawn with no pawn of its side on a file beside it
// A passed pawn by how far it has come (its rank from its side's view).
static const u8 passed_bonus[8] = {0, 8, 12, 20, 34, 56, 90, 0};

// --- The evaluation -------------------------------------------------------------

// Pawns as 64 bits: byte f is file f, its bit r rank r (Position.pawns).
typedef unsigned long long Pawns;
#define BYTES(b) (0x0101010101010101ull * (b)) // b in every byte

// Bits shifted n ranks down each file (within each byte).
static Pawns rank_down(Pawns x, int n) {
    return (x >> n) & BYTES((u8)(0xFF >> n));
}

// Each byte's count of set bits.
static Pawns byte_counts(Pawns x) {
    x -= (x >> 1) & BYTES(0x55);
    x = (x & BYTES(0x33)) + ((x >> 2) & BYTES(0x33));
    return (x + (x >> 4)) & BYTES(0x0F);
}

// The sum of the bytes (at most 255).
static int byte_sum(Pawns x) {
    x += x >> 32;
    x += x >> 16;
    x += x >> 8;
    return (int)(x & 0xFF);
}

// One side's pawn structure: own and their pawns as seen from that side
// (rank 0 its first rank).
static int pawn_side(Pawns own, Pawns their) {
    Pawns counts = byte_counts(own);
    // 1 in each byte of a file with pawns.
    Pawns files = own | rank_down(own, 4);
    files |= rank_down(files, 2);
    files = (files | rank_down(files, 1)) & BYTES(1);
    Pawns isolated = files & ~(files << 8) & ~(files >> 8);
    int score = -DOUBLED * (byte_sum(counts) - byte_sum(files)) -
                ISOLATED * byte_sum(counts & (isolated * 0xFF));
    // Passed: no pawn of theirs ahead on its file or the files beside it.
    // ahead: the ranks below one of their pawns on those files.
    Pawns near = their | (their << 8) | (their >> 8);
    Pawns ahead = rank_down(near, 1);
    ahead |= rank_down(ahead, 1);
    ahead |= rank_down(ahead, 2);
    ahead |= rank_down(ahead, 4);
    Pawns passed = own & ~ahead;
    for (int rank = 1; passed && rank < 7; rank++) // pawns stand on ranks 2 to 7
        score += passed_bonus[rank] * byte_sum((passed >> rank) & BYTES(1));
    return score;
}

// A side's pawns turned to its own view: the ranks reversed in every byte.
static Pawns pawns_turned(Pawns x) {
    x = ((x >> 1) & BYTES(0x55)) | ((x & BYTES(0x55)) << 1);
    x = ((x >> 2) & BYTES(0x33)) | ((x & BYTES(0x33)) << 2);
    return ((x >> 4) & BYTES(0x0F)) | ((x & BYTES(0x0F)) << 4);
}

// The pawn structure's worth to White (White's pawns' less Black's): doubled
// and isolated pawns, passed pawns by how far they have come. Rarely run
// (the cache below): not in IWRAM.
__attribute__((noinline)) static int pawn_structure(const u32* pawns) {
    Pawns white = (Pawns)pawns[1] << 32 | pawns[0];
    Pawns black = (Pawns)pawns[3] << 32 | pawns[2];
    return pawn_side(white, black) - pawn_side(pawns_turned(black), pawns_turned(white));
}

// The pawn structures worked out lately, by their pawns: the search meets
// the same few again and again. An entry holds its pawns whole, so a hit is
// always the right one; all zero, it is the structure with no pawns, whose
// worth is 0. Only time depends on what it holds.
#define PAWN_CACHE_BITS 8
typedef struct {
    u32 pawns[4];
    int score;
} PawnEntry;
static PawnEntry pawn_cache[1 << PAWN_CACHE_BITS] SERVAL_EWRAM_BSS;

// Run from IWRAM as ARM code on the GBA: the search evaluates at most of its
// nodes. The material and piece-square sums, the phase and the pawns come
// with the position (pos_make() keeps them), so nothing here scans the board.
CHESS_HOT int chess_eval(const Position* p) {
    int s = p->psq[0] - p->psq[1]; // White's view until the end

    // The pawn structure, from the cache if it is there.
    u32 w0 = p->pawns[0], w1 = p->pawns[1], w2 = p->pawns[2], w3 = p->pawns[3];
    u32 h = w0 * 0x9E3779B1u + w1 * 0x85EBCA77u + w2 * 0xC2B2AE3Du + w3 * 0x27D4EB2Fu;
    PawnEntry* e = &pawn_cache[h >> (32 - PAWN_CACHE_BITS)];
    if (e->pawns[0] != w0 || e->pawns[1] != w1 || e->pawns[2] != w2 || e->pawns[3] != w3) {
        e->pawns[0] = w0;
        e->pawns[1] = w1;
        e->pawns[2] = w2;
        e->pawns[3] = w3;
        e->score = pawn_structure(p->pawns);
    }
    s += e->score;

    // The kings, tapered by the material left.
    int phase = p->phase > PHASE_MAX ? PHASE_MAX : p->phase;
    int w = TO64_KING(p->king[0]) ^ 56, b = TO64_KING(p->king[1]);
    s += (chess_king_mid[w] * phase + chess_king_end[w] * (PHASE_MAX - phase)) / PHASE_MAX;
    s -= (chess_king_mid[b] * phase + chess_king_end[b] * (PHASE_MAX - phase)) / PHASE_MAX;
    return p->side == WHITE ? s : -s;
}

// --- The search's table -----------------------------------------------------------

static Move scratch[MAX_MOVES] SERVAL_EWRAM_BSS;

// For quiescence, the captures (and promotions) less those that lose
// material at once: a piece taking one worth clearly less (QUIESCE_MARGIN)
// that is defended (QxP with the pawn guarded). Near-even trades (BxN) stay.
// Without this, a tactical middlegame's quiescence runs to tens of thousands
// of nodes.
#define QUIESCE_MARGIN 50
static int game_gen(void* pos, u32* out, bool noisy_only) {
    Position* p = pos;
    if (!noisy_only) {
        int n = pos_gen_legal(p, scratch);
        for (int i = 0; i < n; i++)
            out[i] = scratch[i];
        return n;
    }
    int n = pos_gen_captures(p, scratch), kept = 0;
    int them = p->side ^ BLACK;
    for (int i = 0; i < n; i++) {
        Move m = scratch[i];
        int flags = MOVE_FLAGS(m), to = MOVE_TO(m), from = MOVE_FROM(m);
        if (flags == MF_CAPTURE) {
            // The board read directly (0x88: rank * 16 + file).
            int victim = chess_piece_value[PIECE_KIND(p->board[to + (to & ~7)])];
            int attacker = chess_piece_value[PIECE_KIND(p->board[from + (from & ~7)])];
            if (victim + QUIESCE_MARGIN < attacker && pos_attacked(p, to, them))
                continue;
        }
        out[kept++] = m;
    }
    return kept;
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
CHESS_HOT static int game_order(const void* pos, u32 mv) {
    const Position* p = pos;
    Move m = (Move)mv;
    int from = MOVE_FROM(m), to = MOVE_TO(m), flags = MOVE_FLAGS(m);
    // The board read directly (0x88: rank * 16 + file), not by pos_piece(),
    // which is ROM code.
    Piece pc = p->board[from + (from & ~7)];
    int kind = PIECE_KIND(pc);
    int v = 0;
    if (flags & MF_PROMO) {
        if (MOVE_PROMO_KIND(m) != QUEEN)
            return 0;
        v = SEARCH_ORDER_NOISY + 80;
    }
    if (flags & MF_CAPTURE) {
        int victim = flags == MF_EP ? PAWN : PIECE_KIND(p->board[to + (to & ~7)]);
        return (v ? v : SEARCH_ORDER_NOISY) + victim * 10 - kind;
    }
    if (v)
        return v;
    if (kind == KING)
        return 100;
    int flip = PIECE_COLOR(pc) ? 0 : 56;
    return 100 + chess_pst[kind][to ^ flip] - chess_pst[kind][from ^ flip];
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
    .frame_work = 630, // measured: HUNTER and SERVAL from Kiwipete, release build
};

// Measured on the GBA (release build, games played on from Kiwipete, about
// 1,400-1,500 nodes a second): CUB answers in about 0.6 s, at most 1.5 s,
// shallow and with its random margin and weak moves (beatable, though it takes
// what it is given); HUNTER in about 2.6 s, at most 3 s (depth 1-3); SERVAL
// stops at 10,000 nodes, about 7 s, at most 7.5 s (8 in a gba-ci
// build): depth 2-3 in Kiwipete itself (white's depth 3 takes 17,000 nodes),
// 3-4 in the middlegames that follow, deeper in quieter positions.
static const SearchLevel levels[LEVEL_COUNT] = {
    [LEVEL_CUB] = {2, 1500, 60, 150, 0, 0},
    [LEVEL_HUNTER] = {4, 3600, 0, 0, 0, 0},
    [LEVEL_SERVAL] = {8, 10000, 0, 0, 8, 15},
};

const SearchLevel* chess_level(int level) {
    return &levels[level >= 0 && level < LEVEL_COUNT ? level : LEVEL_CUB];
}
