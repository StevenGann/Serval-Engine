// The game being played: see match.h.

#include "match.h"

#include "checkers_eval.h"
#include "chess_eval.h"
#include "chess_rules.h"

// A legal move list, out of the stack (the GBA's is small).
static u32 list[MAX_MOVES] SERVAL_EWRAM_BSS;
static Move chess_list[MAX_MOVES] SERVAL_EWRAM_BSS;

// The legal moves of the position into list.
static int legal(Match* m) {
    if (m->game == GAME_CHECKERS)
        return ck_gen_legal(&m->pos.checkers, list);
    int n = pos_gen_legal(&m->pos.chess, chess_list);
    for (int i = 0; i < n; i++)
        list[i] = chess_list[i];
    return n;
}

static void make(Match* m, u32 mv) {
    if (m->game == GAME_CHECKERS)
        ck_make(&m->pos.checkers, mv);
    else
        pos_make(&m->pos.chess, (Move)mv);
}

static void unmake(Match* m) {
    if (m->game == GAME_CHECKERS)
        ck_unmake(&m->pos.checkers);
    else
        pos_unmake(&m->pos.chess);
}

static GameResult result(Match* m) {
    return m->game == GAME_CHECKERS ? ck_result(&m->pos.checkers) : chess_result(&m->pos.chess);
}

void match_new(Match* m, GameKind game, MatchMode mode, int human_side, int level) {
    m->game = (u8)game;
    m->mode = (u8)mode;
    m->human_side = (u8)human_side;
    m->level = (u8)level;
    m->result = RESULT_NONE;
    m->count = 0;
    if (game == GAME_CHECKERS)
        ck_start(&m->pos.checkers);
    else
        pos_start(&m->pos.chess);
}

bool match_play(Match* m, u32 mv) {
    if (m->result != RESULT_NONE || m->count >= GAME_PLY_MAX)
        return false;
    int n = legal(m);
    for (int i = 0; i < n; i++) {
        if (list[i] == mv) {
            make(m, mv);
            m->move_index[m->count] = (u8)i;
            m->moves[m->count++] = mv;
            m->result = result(m);
            return true;
        }
    }
    return false;
}

int match_undo(Match* m) {
    int undone = 0;
    while (m->count > 0) {
        unmake(m);
        m->count--;
        undone++;
        if (m->mode == MODE_TWO_PLAYERS || match_side_to_move(m) == m->human_side)
            break;
    }
    if (undone)
        m->result = RESULT_NONE;
    return undone;
}

bool match_replay(Match* m, const u32* moves, int count) {
    GameKind game = (GameKind)m->game;
    MatchMode mode = (MatchMode)m->mode;
    int side = m->human_side, level = m->level;
    match_new(m, game, mode, side, level);
    for (int i = 0; i < count; i++) {
        if (!match_play(m, moves[i])) {
            match_new(m, game, mode, side, level);
            return false;
        }
    }
    return true;
}

bool match_replay_indices(Match* m, GameKind game, MatchMode mode, int human_side, int level,
                          const u8* idx, int count) {
    match_new(m, game, mode, human_side, level);
    for (int i = 0; i < count; i++) {
        int n = m->result == RESULT_NONE ? legal(m) : 0;
        if (idx[i] >= n || !match_play(m, list[idx[i]])) {
            match_new(m, game, mode, human_side, level);
            return false;
        }
    }
    return true;
}

// Takes the moves from ply on back, so the position is the one before it;
// rewind_end() plays them again.
static void rewind_to(Match* m, int ply) {
    for (int i = m->count; i > ply; i--)
        unmake(m);
}

static void rewind_end(Match* m, int ply) {
    for (int i = ply; i < m->count; i++)
        make(m, m->moves[i]);
}

int match_move_index(const Match* m, int ply) {
    return ply < 0 || ply >= m->count ? -1 : m->move_index[ply];
}

int match_notation(Match* m, int ply, char out[24]) {
    out[0] = '\0';
    if (ply < 0 || ply >= m->count)
        return 0;
    rewind_to(m, ply);
    int n;
    if (m->game == GAME_CHECKERS)
        n = ck_notation(&m->pos.checkers, m->moves[ply], out);
    else
        n = chess_san(&m->pos.chess, (Move)m->moves[ply], out);
    rewind_end(m, ply);
    return n;
}

bool match_can_undo(const Match* m) {
    if (m->result != RESULT_NONE)
        return false; // over: the result is the record's to count once
    if (m->mode == MODE_TWO_PLAYERS || m->count >= 2)
        return m->count > 0; // two plies: one of them the human's
    // One ply: the human's if the computer is to move now.
    return m->count == 1 && match_side_to_move(m) != m->human_side;
}

bool match_computer_to_move(const Match* m) {
    return m->mode == MODE_ONE_PLAYER && m->result == RESULT_NONE &&
           match_side_to_move(m) != m->human_side;
}

int match_side_to_move(const Match* m) {
    return m->game == GAME_CHECKERS ? ck_side(&m->pos.checkers) : pos_side(&m->pos.chess);
}

const SearchGame* match_search_game(const Match* m) {
    return m->game == GAME_CHECKERS ? &checkers_search_game : &chess_search_game;
}

const void* match_position(const Match* m) {
    return m->game == GAME_CHECKERS ? (const void*)&m->pos.checkers : (const void*)&m->pos.chess;
}

const SearchLevel* match_level(const Match* m) {
    return m->game == GAME_CHECKERS ? checkers_level(m->level) : chess_level(m->level);
}
