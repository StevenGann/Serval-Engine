#ifndef BOARDGAMES_MATCH_H
#define BOARDGAMES_MATCH_H

// The game being played, chess or checkers: its position, the moves played,
// the result, who plays which side and at what strength, and undo. Portable C
// only. A Match is large (its position carries the game's history): keep it
// in EWRAM on the GBA, never on the stack.

#include "checkers.h"
#include "chess_board.h"
#include "game_result.h"
#include "search.h"

typedef enum { GAME_CHESS, GAME_CHECKERS } GameKind;
typedef enum { MODE_ONE_PLAYER, MODE_TWO_PLAYERS } MatchMode;

typedef struct Match {
    // human_side: WHITE/BLACK (chess) or CK_DARK/CK_LIGHT (checkers).
    u8 game, mode, human_side, level;
    GameResult result;
    u16 count;
    u32 moves[GAME_PLY_MAX];
    union {
        Position chess;
        CheckersPosition checkers;
    } pos;
} Match;

// A new game from the start position.
void match_new(Match* m, GameKind game, MatchMode mode, int human_side, int level);
// Plays mv for the side to move; false, nothing changed, if it is not a legal
// move or the game is over. Updates the result.
bool match_play(Match* m, u32 mv);
// Takes back moves and clears the result: one ply with two players; with one,
// back to the human's turn (or to the start if the computer moved first and
// that is all). Returns the plies taken back, 0 at the start.
int match_undo(Match* m);
// A new game of m's kind and settings with these moves played; false if one
// is illegal (or comes after the end), leaving a new game.
bool match_replay(Match* m, const u32* moves, int count);
// The same from moves given as indices in each ply's legal move list (the
// game's legal generator's order), for saves: a new game of this kind and
// these settings; false, leaving a new game, if an index is out of range or a
// move comes after the end.
bool match_replay_indices(Match* m, GameKind game, MatchMode mode, int human_side, int level,
                          const u8* idx, int count);
// The index of the ply-th move played (0 = the first) in the legal list of the
// position before it; -1 if there is no such move.
int match_move_index(Match* m, int ply);

bool match_computer_to_move(const Match* m); // one player, game on, computer's turn
int match_side_to_move(const Match* m);      // WHITE/BLACK or CK_DARK/CK_LIGHT
const SearchGame* match_search_game(const Match* m);
const void* match_position(const Match* m);
const SearchLevel* match_level(const Match* m);
// The notation of the ply-th move as played (SAN, or checkers' "11-15"):
// its length, 0 (out empty) if there is no such move.
int match_notation(Match* m, int ply, char out[24]);

#endif // BOARDGAMES_MATCH_H
