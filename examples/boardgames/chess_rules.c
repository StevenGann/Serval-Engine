// Chess results, notation and move lookup: see chess_rules.h.

#include "chess_rules.h"

// --- Results ----------------------------------------------------------------

// Whether the current position has come up three times since the last
// capture or pawn move (only positions that many plies back can repeat).
static bool repeated(const Position* p) {
    ChessKey key = pos_key(p);
    int reach = pos_halfmove(p) < pos_ply(p) ? pos_halfmove(p) : pos_ply(p);
    int seen = 1;
    for (int back = 2; back <= reach; back += 2)
        if (pos_key_at(p, back) == key && ++seen == 3)
            return true;
    return false;
}

// Whether no side can mate: bare kings, one minor piece, or only bishops on
// squares of one colour.
static bool insufficient(const Position* p) {
    int minors = 0, knights = 0, shade[2] = {0, 0};
    for (int sq = 0; sq < 64; sq++) {
        int kind = PIECE_KIND(pos_piece(p, sq));
        if (kind == EMPTY || kind == KING)
            continue;
        if (kind == KNIGHT)
            knights++;
        else if (kind == BISHOP)
            shade[(SQ_FILE(sq) + SQ_RANK(sq)) & 1]++;
        else
            return false; // a pawn, rook or queen
        minors++;
    }
    if (minors <= 1)
        return true;
    return knights == 0 && (shade[0] == 0 || shade[1] == 0);
}

GameResult chess_result(Position* p) {
    Move moves[MAX_MOVES];
    if (pos_gen_legal(p, moves) == 0)
        return pos_in_check(p) ? RESULT_CHECKMATE : RESULT_STALEMATE;
    if (pos_halfmove(p) >= 100)
        return RESULT_FIFTY;
    if (repeated(p))
        return RESULT_REPETITION;
    if (insufficient(p))
        return RESULT_MATERIAL;
    if (pos_ply(p) >= GAME_PLY_MAX)
        return RESULT_LENGTH;
    return RESULT_NONE;
}

// --- Notation ---------------------------------------------------------------

int chess_san(Position* p, Move m, char out[8]) {
    static const char letters[7] = {'?', 'P', 'N', 'B', 'R', 'Q', 'K'};
    int from = MOVE_FROM(m), to = MOVE_TO(m), flags = MOVE_FLAGS(m);
    int n = 0;

    if (flags == MF_CASTLE_K || flags == MF_CASTLE_Q) {
        out[n++] = 'O';
        out[n++] = '-';
        out[n++] = 'O';
        if (flags == MF_CASTLE_Q) {
            out[n++] = '-';
            out[n++] = 'O';
        }
    } else {
        int kind = PIECE_KIND(pos_piece(p, from));
        bool capture = (flags & MF_CAPTURE) != 0; // MF_EP and promoting captures have the bit
        if (kind == PAWN) {
            if (capture)
                out[n++] = (char)('a' + SQ_FILE(from));
        } else {
            out[n++] = letters[kind];
            // Other pieces of the kind that could also go there.
            Move all[MAX_MOVES];
            int count = pos_gen_legal(p, all);
            bool other = false, same_file = false, same_rank = false;
            for (int i = 0; i < count; i++) {
                int f = MOVE_FROM(all[i]);
                if (f == from || MOVE_TO(all[i]) != to || PIECE_KIND(pos_piece(p, f)) != kind)
                    continue;
                other = true;
                same_file |= SQ_FILE(f) == SQ_FILE(from);
                same_rank |= SQ_RANK(f) == SQ_RANK(from);
            }
            if (other) {
                if (!same_file)
                    out[n++] = (char)('a' + SQ_FILE(from));
                else if (!same_rank)
                    out[n++] = (char)('1' + SQ_RANK(from));
                else {
                    out[n++] = (char)('a' + SQ_FILE(from));
                    out[n++] = (char)('1' + SQ_RANK(from));
                }
            }
        }
        if (capture)
            out[n++] = 'x';
        out[n++] = (char)('a' + SQ_FILE(to));
        out[n++] = (char)('1' + SQ_RANK(to));
        if (flags & MF_PROMO) {
            out[n++] = '=';
            out[n++] = letters[MOVE_PROMO_KIND(m)];
        }
    }

    // Check or mate: play it and look.
    pos_make(p, m);
    if (pos_in_check(p)) {
        Move replies[MAX_MOVES];
        out[n++] = pos_gen_legal(p, replies) == 0 ? '#' : '+';
    }
    pos_unmake(p);
    out[n] = '\0';
    return n;
}

// --- Lookup -----------------------------------------------------------------

Move chess_find(Position* p, int from, int to, int promo_kind) {
    Move moves[MAX_MOVES];
    int count = pos_gen_legal(p, moves);
    for (int i = 0; i < count; i++) {
        Move m = moves[i];
        if (MOVE_FROM(m) != from || MOVE_TO(m) != to)
            continue;
        if ((MOVE_FLAGS(m) & MF_PROMO) && MOVE_PROMO_KIND(m) != promo_kind)
            continue;
        return m;
    }
    return MOVE_NONE;
}

bool chess_is_promotion(Position* p, int from, int to) {
    Move moves[MAX_MOVES];
    int count = pos_gen_legal(p, moves);
    for (int i = 0; i < count; i++)
        if (MOVE_FROM(moves[i]) == from && MOVE_TO(moves[i]) == to)
            return (MOVE_FLAGS(moves[i]) & MF_PROMO) != 0;
    return false;
}
