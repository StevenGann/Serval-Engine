#ifndef BOARDGAMES_CHECKERS_EVAL_H
#define BOARDGAMES_CHECKERS_EVAL_H

// Checkers for the search (search.h): the evaluation, the SearchGame table and
// the strengths. Portable C only.

#include "checkers.h"
#include "search.h"

// The position's worth to the side to move: men 100, kings 150, men's
// advancement, the back row guarded while the other side still has men, and
// the centre. Symmetric: the position turned half round, colours swapped,
// with the other side to move, scores the same.
int ck_eval(const CheckersPosition* p);

extern const SearchGame checkers_search_game;

// LEVEL_CUB, LEVEL_HUNTER or LEVEL_SERVAL (anything else gives CUB).
const SearchLevel* checkers_level(int level);

#endif // BOARDGAMES_CHECKERS_EVAL_H
