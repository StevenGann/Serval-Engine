#ifndef BOARDGAMES_CHESS_RULES_H
#define BOARDGAMES_CHESS_RULES_H

// Chess beyond the moves: how a game ends, move notation (SAN) and finding
// the legal move a player's choice stands for. Portable C only.

#include "chess_board.h"
#include "game_result.h"

// Whether the game is over, and how. Checked in this order: no legal move
// (CHECKMATE if in check, else STALEMATE); the fifty-move clock at 100
// plies (FIFTY); the position seen three times since the last capture or
// pawn move (REPETITION); neither side able to mate (MATERIAL: K v K, KB v K,
// KN v K, and bishops all on one colour square with no other piece); and
// pos_ply() at GAME_PLY_MAX (LENGTH). Callers stop at any result but NONE:
// the board keeps history for GAME_PLY_MAX plies (plus the search's) and
// ignores moves past it, so LENGTH is what keeps a game inside it.
GameResult chess_result(Position* p);

// The standard algebraic notation of m, a legal move of p, written before
// it is made: "Nf3", "exd5", "Nbd2", "R1e2", "O-O", "e8=Q+", "Qxf7#".
// Returns the length (at most 7); out is NUL-terminated.
int chess_san(Position* p, Move m, char out[8]);

// The legal move from one square to another (0..63), MOVE_NONE if there is
// none. A castling move is the king's two-square step (e1 to g1). For a
// promotion promo_kind (KNIGHT..QUEEN) picks the piece; otherwise it is
// ignored.
Move chess_find(Position* p, int from, int to, int promo_kind);

// Whether the move from to to is a promotion (so the player is asked which
// piece).
bool chess_is_promotion(Position* p, int from, int to);

#endif // BOARDGAMES_CHESS_RULES_H
