// checkers: Paws & Kings' checkers board, moves, results and notation
// (examples/boardgames/checkers.c), natively. Perft counts the legal move
// tree from the start to seven plies against published totals, and an
// independent counter written here (a plain 8x8 array and its own move
// rules, sharing no code with the game) must agree with it to six; the cases
// after it pin down forced captures, multi-jump paths, crowning, kings, each
// result and the notation, that make and unmake leave no trace, and that the
// incremental Zobrist key is the key of the position it stands for.

#include "test.h"

#include <string.h>

#include "../examples/boardgames/checkers.h"

// Positions are large (they carry their history), so the tests share two.
static CheckersPosition pos, other;
static CkMove moves[CK_MAX_MOVES];

// Square numbers as printed (1..32) to the game's squares (0..31).
#define N(n) ((n) - 1)

static bool same_position(const CheckersPosition* a, const CheckersPosition* b) {
    for (int s = 0; s < 32; s++)
        if (ck_piece(a, s) != ck_piece(b, s))
            return false;
    return ck_side(a) == ck_side(b) && ck_key(a) == ck_key(b);
}

// The legal move written as in the notation ("11-15", "6x15x22"), or
// CK_MOVE_NONE.
static CkMove find(CheckersPosition* p, const char* text) {
    int n = ck_gen_legal(p, moves);
    char out[24];
    for (int i = 0; i < n; i++) {
        ck_notation(p, moves[i], out);
        if (strcmp(out, text) == 0)
            return moves[i];
    }
    return CK_MOVE_NONE;
}

static void play(CheckersPosition* p, const char* text) {
    CkMove m = find(p, text);
    CHECK(m != CK_MOVE_NONE);
    if (m != CK_MOVE_NONE)
        ck_make(p, m);
}

// --- An independent counter ---------------------------------------------------

// b[rank][file], rank 0 at the bottom. Dark starts at the top and moves down.
enum { BF_EMPTY, BF_DARK_MAN, BF_DARK_KING, BF_LIGHT_MAN, BF_LIGHT_KING, BF_TAKEN };
typedef struct {
    int b[8][8];
} BfBoard;

static bool bf_dark(int v) {
    return v == BF_DARK_MAN || v == BF_DARK_KING;
}
static bool bf_light(int v) {
    return v == BF_LIGHT_MAN || v == BF_LIGHT_KING;
}
static bool bf_on(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

static unsigned long long bf_count(const BfBoard* x, bool dark, int depth);

// The piece of value v, now at (f, r), has jumped at least once on board w
// (its pieces taken marked BF_TAKEN). Counts the leaves below each complete
// path into *nodes; returns whether it could jump on.
static void bf_jumps(BfBoard* w, int v, int f, int r, bool dark, int depth, bool first,
                     unsigned long long* nodes, bool* any) {
    bool king = v == BF_DARK_KING || v == BF_LIGHT_KING;
    bool went_on = false;
    if (!first && !king && r == (dark ? 0 : 7)) {
        // crowned: the move ends here
    } else {
        for (int df = -1; df <= 1; df += 2)
            for (int dr = -1; dr <= 1; dr += 2) {
                if (!king && dr != (dark ? -1 : 1))
                    continue;
                int mf = f + df, mr = r + dr, lf = f + 2 * df, lr = r + 2 * dr;
                if (!bf_on(lf, lr) || w->b[lr][lf] != BF_EMPTY)
                    continue;
                int mid = w->b[mr][mf];
                if (!(dark ? bf_light(mid) : bf_dark(mid)))
                    continue;
                went_on = true;
                *any = true;
                w->b[mr][mf] = BF_TAKEN;
                w->b[r][f] = BF_EMPTY;
                w->b[lr][lf] = v;
                bf_jumps(w, v, lf, lr, dark, depth, false, nodes, any);
                w->b[lr][lf] = BF_EMPTY;
                w->b[r][f] = v;
                w->b[mr][mf] = mid;
            }
    }
    if (!went_on && !first) {
        BfBoard done = *w;
        for (int rr = 0; rr < 8; rr++)
            for (int ff = 0; ff < 8; ff++)
                if (done.b[rr][ff] == BF_TAKEN)
                    done.b[rr][ff] = BF_EMPTY;
        if (v == BF_DARK_MAN && r == 0)
            done.b[r][f] = BF_DARK_KING;
        if (v == BF_LIGHT_MAN && r == 7)
            done.b[r][f] = BF_LIGHT_KING;
        *nodes += depth <= 1 ? 1 : bf_count(&done, !dark, depth - 1);
    }
}

static unsigned long long bf_count(const BfBoard* x, bool dark, int depth) {
    BfBoard w = *x;
    unsigned long long nodes = 0;
    bool any = false;
    for (int r = 0; r < 8; r++)
        for (int f = 0; f < 8; f++) {
            int v = w.b[r][f];
            if (dark ? bf_dark(v) : bf_light(v))
                bf_jumps(&w, v, f, r, dark, depth, true, &nodes, &any);
        }
    if (any)
        return nodes;
    for (int r = 0; r < 8; r++)
        for (int f = 0; f < 8; f++) {
            int v = w.b[r][f];
            if (!(dark ? bf_dark(v) : bf_light(v)))
                continue;
            bool king = v == BF_DARK_KING || v == BF_LIGHT_KING;
            for (int df = -1; df <= 1; df += 2)
                for (int dr = -1; dr <= 1; dr += 2) {
                    if (!king && dr != (dark ? -1 : 1))
                        continue;
                    int tf = f + df, tr = r + dr;
                    if (!bf_on(tf, tr) || w.b[tr][tf] != BF_EMPTY)
                        continue;
                    if (depth <= 1) {
                        nodes++;
                        continue;
                    }
                    BfBoard next = w;
                    next.b[r][f] = BF_EMPTY;
                    next.b[tr][tf] = v;
                    if (v == BF_DARK_MAN && tr == 0)
                        next.b[tr][tf] = BF_DARK_KING;
                    if (v == BF_LIGHT_MAN && tr == 7)
                        next.b[tr][tf] = BF_LIGHT_KING;
                    nodes += bf_count(&next, !dark, depth - 1);
                }
        }
    return nodes;
}

static unsigned long long bf_perft_start(int depth) {
    BfBoard x;
    memset(&x, 0, sizeof x);
    for (int r = 0; r < 8; r++)
        for (int f = 0; f < 8; f++)
            if (((f + r) & 1) == 0) // a1 is a playing square
                x.b[r][f] = r <= 2 ? BF_LIGHT_MAN : r >= 5 ? BF_DARK_MAN : BF_EMPTY;
    return bf_count(&x, true, depth);
}

// --- Perft ------------------------------------------------------------------

static void ck_perft_start(void) {
    static const unsigned long long published[8] = {1, 7, 49, 302, 1469, 7361, 36768, 179740};
    ck_start(&pos);
    for (int d = 1; d <= 7; d++)
        CHECK(ck_perft(&pos, d) == published[d]);
    for (int d = 1; d <= 6; d++)
        CHECK(bf_perft_start(d) == published[d]);
    CHECK(ck_ply(&pos) == 0); // perft leaves the position as it found it
}

// The independent counter's board for a position set from text.
static unsigned long long bf_perft(const CheckersPosition* p, int depth) {
    static const int values[8] = {BF_EMPTY, BF_DARK_MAN,  BF_DARK_KING,  0,
                                  0,        BF_LIGHT_MAN, BF_LIGHT_KING, 0};
    BfBoard x;
    memset(&x, 0, sizeof x);
    for (int s = 0; s < 32; s++)
        x.b[ck_square_rank(s)][ck_square_file(s)] = values[ck_piece(p, s) & 7];
    return bf_count(&x, ck_side(p) == CK_DARK, depth);
}

// Positions with kings and men, the king circuit and a man whose jump could go
// on backward among them: both counters agree to four plies.
static void ck_perft_positions(void) {
    static const char* const texts[] = {
        "W:WK22,29:B2,18,19,26,27",      // the king circuit (ck_king_circuit), and more
        "W:W22,K30:B11,18,19",           // a man's jump a king would continue backward
        "B:W18,19,K27,30:B6,10,K14,K23", // kings and men, captures both ways
        "W:W21,22,25,K32:B5,9,K17,K28",
    };
    for (unsigned t = 0; t < sizeof texts / sizeof texts[0]; t++) {
        CHECK(ck_from_text(&pos, texts[t]));
        for (int d = 1; d <= 4; d++) {
            unsigned long long n = ck_perft(&pos, d);
            CHECK(n > 0);
            CHECK(n == bf_perft(&pos, d));
        }
    }
}

// --- The board ----------------------------------------------------------------

static void ck_squares(void) {
    // The corners and the numbering as printed: 1 = b8, 4 = h8, 5 = a7,
    // 29 = a1, 32 = g1.
    CHECK(ck_square_file(N(1)) == 1 && ck_square_rank(N(1)) == 7);
    CHECK(ck_square_file(N(4)) == 7 && ck_square_rank(N(4)) == 7);
    CHECK(ck_square_file(N(5)) == 0 && ck_square_rank(N(5)) == 6);
    CHECK(ck_square_file(N(29)) == 0 && ck_square_rank(N(29)) == 0);
    CHECK(ck_square_file(N(32)) == 6 && ck_square_rank(N(32)) == 0);
    CHECK(ck_square_at(0, 1) == -1); // a2 is light
    CHECK(ck_square_at(8, 0) == -1 && ck_square_at(0, -2) == -1);
    for (int s = 0; s < 32; s++)
        CHECK(ck_square_at(ck_square_file(s), ck_square_rank(s)) == s);
    ck_start(&pos);
    CHECK(ck_side(&pos) == CK_DARK);
    for (int s = 0; s < 32; s++)
        CHECK(ck_piece(&pos, s) == (s < 12    ? CK_DARK | CK_MAN
                                    : s >= 20 ? CK_LIGHT | CK_MAN
                                              : CK_EMPTY));
    CHECK(ck_from_text(&other, "B:W21-32:B1-12"));
    CHECK(same_position(&pos, &other));
}

static void ck_text_rejects(void) {
    CHECK(ck_from_text(&pos, "W:WK10,11:BK1,6."));
    CHECK(ck_side(&pos) == CK_LIGHT);
    CHECK(ck_piece(&pos, N(10)) == (CK_LIGHT | CK_KING));
    CHECK(ck_piece(&pos, N(1)) == (CK_DARK | CK_KING));
    CHECK(ck_piece(&pos, N(6)) == (CK_DARK | CK_MAN) &&
          ck_piece(&pos, N(11)) == (CK_LIGHT | CK_MAN));
    static const char* const bad[] = {
        "",         "X:W1:B2", "B:W33:B1",  "B:W0:B1",    "B:W5:B5",  "B:W5,5:B1", "B:W1:B9",
        "B:W9:B29", "B:W9",    "B:W9-3:B1", "B:W9:B1:W2", "B:W9,:B1", "B:WQ9:B1",
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(!ck_from_text(&pos, bad[i]));
    // A failed set leaves the position as it was.
    CHECK(ck_side(&pos) == CK_LIGHT && ck_piece(&pos, N(10)) == (CK_LIGHT | CK_KING));
}

// --- Moves --------------------------------------------------------------------

static void ck_forced_capture(void) {
    // Dark's man on 14 can take 18; its man on 1 could step, but may not.
    CHECK(ck_from_text(&pos, "B:W18:B1,14"));
    int n = ck_gen_legal(&pos, moves);
    CHECK(n == 1);
    CHECK(ck_move_is_jump(moves[0]) && CK_MOVE_FROM(moves[0]) == N(14));
    CHECK(ck_move_steps(moves[0]) == 1 && ck_move_to(moves[0]) == N(23));
    CkMove jumps[CK_MAX_MOVES];
    CHECK(ck_gen_jumps(&pos, jumps) == 1 && jumps[0] == moves[0]);
    int land = N(23);
    CHECK(ck_find(&pos, N(14), &land, 1) == moves[0]);
    int next[4];
    CHECK(ck_continuations(&pos, N(1), NULL, 0, next) == 0); // the man on 1 shows nothing
    // With no capture, simple moves and no jumps.
    CHECK(ck_from_text(&pos, "B:W30:B1,14"));
    n = ck_gen_legal(&pos, moves);
    CHECK(n == 4);
    for (int i = 0; i < n; i++)
        CHECK(!ck_move_is_jump(moves[i]) && ck_move_steps(moves[i]) == 1);
    CHECK(ck_gen_jumps(&pos, jumps) == 0);
}

static void ck_multi_jump(void) {
    // Dark's man on 6 takes 10 to land on 15, then 18 (to 22) or 19 (to 24).
    CHECK(ck_from_text(&pos, "B:W10,18,19:B6"));
    int n = ck_gen_legal(&pos, moves);
    CHECK(n == 2);
    CHECK(find(&pos, "6x15x22") != CK_MOVE_NONE);
    CHECK(find(&pos, "6x15x24") != CK_MOVE_NONE);
    for (int i = 0; i < n; i++) {
        CHECK(ck_move_steps(moves[i]) == 2);
        CHECK(ck_move_landing(moves[i], 0) == N(15));
    }
    int next[4];
    CHECK(ck_continuations(&pos, N(6), NULL, 0, next) == 1 && next[0] == N(15));
    int path[2] = {N(15), N(22)};
    int c = ck_continuations(&pos, N(6), path, 1, next);
    CHECK(c == 2);
    CHECK((next[0] == N(22) && next[1] == N(24)) || (next[0] == N(24) && next[1] == N(22)));
    CHECK(ck_continuations(&pos, N(6), path, 2, next) == 0);
    CHECK(ck_find(&pos, N(6), path, 1) == CK_MOVE_NONE); // two ways on
    CkMove m = ck_find(&pos, N(6), path, 2);
    CHECK(m == find(&pos, "6x15x22"));
    CHECK(ck_move_to(m) == N(22) && ck_move_landing(m, 1) == N(22));
    path[1] = N(24);
    CHECK(ck_find(&pos, N(6), path, 2) == find(&pos, "6x15x24"));
    path[1] = N(23);
    CHECK(ck_find(&pos, N(6), path, 2) == CK_MOVE_NONE);
    // Playing one takes both men.
    play(&pos, "6x15x24");
    CHECK(ck_piece(&pos, N(10)) == CK_EMPTY && ck_piece(&pos, N(19)) == CK_EMPTY);
    CHECK(ck_piece(&pos, N(18)) == (CK_LIGHT | CK_MAN));
    CHECK(ck_piece(&pos, N(24)) == (CK_DARK | CK_MAN) && ck_piece(&pos, N(6)) == CK_EMPTY);
    // With one way on, a first landing names the move.
    CHECK(ck_from_text(&pos, "B:W10,18:B6"));
    path[0] = N(15);
    m = ck_find(&pos, N(6), path, 1);
    CHECK(m != CK_MOVE_NONE && ck_move_steps(m) == 2 && ck_move_to(m) == N(22));
}

static void ck_crowning_ends_move(void) {
    // Light's man on 10 takes 7 and is crowned on 3; a king there could take
    // 8 too, but the move ends.
    CHECK(ck_from_text(&pos, "W:W10:B7,8"));
    int n = ck_gen_legal(&pos, moves);
    CHECK(n == 1);
    CHECK(ck_move_steps(moves[0]) == 1 && ck_move_to(moves[0]) == N(3));
    ck_make(&pos, moves[0]);
    CHECK(ck_piece(&pos, N(3)) == (CK_LIGHT | CK_KING));
    CHECK(ck_piece(&pos, N(8)) == (CK_DARK | CK_MAN));
    ck_unmake(&pos);
    CHECK(ck_piece(&pos, N(10)) == (CK_LIGHT | CK_MAN) && ck_piece(&pos, N(3)) == CK_EMPTY);
    // A king already would go on.
    CHECK(ck_from_text(&pos, "W:WK10:B7,8"));
    CHECK(ck_gen_legal(&pos, moves) == 1);
    CHECK(find(&pos, "10x3x12") != CK_MOVE_NONE);
    // Dark is crowned on its own far row by a simple move.
    CHECK(ck_from_text(&pos, "B:W30:B25"));
    play(&pos, "25-29");
    CHECK(ck_piece(&pos, N(29)) == (CK_DARK | CK_KING));
}

static void ck_king_backward_capture(void) {
    // Light's king on 18 takes 22 backward, to 25.
    CHECK(ck_from_text(&pos, "W:WK18:B22"));
    int n = ck_gen_legal(&pos, moves);
    CHECK(n == 1 && find(&pos, "18x25") != CK_MOVE_NONE);
    // A man there cannot, and steps forward instead.
    CHECK(ck_from_text(&pos, "W:W18:B22"));
    n = ck_gen_legal(&pos, moves);
    CHECK(n == 2);
    CHECK(find(&pos, "18-14") != CK_MOVE_NONE && find(&pos, "18-15") != CK_MOVE_NONE);
    // Kings step all four ways; Dark's king backward (up) too.
    CHECK(ck_from_text(&pos, "B:W30:BK18"));
    CHECK(ck_gen_legal(&pos, moves) == 4);
    CHECK(find(&pos, "18-14") != CK_MOVE_NONE && find(&pos, "18-23") != CK_MOVE_NONE);
}

// The squares m jumps over, into taken; returns how many, -1 if two repeat.
static int captured_squares(CkMove m, int* taken) {
    int count = ck_move_is_jump(m) ? ck_move_steps(m) : 0;
    int s = CK_MOVE_FROM(m);
    for (int i = 0; i < count; i++) {
        int t = ck_move_landing(m, i);
        int over = ck_square_at((ck_square_file(s) + ck_square_file(t)) >> 1,
                                (ck_square_rank(s) + ck_square_rank(t)) >> 1);
        for (int k = 0; k < i; k++)
            if (taken[k] == over)
                return -1;
        taken[i] = over;
        s = t;
    }
    return count;
}

static void ck_king_circuit(void) {
    // Light's king on 22 (c3) goes round d4, f4, f2 and d2 and lands on its
    // own square again, either way round; it takes each man once.
    CHECK(ck_from_text(&pos, "W:WK22:B18,19,26,27"));
    int n = ck_gen_legal(&pos, moves);
    CHECK(n == 2);
    CHECK(find(&pos, "22x15x24x31x22") != CK_MOVE_NONE);
    CHECK(find(&pos, "22x31x24x15x22") != CK_MOVE_NONE);
    for (int i = 0; i < n; i++) {
        int taken[CK_JUMPS_MAX];
        CHECK(captured_squares(moves[i], taken) == ck_move_steps(moves[i]));
        CHECK(ck_move_to(moves[i]) == N(22));
    }
    play(&pos, "22x15x24x31x22");
    for (int s = 0; s < 32; s++)
        CHECK(ck_piece(&pos, s) == (s == N(22) ? (CK_LIGHT | CK_KING) : CK_EMPTY));
    ck_unmake(&pos);
    CHECK(ck_from_text(&other, "W:WK22:B18,19,26,27"));
    CHECK(same_position(&pos, &other));
    // Every jump of a crowded position takes each piece at most once.
    CHECK(ck_from_text(&pos, "B:W18,19,K27,30:B6,10,K14,K23"));
    n = ck_gen_legal(&pos, moves);
    CHECK(n > 0);
    for (int i = 0; i < n; i++) {
        int taken[CK_JUMPS_MAX];
        CHECK(ck_move_is_jump(moves[i]));
        CHECK(captured_squares(moves[i], taken) == ck_move_steps(moves[i]));
    }
}

static void ck_man_jumps_forward_only(void) {
    // Light's man on 22 takes 18 to land on 15; from there a king could take
    // 19 backward to 24, but a man's move ends.
    CHECK(ck_from_text(&pos, "W:W22:B18,19"));
    int n = ck_gen_legal(&pos, moves);
    CHECK(n == 1 && find(&pos, "22x15") != CK_MOVE_NONE);
    int next[4], land = N(15);
    CHECK(ck_continuations(&pos, N(22), &land, 1, next) == 0);
    // A king from the same square goes on.
    CHECK(ck_from_text(&pos, "W:WK22:B18,19"));
    n = ck_gen_legal(&pos, moves);
    CHECK(n == 1 && find(&pos, "22x15x24") != CK_MOVE_NONE);
    CHECK(ck_continuations(&pos, N(22), &land, 1, next) == 1 && next[0] == N(24));
    // The same for Dark, moving down: its man on 6 takes 10 to 15, and a
    // king would then take 11 backward (up) to 8.
    CHECK(ck_from_text(&pos, "B:W10,11:B6"));
    CHECK(ck_gen_legal(&pos, moves) == 1 && find(&pos, "6x15") != CK_MOVE_NONE);
    CHECK(ck_from_text(&pos, "B:W10,11:BK6"));
    CHECK(ck_gen_legal(&pos, moves) == 1 && find(&pos, "6x15x8") != CK_MOVE_NONE);
}

// --- Make and unmake, keys --------------------------------------------------

static void ck_make_unmake_restores(void) {
    static const char* const texts[] = {"B:W10,18,19,K27:B6,K23", "W:WK10,18:B7,8,K15,22",
                                        "B:W21-32:B1-12"};
    for (unsigned t = 0; t < sizeof texts / sizeof texts[0]; t++) {
        CHECK(ck_from_text(&pos, texts[t]));
        CHECK(ck_from_text(&other, texts[t]));
        CkMove list[CK_MAX_MOVES];
        int n = ck_gen_legal(&pos, list);
        CHECK(n > 0);
        for (int i = 0; i < n; i++) {
            ck_make(&pos, list[i]);
            CHECK(ck_ply(&pos) == 1);
            CHECK(ck_side(&pos) != ck_side(&other));
            CHECK(ck_key(&pos) != ck_key(&other));
            CHECK(ck_key_at(&pos, 1) == ck_key(&other));
            ck_unmake(&pos);
            CHECK(ck_ply(&pos) == 0);
            CHECK(same_position(&pos, &other));
            CHECK(ck_clock(&pos) == ck_clock(&other));
        }
    }
    ck_unmake(&pos); // nothing to take back: nothing happens
    CHECK(same_position(&pos, &other));
}

static void ck_key_incremental(void) {
    // A man move, a double jump, a crowning jump, king moves; the key after
    // each is the key of the position set from text.
    CHECK(ck_from_text(&pos, "B:W27,K32:B11,K1"));
    play(&pos, "11-15");
    CHECK(ck_from_text(&other, "W:W27,K32:B15,K1"));
    CHECK(same_position(&pos, &other));
    play(&pos, "27-24");
    play(&pos, "1-5");
    play(&pos, "32-28");
    play(&pos, "15-18");
    play(&pos, "24-20");
    CHECK(ck_from_text(&other, "B:W20,K28:B18,K5"));
    CHECK(same_position(&pos, &other));
    CHECK(ck_ply(&pos) == 6);
    for (int i = 0; i < 6; i++)
        ck_unmake(&pos);
    CHECK(ck_from_text(&other, "B:W27,K32:B11,K1"));
    CHECK(same_position(&pos, &other));
    // Captures and crowning.
    CHECK(ck_from_text(&pos, "B:W19,27:B15"));
    play(&pos, "15x24x31");
    CHECK(ck_from_text(&other, "W:W:BK31"));
    CHECK(same_position(&pos, &other));
    ck_unmake(&pos);
    CHECK(ck_from_text(&other, "B:W19,27:B15"));
    CHECK(same_position(&pos, &other));
}

// --- Results ----------------------------------------------------------------

// Plays plies king moves, none a jump, each to a position not seen before
// and leaving the other side no jump, every ply but the last checked to be
// no result yet; backs out of dead ends. Returns whether it found them.
static bool wander(CheckersPosition* p, int plies) {
    if (plies == 0)
        return true;
    CkMove list[CK_MAX_MOVES], jumps[CK_MAX_MOVES];
    int n = ck_gen_legal(p, list);
    for (int i = 0; i < n; i++) {
        CkMove m = list[i];
        if (ck_move_is_jump(m) || CK_KIND(ck_piece(p, CK_MOVE_FROM(m))) != CK_KING)
            continue;
        ck_make(p, m);
        bool fresh = ck_gen_jumps(p, jumps) == 0;
        for (int back = 1; back <= ck_ply(p) && fresh; back++)
            if (ck_key_at(p, back) == ck_key(p))
                fresh = false;
        if (fresh && ck_clock(p) == ck_ply(p) && (plies == 1 || ck_result(p) == RESULT_NONE) &&
            wander(p, plies - 1))
            return true;
        ck_unmake(p);
    }
    return false;
}

static void ck_results(void) {
    // No pieces, and all blocked: the side to move loses.
    CHECK(ck_from_text(&pos, "W:W:B1"));
    CHECK(ck_result(&pos) == RESULT_NO_MOVES);
    CHECK(ck_from_text(&pos, "W:W29:B22,25"));
    CHECK(ck_gen_legal(&pos, moves) == 0);
    CHECK(ck_result(&pos) == RESULT_NO_MOVES);
    CHECK(ck_from_text(&pos, "B:W29:B22,25"));
    CHECK(ck_result(&pos) == RESULT_NONE);

    // Threefold: two kings shuffle; the start comes up a third time on ply 8.
    static const char* const shuffle[4] = {"1-5", "32-28", "5-1", "28-32"};
    CHECK(ck_from_text(&pos, "B:WK32:BK1"));
    for (int i = 0; i < 8; i++) {
        play(&pos, shuffle[i & 3]);
        CHECK(ck_result(&pos) == (i == 7 ? RESULT_REPETITION : RESULT_NONE));
    }
    // After a man move the count starts afresh (no earlier position can match
    // one after it): the position after the men's moves, seen a second time,
    // is not a draw yet.
    CHECK(ck_from_text(&pos, "B:W30,K32:B3,K1"));
    for (int i = 0; i < 4; i++)
        play(&pos, shuffle[i]);
    play(&pos, "3-8");
    CHECK(ck_clock(&pos) == 0);
    play(&pos, "30-26");
    for (int i = 0; i < 4; i++)
        play(&pos, shuffle[i]);
    CHECK(ck_result(&pos) == RESULT_NONE);

    // Forty moves each with no capture and no man moved: kings wander (found
    // by a search that backs out of dead ends), never to a position seen
    // before and never into a capture.
    CHECK(ck_from_text(&pos, "B:W22,K29,K32:B11,K1,K4"));
    CHECK(wander(&pos, 80));
    CHECK(ck_ply(&pos) == 80);
    CHECK(ck_result(&pos) == RESULT_FORTY);
    ck_unmake(&pos);
    CHECK(ck_clock(&pos) == 79);
    CHECK(ck_result(&pos) == RESULT_NONE);
    // A man move resets the clock; a king's does not.
    CHECK(ck_from_text(&pos, "B:W22,K32:B11,K1"));
    play(&pos, "1-5");
    play(&pos, "32-28");
    CHECK(ck_clock(&pos) == 2);
    play(&pos, "11-15");
    CHECK(ck_clock(&pos) == 0);
    CHECK(ck_result(&pos) == RESULT_NONE);

    // The length cap (by setting the ply count, as for chess).
    ck_start(&pos);
    pos.ply = GAME_PLY_MAX - 1;
    CHECK(ck_result(&pos) == RESULT_NONE);
    pos.ply = GAME_PLY_MAX;
    CHECK(ck_result(&pos) == RESULT_LENGTH);
}

// --- Notation -----------------------------------------------------------------

static bool notation_is(const char* text, const char* want) {
    if (!ck_from_text(&pos, text))
        return false;
    CkMove m = find(&pos, want);
    if (m == CK_MOVE_NONE)
        return false;
    char out[24];
    return ck_notation(&pos, m, out) == (int)strlen(want) && strcmp(out, want) == 0;
}

static void ck_notation_cases(void) {
    CHECK(notation_is("B:W21-32:B1-12", "11-15"));
    CHECK(notation_is("B:W19:B15", "15x24"));
    CHECK(notation_is("B:W19,27:B15", "15x24x31"));
    CHECK(notation_is("W:W21-32:B1-12", "22-18"));
    CHECK(notation_is("W:WK18:B22", "18x25"));
    // Every opening move of the start position, in the order of the board.
    ck_start(&pos);
    int n = ck_gen_legal(&pos, moves);
    CHECK(n == 7);
    static const char* const opening[7] = {"9-13",  "9-14",  "10-14", "10-15",
                                           "11-15", "11-16", "12-16"};
    for (int i = 0; i < 7; i++)
        CHECK(find(&pos, opening[i]) != CK_MOVE_NONE);
}

TEST_SUITE(checkers_tests, "checkers", {"ck_perft_start", ck_perft_start},
           {"ck_perft_positions", ck_perft_positions}, {"ck_squares", ck_squares},
           {"ck_text_rejects", ck_text_rejects}, {"ck_forced_capture", ck_forced_capture},
           {"ck_multi_jump", ck_multi_jump}, {"ck_crowning_ends_move", ck_crowning_ends_move},
           {"ck_king_backward_capture", ck_king_backward_capture},
           {"ck_king_circuit", ck_king_circuit},
           {"ck_man_jumps_forward_only", ck_man_jumps_forward_only},
           {"ck_make_unmake_restores", ck_make_unmake_restores},
           {"ck_key_incremental", ck_key_incremental}, {"ck_results", ck_results},
           {"ck_notation_cases", ck_notation_cases});
