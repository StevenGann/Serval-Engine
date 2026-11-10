// chess: Serval Chess's board, move generation, results and notation
// (examples/boardgames/chess_board.c, chess_rules.c),
// natively. Perft counts the legal move tree from well-known positions to a
// few plies against published totals, so every rule of movement (castling,
// en passant, promotion, pins, checks) is proven by numbers no other bug
// could produce; the cases after it pin down the rules one at a time, that
// make and unmake leave no trace, and that the incremental Zobrist key is the
// key of the position it stands for.

#include "test.h"

#include <string.h>

#include "../examples/boardgames/chess_board.h"
#include "../examples/boardgames/chess_rules.h"

// --- Helpers ----------------------------------------------------------------

#define KIWIPETE "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"

// Positions are large (they carry their history), so the tests share two.
static Position pos, other;
static Move moves[MAX_MOVES];

static int square(const char* s) {
    return (s[1] - '1') * 8 + (s[0] - 'a');
}

// The legal move written as "e2e4" (or "b7b8q"), MOVE_NONE if there is none.
static Move find(Position* p, const char* uci) {
    int n = pos_gen_legal(p, moves);
    int from = square(uci), to = square(uci + 2);
    int promo = -1;
    switch (uci[4]) {
    case 'n':
        promo = 0;
        break;
    case 'b':
        promo = 1;
        break;
    case 'r':
        promo = 2;
        break;
    case 'q':
        promo = 3;
        break;
    default:
        break;
    }
    for (int i = 0; i < n; i++) {
        Move m = moves[i];
        if (MOVE_FROM(m) != from || MOVE_TO(m) != to)
            continue;
        if (promo < 0 ? !(MOVE_FLAGS(m) & 8) : (MOVE_FLAGS(m) & 8) && (MOVE_FLAGS(m) & 3) == promo)
            return m;
    }
    return MOVE_NONE;
}

// Plays the move written as "e2e4"; a failed check if it is not legal.
static void play(Position* p, const char* uci) {
    Move m = find(p, uci);
    CHECK(m != MOVE_NONE);
    if (m != MOVE_NONE)
        pos_make(p, m);
}

// Whether any legal move has these flags.
static bool has_flags(Position* p, int flags) {
    int n = pos_gen_legal(p, moves);
    for (int i = 0; i < n; i++)
        if (MOVE_FLAGS(moves[i]) == flags)
            return true;
    return false;
}

static bool same_position(const Position* a, const Position* b) {
    for (int sq = 0; sq < 64; sq++)
        if (pos_piece(a, sq) != pos_piece(b, sq))
            return false;
    return pos_side(a) == pos_side(b) && pos_castling(a) == pos_castling(b) &&
           pos_ep_square(a) == pos_ep_square(b) && pos_halfmove(a) == pos_halfmove(b) &&
           pos_key(a) == pos_key(b);
}

// --- Perft ------------------------------------------------------------------

static void perft_start(void) {
    pos_start(&pos);
    CHECK(perft(&pos, 1) == 20);
    CHECK(perft(&pos, 2) == 400);
    CHECK(perft(&pos, 3) == 8902);
    CHECK(perft(&pos, 4) == 197281);
    CHECK(pos_ply(&pos) == 0); // perft leaves the position as it found it
}

static void perft_kiwipete(void) {
    CHECK(pos_from_fen(&pos, KIWIPETE));
    CHECK(perft(&pos, 1) == 48);
    CHECK(perft(&pos, 2) == 2039);
    CHECK(perft(&pos, 3) == 97862);
}

static void perft_position3(void) {
    CHECK(pos_from_fen(&pos, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"));
    CHECK(perft(&pos, 4) == 43238);
}

static void perft_position4(void) {
    CHECK(pos_from_fen(&pos, "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"));
    CHECK(perft(&pos, 3) == 9467);
}

static void perft_position5(void) {
    CHECK(pos_from_fen(&pos, "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"));
    CHECK(perft(&pos, 3) == 62379);
}

// --- Make and unmake, keys --------------------------------------------------

static void make_unmake_restores(void) {
    CHECK(pos_from_fen(&pos, KIWIPETE));
    CHECK(pos_from_fen(&other, KIWIPETE));
    Move list[MAX_MOVES];
    int n = pos_gen_legal(&pos, list);
    CHECK(n == 48);
    for (int i = 0; i < n; i++) {
        pos_make(&pos, list[i]);
        CHECK(pos_ply(&pos) == 1);
        CHECK(pos_key(&pos) != pos_key(&other));
        CHECK(pos_key_at(&pos, 1) == pos_key(&other));
        pos_unmake(&pos);
        CHECK(pos_ply(&pos) == 0);
        CHECK(same_position(&pos, &other));
    }
}

static void key_incremental(void) {
    // White castles short, takes en passant and promotes with check; ten
    // plies in all, ending in a position with no castling and no en passant.
    CHECK(pos_from_fen(&pos, "r3k3/1P1p4/8/4P3/8/8/8/4K2R w Kq - 0 1"));
    static const char* const line[] = {"e1g1", "d7d5", "e5d6", "a8a2", "b7b8q",
                                       "e8d7", "f1f7", "d7e6", "g1h1", "a2a1"};
    for (unsigned i = 0; i < sizeof(line) / sizeof(line[0]); i++) {
        play(&pos, line[i]);
        if (i == 4) { // after the promotion, Black to move
            CHECK(pos_from_fen(&other, "1Q2k3/8/3P4/8/8/8/r7/5RK1 b - - 0 3"));
            CHECK(same_position(&pos, &other));
        }
    }
    CHECK(pos_ply(&pos) == 10);
    CHECK(pos_from_fen(&other, "1Q6/5R2/3Pk3/8/8/8/8/r6K w - - 5 6"));
    CHECK(same_position(&pos, &other));
    CHECK(pos_in_check(&pos));
    // Back to the start, the key is the start's again.
    ChessKey after = pos_key(&pos);
    CHECK(pos_key_at(&pos, 0) == after);
    for (int i = 0; i < 10; i++)
        pos_unmake(&pos);
    CHECK(pos_from_fen(&other, "r3k3/1P1p4/8/4P3/8/8/8/4K2R w Kq - 0 1"));
    CHECK(same_position(&pos, &other));
}

static void captures_are_the_noisy_legal_moves(void) {
    CHECK(pos_from_fen(&pos, "r3k2r/8/8/8/8/2n5/1p4q1/R2BK2R b KQkq - 0 1"));
    Move noisy[MAX_MOVES];
    int n = pos_gen_legal(&pos, moves);
    int c = pos_gen_captures(&pos, noisy);
    int expect = 0;
    for (int i = 0; i < n; i++) {
        int f = MOVE_FLAGS(moves[i]);
        if (f == MF_CAPTURE || f == MF_EP || (f & MF_PROMO)) {
            bool found = false;
            for (int j = 0; j < c; j++)
                found |= noisy[j] == moves[i];
            CHECK(found);
            expect++;
        }
    }
    CHECK(expect > 8); // b2xa1 and b2b1 promote, four ways each
    CHECK(c == expect);
}

// --- FEN --------------------------------------------------------------------

static void fen_rejects(void) {
    pos_start(&pos);
    ChessKey start = pos_key(&pos);
    static const char* const bad[] = {
        "",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPPP/RNBQKBNR w KQkq - 0 1", // nine squares
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQ1BNR w kq - 0 1",    // no white king
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1",  // side x
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1",           // seven ranks
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e6 0 1", // no pawn to take
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkz - 0 1",  // castling z
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - x 1",  // halfmove x
        "4k3/8/8/8/8/8/8/4K2R w K - 0 1 junk",                       // trailing text
        "4k2R/8/8/8/8/8/8/4K3 w - - 0 1", // Black in check, White to move
        "P3k3/8/8/8/8/8/8/4K3 w - - 0 1", // a pawn on the eighth rank
        "4k3/8/8/8/8/8/8/K3K3 w - - 0 1", // two white kings
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(!pos_from_fen(&pos, bad[i]));
        CHECK(pos_key(&pos) == start); // unchanged
        CHECK(pos_piece(&pos, square("e1")) == (WHITE | KING));
    }
    // The move counters are optional.
    CHECK(pos_from_fen(&pos, "4k3/8/8/8/8/8/8/4K3 b - -"));
    CHECK(pos_side(&pos) == BLACK && pos_halfmove(&pos) == 0);
}

// --- Rules of movement ------------------------------------------------------

static void castling_conditions(void) {
    // In check: neither side.
    CHECK(pos_from_fen(&pos, "r3k2r/8/8/8/4r3/8/8/R3K2R w KQkq - 0 1"));
    CHECK(!has_flags(&pos, MF_CASTLE_K) && !has_flags(&pos, MF_CASTLE_Q));
    // Through an attacked square (f1): only queenside.
    CHECK(pos_from_fen(&pos, "4k3/8/8/8/8/8/5r2/R3K2R w KQ - 0 1"));
    CHECK(!has_flags(&pos, MF_CASTLE_K) && has_flags(&pos, MF_CASTLE_Q));
    // Into check (g1): only queenside.
    CHECK(pos_from_fen(&pos, "4k3/8/8/8/8/8/6r1/R3K2R w KQ - 0 1"));
    CHECK(!has_flags(&pos, MF_CASTLE_K) && has_flags(&pos, MF_CASTLE_Q));
    // Only b1 attacked: the king never crosses it, so queenside is allowed.
    CHECK(pos_from_fen(&pos, "4k3/8/8/8/8/8/1r6/R3K2R w KQ - 0 1"));
    CHECK(has_flags(&pos, MF_CASTLE_K) && has_flags(&pos, MF_CASTLE_Q));
    // A piece between king and rook (b1): no queenside.
    CHECK(pos_from_fen(&pos, "4k3/8/8/8/8/8/8/RN2K2R w KQ - 0 1"));
    CHECK(has_flags(&pos, MF_CASTLE_K) && !has_flags(&pos, MF_CASTLE_Q));
    // A rook that moves and comes back has lost its side's right.
    CHECK(pos_from_fen(&pos, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    play(&pos, "h1h2");
    play(&pos, "a8a7");
    play(&pos, "h2h1");
    play(&pos, "a7a8");
    CHECK(pos_castling(&pos) == (2 | 4));
    CHECK(!has_flags(&pos, MF_CASTLE_K) && has_flags(&pos, MF_CASTLE_Q));
    // A rook captured at home: its side loses that right.
    CHECK(pos_from_fen(&pos, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    play(&pos, "h1h8");
    CHECK(pos_castling(&pos) == (2 | 8));
    // A king move loses both.
    CHECK(pos_from_fen(&pos, "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1"));
    play(&pos, "e8d8");
    CHECK(pos_castling(&pos) == (1 | 2));
    // Castling moves the rook.
    CHECK(pos_from_fen(&pos, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    play(&pos, "e1c1");
    CHECK(pos_piece(&pos, square("d1")) == (WHITE | ROOK));
    CHECK(pos_piece(&pos, square("a1")) == EMPTY);
    CHECK(pos_piece(&pos, square("c1")) == (WHITE | KING));
    play(&pos, "e8g8");
    CHECK(pos_piece(&pos, square("f8")) == (BLACK | ROOK));
    CHECK(pos_piece(&pos, square("h8")) == EMPTY);
    CHECK(pos_castling(&pos) == 0);
}

static void ep_only_immediately(void) {
    CHECK(pos_from_fen(&pos, "4k3/3p4/8/4P3/8/8/8/4K3 b - - 0 1"));
    play(&pos, "d7d5");
    CHECK(pos_ep_square(&pos) == square("d6"));
    CHECK(has_flags(&pos, MF_EP));
    // Taking it removes the pawn behind.
    Move m = find(&pos, "e5d6");
    CHECK(m != MOVE_NONE && MOVE_FLAGS(m) == MF_EP);
    pos_make(&pos, m);
    CHECK(pos_piece(&pos, square("d5")) == EMPTY);
    CHECK(pos_piece(&pos, square("d6")) == (WHITE | PAWN));
    pos_unmake(&pos);
    CHECK(pos_piece(&pos, square("d5")) == (BLACK | PAWN));
    // One ply later, it is gone.
    play(&pos, "e1d1");
    CHECK(pos_ep_square(&pos) == -1);
    play(&pos, "e8f8");
    CHECK(!has_flags(&pos, MF_EP));
    CHECK(find(&pos, "e5d6") == MOVE_NONE);
    // A double step with no pawn beside it to take sets no square.
    CHECK(pos_from_fen(&pos, "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));
    play(&pos, "e2e4");
    CHECK(pos_ep_square(&pos) == -1);
    // An en passant that uncovers the king along the rank is illegal.
    CHECK(pos_from_fen(&pos, "8/8/8/KPp4r/8/8/8/7k w - c6 0 1"));
    CHECK(pos_ep_square(&pos) == square("c6"));
    CHECK(!has_flags(&pos, MF_EP));
    CHECK(find(&pos, "b5b6") != MOVE_NONE);
}

static void promotions_four_ways(void) {
    CHECK(pos_from_fen(&pos, "1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1"));
    CHECK(find(&pos, "a7a8q") != MOVE_NONE && find(&pos, "a7a8r") != MOVE_NONE &&
          find(&pos, "a7a8b") != MOVE_NONE && find(&pos, "a7a8n") != MOVE_NONE);
    CHECK(MOVE_FLAGS(find(&pos, "a7b8n")) == (MF_PROMO | MF_CAPTURE | 0));
    CHECK(MOVE_FLAGS(find(&pos, "a7a8q")) == (MF_PROMO | 3));
    play(&pos, "a7b8r");
    CHECK(pos_piece(&pos, square("b8")) == (WHITE | ROOK));
    CHECK(pos_halfmove(&pos) == 0);
    pos_unmake(&pos);
    CHECK(pos_piece(&pos, square("a7")) == (WHITE | PAWN));
    CHECK(pos_piece(&pos, square("b8")) == (BLACK | KNIGHT));
}

static void attacks_and_check(void) {
    pos_start(&pos);
    CHECK(!pos_in_check(&pos));
    CHECK(pos_attacked(&pos, square("f3"), WHITE));  // g1 knight, e2 and g2 pawns
    CHECK(!pos_attacked(&pos, square("e4"), WHITE)); // nothing reaches it yet
    CHECK(pos_attacked(&pos, square("d6"), BLACK));
    CHECK(!pos_attacked(&pos, square("d6"), WHITE));
    // Fool's mate: checkmated, no legal move.
    play(&pos, "f2f3");
    play(&pos, "e7e5");
    play(&pos, "g2g4");
    play(&pos, "d8h4");
    CHECK(pos_in_check(&pos));
    CHECK(pos_gen_legal(&pos, moves) == 0);
    CHECK(pos_halfmove(&pos) == 1);
}

// --- Transposition ------------------------------------------------------------

static void key_transposition(void) {
    pos_start(&pos);
    play(&pos, "g1f3");
    play(&pos, "g8f6");
    play(&pos, "b1c3");
    play(&pos, "b8c6");
    pos_start(&other);
    play(&other, "b1c3");
    play(&other, "b8c6");
    play(&other, "g1f3");
    play(&other, "g8f6");
    CHECK(pos_key(&pos) == pos_key(&other));
    CHECK(same_position(&pos, &other));
}

// --- Results ------------------------------------------------------------------

static void result_mate_and_stalemate(void) {
    pos_start(&pos);
    CHECK(chess_result(&pos) == RESULT_NONE);
    play(&pos, "f2f3");
    play(&pos, "e7e5");
    play(&pos, "g2g4");
    CHECK(chess_result(&pos) == RESULT_NONE);
    play(&pos, "d8h4");
    CHECK(chess_result(&pos) == RESULT_CHECKMATE);
    CHECK(pos_from_fen(&pos, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
    CHECK(chess_result(&pos) == RESULT_STALEMATE);
    // In check with an escape: play on.
    CHECK(pos_from_fen(&pos, "7k/8/5K2/8/8/8/8/7R b - - 0 1"));
    CHECK(pos_in_check(&pos));
    CHECK(chess_result(&pos) == RESULT_NONE);
}

static void result_repetition(void) {
    static const char* const shuffle[8] = {"g1f3", "g8f6", "f3g1", "f6g8",
                                           "g1f3", "g8f6", "f3g1", "f6g8"};
    pos_start(&pos);
    for (int i = 0; i < 8; i++) {
        play(&pos, shuffle[i]);
        // The start position has now come up twice (ply 4), a third time on ply 8.
        CHECK(chess_result(&pos) == (i == 7 ? RESULT_REPETITION : RESULT_NONE));
    }
    // A pawn move in between breaks the chain.
    pos_start(&pos);
    for (int i = 0; i < 4; i++)
        play(&pos, shuffle[i]);
    play(&pos, "e2e4");
    play(&pos, "e7e5");
    for (int i = 0; i < 8; i++)
        play(&pos, shuffle[i]);
    CHECK(chess_result(&pos) == RESULT_REPETITION);
    pos_start(&pos);
    for (int i = 0; i < 4; i++)
        play(&pos, shuffle[i]);
    play(&pos, "e2e4");
    play(&pos, "e7e5");
    for (int i = 0; i < 4; i++)
        play(&pos, shuffle[i]);
    CHECK(chess_result(&pos) == RESULT_NONE); // only the second time since e5
}

static void result_fifty(void) {
    CHECK(pos_from_fen(&pos, "4k3/8/8/8/8/8/8/R3K3 w - - 99 80"));
    play(&pos, "e1e2");
    CHECK(pos_halfmove(&pos) == 100);
    CHECK(chess_result(&pos) == RESULT_FIFTY);
    CHECK(pos_from_fen(&pos, "4k3/8/8/8/8/8/r7/R3K3 w - - 99 80"));
    play(&pos, "a1a2");
    CHECK(pos_halfmove(&pos) == 0);
    CHECK(chess_result(&pos) == RESULT_NONE);
}

static void result_material(void) {
    static const struct {
        const char* fen;
        GameResult want;
    } cases[] = {
        {"4k3/8/8/8/8/8/8/4K3 w - - 0 1", RESULT_MATERIAL},
        {"4k3/8/8/8/8/8/8/3BK3 w - - 0 1", RESULT_MATERIAL},
        {"4k3/8/8/8/8/8/8/3NK3 w - - 0 1", RESULT_MATERIAL},
        {"2b1k3/8/8/8/8/8/8/4KB2 w - - 0 1", RESULT_MATERIAL}, // c8, f1: both light
        {"4kb2/8/8/8/8/8/8/4KB2 w - - 0 1", RESULT_NONE},      // f8 dark, f1 light
        {"4k3/8/8/8/8/8/8/2NNK3 w - - 0 1", RESULT_NONE},
        {"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", RESULT_NONE},
        {"4k3/8/8/8/8/8/8/3RK3 w - - 0 1", RESULT_NONE},
        {"4k3/8/8/8/8/8/8/3QK3 w - - 0 1", RESULT_NONE},
        {"4kn2/8/8/8/8/8/8/4KB2 w - - 0 1", RESULT_NONE},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        CHECK(pos_from_fen(&pos, cases[i].fen));
        CHECK(chess_result(&pos) == cases[i].want);
    }
}

static void result_length(void) {
    // Replaying 800 plies without a repetition or a fifty-move draw is not
    // possible by hand: the test sets the ply count (the history it would
    // look back through is not read with a fresh halfmove clock).
    pos_start(&pos);
    pos.ply = GAME_PLY_MAX - 1;
    CHECK(chess_result(&pos) == RESULT_NONE);
    pos.ply = GAME_PLY_MAX;
    CHECK(pos_ply(&pos) == GAME_PLY_MAX);
    CHECK(chess_result(&pos) == RESULT_LENGTH);
}

// --- Notation -----------------------------------------------------------------

// SAN of the move "e2e4" in the position written as FEN.
static bool san_is(const char* fen, const char* uci, const char* want) {
    char out[8];
    if (!pos_from_fen(&pos, fen))
        return false;
    Move m = find(&pos, uci);
    if (m == MOVE_NONE)
        return false;
    int n = chess_san(&pos, m, out);
    return n == (int)strlen(want) && strcmp(out, want) == 0;
}

static void san_cases(void) {
    CHECK(san_is("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "g1f3", "Nf3"));
    CHECK(san_is("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4", "e4"));
    CHECK(san_is("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5", "exd5"));
    CHECK(san_is("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1g1", "O-O"));
    CHECK(san_is("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1c1", "O-O-O"));
    CHECK(san_is("7k/4P3/8/8/8/8/8/K7 w - - 0 1", "e7e8q", "e8=Q+"));
    CHECK(san_is("3r3k/4P3/8/8/8/8/8/K7 w - - 0 1", "e7d8n", "exd8=N"));
    CHECK(san_is("7k/8/8/8/8/8/5p2/K4Q2 w - - 0 1", "f1f2", "Qxf2"));
    CHECK(san_is("6k1/5ppp/8/8/8/8/8/K3Q3 w - - 0 1", "e1e8", "Qe8#"));
    CHECK(san_is("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", "h5f7",
                 "Qxf7#"));
    // Disambiguation: by file, by rank, by both (three queens).
    CHECK(san_is("4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1", "b1d2", "Nbd2"));
    CHECK(san_is("4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1", "f3d2", "Nfd2"));
    CHECK(san_is("4k3/8/8/8/8/8/R7/K6R w - - 0 1", "a2h2", "Rah2"));
    CHECK(san_is("4k3/8/8/8/R7/8/8/R3K3 w - - 0 1", "a1a3", "R1a3"));
    CHECK(san_is("4k3/8/8/8/R7/8/8/R3K3 w - - 0 1", "a4a3", "R4a3"));
    CHECK(san_is("8/k7/8/8/4Q2Q/8/8/K6Q w - - 0 1", "h4e1", "Qh4e1"));
    // En passant.
    CHECK(san_is("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", "exd6"));
}

static void find_moves(void) {
    CHECK(pos_from_fen(&pos, "4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1"));
    Move m = chess_find(&pos, square("e1"), square("g1"), 0);
    CHECK(m != MOVE_NONE && MOVE_FLAGS(m) == MF_CASTLE_K);
    m = chess_find(&pos, square("e1"), square("c1"), 0);
    CHECK(m != MOVE_NONE && MOVE_FLAGS(m) == MF_CASTLE_Q);
    CHECK(chess_find(&pos, square("e1"), square("e3"), 0) == MOVE_NONE);
    CHECK(chess_find(&pos, square("a1"), square("h8"), 0) == MOVE_NONE);
    CHECK(!chess_is_promotion(&pos, square("a1"), square("a8")));
    CHECK(pos_from_fen(&pos, "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"));
    m = chess_find(&pos, square("e5"), square("d6"), 0);
    CHECK(m != MOVE_NONE && MOVE_FLAGS(m) == MF_EP);
    CHECK(pos_from_fen(&pos, "3r3k/4P3/8/8/8/8/8/K7 w - - 0 1"));
    CHECK(chess_is_promotion(&pos, square("e7"), square("e8")));
    CHECK(chess_is_promotion(&pos, square("e7"), square("d8")));
    CHECK(!chess_is_promotion(&pos, square("a1"), square("a2")));
    static const int kinds[4] = {KNIGHT, BISHOP, ROOK, QUEEN};
    for (int i = 0; i < 4; i++) {
        m = chess_find(&pos, square("e7"), square("e8"), kinds[i]);
        CHECK(m != MOVE_NONE && (MOVE_FLAGS(m) & 8) && MOVE_PROMO_KIND(m) == kinds[i]);
    }
    m = chess_find(&pos, square("e7"), square("d8"), ROOK);
    CHECK(m != MOVE_NONE && MOVE_PROMO_KIND(m) == ROOK && (MOVE_FLAGS(m) & MF_CAPTURE));
    CHECK(chess_find(&pos, square("e7"), square("e8"), 0) == MOVE_NONE);
}

TEST_SUITE(chess_tests, "chess", {"perft_start", perft_start}, {"perft_kiwipete", perft_kiwipete},
           {"perft_position3", perft_position3}, {"perft_position4", perft_position4},
           {"perft_position5", perft_position5}, {"make_unmake_restores", make_unmake_restores},
           {"key_incremental", key_incremental},
           {"captures_are_the_noisy_legal_moves", captures_are_the_noisy_legal_moves},
           {"fen_rejects", fen_rejects}, {"castling_conditions", castling_conditions},
           {"ep_only_immediately", ep_only_immediately},
           {"promotions_four_ways", promotions_four_ways}, {"attacks_and_check", attacks_and_check},
           {"key_transposition", key_transposition},
           {"result_mate_and_stalemate", result_mate_and_stalemate},
           {"result_repetition", result_repetition}, {"result_fifty", result_fifty},
           {"result_material", result_material}, {"result_length", result_length},
           {"san_cases", san_cases}, {"find_moves", find_moves});
