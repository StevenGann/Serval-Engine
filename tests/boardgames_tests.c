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
    board_records_save_game(&match);
    CHECK(!board_records_save_failed());
    board_records_load(); // as after a power-off
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
    board_records_save_game(&match);
    board_records_load();
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
    match_new(&match, GAME_CHESS, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    CHECK(play(&match, "e4") && play(&match, "e5"));
    match.result = RESULT_NONE;
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
    CHECK(save_read(0, raw, size, 1) == SAVE_OK);

    // Another version: no game, no records.
    CHECK(save_write(0, raw, size, 2));
    board_records_load();
    CHECK(!board_records_has_game());
    CHECK(board_records_get(GAME_CHESS)->two_player_games == 0);
    // Another size.
    CHECK(save_write(0, raw, size - 2, 1));
    board_records_load();
    CHECK(!board_records_has_game());
    // The good save once more reads.
    CHECK(save_write(0, raw, size, 1));
    board_records_load();
    CHECK(board_records_has_game());
    CHECK(board_records_restore_game(&restored) && restored.count == 2);

    // An index past the list's end: restore fails, the game is forgotten, and
    // the forgetting is saved. The index is the last byte of the moves (the
    // struct's end is the 800-byte move array) at ply 0.
    raw[size - GAME_PLY_MAX] = 255;
    CHECK(save_write(0, raw, size, 1));
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
    CHECK(save_write(0, raw, size, 1));
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
           {"search_legal_always", search_legal_always},
           {"search_matches_plain_negamax", search_matches_plain_negamax},
           {"eval_symmetric", eval_symmetric}, {"match_undo_edges", match_undo_edges},
           {"match_replay_rejects", match_replay_rejects},
           {"match_refuses_finished", match_refuses_finished},
           {"match_indices_round_trip", match_indices_round_trip},
           {"match_stores_indices", match_stores_indices},
           {"search_mate_beats_fifty", search_mate_beats_fifty},
           {"search_abort_keeps_best", search_abort_keeps_best},
           {"match_feeds_search", match_feeds_search}, {"records_roundtrip", records_roundtrip},
           {"records_result_counts", records_result_counts},
           {"records_bad_saves", records_bad_saves},
           {"records_erase_clears", records_erase_clears});
