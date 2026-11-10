#ifndef BOARDGAMES_CHESS_EVAL_H
#define BOARDGAMES_CHESS_EVAL_H

// Chess for the search (search.h): the evaluation, the SearchGame table and
// the strengths. Portable C only.

#include "chess_board.h"
#include "search.h"

// The position's worth to the side to move, in centipawns: material (P 100,
// N 320, B 330, R 500, Q 900), piece-square tables, the king's tapered from
// the middlegame to the endgame by the material left, and doubled, isolated
// and passed pawns. Symmetric: the colour-mirrored position with the other
// side to move scores the same.
int chess_eval(const Position* p);

// The king's piece-square tables, the middlegame's and the endgame's (as
// chess_pst[]: White's view as printed), tapered by the phase.
extern const s8 chess_king_mid[64];
extern const s8 chess_king_end[64];

extern const SearchGame chess_search_game;

// LEVEL_CUB, LEVEL_HUNTER or LEVEL_SERVAL (anything else gives CUB).
const SearchLevel* chess_level(int level);

#endif // BOARDGAMES_CHESS_EVAL_H
