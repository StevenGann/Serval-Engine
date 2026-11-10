// The chess board: see chess_board.h. A 0x88 mailbox: a square is
// rank * 16 + file, and a step off the board sets a bit of 0x88, so move
// generation needs no bounds tables and no division. Legal moves are the
// pseudo-legal ones whose king is not attacked once the move is made on the
// board alone (no keys, no history); make and unmake keep an undo record a
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

// --- Attacks ----------------------------------------------------------------

static bool slides_to(const u8* b, int s, const s8* steps, Piece a, Piece c) {
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
static bool attacked88(const u8* b, int s, int by) {
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

bool pos_attacked(const Position* p, int sq, int by_color) {
    return attacked88(p->board, TO88(sq & 63), by_color);
}

bool pos_in_check(const Position* p) {
    return attacked88(p->board, p->king[p->side >> 3], p->side ^ BLACK);
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
static bool leaves_king_safe(Position* p, int from, int to, int flags) {
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

typedef struct {
    Position* p;
    Move* out;
    int count;
} Gen;

static void add(Gen* g, int from, int to, int flags) {
    if (leaves_king_safe(g->p, from, to, flags))
        g->out[g->count++] = MOVE_MAKE(TO64(from), TO64(to), flags);
}

// The four promotions, queen first; capture is MF_CAPTURE or 0.
static void add_promotions(Gen* g, int from, int to, int capture) {
    if (!leaves_king_safe(g->p, from, to, capture))
        return; // the same test for all four
    int f = TO64(from), t = TO64(to);
    for (int kind = 3; kind >= 0; kind--)
        g->out[g->count++] = MOVE_MAKE(f, t, MF_PROMO | capture | kind);
}

static void gen_pawn(Gen* g, int s, bool noisy) {
    const u8* b = g->p->board;
    int us = g->p->side;
    int up = us == WHITE ? 16 : -16;
    int last = us == WHITE ? 7 : 0;  // the promotion rank
    int start = us == WHITE ? 1 : 6; // the double step's rank
    int one = s + up;
    if (!OFF(one) && !b[one]) {
        if (RANK88(one) == last)
            add_promotions(g, s, one, 0);
        else if (!noisy) {
            add(g, s, one, MF_QUIET);
            if (RANK88(s) == start && !b[one + up])
                add(g, s, one + up, MF_DOUBLE);
        }
    }
    for (int side = -1; side <= 1; side += 2) {
        int t = one + side;
        if (OFF(t))
            continue;
        if (b[t] && PIECE_COLOR(b[t]) != us) {
            if (RANK88(t) == last)
                add_promotions(g, s, t, MF_CAPTURE);
            else
                add(g, s, t, MF_CAPTURE);
        } else if (g->p->ep >= 0 && t == TO88(g->p->ep)) {
            add(g, s, t, MF_EP);
        }
    }
}

static void gen_steps(Gen* g, int s, const s8* steps, int n, bool slide, bool noisy) {
    const u8* b = g->p->board;
    int us = g->p->side;
    for (int i = 0; i < n; i++) {
        int step = steps[i];
        for (int t = s + step; !OFF(t); t += step) {
            if (b[t]) {
                if (PIECE_COLOR(b[t]) != us)
                    add(g, s, t, MF_CAPTURE);
                break;
            }
            if (!noisy)
                add(g, s, t, MF_QUIET);
            if (!slide)
                break;
        }
    }
}

static void gen_castling(Gen* g) {
    Position* p = g->p;
    const u8* b = p->board;
    int them = p->side ^ BLACK;
    int home = p->side == WHITE ? 0x04 : 0x74; // e1 or e8
    u8 kside = p->side == WHITE ? CASTLE_WK : CASTLE_BK;
    u8 qside = p->side == WHITE ? CASTLE_WQ : CASTLE_BQ;
    if (!(p->castling & (kside | qside)) || attacked88(b, home, them))
        return;
    // The rights guarantee king and rook at home; the king's last square is
    // tested by add() like any king move.
    if ((p->castling & kside) && !b[home + 1] && !b[home + 2] && !attacked88(b, home + 1, them))
        add(g, home, home + 2, MF_CASTLE_K);
    if ((p->castling & qside) && !b[home - 1] && !b[home - 2] && !b[home - 3] &&
        !attacked88(b, home - 1, them))
        add(g, home, home - 2, MF_CASTLE_Q);
}

static int generate(Position* p, Move* out, bool noisy) {
    Gen g = {p, out, 0};
    const u8* b = p->board;
    int us = p->side;
    for (int s = 0; s < 128; s++) {
        if (OFF(s)) {
            s += 7;
            continue;
        }
        Piece pc = b[s];
        if (!pc || PIECE_COLOR(pc) != us)
            continue;
        switch (PIECE_KIND(pc)) {
        case PAWN:
            gen_pawn(&g, s, noisy);
            break;
        case KNIGHT:
            gen_steps(&g, s, knight_steps, 8, false, noisy);
            break;
        case BISHOP:
            gen_steps(&g, s, diagonal_steps, 4, true, noisy);
            break;
        case ROOK:
            gen_steps(&g, s, straight_steps, 4, true, noisy);
            break;
        case QUEEN:
            gen_steps(&g, s, diagonal_steps, 4, true, noisy);
            gen_steps(&g, s, straight_steps, 4, true, noisy);
            break;
        default:
            gen_steps(&g, s, king_steps, 8, false, noisy);
            break;
        }
    }
    if (!noisy)
        gen_castling(&g);
    return g.count;
}

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

void pos_make(Position* p, Move m) {
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
    }
    Piece moving = b[from];
    Piece placed = (flags & MF_PROMO) ? (Piece)(us | MOVE_PROMO_KIND(m)) : moving;
    b[from] = EMPTY;
    b[to] = placed;
    key ^= z_piece[moving][from64] ^ z_piece[placed][to64];

    if (PIECE_KIND(moving) == KING) {
        p->king[us >> 3] = (u8)to;
        if (flags == MF_CASTLE_K || flags == MF_CASTLE_Q) {
            int rfrom = flags == MF_CASTLE_K ? to + 1 : to - 2;
            int rto = flags == MF_CASTLE_K ? to - 1 : to + 1;
            Piece rook = b[rfrom];
            b[rto] = rook;
            b[rfrom] = EMPTY;
            key ^= z_piece[rook][TO64(rfrom)] ^ z_piece[rook][TO64(rto)];
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
    key ^= z_side;
    p->keys[++p->ply] = key;
}

void pos_unmake(Position* p) {
    if (!p->ply)
        return;
    const PosUndo* u = &p->undo[--p->ply];
    u8* b = p->board;
    p->side ^= BLACK;
    int us = p->side, flags = MOVE_FLAGS(u->move);
    int from = TO88(MOVE_FROM(u->move)), to = TO88(MOVE_TO(u->move));
    Piece placed = b[to];
    Piece moving = (flags & MF_PROMO) ? (Piece)(us | PAWN) : placed;
    b[to] = EMPTY;
    b[from] = moving;
    if (u->captured)
        b[flags == MF_EP ? (us == WHITE ? to - 16 : to + 16) : to] = u->captured;
    if (PIECE_KIND(moving) == KING) {
        p->king[us >> 3] = (u8)from;
        if (flags == MF_CASTLE_K || flags == MF_CASTLE_Q) {
            int rfrom = flags == MF_CASTLE_K ? to + 1 : to - 2;
            int rto = flags == MF_CASTLE_K ? to - 1 : to + 1;
            b[rfrom] = b[rto];
            b[rto] = EMPTY;
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
