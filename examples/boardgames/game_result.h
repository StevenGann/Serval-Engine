#ifndef BOARDGAMES_GAME_RESULT_H
#define BOARDGAMES_GAME_RESULT_H

// How a game ends, for both games of the cartridge. chess_result() (chess_rules.h)
// reports RESULT_NONE (play on), CHECKMATE, STALEMATE, REPETITION, FIFTY,
// MATERIAL and LENGTH; checkers' rules report NONE, NO_MOVES (the side to
// move has no piece that can move: it loses), FORTY (80 plies, 40 by each
// side, with no capture and no man moved: a draw), REPETITION and LENGTH.

typedef enum {
    RESULT_NONE,
    RESULT_CHECKMATE,  // chess: the side to move is mated
    RESULT_STALEMATE,  // chess: no legal move, not in check: a draw
    RESULT_REPETITION, // either: the position came up a third time: a draw
    RESULT_FIFTY,      // chess: fifty moves without a capture or pawn move: a draw
    RESULT_MATERIAL,   // chess: neither side can mate: a draw
    RESULT_LENGTH,     // either: the game reached its length cap: a draw
    RESULT_NO_MOVES,   // checkers: the side to move cannot move and loses
    RESULT_FORTY,      // checkers: the forty-move rule: a draw
} GameResult;

#endif // BOARDGAMES_GAME_RESULT_H
