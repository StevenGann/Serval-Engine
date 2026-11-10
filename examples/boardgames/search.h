#ifndef BOARDGAMES_SEARCH_H
#define BOARDGAMES_SEARCH_H

// One search for both games: iterative deepening, negamax alpha-beta with a
// quiescence search, a transposition table, and move ordering by the table's
// move, the game's own order() value and two killer moves a ply. A game plugs
// in through a SearchGame table (chess_eval.h, checkers_eval.h); moves cross it
// as u32.
//
// It thinks a little at a time: search_start() sets it up, then
// search_step(budget) searches at most budget nodes and returns, to be called
// again (next frame) until it returns true. The tree being searched lives on an
// explicit stack, not in C recursion, so a step can stop between any two nodes
// and the next carries on exactly where it stopped: the same position, level
// and seed give the same move, score, depth and node count however the work is
// split into steps, on every platform (no floating point, no rand(), nothing
// that depends on pointer values).
//
// Only one search runs at a time (its state is static). Memory, all in EWRAM
// on the GBA (SERVAL_EWRAM_BSS): the copy of the position it searches
// (SEARCH_POS_BYTES, 14 KB), the transposition table (4096 entries of 12
// bytes, 48 KB), the move stack (SEARCH_STACK_MOVES moves and their ordering
// values, 24 KB), the root's moves and scores (4 KB: this iteration's and the
// last whole one's), the frames and killers (2.5 KB): 94,976 bytes in all
// (.sbss of search.o). Static scalars: under 100 bytes of IWRAM.

#include <serval/core.h>

#include "chess_board.h" // ChessKey, MAX_MOVES, SEARCH_PLY_MAX

// Scores: centipawn-like, from the side to move's view. A mate found n plies
// from the root scores SEARCH_MATE - n (for the winner).
#define SEARCH_MATE 30000
#define SEARCH_INF 32000
// Scores beyond this are mates.
#define SEARCH_MATE_BOUND (SEARCH_MATE - 2 * SEARCH_PLY_MAX)

// order() values: at least SEARCH_ORDER_NOISY for a capture (or promotion),
// below SEARCH_ORDER_KILLER for a quiet move. Killers sort between them.
#define SEARCH_ORDER_NOISY 1000
#define SEARCH_ORDER_KILLER 900

// The bytes a search can copy: the larger of the games' positions (each game's
// file checks its own with a static assertion).
#define SEARCH_POS_BYTES (14 * 1024)
// The search's moves, all plies' lists together. A node whose list might not
// fit (fewer than MAX_MOVES places left) is scored by the evaluation instead;
// with 35 moves a ply in chess that is over 100 plies deep, past
// SEARCH_PLY_MAX, so it does not happen in play.
#define SEARCH_STACK_MOVES 4096

typedef struct SearchGame {
    u32 pos_size;                                     // bytes copied by search_start
    int (*gen)(void* pos, u32* out, bool noisy_only); // legal moves; noisy_only for quiescence
    void (*make)(void* pos, u32 m);
    void (*unmake)(void* pos);
    int (*eval)(const void* pos); // centipawn-like, side to move's view
    ChessKey (*key)(const void* pos);
    // The score with no legal move: chess -SEARCH_MATE + ply in check, else
    // 0; checkers -SEARCH_MATE + ply.
    int (*no_moves_score)(const void* pos, int ply);
    // Cheap draws inside the search: repetition since the last irreversible
    // move, the fifty/forty-move clock, the game's length cap.
    bool (*is_draw)(void* pos);
    int (*order)(const void* pos, u32 m); // move-ordering value (see SEARCH_ORDER_*)
    // At the horizon, a side in check (in_check()) searches all its moves, with
    // no standing pat, so mates there are seen (chess).
    bool quiesce_in_check_all;
    bool (*in_check)(const void* pos); // NULL if the game has no check
    // The noisy moves, when there are any, are the only legal moves (checkers:
    // a jump is compulsory), so quiescence cannot stand pat when they exist.
    bool noisy_forced;
    int (*ply)(const void* pos); // plies played in the game (for SearchLevel's opening rule)
} SearchGame;

// A strength: the deepest iteration, the nodes after which the search stops
// (keeping its last whole iteration; the first always completes), the root's
// random choice among moves within random_margin of the best, the chance in a
// thousand of a deliberate weak move (a random move not losing more than
// SEARCH_BLUNDER_LOSS), and for the first opening_plies plies of the game a
// random choice among moves within opening_margin (0: none).
typedef struct SearchLevel {
    u8 max_depth;
    u32 max_nodes;
    u16 random_margin;
    u16 blunder_permille;
    u8 opening_plies;
    u16 opening_margin;
} SearchLevel;
enum { LEVEL_CUB, LEVEL_HUNTER, LEVEL_SERVAL, LEVEL_COUNT };

// What a deliberate weak move may give away at most.
#define SEARCH_BLUNDER_LOSS 250

// Starts a search of pos (copied, g->pos_size bytes) at the level; seed drives
// the root's random choices. Ends any search running.
void search_start(const SearchGame* g, const void* pos, const SearchLevel* lv, u32 seed);
// Searches up to node_budget nodes; true when the search is finished (and
// at once with a single legal move). Resumes exactly where the last step left.
bool search_step(u32 node_budget);
// The chosen move once finished (the best so far before); 0 if the position
// had no legal move.
u32 search_best(void);
int search_score(void); // its score, the side to move's view
int search_depth(void); // the last iteration completed
u32 search_nodes(void); // nodes searched
// Stops the search; search_running() is false after, and search_best() and
// search_score() are those of the last iteration completed (as before it).
void search_abort(void);
bool search_running(void);

#endif // BOARDGAMES_SEARCH_H
