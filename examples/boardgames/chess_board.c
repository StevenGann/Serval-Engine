// The chess board: see chess_board.h. A 0x88 mailbox: a square is
// rank * 16 + file, and a step off the board sets a bit of 0x88, so move
// generation needs no bounds tables and no division. Legal moves are the
// pseudo-legal ones whose king is not attacked once the move is made on the
// board alone (no keys, no history), a test only the moves that might fail
// it need (the king's, a pinned piece's, en passant, all when in check);
// make and unmake keep an undo record a
// ply and update the Zobrist key as they go.

#include "chess_board.h"

#include <string.h>

#define TO88(sq) ((sq) + ((sq) & ~7))
#define TO64(s) (((s) + ((s) & 7)) >> 1)
#define OFF(s) ((s) & 0x88)
#define RANK88(s) ((s) >> 4)

static const s8 knight_steps[8] = {33, 31, 18, 14, -14, -18, -31, -33};
static const s8 king_steps[8] = {17, 16, 15, 1, -1, -15, -16, -17};
static const s8 diagonal_steps[4] = {17, 15, -15, -17};
static const s8 straight_steps[4] = {16, 1, -1, -16};
static const s8 queen_steps[8] = {17, 15, -15, -17, 16, 1, -1, -16}; // diagonals first

// Each piece's steps (by kind; pawns have their own rules).
static const struct {
    const s8* steps;
    u8 count;
    bool slide;
} piece_steps[7] = {
    [KNIGHT] = {knight_steps, 8, false}, [BISHOP] = {diagonal_steps, 4, true},
    [ROOK] = {straight_steps, 4, true},  [QUEEN] = {queen_steps, 8, true},
    [KING] = {king_steps, 8, false},
};

// The hottest code, the search's, runs from IWRAM as ARM code on the GBA
// (SERVAL_IWRAM_CODE): move generation (one function, its helpers inlined
// into it), the attack test, make and unmake. Elsewhere these are plain C.
#define INLINE static inline __attribute__((always_inline))
// One copy of generate() in IWRAM, not one a value of `noisy` (GCC).
#if defined(__GNUC__) && !defined(__clang__)
#define NOCLONE __attribute__((noclone))
#else
#define NOCLONE
#endif

// The rights a move from or to each square keeps: a king or rook leaving
// home, or a rook taken there, ends that side's castling.
#define ALL 15
#define WQ (ALL & ~CASTLE_WQ)
#define WK (ALL & ~CASTLE_WK)
#define W_ (ALL & ~(CASTLE_WK | CASTLE_WQ))
#define BQ (ALL & ~CASTLE_BQ)
#define BK (ALL & ~CASTLE_BK)
#define B_ (ALL & ~(CASTLE_BK | CASTLE_BQ))
// clang-format off
static const u8 castle_keep[64] = {
    WQ,  ALL, ALL, ALL, W_,  ALL, ALL, WK,
    ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL,
    ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL,
    ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL,
    ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL,
    ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL,
    ALL, ALL, ALL, ALL, ALL, ALL, ALL, ALL,
    BQ,  ALL, ALL, ALL, B_,  ALL, ALL, BK,
};
// clang-format on
#undef ALL
#undef WQ
#undef WK
#undef W_
#undef BQ
#undef BK
#undef B_

// --- Zobrist keys -------------------------------------------------------------

// Built at first use from a fixed seed, so every build and platform has the
// same keys. Indexed by piece (colour | kind) and 0..63 square.
static ChessKey z_piece[16][64] SERVAL_EWRAM_BSS;
static ChessKey z_castle[16] SERVAL_EWRAM_BSS;
static ChessKey z_ep[8] SERVAL_EWRAM_BSS;
static ChessKey z_side;
static bool z_ready;
// The evaluation's sums, built with the keys: a piece's value and
// piece-square value (from its side's view) by piece (colour | kind) and
// square, and its phase by piece.
static s16 psq_table[16][64] SERVAL_EWRAM_BSS;
static u8 phase_table[16];

static u32 z_next(u32* x) {
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}

static void keys_init(void) {
    if (z_ready)
        return;
    u32 x = 0x5E4A1C0Du; // xorshift32: 32-bit arithmetic only
    ChessKey* all = &z_piece[0][0];
    for (int i = 0; i < 16 * 64; i++) {
        u32 hi = z_next(&x);
        all[i] = (ChessKey)hi << 32 | z_next(&x);
    }
    for (int i = 0; i < 16; i++) {
        u32 hi = z_next(&x);
        z_castle[i] = (ChessKey)hi << 32 | z_next(&x);
    }
    for (int i = 0; i < 8; i++) {
        u32 hi = z_next(&x);
        z_ep[i] = (ChessKey)hi << 32 | z_next(&x);
    }
    u32 hi = z_next(&x);
    z_side = (ChessKey)hi << 32 | z_next(&x);
    for (int kind = PAWN; kind < KING; kind++) {
        for (int sq = 0; sq < 64; sq++) {
            psq_table[WHITE | kind][sq] = (s16)(chess_piece_value[kind] + chess_pst[kind][sq ^ 56]);
            psq_table[BLACK | kind][sq] = (s16)(chess_piece_value[kind] + chess_pst[kind][sq]);
        }
        phase_table[WHITE | kind] = phase_table[BLACK | kind] = chess_phase_weight[kind];
    }
    z_ready = true;
}

static ChessKey key_of(const u8* b, int side, int castling, int ep) {
    ChessKey key = z_castle[castling];
    for (int s = 0; s < 128; s++) {
        if (OFF(s)) {
            s += 7;
            continue;
        }
        if (b[s])
            key ^= z_piece[b[s]][TO64(s)];
    }
    if (ep >= 0)
        key ^= z_ep[ep & 7];
    if (side == BLACK)
        key ^= z_side;
    return key;
}

// What a position keeps of its pieces besides the board (Position.occ, and
// the evaluation's pawns, psq and phase) with a piece put on or taken off a
// square (0..63).
INLINE void sum_put(Position* p, Piece pc, int sq) {
    int c = pc >> 3;
    p->occ[c][sq >> 5] |= 1u << (sq & 31);
    p->psq[c] = (s16)(p->psq[c] + psq_table[pc][sq]);
    p->phase = (u8)(p->phase + phase_table[pc]);
    if (PIECE_KIND(pc) == PAWN)
        p->pawns[c * 2 + ((sq >> 2) & 1)] |= 1u << ((sq & 3) * 8 + (sq >> 3));
}

INLINE void sum_take(Position* p, Piece pc, int sq) {
    int c = pc >> 3;
    p->occ[c][sq >> 5] &= ~(1u << (sq & 31));
    p->psq[c] = (s16)(p->psq[c] - psq_table[pc][sq]);
    p->phase = (u8)(p->phase - phase_table[pc]);
    if (PIECE_KIND(pc) == PAWN)
        p->pawns[c * 2 + ((sq >> 2) & 1)] &= ~(1u << ((sq & 3) * 8 + (sq >> 3)));
}

// A piece moved from one square to another (0..63), the same piece after.
INLINE void sum_move(Position* p, Piece pc, int from, int to) {
    int c = pc >> 3;
    p->psq[c] = (s16)(p->psq[c] + psq_table[pc][to] - psq_table[pc][from]);
    u32* occ = p->occ[c];
    occ[from >> 5] ^= 1u << (from & 31);
    occ[to >> 5] ^= 1u << (to & 31);
    if (PIECE_KIND(pc) == PAWN) {
        p->pawns[c * 2 + ((from >> 2) & 1)] ^= 1u << ((from & 3) * 8 + (from >> 3));
        p->pawns[c * 2 + ((to >> 2) & 1)] ^= 1u << ((to & 3) * 8 + (to >> 3));
    }
}

// --- Attacks ----------------------------------------------------------------

INLINE bool slides_to(const u8* b, int s, const s8* steps, Piece a, Piece c) {
    for (int i = 0; i < 4; i++) {
        int step = steps[i];
        for (int t = s + step; !OFF(t); t += step) {
            if (b[t]) {
                if (b[t] == a || b[t] == c)
                    return true;
                break;
            }
        }
    }
    return false;
}

// Whether a piece of colour by attacks the 0x88 square s on board b.
CHESS_HOT static bool attacked88(const u8* b, int s, int by) {
    Piece pawn = (Piece)(by | PAWN);
    int behind = by == WHITE ? -16 : 16; // where its pawns attack from
    if (!OFF(s + behind - 1) && b[s + behind - 1] == pawn)
        return true;
    if (!OFF(s + behind + 1) && b[s + behind + 1] == pawn)
        return true;
    Piece knight = (Piece)(by | KNIGHT), king = (Piece)(by | KING);
    for (int i = 0; i < 8; i++) {
        int t = s + knight_steps[i];
        if (!OFF(t) && b[t] == knight)
            return true;
        t = s + king_steps[i];
        if (!OFF(t) && b[t] == king)
            return true;
    }
    Piece queen = (Piece)(by | QUEEN);
    return slides_to(b, s, diagonal_steps, (Piece)(by | BISHOP), queen) ||
           slides_to(b, s, straight_steps, (Piece)(by | ROOK), queen);
}

// Whether the side to move is in check, worked out once a position (the
// search asks, then generates).
INLINE bool check_now(Position* p) {
    if (!p->check_known)
        p->check_known = attacked88(p->board, p->king[p->side >> 3], p->side ^ BLACK) ? 2 : 1;
    return p->check_known == 2;
}

bool pos_attacked(const Position* p, int sq, int by_color) {
    return attacked88(p->board, TO88(sq & 63), by_color);
}

bool pos_in_check(const Position* p) {
    return check_now((Position*)p); // only the cache changes
}

// --- Reading ----------------------------------------------------------------

Piece pos_piece(const Position* p, int sq) {
    return p->board[TO88(sq & 63)];
}

int pos_side(const Position* p) {
    return p->side;
}

int pos_ep_square(const Position* p) {
    return p->ep;
}

u8 pos_castling(const Position* p) {
    return p->castling;
}

int pos_halfmove(const Position* p) {
    return p->halfmove;
}

int pos_ply(const Position* p) {
    return p->ply;
}

ChessKey pos_key(const Position* p) {
    return p->keys[p->ply];
}

ChessKey pos_key_at(const Position* p, int plies_back) {
    if (plies_back < 0 || plies_back > p->ply)
        return 0;
    return p->keys[p->ply - plies_back];
}

// --- Move generation --------------------------------------------------------

// Whether the move leaves the mover's king unattacked: made and taken back
// on the board alone. Castling's other squares are checked by the generator.
CHESS_HOT static bool leaves_king_safe(Position* p, int from, int to, int flags) {
    u8* b = p->board;
    int us = p->side;
    int capsq = flags == MF_EP ? (us == WHITE ? to - 16 : to + 16) : to;
    Piece moving = b[from], captured = b[capsq];
    b[capsq] = EMPTY;
    b[from] = EMPTY;
    b[to] = moving;
    int king = PIECE_KIND(moving) == KING ? to : p->king[us >> 3];
    bool safe = !attacked88(b, king, us ^ BLACK);
    b[to] = EMPTY;
    b[capsq] = captured;
    b[from] = moving;
    return safe;
}

// The index of the lowest bit set in x (not 0), by a de Bruijn sequence:
// the ARM7TDMI has no instruction that counts zeros.
static const u8 debruijn[32] SERVAL_IWRAM_DATA = {
    0,  1,  28, 2,  29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4,  8,
    31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6,  11, 5,  10, 9,
};
INLINE int lowest_bit(u32 x) {
    return debruijn[((x & (0u - x)) * 0x077CB531u) >> 27];
}

// Adds a move if it is legal: a move by a piece that is not the king, not
// pinned to it, with the king not in check (`test` false), cannot expose the
// king (en passant aside, which takes two pieces off a rank, so it is always
// tested); the others are made on the board and tested.
// In check, a move by another piece than the king (`free` false) must also
// reach a square of `reach` (it takes the checking piece or steps between it
// and the king), or it is dropped untested.
#define REACHES(to) (free || (reach[TO64(to) >> 5] >> (TO64(to) & 31) & 1))
#define ADD(from, to, flags, test)                                                                 \
    do {                                                                                           \
        if (REACHES(to) && (!(test) || leaves_king_safe(p, from, to, flags)))                      \
            *o++ = MOVE_MAKE(TO64(from), TO64(to), flags);                                         \
    } while (0)

// The four promotions, queen first; capture is MF_CAPTURE or 0.
#define ADD_PROMOTIONS(from, to, capture, test)                                                    \
    do {                                                                                           \
        if (REACHES(to) && (!(test) || leaves_king_safe(p, from, to, capture))) {                  \
            for (int promo = 3; promo >= 0; promo--)                                               \
                *o++ = MOVE_MAKE(TO64(from), TO64(to), MF_PROMO | (capture) | promo);              \
        }                                                                                          \
    } while (0)

// The squares (bits 0..63 of reach[2]) a move by another piece than the
// king must reach to answer a check on the king (0x88) by colour `by`: the
// checking piece's square and, for a slider, those between it and the king.
// None in double check, which only the king can answer. Run only in check:
// not in IWRAM.
__attribute__((noinline)) static void check_reach(const u8* b, int king, int by, u32 reach[2]) {
    int checkers = 0;
    reach[0] = reach[1] = 0;
    Piece pawn = (Piece)(by | PAWN), knight = (Piece)(by | KNIGHT), queen = (Piece)(by | QUEEN);
    int behind = by == WHITE ? -16 : 16; // where its pawns attack from
    for (int i = 0; i < 8; i++) {
        int t = king + knight_steps[i];
        if (!OFF(t) && b[t] == knight) {
            reach[TO64(t) >> 5] |= 1u << (TO64(t) & 31);
            checkers++;
        }
        if (i < 2) {
            t = king + behind + (i ? 1 : -1);
            if (!OFF(t) && b[t] == pawn) {
                reach[TO64(t) >> 5] |= 1u << (TO64(t) & 31);
                checkers++;
            }
        }
    }
    for (int line = 0; line < 8; line++) {
        int step = queen_steps[line];
        Piece slider = (Piece)(by | (line < 4 ? BISHOP : ROOK));
        int t = king + step;
        while (!OFF(t) && !b[t])
            t += step;
        if (OFF(t) || (b[t] != slider && b[t] != queen))
            continue;
        checkers++;
        for (int u = king + step;; u += step) {
            reach[TO64(u) >> 5] |= 1u << (TO64(u) & 31);
            if (u == t)
                break;
        }
    }
    if (checkers > 1)
        reach[0] = reach[1] = 0;
}

// The pieces of the side to move pinned to its king, as bits (0..63) of
// pins[2]: on each line out of the king, its own piece with a slider of the
// other side behind it that moves along that line.
INLINE void find_pins(const u8* b, int king, int us, u32 pins[2]) {
    Piece queen = (Piece)((us ^ BLACK) | QUEEN);
    for (int line = 0; line < 8; line++) {
        int step = queen_steps[line];
        Piece slider = (Piece)((us ^ BLACK) | (line < 4 ? BISHOP : ROOK));
        int own = -1;
        for (int t = king + step; !OFF(t); t += step) {
            Piece pc = b[t];
            if (!pc)
                continue;
            if (own < 0 && PIECE_COLOR(pc) == us) {
                own = TO64(t);
                continue;
            }
            if (own >= 0 && (pc == slider || pc == queen))
                pins[own >> 5] |= 1u << (own & 31);
            break;
        }
    }
}

__attribute__((noinline)) static Move* gen_castling(Position* p, Move* o) {
    const u8* b = p->board;
    const bool free = true; // never in check
    const u32* reach = NULL;
    int them = p->side ^ BLACK;
    int home = p->side == WHITE ? 0x04 : 0x74; // e1 or e8
    u8 kside = p->side == WHITE ? CASTLE_WK : CASTLE_BK;
    u8 qside = p->side == WHITE ? CASTLE_WQ : CASTLE_BQ;
    // The rights guarantee king and rook at home; the king's last square is
    // tested like any king move's.
    if ((p->castling & kside) && !b[home + 1] && !b[home + 2] && !attacked88(b, home + 1, them))
        ADD(home, home + 2, MF_CASTLE_K, true);
    if ((p->castling & qside) && !b[home - 1] && !b[home - 2] && !b[home - 3] &&
        !attacked88(b, home - 1, them))
        ADD(home, home - 2, MF_CASTLE_Q, true);
    return o;
}

// Compiled at -O2 even in -O3 builds (GCC): -O3 makes it half as big again
// (4 KB of IWRAM) for 1% of the search's speed.
#if defined(__OPTIMIZE__) && defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("O2")
#endif
// The moves of the side to move into out (noisy: captures and promotions
// only), in the order of their pieces' squares, a1 first. The pieces are
// found from the occupancy bits, not by scanning the board.
CHESS_HOT NOCLONE static int generate(Position* p, Move* out, bool noisy) {
    u8* b = p->board;
    int us = p->side, c = us >> 3;
    int king = p->king[c];
    bool check = check_now(p);
    u32 pins[2] = {0, 0}, reach[2] = {0, 0};
    if (!check)
        find_pins(b, king, us, pins);
    else
        check_reach(b, king, us ^ BLACK, reach);
    int up = us == WHITE ? 16 : -16;
    int last = us == WHITE ? 7 : 0;  // the promotion rank
    int start = us == WHITE ? 1 : 6; // the double step's rank
    int ep = p->ep >= 0 ? TO88(p->ep) : -1;
    Move* o = out;
    for (int half = 0; half < 2; half++) {
        for (u32 bits = p->occ[c][half]; bits; bits &= bits - 1) {
            int s64 = half * 32 + lowest_bit(bits), s = TO88(s64);
            Piece pc = b[s];
            int kind = PIECE_KIND(pc);
            bool test = check || (pins[half] >> (s64 & 31) & 1) || kind == KING;
            bool free = !check || kind == KING;
            if (kind == PAWN) {
                int one = s + up;
                if (!b[one]) { // a pawn is never on the last rank: one is on the board
                    if (RANK88(one) == last)
                        ADD_PROMOTIONS(s, one, 0, test);
                    else if (!noisy) {
                        ADD(s, one, MF_QUIET, test);
                        if (RANK88(s) == start && !b[one + up])
                            ADD(s, one + up, MF_DOUBLE, test);
                    }
                }
                for (int side = -1; side <= 1; side += 2) {
                    int t = one + side;
                    if (OFF(t))
                        continue;
                    if (b[t] && PIECE_COLOR(b[t]) != us) {
                        if (RANK88(t) == last)
                            ADD_PROMOTIONS(s, t, MF_CAPTURE, test);
                        else
                            ADD(s, t, MF_CAPTURE, test);
                    } else if (t == ep && leaves_king_safe(p, s, t, MF_EP)) {
                        *o++ = MOVE_MAKE(TO64(s), TO64(t), MF_EP); // tested whatever the check
                    }
                }
                continue;
            }
            const s8* steps = piece_steps[kind].steps;
            int n = piece_steps[kind].count;
            if (piece_steps[kind].slide) {
                for (int i = 0; i < n; i++) {
                    int step = steps[i];
                    int t = s + step;
                    for (; !OFF(t) && !b[t]; t += step)
                        if (!noisy)
                            ADD(s, t, MF_QUIET, test);
                    if (!OFF(t) && PIECE_COLOR(b[t]) != us)
                        ADD(s, t, MF_CAPTURE, test);
                }
            } else {
                for (int i = 0; i < n; i++) {
                    int t = s + steps[i];
                    if (OFF(t))
                        continue;
                    if (!b[t]) {
                        if (!noisy)
                            ADD(s, t, MF_QUIET, test);
                    } else if (PIECE_COLOR(b[t]) != us) {
                        ADD(s, t, MF_CAPTURE, test);
                    }
                }
            }
        }
    }
    if (!noisy && !check &&
        (p->castling & (us == WHITE ? CASTLE_WK | CASTLE_WQ : CASTLE_BK | CASTLE_BQ)))
        o = gen_castling(p, o);
    return (int)(o - out);
}

#if defined(__OPTIMIZE__) && defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

int pos_gen_legal(Position* p, Move* out) {
    return generate(p, out, false);
}

int pos_gen_captures(Position* p, Move* out) {
    return generate(p, out, true);
}

// --- Make and unmake --------------------------------------------------------

// The en passant target a double step to `to` leaves, if a pawn of `them`
// stands beside it to take; -1 if none.
static int ep_after_double(const u8* b, int from, int to, int them) {
    Piece pawn = (Piece)(them | PAWN);
    if ((!OFF(to - 1) && b[to - 1] == pawn) || (!OFF(to + 1) && b[to + 1] == pawn))
        return TO64((from + to) >> 1);
    return -1;
}

CHESS_HOT void pos_make(Position* p, Move m) {
    if (p->ply >= POS_HISTORY_MAX)
        return;
    u8* b = p->board;
    int us = p->side, flags = MOVE_FLAGS(m);
    int from64 = MOVE_FROM(m), to64 = MOVE_TO(m);
    int from = TO88(from64), to = TO88(to64);
    ChessKey key = p->keys[p->ply];
    PosUndo* u = &p->undo[p->ply];
    u->move = m;
    u->halfmove = p->halfmove;
    u->state = (u8)(p->castling | (p->ep >= 0 ? ((p->ep & 7) + 1) << 4 : 0));
    if (p->ep >= 0)
        key ^= z_ep[p->ep & 7];

    int capsq = flags == MF_EP ? (us == WHITE ? to - 16 : to + 16) : to;
    Piece captured = b[capsq];
    u->captured = captured;
    if (captured) {
        key ^= z_piece[captured][TO64(capsq)];
        b[capsq] = EMPTY;
        sum_take(p, captured, TO64(capsq));
    }
    Piece moving = b[from];
    Piece placed = (flags & MF_PROMO) ? (Piece)(us | MOVE_PROMO_KIND(m)) : moving;
    b[from] = EMPTY;
    b[to] = placed;
    key ^= z_piece[moving][from64] ^ z_piece[placed][to64];
    if (placed == moving) {
        sum_move(p, moving, from64, to64);
    } else {
        sum_take(p, moving, from64);
        sum_put(p, placed, to64);
    }

    if (PIECE_KIND(moving) == KING) {
        p->king[us >> 3] = (u8)to;
        if (flags == MF_CASTLE_K || flags == MF_CASTLE_Q) {
            int rfrom = flags == MF_CASTLE_K ? to + 1 : to - 2;
            int rto = flags == MF_CASTLE_K ? to - 1 : to + 1;
            Piece rook = b[rfrom];
            b[rto] = rook;
            b[rfrom] = EMPTY;
            key ^= z_piece[rook][TO64(rfrom)] ^ z_piece[rook][TO64(rto)];
            sum_move(p, rook, TO64(rfrom), TO64(rto));
        }
    }

    u8 castling = p->castling & castle_keep[from64] & castle_keep[to64];
    key ^= z_castle[p->castling] ^ z_castle[castling];
    p->castling = castling;

    p->ep = -1;
    if (flags == MF_DOUBLE) {
        p->ep = (s8)ep_after_double(b, from, to, us ^ BLACK);
        if (p->ep >= 0)
            key ^= z_ep[p->ep & 7];
    }
    p->halfmove = (PIECE_KIND(moving) == PAWN || captured) ? 0 : p->halfmove + 1;
    if (us == BLACK)
        p->fullmove++;
    p->side ^= BLACK;
    p->check_known = 0;
    key ^= z_side;
    p->keys[++p->ply] = key;
}

CHESS_HOT void pos_unmake(Position* p) {
    if (!p->ply)
        return;
    const PosUndo* u = &p->undo[--p->ply];
    u8* b = p->board;
    p->side ^= BLACK;
    p->check_known = 0;
    int us = p->side, flags = MOVE_FLAGS(u->move);
    int from = TO88(MOVE_FROM(u->move)), to = TO88(MOVE_TO(u->move));
    Piece placed = b[to];
    Piece moving = (flags & MF_PROMO) ? (Piece)(us | PAWN) : placed;
    b[to] = EMPTY;
    b[from] = moving;
    if (placed == moving) {
        sum_move(p, moving, MOVE_TO(u->move), MOVE_FROM(u->move));
    } else {
        sum_take(p, placed, MOVE_TO(u->move));
        sum_put(p, moving, MOVE_FROM(u->move));
    }
    if (u->captured) {
        int capsq = flags == MF_EP ? (us == WHITE ? to - 16 : to + 16) : to;
        b[capsq] = u->captured;
        sum_put(p, u->captured, TO64(capsq));
    }
    if (PIECE_KIND(moving) == KING) {
        p->king[us >> 3] = (u8)from;
        if (flags == MF_CASTLE_K || flags == MF_CASTLE_Q) {
            int rfrom = flags == MF_CASTLE_K ? to + 1 : to - 2;
            int rto = flags == MF_CASTLE_K ? to - 1 : to + 1;
            b[rfrom] = b[rto];
            b[rto] = EMPTY;
            sum_move(p, b[rfrom], TO64(rto), TO64(rfrom));
        }
    }
    p->castling = u->state & 15;
    int ep_file = u->state >> 4;
    p->ep = (s8)(ep_file ? (us == WHITE ? SQ(0, 5) : SQ(0, 2)) + ep_file - 1 : -1);
    p->halfmove = u->halfmove;
    if (us == BLACK)
        p->fullmove--;
}

// --- FEN --------------------------------------------------------------------

static Piece piece_of(char c) {
    static const char letters[] = "pnbrqk";
    Piece color = WHITE;
    if (c >= 'a' && c <= 'z')
        color = BLACK;
    else if (c >= 'A' && c <= 'Z')
        c = (char)(c - 'A' + 'a');
    else
        return EMPTY;
    for (int i = 0; i < 6; i++)
        if (letters[i] == c)
            return (Piece)(color | (PAWN + i));
    return EMPTY;
}

// Reads a move counter (digits, at most 9999) at *s; false if there is none.
static bool read_count(const char** s, int* value) {
    int v = 0, digits = 0;
    while (**s >= '0' && **s <= '9') {
        v = v * 10 + (**s - '0');
        (*s)++;
        if (++digits > 4)
            return false;
    }
    *value = v;
    return digits > 0;
}

bool pos_from_fen(Position* p, const char* fen) {
    keys_init();
    u8 b[128];
    memset(b, 0, sizeof(b));
    const char* s = fen;
    int kings[2] = {-1, -1};

    // Placement, from the eighth rank down.
    int rank = 7, file = 0;
    for (;; s++) {
        char c = *s;
        if (c == ' ')
            break;
        if (c == '/') {
            if (file != 8 || rank == 0)
                return false;
            rank--;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8)
                return false;
        } else {
            Piece pc = piece_of(c); // EMPTY for any other character, '\0' too
            if (!pc || file >= 8)
                return false;
            if (PIECE_KIND(pc) == PAWN && (rank == 0 || rank == 7))
                return false;
            if (PIECE_KIND(pc) == KING) {
                if (kings[pc >> 3] >= 0)
                    return false;
                kings[pc >> 3] = rank * 16 + file;
            }
            b[rank * 16 + file] = pc;
            file++;
        }
    }
    if (rank != 0 || file != 8 || kings[0] < 0 || kings[1] < 0)
        return false;

    // Side to move.
    s++;
    int side;
    if (*s == 'w')
        side = WHITE;
    else if (*s == 'b')
        side = BLACK;
    else
        return false;
    if (*++s != ' ')
        return false;
    s++;

    // Castling.
    int castling = 0;
    if (*s == '-') {
        s++;
    } else {
        static const char letters[] = "KQkq";
        while (*s && *s != ' ') {
            int bit = -1;
            for (int i = 0; i < 4; i++)
                if (letters[i] == *s)
                    bit = 1 << i;
            if (bit < 0 || (castling & bit))
                return false;
            castling |= bit;
            s++;
        }
        if (!castling)
            return false;
    }
    if (*s != ' ')
        return false;
    s++;
    // Rights whose king or rook has left home are dropped.
    if (b[0x04] != (WHITE | KING) || b[0x07] != (WHITE | ROOK))
        castling &= ~CASTLE_WK;
    if (b[0x04] != (WHITE | KING) || b[0x00] != (WHITE | ROOK))
        castling &= ~CASTLE_WQ;
    if (b[0x74] != (BLACK | KING) || b[0x77] != (BLACK | ROOK))
        castling &= ~CASTLE_BK;
    if (b[0x74] != (BLACK | KING) || b[0x70] != (BLACK | ROOK))
        castling &= ~CASTLE_BQ;

    // En passant: the square behind a pawn of the side not to move that has
    // just stepped two; kept only if a pawn stands beside it to take.
    int ep = -1;
    if (*s == '-') {
        s++;
    } else {
        if (s[0] < 'a' || s[0] > 'h' || s[1] != (side == WHITE ? '6' : '3'))
            return false;
        int target = (s[1] - '1') * 16 + (s[0] - 'a');
        int up = side == WHITE ? 16 : -16; // the direction the side to move goes
        int pawn_at = target - up;
        if (b[target] || b[target + up] || b[pawn_at] != ((side ^ BLACK) | PAWN))
            return false;
        ep = ep_after_double(b, target + up, pawn_at, side);
        s += 2;
    }

    // Move counters, optional.
    int halfmove = 0, fullmove = 1;
    if (*s) {
        if (*s++ != ' ' || !read_count(&s, &halfmove))
            return false;
        if (*s) {
            if (*s++ != ' ' || !read_count(&s, &fullmove) || *s)
                return false;
        }
    }
    if (fullmove < 1)
        fullmove = 1;

    // The side not to move must not be in check.
    if (attacked88(b, kings[(side ^ BLACK) >> 3], side))
        return false;

    memcpy(p->board, b, sizeof(b));
    p->side = (u8)side;
    p->castling = (u8)castling;
    p->ep = (s8)ep;
    p->king[0] = (u8)kings[0];
    p->king[1] = (u8)kings[1];
    p->halfmove = (u16)halfmove;
    p->fullmove = (u16)fullmove;
    p->ply = 0;
    p->keys[0] = key_of(b, side, castling, ep);
    p->psq[0] = p->psq[1] = 0;
    p->phase = 0;
    memset(p->pawns, 0, sizeof(p->pawns));
    memset(p->occ, 0, sizeof(p->occ));
    p->check_known = 0;
    for (int sq = 0; sq < 64; sq++)
        if (b[TO88(sq)])
            sum_put(p, b[TO88(sq)], sq);
    return true;
}

void pos_start(Position* p) {
    pos_from_fen(p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

// --- Perft ------------------------------------------------------------------

unsigned long long perft(Position* p, int depth) {
    if (depth <= 0)
        return 1;
    Move list[MAX_MOVES];
    int n = pos_gen_legal(p, list);
    if (depth == 1)
        return (unsigned long long)n;
    unsigned long long nodes = 0;
    for (int i = 0; i < n; i++) {
        pos_make(p, list[i]);
        nodes += perft(p, depth - 1);
        pos_unmake(p);
    }
    return nodes;
}
