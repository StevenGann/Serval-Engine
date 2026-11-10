// boardgames: Paws & Kings' search, evaluations and match
// (examples/boardgames/search.c, chess_eval.c, checkers_eval.c, match.c),
// natively. The search must find mates and win material in both games, give
// exactly the same answer however its work is split into steps, vary with
// its seed at the weakest strength, and only ever answer legal moves; the
// evaluations must be colour-symmetric; the match must undo, replay and
// refuse moves after the end as the play screen expects.

#include "test.h"

#include <string.h>

#include <serval/save.h>

#include "../examples/boardgames/checkers_eval.h"
#include "../examples/boardgames/chess_eval.h"
#include "../examples/boardgames/chess_rules.h"
#include "../examples/boardgames/match.h"
#include "../examples/boardgames/records.h"
#include "../src/core/save_internal.h" // a save memory that refuses writes

// Positions and matches are large (they carry their history): static.
static Position cpos, cother;
static CheckersPosition kpos, kother;
static Match match;
static Move cmoves[MAX_MOVES];
static CkMove kmoves[CK_MAX_MOVES];

// The legal chess move written in SAN, or MOVE_NONE.
static Move chess_move(Position* p, const char* san) {
    int n = pos_gen_legal(p, cmoves);
    char out[8];
    for (int i = 0; i < n; i++) {
        chess_san(p, cmoves[i], out);
        if (strcmp(out, san) == 0)
            return cmoves[i];
    }
    return MOVE_NONE;
}

// The legal checkers move written as in the notation, or CK_MOVE_NONE.
static CkMove ck_move(CheckersPosition* p, const char* text) {
    int n = ck_gen_legal(p, kmoves);
    char out[24];
    for (int i = 0; i < n; i++) {
        ck_notation(p, kmoves[i], out);
        if (strcmp(out, text) == 0)
            return kmoves[i];
    }
    return CK_MOVE_NONE;
}

static bool chess_legal(Position* p, u32 m) {
    int n = pos_gen_legal(p, cmoves);
    for (int i = 0; i < n; i++)
        if (cmoves[i] == m)
            return true;
    return false;
}

static bool ck_legal(CheckersPosition* p, u32 m) {
    int n = ck_gen_legal(p, kmoves);
    for (int i = 0; i < n; i++)
        if (kmoves[i] == m)
            return true;
    return false;
}

// Runs a search to the end in one step.
static u32 think(const SearchGame* g, const void* pos, const SearchLevel* lv, u32 seed) {
    search_start(g, pos, lv, seed);
    while (!search_step(0xFFFFFFFFu)) {
    }
    return search_best();
}

static u32 think_chess(const char* fen, int level, u32 seed) {
    CHECK(pos_from_fen(&cpos, fen));
    return think(&chess_search_game, &cpos, chess_level(level), seed);
}

// --- The search ---------------------------------------------------------------

static void search_mate_in_one(void) {
    u32 m = think_chess("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", LEVEL_SERVAL, 1);
    CHECK(m == chess_move(&cpos, "Rd8#"));
    CHECK(search_score() == SEARCH_MATE - 1);
}

// Légal's mate: after 5...Bxd1, 6.Bxf7+ Ke7 7.Nd5#.
static void search_mate_in_two(void) {
    u32 m = think_chess("rn1qkbnr/ppp2p1p/3p2p1/4N3/2B1P3/2N5/PPPP1PPP/R1BbK2R w KQkq - 0 6",
                        LEVEL_SERVAL, 1);
    CHECK(m == chess_move(&cpos, "Bxf7+"));
    CHECK(search_score() == SEARCH_MATE - 3);
}

// Queen and king mate in four (seven plies): many lines reach the same
// positions at different plies, so the mate scores the table keeps must be
// re-based to each ply that reads them.
static void search_mate_through_table(void) {
    static const SearchLevel seven = {7, 0xFFFFFFFFu, 0, 0, 0, 0};
    CHECK(pos_from_fen(&cpos, "8/8/8/8/8/8/Q7/K2k4 w - - 0 1"));
    think(&chess_search_game, &cpos, &seven, 1);
    CHECK(search_score() == SEARCH_MATE - 7);
}

static void search_takes_hanging_queen(void) {
    // Black's queen on e4 stands alone in front of the knight on c3.
    u32 m = think_chess("rnb1kbnr/pppp1ppp/8/8/4q3/2N5/PPPPPPPP/R1BQKBNR w KQkq - 0 1",
                        LEVEL_HUNTER, 1);
    CHECK(m == chess_move(&cpos, "Nxe4"));
}

// Light (moving up) gives a man to take two: 23-19 offers the man on 19 (f4);
// Dark's only capture is 16x23 (g5 over f4 to e3: e5's way to g3 is blocked by
// Light's man on 24, and from e3 Dark cannot go on, 30 and 27's far side being
// taken or empty), and then Light's man on 26 jumps 23 and 15, 26x19x10. Any
// other move gives Light nothing, and 24-19 or 23-18 lose a man.
static void search_checkers_wins_material(void) {
    CHECK(ck_from_text(&kpos, "W:W23,24,26,30:B1,2,3,15,16"));
    u32 m = think(&checkers_search_game, &kpos, checkers_level(LEVEL_HUNTER), 1);
    CHECK(m == ck_move(&kpos, "23-19"));
    // The line goes as the comment says.
    ck_make(&kpos, ck_move(&kpos, "23-19"));
    CHECK(ck_gen_legal(&kpos, kmoves) == 1);
    CHECK(ck_move(&kpos, "16x23") != CK_MOVE_NONE);
    ck_make(&kpos, ck_move(&kpos, "16x23"));
    CHECK(ck_move(&kpos, "26x19x10") != CK_MOVE_NONE);
}

// The same search split into steps of 37 nodes and done in one.
static void resume_case(const SearchGame* g, const void* pos, const SearchLevel* lv) {
    search_start(g, pos, lv, 7);
    int steps = 0;
    while (!search_step(37))
        steps++;
    u32 best = search_best(), nodes = search_nodes();
    int score = search_score(), depth = search_depth();
    CHECK(steps > 10);
    CHECK(!search_running());
    think(g, pos, lv, 7);
    CHECK(search_best() == best);
    CHECK(search_score() == score);
    CHECK(search_depth() == depth);
    CHECK(search_nodes() == nodes);
}

static void search_resume_same_as_whole(void) {
    pos_start(&cpos);
    resume_case(&chess_search_game, &cpos, chess_level(LEVEL_HUNTER));
    CHECK(pos_from_fen(&cpos, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -"));
    resume_case(&chess_search_game, &cpos, chess_level(LEVEL_HUNTER));
    ck_start(&kpos);
    resume_case(&checkers_search_game, &kpos, checkers_level(LEVEL_HUNTER));
    // A search that stops on its node limit (SERVAL's) resumes exactly too.
    resume_case(&checkers_search_game, &kpos, checkers_level(LEVEL_SERVAL));
}

static void seeded_case(const SearchGame* g, const void* pos, const SearchLevel* lv) {
    u32 seen[20];
    int distinct = 0;
    for (u32 seed = 1; seed <= 20; seed++) {
        u32 m = think(g, pos, lv, seed);
        bool known = false;
        for (int i = 0; i < distinct; i++)
            known |= seen[i] == m;
        if (!known)
            seen[distinct++] = m;
    }
    CHECK(distinct >= 3);
    u32 a = think(g, pos, lv, 12345), na = search_nodes();
    u32 b = think(g, pos, lv, 12345), nb = search_nodes();
    CHECK(a == b);
    CHECK(na == nb);
}

static void search_seeded(void) {
    pos_start(&cpos);
    seeded_case(&chess_search_game, &cpos, chess_level(LEVEL_CUB));
    ck_start(&kpos);
    seeded_case(&checkers_search_game, &kpos, checkers_level(LEVEL_CUB));
}

static void search_single_move(void) {
    CHECK(pos_from_fen(&cpos, "7k/8/8/8/8/8/6q1/7K w - - 0 1"));
    search_start(&chess_search_game, &cpos, chess_level(LEVEL_SERVAL), 1);
    CHECK(search_step(1));
    CHECK(search_best() == chess_move(&cpos, "Kxg2"));
    CHECK(!search_running());
    // Dark's man on 1 must jump Light's on 6.
    CHECK(ck_from_text(&kpos, "B:W6:B1"));
    search_start(&checkers_search_game, &kpos, checkers_level(LEVEL_SERVAL), 1);
    CHECK(search_step(1));
    CHECK(search_best() == ck_move(&kpos, "1x10"));
}

// A small generator of the tests' own for random play.
static u32 lcg(u32* s) {
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

// --- The pace ---------------------------------------------------------------------

// The GBA's frames simulated: other work of `other` cycles, and the
// search's work costing 160 to 240 cycles a unit, varying from frame to
// frame (chess's spread on the GBA, measured); the frame's cycles fed back as
// frame_cpu_cycles() would. Returns the busiest frame after the first 30,
// and the average, as thousandths of the budget.
static void pace_case(u32 other, u32* busiest, u32* average) {
    const u32 budget = 280896;
    SearchPace pace;
    search_pace_init(&pace);
    u32 s = 17, cycles = 0, work = 0, sum = 0, frames = 0;
    *busiest = 0;
    for (int frame = 0; frame < 1030; frame++) {
        u32 n = search_pace(&pace, cycles, work, budget);
        CHECK(n >= 1 && n <= SEARCH_PACE_MAX);
        cycles = other + n * (160 + (lcg(&s) & 0xFFFF) * 80 / 0x10000);
        work = n;
        if (frame >= 30) {
            u32 permille = cycles * 1000 / budget;
            if (permille > *busiest)
                *busiest = permille;
            sum += permille;
            frames++;
        }
    }
    *average = sum / frames;
}

static void search_pace_adapts(void) {
    SearchPace pace;
    search_pace_init(&pace);
    // With nothing learnt yet, and from a frame that did not think, the
    // starting allowance.
    CHECK(search_pace(&pace, 0, 0, 280896) == SEARCH_WORK_PER_FRAME);
    CHECK(search_pace(&pace, 250000, 0, 280896) == SEARCH_WORK_PER_FRAME);
    // A frame that thought cheaply asks for more; one that ran long, less.
    CHECK(search_pace(&pace, 100000, SEARCH_WORK_PER_FRAME, 280896) > SEARCH_WORK_PER_FRAME);
    search_pace_init(&pace);
    CHECK(search_pace(&pace, 270000, SEARCH_WORK_PER_FRAME, 280896) < SEARCH_WORK_PER_FRAME);
    // The frame that finished a search, a few units of work in a frame of
    // other work, is no sample: the next search does not start slow.
    search_pace_init(&pace);
    u32 given = search_pace(&pace, 0, 0, 280896);
    u32 next = search_pace(&pace, 100000, given, 280896);
    CHECK(search_pace(&pace, 120000, 3, 280896) == next);
    // Simulated: the frames settle near SEARCH_PACE_TARGET (76% of the
    // budget) and none reaches 95%, with little or much other work.
    u32 busiest, average;
    static const u32 others[3] = {20000, 60000, 120000};
    for (int i = 0; i < 3; i++) {
        pace_case(others[i], &busiest, &average);
        CHECK(average >= 650 && average <= 800);
        CHECK(busiest < 950);
    }
}

static void search_legal_always(void) {
    u32 s = 99;
    for (int game = 0; game < 50; game++) {
        int plies = (int)(lcg(&s) & 31);
        pos_start(&cpos);
        for (int i = 0; i < plies && chess_result(&cpos) == RESULT_NONE; i++) {
            int n = pos_gen_legal(&cpos, cmoves);
            pos_make(&cpos, cmoves[(int)((lcg(&s) & 0xFFFF) * (u32)n >> 16)]);
        }
        if (chess_result(&cpos) == RESULT_NONE) {
            CHECK(chess_legal(&cpos, think(&chess_search_game, &cpos, chess_level(LEVEL_CUB), s)));
            CHECK(
                chess_legal(&cpos, think(&chess_search_game, &cpos, chess_level(LEVEL_HUNTER), s)));
        }
        ck_start(&kpos);
        for (int i = 0; i < plies && ck_result(&kpos) == RESULT_NONE; i++) {
            int n = ck_gen_legal(&kpos, kmoves);
            ck_make(&kpos, kmoves[(int)((lcg(&s) & 0xFFFF) * (u32)n >> 16)]);
        }
        if (ck_result(&kpos) == RESULT_NONE) {
            CHECK(
                ck_legal(&kpos, think(&checkers_search_game, &kpos, checkers_level(LEVEL_CUB), s)));
            CHECK(ck_legal(&kpos,
                           think(&checkers_search_game, &kpos, checkers_level(LEVEL_HUNTER), s)));
        }
    }
}

// A plain recursive negamax with the same rules, moves sorted by the game's
// order() value alone (no table, no killers): at a depth too shallow for a repetition, the
// resumable search's score must be exactly its value.
static union {
    ChessKey align;
    u8 bytes[SEARCH_POS_BYTES];
} ref_buf;
static u32 ref_moves[SEARCH_PLY_MAX][MAX_MOVES];

static int ref_negamax(const SearchGame* g, int alpha, int beta, int depth, int ply) {
    void* p = ref_buf.bytes;
    if (ply > 0 && g->is_draw(p))
        return 0;
    if (ply >= SEARCH_PLY_MAX - 1)
        return g->eval(p);
    int best = -SEARCH_INF, n;
    if (depth <= 0) {
        bool all = g->quiesce_in_check_all && g->in_check && g->in_check(p);
        n = g->gen(p, ref_moves[ply], !all);
        if (all) {
            if (n == 0)
                return g->no_moves_score(p, ply);
        } else if (!(n > 0 && g->noisy_forced)) {
            int stand = g->eval(p);
            if (stand >= beta || n == 0)
                return stand;
            best = stand;
            if (stand > alpha)
                alpha = stand;
        }
    } else {
        n = g->gen(p, ref_moves[ply], false);
        if (n == 0)
            return g->no_moves_score(p, ply);
    }
    u32* list = ref_moves[ply];
    for (int i = 1; i < n; i++) { // insertion sort, best first
        u32 m = list[i];
        int v = g->order(p, m), j = i;
        for (; j > 0 && g->order(p, list[j - 1]) < v; j--)
            list[j] = list[j - 1];
        list[j] = m;
    }
    for (int i = 0; i < n; i++) {
        g->make(p, list[i]);
        int v = -ref_negamax(g, -beta, -alpha, depth - 1, ply + 1);
        g->unmake(p);
        if (v > best) {
            best = v;
            if (v > alpha)
                alpha = v;
            if (alpha >= beta)
                break;
        }
    }
    return best;
}

static void reference_case(const SearchGame* g, const void* pos, int depth) {
    static const SearchLevel fixed[8] = {
        {1, 0xFFFFFFFFu, 0, 0, 0, 0}, {2, 0xFFFFFFFFu, 0, 0, 0, 0}, {3, 0xFFFFFFFFu, 0, 0, 0, 0},
        {4, 0xFFFFFFFFu, 0, 0, 0, 0}, {5, 0xFFFFFFFFu, 0, 0, 0, 0}, {6, 0xFFFFFFFFu, 0, 0, 0, 0},
        {7, 0xFFFFFFFFu, 0, 0, 0, 0}, {8, 0xFFFFFFFFu, 0, 0, 0, 0}};
    memcpy(ref_buf.bytes, pos, g->pos_size);
    int want = ref_negamax(g, -SEARCH_INF, SEARCH_INF, depth, 0);
    think(g, pos, &fixed[depth - 1], 1);
    CHECK(search_depth() == depth);
    CHECK(search_score() == want);
}

static void search_matches_plain_negamax(void) {
    static const char* const fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
        "rn1qkbnr/ppp2p1p/3p2p1/4N3/2B1P3/2N5/PPPP1PPP/R1BbK2R w KQkq - 0 6",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    };
    for (int i = 0; i < 5; i++) {
        CHECK(pos_from_fen(&cpos, fens[i]));
        reference_case(&chess_search_game, &cpos, i == 1 ? 2 : 3);
    }
    static const char* const boards[] = {
        "B:W21-32:B1-12",
        "W:W23,24,26,30:B1,2,3,15,16",
        "B:W17,21,22,23,24,K26,28:B1,2,5,K10,11,12,15",
    };
    for (int i = 0; i < 3; i++) {
        CHECK(ck_from_text(&kpos, boards[i]));
        reference_case(&checkers_search_game, &kpos, 5);
    }
}

// --- The evaluations ------------------------------------------------------------

static void eval_symmetric(void) {
    pos_start(&cpos);
    CHECK(pos_from_fen(&cother, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1"));
    CHECK(chess_eval(&cpos) == chess_eval(&cother));
    // Kiwipete, and the same with the colours swapped and the board turned
    // over (ranks reversed, files kept).
    CHECK(pos_from_fen(&cpos, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -"));
    CHECK(
        pos_from_fen(&cother, "r3k2r/pppbbppp/2n2q1P/1P2p3/3pn3/BN2PNP1/P1PPQPB1/R3K2R b KQkq -"));
    CHECK(chess_eval(&cpos) == chess_eval(&cother));
    CHECK(chess_eval(&cpos) != 0);
    // An endgame with passed, doubled and isolated pawns.
    CHECK(pos_from_fen(&cpos, "8/5k2/8/1P6/1P4p1/8/5K2/8 w - -"));
    CHECK(pos_from_fen(&cother, "8/5k2/8/1p4P1/1p6/8/5K2/8 b - -"));
    CHECK(chess_eval(&cpos) == chess_eval(&cother));
    // Checkers: the start, Light to move, is the start turned round.
    ck_start(&kpos);
    CHECK(ck_from_text(&kother, "W:W21-32:B1-12"));
    CHECK(ck_eval(&kpos) == ck_eval(&kother));
    // Square s turned half round is 33 - s (as printed).
    CHECK(ck_from_text(&kpos, "B:W18,22,K27,30:B1,K9,14,15"));
    CHECK(ck_from_text(&kother, "W:W18,19,K24,32:B3,K6,11,15"));
    CHECK(ck_eval(&kpos) == ck_eval(&kother));
    CHECK(ck_eval(&kpos) != 0);
}

// The position as FEN (the move counters left as "0 1").
static void write_fen(const Position* p, char* out) {
    static const char letters[] = " PNBRQK  pnbrqk";
    for (int rank = 7; rank >= 0; rank--) {
        int empty = 0;
        for (int file = 0; file < 8; file++) {
            Piece pc = pos_piece(p, SQ(file, rank));
            if (!pc) {
                empty++;
                continue;
            }
            if (empty)
                *out++ = (char)('0' + empty);
            empty = 0;
            *out++ = letters[pc];
        }
        if (empty)
            *out++ = (char)('0' + empty);
        if (rank)
            *out++ = '/';
    }
    *out++ = ' ';
    *out++ = pos_side(p) == WHITE ? 'w' : 'b';
    *out++ = ' ';
    u8 c = pos_castling(p);
    for (int i = 0; i < 4; i++)
        if (c & (1 << i))
            *out++ = "KQkq"[i];
    if (!c)
        *out++ = '-';
    *out++ = ' ';
    int ep = pos_ep_square(p);
    if (ep >= 0) {
        *out++ = (char)('a' + SQ_FILE(ep));
        *out++ = (char)('1' + SQ_RANK(ep));
    } else {
        *out++ = '-';
    }
    strcpy(out, " 0 1");
}

// The evaluation as first written, from the board alone: material,
// piece-square tables, the kings tapered by the phase, doubled, isolated and
// passed pawns.
static int reference_eval(const Position* p) {
    static const u8 bonus[8] = {0, 8, 12, 20, 34, 56, 90, 0};
    int score[2] = {0, 0}, phase = 0, king[2] = {0, 0};
    int count[2][10] = {{0}}, low[10], high[10]; // by file + 1: White's lowest, Black's highest
    for (int f = 0; f < 10; f++)
        low[f] = 8, high[f] = -1;
    for (int sq = 0; sq < 64; sq++) {
        Piece pc = pos_piece(p, sq);
        if (!pc)
            continue;
        int kind = PIECE_KIND(pc), c = PIECE_COLOR(pc) >> 3, idx = c ? sq : sq ^ 56;
        if (kind == KING) {
            king[c] = idx;
            continue;
        }
        score[c] += chess_piece_value[kind] + chess_pst[kind][idx];
        phase += chess_phase_weight[kind];
        if (kind == PAWN) {
            int f = SQ_FILE(sq) + 1, r = SQ_RANK(sq);
            count[c][f]++;
            if (c == 0 && r < low[f])
                low[f] = r;
            if (c == 1 && r > high[f])
                high[f] = r;
        }
    }
    for (int sq = 0; sq < 64; sq++) {
        Piece pc = pos_piece(p, sq);
        if (PIECE_KIND(pc) != PAWN)
            continue;
        int c = PIECE_COLOR(pc) >> 3, f = SQ_FILE(sq) + 1, r = SQ_RANK(sq);
        if (count[c][f - 1] == 0 && count[c][f + 1] == 0)
            score[c] -= 12;
        bool passed = true;
        for (int g = f - 1; g <= f + 1; g++)
            passed &= c == 0 ? high[g] <= r : low[g] >= r;
        if (passed)
            score[c] += bonus[c ? 7 - r : r];
    }
    for (int c = 0; c < 2; c++) {
        for (int f = 1; f <= 8; f++)
            if (count[c][f] > 1)
                score[c] -= 14 * (count[c][f] - 1);
    }
    if (phase > 24)
        phase = 24;
    for (int c = 0; c < 2; c++)
        score[c] += (chess_king_mid[king[c]] * phase + chess_king_end[king[c]] * (24 - phase)) / 24;
    int s = score[0] - score[1];
    return pos_side(p) == WHITE ? s : -s;
}

// The evaluation reads sums that make and unmake keep: after every move made
// and taken back in random games (captures, castling, en passant and
// promotions among them) it equals the evaluation of the same position set
// up afresh, and the evaluation as first written, from the board.
static void eval_incremental(void) {
    static const char* const fens[] = {
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    };
    char fen[100];
    u32 s = 5;
    int checked = 0;
    for (int f = 0; f < 4; f++) {
        for (int game = 0; game < 25; game++) {
            CHECK(pos_from_fen(&cpos, fens[f]));
            int made = 0;
            for (; made < 40 && chess_result(&cpos) == RESULT_NONE; made++) {
                int n = pos_gen_legal(&cpos, cmoves);
                pos_make(&cpos, cmoves[(int)((lcg(&s) & 0xFFFF) * (u32)n >> 16)]);
                write_fen(&cpos, fen);
                CHECK(pos_from_fen(&cother, fen));
                CHECK(chess_eval(&cpos) == chess_eval(&cother));
                CHECK(chess_eval(&cpos) == reference_eval(&cpos));
                checked++;
            }
            for (; made > 0; made--) {
                pos_unmake(&cpos);
                write_fen(&cpos, fen);
                CHECK(pos_from_fen(&cother, fen));
                CHECK(chess_eval(&cpos) == chess_eval(&cother));
            }
        }
    }
    CHECK(checked > 2000);
}

// --- The match ------------------------------------------------------------------

// Plays the move written in the notation (SAN or checkers').
static bool play(Match* m, const char* text) {
    if (m->game == GAME_CHESS)
        return match_play(m, chess_move(&m->pos.chess, text));
    return match_play(m, ck_move(&m->pos.checkers, text));
}

static void match_undo_edges(void) {
    // Chess, one player, the human White.
    match_new(&match, GAME_CHESS, MODE_ONE_PLAYER, WHITE, LEVEL_CUB);
    CHECK(match_undo(&match) == 0);
    CHECK(!match_computer_to_move(&match));
    CHECK(play(&match, "e4"));
    CHECK(match_computer_to_move(&match));
    CHECK(play(&match, "e5"));
    CHECK(match_undo(&match) == 2);
    CHECK(match.count == 0);
    CHECK(match_side_to_move(&match) == WHITE);
    CHECK(!match_computer_to_move(&match));
    // After a result (the fool's mate, the human Black): undo clears it.
    match_new(&match, GAME_CHESS, MODE_ONE_PLAYER, BLACK, LEVEL_CUB);
    CHECK(play(&match, "f3") && play(&match, "e5") && play(&match, "g4") && play(&match, "Qh4#"));
    CHECK(match.result == RESULT_CHECKMATE);
    CHECK(!match_computer_to_move(&match));
    CHECK(match_undo(&match) == 1); // the human's mate: back to the human's turn
    CHECK(match.result == RESULT_NONE);
    CHECK(match_side_to_move(&match) == BLACK);
    CHECK(!match_computer_to_move(&match));
    CHECK(play(&match, "Qh4#"));
    // The computer moved first and that is all: back to the start.
    match_new(&match, GAME_CHESS, MODE_ONE_PLAYER, BLACK, LEVEL_CUB);
    CHECK(play(&match, "Nf3"));
    CHECK(match_undo(&match) == 1);
    CHECK(match.count == 0 && match_computer_to_move(&match));
    // Two players: one ply.
    match_new(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    CHECK(play(&match, "d4") && play(&match, "d5"));
    CHECK(match_undo(&match) == 1);
    CHECK(match_side_to_move(&match) == BLACK);
    // Checkers, one player, the human Dark.
    match_new(&match, GAME_CHECKERS, MODE_ONE_PLAYER, CK_DARK, LEVEL_CUB);
    CHECK(match_undo(&match) == 0);
    CHECK(play(&match, "11-15") && play(&match, "22-18"));
    CHECK(match_undo(&match) == 2);
    CHECK(match_side_to_move(&match) == CK_DARK);
    CHECK(match.count == 0);
    // Checkers to a result: Light, the human, wins by taking Dark's last man.
    match_new(&match, GAME_CHECKERS, MODE_ONE_PLAYER, CK_LIGHT, LEVEL_CUB);
    CHECK(ck_from_text(&match.pos.checkers, "B:W22:B14")); // a test's shortcut to an ending
    CHECK(play(&match, "14-18"));
    CHECK(play(&match, "22x15"));
    CHECK(match.result == RESULT_NO_MOVES);
    CHECK(!match_computer_to_move(&match));
    CHECK(!play(&match, "14-17"));
    CHECK(match_undo(&match) == 1);
    CHECK(match.result == RESULT_NONE);
    CHECK(match_side_to_move(&match) == CK_LIGHT);
    CHECK(match_undo(&match) == 1); // the computer's 14-18: Light can't move before it
    CHECK(match.count == 0 && match_computer_to_move(&match));
    CHECK(play(&match, "14-17"));
    CHECK(play(&match, "22x13"));
    // Two players: one ply.
    match_new(&match, GAME_CHECKERS, MODE_TWO_PLAYERS, CK_DARK, LEVEL_CUB);
    CHECK(play(&match, "9-13") && play(&match, "22-18"));
    CHECK(match_undo(&match) == 1);
    CHECK(match_side_to_move(&match) == CK_LIGHT);
}

static void match_replay_rejects(void) {
    match_new(&match, GAME_CHESS, MODE_ONE_PLAYER, WHITE, LEVEL_HUNTER);
    pos_start(&cpos);
    u32 moves[3];
    moves[0] = chess_move(&cpos, "e4");
    pos_make(&cpos, (Move)moves[0]);
    moves[1] = chess_move(&cpos, "e5");
    moves[2] = MOVE_MAKE(SQ(4, 3), SQ(4, 4), MF_QUIET); // e4-e5, blocked
    CHECK(match_replay(&match, moves, 2));
    CHECK(match.count == 2);
    CHECK(!match_replay(&match, moves, 3));
    CHECK(match.count == 0);
    CHECK(match.game == GAME_CHESS && match.level == LEVEL_HUNTER && match.human_side == WHITE);
    CHECK(match_side_to_move(&match) == WHITE);
    // Checkers likewise.
    match_new(&match, GAME_CHECKERS, MODE_TWO_PLAYERS, CK_LIGHT, LEVEL_SERVAL);
    ck_start(&kpos);
    moves[0] = ck_move(&kpos, "11-15");
    ck_make(&kpos, moves[0]);
    moves[1] = ck_move(&kpos, "23-19");
    moves[2] = moves[0];
    CHECK(!match_replay(&match, moves, 3));
    CHECK(match.count == 0);
    CHECK(match.game == GAME_CHECKERS && match.mode == MODE_TWO_PLAYERS);
}

static void match_refuses_finished(void) {
    match_new(&match, GAME_CHESS, MODE_ONE_PLAYER, WHITE, LEVEL_CUB);
    CHECK(play(&match, "f3") && play(&match, "e5") && play(&match, "g4"));
    CHECK(play(&match, "Qh4#"));
    CHECK(match.result == RESULT_CHECKMATE);
    CHECK(!match_computer_to_move(&match));
    int n = pos_gen_legal(&match.pos.chess, cmoves);
    CHECK(n == 0);
    CHECK(!match_play(&match, MOVE_MAKE(SQ(7, 1), SQ(7, 2), MF_QUIET)));
    CHECK(match.count == 4);
    // An illegal move in a game going on changes nothing.
    match_new(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    CHECK(!match_play(&match, MOVE_MAKE(SQ(4, 1), SQ(4, 4), MF_QUIET)));
    CHECK(match.count == 0);
}

// Moves saved as indices come back as the same game, a checkers multi-jump
// included.
static void match_indices_round_trip(void) {
    static const char* const chess_line[] = {"e4", "d5", "exd5", "Qxd5", "Nc3", "Qa5", "O-O-O"};
    match_new(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    for (int i = 0; i < 6; i++)
        CHECK(play(&match, chess_line[i]));
    CHECK(!play(&match, chess_line[6]));
    u8 idx[16];
    for (int i = 0; i < 6; i++) {
        int k = match_move_index(&match, i);
        CHECK(k >= 0 && k < 256);
        idx[i] = (u8)k;
    }
    CHECK(match_move_index(&match, 6) == -1);
    ChessKey key = pos_key(&match.pos.chess);
    char text[24];
    CHECK(match_notation(&match, 3, text) == 4 && strcmp(text, "Qxd5") == 0);
    CHECK(match_replay_indices(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB, idx, 6));
    CHECK(match.count == 6);
    CHECK(pos_key(&match.pos.chess) == key);
    CHECK(match_notation(&match, 5, text) == 3 && strcmp(text, "Qa5") == 0);
    idx[6] = 255;
    CHECK(!match_replay_indices(&match, GAME_CHESS, MODE_ONE_PLAYER, BLACK, LEVEL_HUNTER, idx, 7));
    CHECK(match.count == 0 && match.mode == MODE_ONE_PLAYER && match.human_side == BLACK);

    // Checkers, to Light's double jump 22x15x6.
    static const char* const ck_line[] = {"9-13", "21-17", "6-9", "17-14", "9x18", "22x15x6"};
    match_new(&match, GAME_CHECKERS, MODE_TWO_PLAYERS, CK_DARK, LEVEL_CUB);
    for (int i = 0; i < 6; i++)
        CHECK(play(&match, ck_line[i]));
    for (int i = 0; i < 6; i++) {
        int k = match_move_index(&match, i);
        CHECK(k >= 0 && k < 256);
        idx[i] = (u8)k;
    }
    key = ck_key(&match.pos.checkers);
    CHECK(
        match_replay_indices(&match, GAME_CHECKERS, MODE_TWO_PLAYERS, CK_DARK, LEVEL_CUB, idx, 6));
    CHECK(match.count == 6);
    CHECK(ck_key(&match.pos.checkers) == key);
    CHECK(match_notation(&match, 5, text) == 7 && strcmp(text, "22x15x6") == 0);
    CHECK(match_notation(&match, 6, text) == 0);
}

// The index each move is stored with as it is played is the one a fresh
// replay finds, for both games, and undo keeps the list right.
static void match_stores_indices(void) {
    match_new(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    for (int i = 0; i < 30; i++) {
        int n = pos_gen_legal(&match.pos.chess, cmoves);
        CHECK(n > 0);
        CHECK(match_play(&match, cmoves[(i * 7 + 3) % n]));
    }
    CHECK(match.count == 30);
    pos_start(&cpos);
    for (int i = 0; i < 30; i++) {
        int n = pos_gen_legal(&cpos, cmoves), found = -1;
        for (int k = 0; k < n; k++)
            if (cmoves[k] == match.moves[i])
                found = k;
        CHECK(found >= 0);
        CHECK(match.move_index[i] == found);
        CHECK(match_move_index(&match, i) == found);
        pos_make(&cpos, (Move)match.moves[i]);
    }
    // Undo, then a different move at that ply: its index replaces the old.
    CHECK(match_undo(&match) == 1);
    int n = pos_gen_legal(&match.pos.chess, cmoves);
    CHECK(n > 1);
    CHECK(match_play(&match, cmoves[n - 1]));
    CHECK(match_move_index(&match, 29) == n - 1);
    // Checkers: a game of the first move each time.
    match_new(&match, GAME_CHECKERS, MODE_TWO_PLAYERS, CK_DARK, LEVEL_CUB);
    for (int i = 0; i < 30 && match.result == RESULT_NONE; i++) {
        int k = ck_gen_legal(&match.pos.checkers, kmoves);
        CHECK(match_play(&match, kmoves[(i * 5) % k]));
        CHECK(match_move_index(&match, i) == (i * 5) % k);
    }
}

// A side with no move is mated before the fifty-move rule draws: mate on the
// move that reaches halfmove 100 scores as mate.
static void search_mate_beats_fifty(void) {
    u32 m = think_chess("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 99 80", LEVEL_SERVAL, 1);
    CHECK(m == chess_move(&cpos, "Rd8#"));
    CHECK(search_score() == SEARCH_MATE - 1);
}

// A search out of nodes before its first iteration is whole answers with
// the best of the root's moves it has searched (search_depth() 0), the same
// stepped as whole.
#define KIWIPETE "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -"
static void search_first_iteration_cut(void) {
    static const SearchLevel tiny = {8, 5, 0, 0, 0, 0};
    CHECK(pos_from_fen(&cpos, KIWIPETE));
    u32 m = think(&chess_search_game, &cpos, &tiny, 1);
    CHECK(search_depth() == 0);
    // The first root move by the order, Bxa6 (the best capture), is the one
    // searched to the end by then (about 45 nodes); the next has just begun.
    CHECK(m == chess_move(&cpos, "Bxa6"));
    u32 nodes = search_nodes();
    int score = search_score();
    search_start(&chess_search_game, &cpos, &tiny, 1);
    while (!search_step(1)) {
    }
    CHECK(search_best() == m);
    CHECK(search_score() == score);
    CHECK(search_nodes() == nodes);
}

// The partial first iteration chooses as a whole one does: with CUB's random
// margin and weak moves, among the root's moves it has searched, by the seed.
// (From the start: each root move takes a node or two, and many are near
// equal. In Kiwipete the first iteration takes about 7,000 nodes, and CUB
// stops at 1,500, so CUB always plays from a partial one there.)
static void search_first_iteration_random(void) {
    static const SearchLevel cub_cut = {2, 8, 60, 150, 0, 0};
    static const SearchLevel plain_cut = {2, 8, 0, 0, 0, 0};
    pos_start(&cpos);
    u32 best = think(&chess_search_game, &cpos, &plain_cut, 1);
    CHECK(search_depth() == 0);
    u32 seen[20];
    int distinct = 0;
    bool other = false;
    for (u32 seed = 1; seed <= 20; seed++) {
        u32 m = think(&chess_search_game, &cpos, &cub_cut, seed);
        CHECK(search_depth() == 0);
        CHECK(chess_legal(&cpos, m));
        other |= m != best;
        bool known = false;
        for (int i = 0; i < distinct; i++)
            known |= seen[i] == m;
        if (!known)
            seen[distinct++] = m;
    }
    CHECK(distinct >= 3);
    CHECK(other);
    // Stepped, the same.
    u32 m = think(&chess_search_game, &cpos, &cub_cut, 9);
    search_start(&chess_search_game, &cpos, &cub_cut, 9);
    while (!search_step(3)) {
    }
    CHECK(search_best() == m);
}

// The transposition table's generations run out every 63 searches, when the
// table is cleared over the next search's first steps (nothing searched until
// that is done): a search never sees an entry of the search 63 before it.
// Here that one is a deeper search of the same position, whose entries would
// cut the small search short; the searches between have a single legal move
// (they count, and store nothing).
static void search_table_wrap(void) {
    static const SearchLevel deep = {4, 0xFFFFFFFFu, 0, 0, 0, 0};
    static const SearchLevel small = {3, 0xFFFFFFFFu, 0, 0, 0, 0};
    // The Italian game: depth 3 whole in about 1,900 nodes.
    CHECK(
        pos_from_fen(&cpos, "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4"));
    CHECK(pos_from_fen(&cother, "7k/8/8/8/8/8/6q1/7K w - - 0 1")); // Kxg2 only
    think(&chess_search_game, &cpos, &deep, 5);
    u32 want = think(&chess_search_game, &cpos, &small, 3), want_nodes = search_nodes();
    for (int i = 0; i < 2; i++) {
        think(&chess_search_game, &cpos, &deep, 5);
        for (int j = 0; j < 62; j++)
            think(&chess_search_game, &cother, &small, 3);
        // 63 searches after the deep one: a wrap came between.
        search_start(&chess_search_game, &cpos, &small, 3);
        while (!search_step(i ? 7 : 0xFFFFFFFFu)) {
        }
        CHECK(search_best() == want);
        CHECK(search_nodes() == want_nodes);
    }
}

// Quiescence leaves out captures that lose material at once (a piece taking
// a defended one worth clearly less), and keeps the rest: winning and
// near-even captures, and captures of undefended pieces.
static bool quiet_search_has(const char* fen, const char* san) {
    CHECK(pos_from_fen(&cpos, fen));
    Move m = chess_move(&cpos, san);
    CHECK(m != MOVE_NONE);
    static u32 noisy[MAX_MOVES];
    int n = chess_search_game.gen(&cpos, noisy, true);
    for (int i = 0; i < n; i++)
        if (noisy[i] == m)
            return true;
    return false;
}

static void quiescence_filter(void) {
    // The pawn on d5 is defended by e6.
    CHECK(quiet_search_has("7k/8/4p3/3p4/4P3/8/8/K2Q4 w - - 0 1", "exd5"));
    CHECK(!quiet_search_has("7k/8/4p3/3p4/4P3/8/8/K2Q4 w - - 0 1", "Qxd5"));
    CHECK(quiet_search_has("7k/8/8/3p4/8/8/8/K2Q4 w - - 0 1", "Qxd5")); // undefended
    // A bishop for a defended knight: near even, kept.
    CHECK(quiet_search_has("7k/8/4p3/3n4/8/1B6/8/K7 w - - 0 1", "Bxd5"));
    // A rook for a defended knight: dropped.
    CHECK(!quiet_search_has("7k/8/4p3/3n4/8/8/8/K2R4 w - - 0 1", "Rxd5"));
}

// An aborted search answers with its last whole iteration's move.
static void search_abort_keeps_best(void) {
    CHECK(pos_from_fen(&cpos, "rnb1kbnr/pppp1ppp/8/8/4q3/2N5/PPPPPPPP/R1BQKBNR w KQkq - 0 1"));
    search_start(&chess_search_game, &cpos, chess_level(LEVEL_SERVAL), 1);
    while (search_depth() < 2 && !search_step(50)) {
    }
    CHECK(search_running());
    u32 before = search_best();
    CHECK(before != 0);
    search_abort();
    CHECK(!search_running());
    CHECK(search_best() == before);
}

static bool refuse_write(u32 offset, const u8* src, u32 count) {
    (void)offset, (void)src, (void)count;
    return false;
}

// --- The save -----------------------------------------------------------------

static Match restored;

// Plays a checkers game of pseudo-random moves from seed until a move jumps
// three men (at least 3 x in its notation) and has been played; false if none
// does within 300 plies.
static bool ck_playout_to_triple(Match* m, u32 seed) {
    match_new(m, GAME_CHECKERS, MODE_TWO_PLAYERS, CK_DARK, LEVEL_CUB);
    u32 r = seed;
    char text[24];
    for (int i = 0; i < 300 && m->result == RESULT_NONE; i++) {
        int n = ck_gen_legal(&m->pos.checkers, kmoves);
        r = r * 1664525u + 1013904223u;
        int pick = (int)((r >> 16) % (u32)n);
        ck_notation(&m->pos.checkers, kmoves[pick], text);
        int x = 0;
        for (const char* c = text; *c; c++)
            x += *c == 'x';
        if (!match_play(m, kmoves[pick]))
            return false;
        if (x >= 3)
            return true;
    }
    return false;
}

// A game in progress comes back from the save as the same game, both kinds,
// with a three-man jump in the moves.
static void records_roundtrip(void) {
    save_erase(0);
    board_records_load();
    CHECK(!board_records_has_game());
    static const char* const chess_line[] = {"e4", "e5", "Nf3", "Nc6", "Bb5"};
    match_new(&match, GAME_CHESS, MODE_ONE_PLAYER, BLACK, LEVEL_SERVAL);
    for (int i = 0; i < 5; i++)
        CHECK(play(&match, chess_line[i]));
    ChessKey key = pos_key(&match.pos.chess);
    board_records_set_flip(true);
    board_records_save_game(&match);
    CHECK(!board_records_save_failed());
    board_records_set_flip(false);
    board_records_load(); // as after a power-off
    CHECK(board_records_saved_flip());
    CHECK(board_records_has_game() && board_records_saved_kind() == GAME_CHESS);
    match_new(&restored, GAME_CHECKERS, MODE_TWO_PLAYERS, CK_DARK, LEVEL_CUB);
    CHECK(board_records_restore_game(&restored));
    CHECK(restored.game == GAME_CHESS && restored.mode == MODE_ONE_PLAYER);
    CHECK(restored.human_side == BLACK && restored.level == LEVEL_SERVAL);
    CHECK(restored.count == 5 && pos_key(&restored.pos.chess) == key);
    CHECK(restored.moves[4] == match.moves[4]);

    bool found = false;
    for (u32 seed = 1; seed < 200 && !found; seed++)
        found = ck_playout_to_triple(&match, seed);
    CHECK(found);
    CHECK(match.count >= 3);
    ChessKey kkey = ck_key(&match.pos.checkers);
    board_records_set_flip(false);
    board_records_save_game(&match);
    board_records_load();
    CHECK(!board_records_saved_flip());
    CHECK(board_records_has_game() && board_records_saved_kind() == GAME_CHECKERS);
    CHECK(board_records_restore_game(&restored));
    CHECK(restored.game == GAME_CHECKERS && restored.mode == MODE_TWO_PLAYERS);
    CHECK(restored.count == match.count && ck_key(&restored.pos.checkers) == kkey);
    for (int i = 0; i < match.count; i++)
        CHECK(restored.moves[i] == match.moves[i]);
    save_erase(0);
}

// Wins, draws and losses go in their place by game and strength; a game ended
// is no longer in progress.
static void records_result_counts(void) {
    save_erase(0);
    board_records_load();
    // A chess win at HUNTER: Fool's mate, 1.f3 e5 2.g4 Qh4#, is Black's win.
    match_new(&match, GAME_CHESS, MODE_ONE_PLAYER, BLACK, LEVEL_HUNTER);
    CHECK(play(&match, "f3") && play(&match, "e5") && play(&match, "g4") && play(&match, "Qh4#"));
    board_records_save_game(&match); // over: nothing to keep
    CHECK(!board_records_has_game());
    board_records_end_game(&match);
    CHECK(board_records_get(GAME_CHESS)->wins[LEVEL_HUNTER] == 1);
    // A chess loss at SERVAL: the same game, the human White.
    match_new(&match, GAME_CHESS, MODE_ONE_PLAYER, WHITE, LEVEL_SERVAL);
    CHECK(play(&match, "f3") && play(&match, "e5") && play(&match, "g4") && play(&match, "Qh4#"));
    board_records_end_game(&match);
    CHECK(board_records_get(GAME_CHESS)->losses[LEVEL_SERVAL] == 1);
    // A checkers draw at CUB (a game by the forty-move rule is long; set the result).
    match_new(&match, GAME_CHECKERS, MODE_ONE_PLAYER, CK_DARK, LEVEL_CUB);
    CHECK(play(&match, "9-13"));
    match.result = RESULT_FORTY;
    board_records_end_game(&match);
    CHECK(board_records_get(GAME_CHECKERS)->draws[LEVEL_CUB] == 1);
    // A two-player checkers game, then one more.
    for (int g = 0; g < 2; g++) {
        match_new(&match, GAME_CHECKERS, MODE_TWO_PLAYERS, CK_DARK, LEVEL_CUB);
        CHECK(play(&match, "9-13"));
        match.result = RESULT_NO_MOVES;
        board_records_end_game(&match);
    }
    // Everything in its place and nothing elsewhere.
    for (int g = 0; g < 2; g++) {
        const GameRecord* r = board_records_get((GameKind)g);
        for (int l = 0; l < LEVEL_COUNT; l++) {
            int w = g == GAME_CHESS && l == LEVEL_HUNTER;
            int d = g == GAME_CHECKERS && l == LEVEL_CUB;
            int lo = g == GAME_CHESS && l == LEVEL_SERVAL;
            CHECK(r->wins[l] == w && r->draws[l] == d && r->losses[l] == lo);
        }
        CHECK(r->two_player_games == (g == GAME_CHECKERS ? 2 : 0));
    }
    // A game in progress is forgotten by ending a game, and by itself.
    match_new(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    CHECK(play(&match, "e4"));
    board_records_save_game(&match);
    CHECK(board_records_has_game());
    board_records_end_game(&match); // not over: forgotten, not counted
    CHECK(!board_records_has_game() && board_records_get(GAME_CHESS)->two_player_games == 0);
    board_records_save_game(&match);
    board_records_forget_game();
    CHECK(!board_records_has_game());
    // And all of it comes back after a power-off.
    board_records_load();
    CHECK(board_records_get(GAME_CHESS)->wins[LEVEL_HUNTER] == 1);
    CHECK(board_records_get(GAME_CHECKERS)->two_player_games == 2);
    CHECK(!board_records_has_game());
    save_erase(0);
}

// Saves that cannot be read: another version, another size, a damaged game,
// a saved illegal move.
static void records_bad_saves(void) {
    save_erase(0);
    board_records_load();
    match_new(&match, GAME_CHECKERS, MODE_ONE_PLAYER, CK_DARK, LEVEL_HUNTER);
    CHECK(play(&match, "9-13"));
    board_records_save_game(&match);
    board_records_end_game(&match); // a record of nothing: forgotten only
    match_new(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    CHECK(play(&match, "e4") && play(&match, "e5"));
    board_records_save_game(&match);
    u32 size = save_slot_size(0);
    CHECK(size > 0 && size <= SAVE_SLOT_MAX);
    static u8 raw[SAVE_SLOT_MAX];
    CHECK(save_read(0, raw, size, 2) == SAVE_OK);

    // Another version: no game, no records.
    CHECK(save_write(0, raw, size, 3));
    board_records_load();
    CHECK(!board_records_has_game());
    CHECK(board_records_get(GAME_CHESS)->two_player_games == 0);
    // Another size.
    CHECK(save_write(0, raw, size - 2, 2));
    board_records_load();
    CHECK(!board_records_has_game());
    // The good save once more reads.
    CHECK(save_write(0, raw, size, 2));
    board_records_load();
    CHECK(board_records_has_game());
    CHECK(board_records_restore_game(&restored) && restored.count == 2);

    // An index past the list's end: restore fails, the game is forgotten, and
    // the forgetting is saved. The index is the first byte of the moves (the
    // struct's end is the 800-byte move array), ply 0.
    raw[size - GAME_PLY_MAX] = 255;
    CHECK(save_write(0, raw, size, 2));
    board_records_load();
    CHECK(board_records_has_game());
    CHECK(!board_records_restore_game(&restored));
    CHECK(!board_records_has_game());
    CHECK(restored.count == 0);
    board_records_load();
    CHECK(!board_records_has_game());
    // A game with settings this version cannot play is dropped at load.
    raw[size - GAME_PLY_MAX] = 0;
    raw[1] = 7; // the kind
    CHECK(save_write(0, raw, size, 2));
    board_records_load();
    CHECK(!board_records_has_game());
    raw[1] = 0;
    raw[5] = 2; // the flip is 0 or 1
    CHECK(save_write(0, raw, size, 2));
    board_records_load();
    CHECK(!board_records_has_game());
    save_erase(0);
    board_records_load();
    CHECK(!board_records_restore_game(&restored)); // no game to restore
}

// Erasing clears the records and the game, and saves.
static void records_erase_clears(void) {
    save_erase(0);
    board_records_load();
    match_new(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    CHECK(play(&match, "e4"));
    board_records_save_game(&match);
    match.result = RESULT_STALEMATE;
    board_records_end_game(&match);
    match_new(&match, GAME_CHECKERS, MODE_TWO_PLAYERS, CK_DARK, LEVEL_CUB);
    CHECK(play(&match, "9-13"));
    board_records_save_game(&match);
    CHECK(board_records_has_game());
    board_records_erase();
    CHECK(!board_records_save_failed());
    CHECK(!board_records_has_game());
    CHECK(board_records_get(GAME_CHESS)->two_player_games == 0);
    board_records_load();
    CHECK(!board_records_has_game());
    CHECK(board_records_get(GAME_CHESS)->two_player_games == 0);
    CHECK(save_slot_size(0) > 0); // saved cleared, not left as it was

    // A save the memory refuses sets the flag; the next that works clears it.
    SaveDevice refusing = *serval_save_device;
    refusing.write = refuse_write;
    const SaveDevice* real = serval_save_device;
    serval_save_device = &refusing;
    board_records_erase();
    CHECK(board_records_save_failed());
    serval_save_device = real;
    board_records_erase();
    CHECK(!board_records_save_failed());
    save_erase(0);
}

// UNDO is offered only when it takes back a move of the human's: in one
// player games, not before the human has moved (the computer's first move
// alone), in two player games after any move.
static void match_can_undo_human(void) {
    // Over, nothing is taken back: the result is counted once.
    match_new(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    CHECK(play(&match, "f3") && play(&match, "e5") && play(&match, "g4") && play(&match, "Qh4#"));
    CHECK(match.result != RESULT_NONE && !match_can_undo(&match));
    match_new(&match, GAME_CHESS, MODE_ONE_PLAYER, WHITE, LEVEL_CUB);
    CHECK(!match_can_undo(&match));
    CHECK(play(&match, "e4"));
    CHECK(match_can_undo(&match)); // the human's move, the computer thinking
    CHECK(play(&match, "e5"));
    CHECK(match_can_undo(&match));
    match_new(&match, GAME_CHESS, MODE_ONE_PLAYER, BLACK, LEVEL_CUB);
    CHECK(play(&match, "Nf3")); // the computer's
    CHECK(!match_can_undo(&match));
    CHECK(play(&match, "Nf6"));
    CHECK(match_can_undo(&match));
    match_new(&match, GAME_CHECKERS, MODE_ONE_PLAYER, CK_LIGHT, LEVEL_CUB);
    CHECK(play(&match, "11-15")); // Dark, the computer, moves first
    CHECK(!match_can_undo(&match));
    CHECK(play(&match, "22-18"));
    CHECK(match_can_undo(&match));
    match_new(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    CHECK(!match_can_undo(&match));
    CHECK(play(&match, "d4"));
    CHECK(match_can_undo(&match));
}

// The SearchGame tables and a match agree with each other.
static void match_feeds_search(void) {
    match_new(&match, GAME_CHECKERS, MODE_ONE_PLAYER, CK_LIGHT, LEVEL_CUB);
    CHECK(match_computer_to_move(&match));
    CHECK(match_search_game(&match) == &checkers_search_game);
    CHECK(match_level(&match) == checkers_level(LEVEL_CUB));
    u32 m = think(match_search_game(&match), match_position(&match), match_level(&match), 3);
    CHECK(match_play(&match, m));
    CHECK(!match_computer_to_move(&match));
    match_new(&match, GAME_CHESS, MODE_ONE_PLAYER, BLACK, LEVEL_HUNTER);
    CHECK(match_search_game(&match) == &chess_search_game);
    CHECK(match_level(&match) == chess_level(LEVEL_HUNTER));
    CHECK(match_position(&match) == &match.pos.chess);
}

TEST_SUITE(boardgames_tests, "boardgames", {"search_mate_in_one", search_mate_in_one},
           {"search_mate_in_two", search_mate_in_two},
           {"search_mate_through_table", search_mate_through_table},
           {"search_takes_hanging_queen", search_takes_hanging_queen},
           {"search_checkers_wins_material", search_checkers_wins_material},
           {"search_resume_same_as_whole", search_resume_same_as_whole},
           {"search_seeded", search_seeded}, {"search_single_move", search_single_move},
           {"search_pace_adapts", search_pace_adapts}, {"search_legal_always", search_legal_always},
           {"search_matches_plain_negamax", search_matches_plain_negamax},
           {"eval_symmetric", eval_symmetric}, {"eval_incremental", eval_incremental},
           {"match_undo_edges", match_undo_edges}, {"match_replay_rejects", match_replay_rejects},
           {"match_refuses_finished", match_refuses_finished},
           {"match_indices_round_trip", match_indices_round_trip},
           {"match_stores_indices", match_stores_indices},
           {"search_mate_beats_fifty", search_mate_beats_fifty},
           {"search_abort_keeps_best", search_abort_keeps_best},
           {"search_first_iteration_cut", search_first_iteration_cut},
           {"search_first_iteration_random", search_first_iteration_random},
           {"search_table_wrap", search_table_wrap}, {"quiescence_filter", quiescence_filter},
           {"match_can_undo_human", match_can_undo_human},
           {"match_feeds_search", match_feeds_search}, {"records_roundtrip", records_roundtrip},
           {"records_result_counts", records_result_counts},
           {"records_bad_saves", records_bad_saves},
           {"records_erase_clears", records_erase_clears});
