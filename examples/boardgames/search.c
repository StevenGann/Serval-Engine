// The board games' search: see search.h.
//
// The tree lives on an explicit stack of frames, one a ply: frames[0] is the
// root, frames[sp] the node being worked on. A frame is in one of two states:
// F_ENTER, a node just reached (the move to it made, nothing else done), and
// F_NEXT, a node whose moves are generated and that searches the next one
// (or, out of moves, hands its value to its parent). search_step() pauses only
// before entering a node, so everything it needs to go on is in the frames,
// the move stack and the static state below: nothing lives in C locals across
// a return, and no function here calls itself.
//
// A value travels up the stack in handle_value(): the parent takes back its
// move, keeps the best, and either cuts off (a value of its own to hand up in
// turn) or goes on to its next move.
//
// Quiescence uses the same frames (depth 0 or below): the side to move may
// stand pat on the evaluation and searches only noisy moves, except when in
// check (quiesce_in_check_all: every move, mates seen) or when the noisy moves
// are forced (noisy_forced: checkers' compulsory jumps). It ends by itself
// when the noisy moves run out and, at worst, at SEARCH_PLY_MAX.

#include "search.h"

// memcpy and memset: the engine-provided C library routines (src/gba/libc.c)
// that the repo's portable code uses.
#include <string.h>

#define NO_MOVE 0xFFFFFFFFu

// --- The transposition table --------------------------------------------------

#define TT_BITS 12
#define TT_SIZE (1 << TT_BITS)
enum { TT_EXACT = 1, TT_LOWER = 2, TT_UPPER = 3 };

typedef struct {
    u32 lock; // the key's high half
    u32 move; // the best move found, NO_MOVE if none
    s16 score;
    u8 depth;
    u8 tag; // generation (bits 2-7) | TT_* (bits 0-1); a generation not this search's is empty
} TtEntry;

// --- The frames ---------------------------------------------------------------

enum { F_ENTER, F_NEXT };

typedef struct {
    int alpha, beta, alpha_orig, best;
    u32 best_move, move;    // move: the one being searched below
    u16 first, count, next; // its moves: the move stack's [first, first + count), next to try
    s8 depth;               // depth left; 0 or less in quiescence
    u8 state;               // F_*
    u8 quiesce;
} Frame;

// --- State --------------------------------------------------------------------

static union {
    ChessKey align;
    u8 bytes[SEARCH_POS_BYTES];
} pos_buf SERVAL_EWRAM_BSS;
static TtEntry tt[TT_SIZE] SERVAL_EWRAM_BSS;
static u32 stack_moves[SEARCH_STACK_MOVES] SERVAL_EWRAM_BSS;
static s16 stack_order[SEARCH_STACK_MOVES] SERVAL_EWRAM_BSS;
static Frame frames[SEARCH_PLY_MAX] SERVAL_EWRAM_BSS;
static u32 killers[SEARCH_PLY_MAX][2] SERVAL_EWRAM_BSS;
// The root's moves, in the order the next iteration tries them, and their
// scores in the iteration under way; done_*: the last completed iteration's,
// best first.
static u32 root_moves[MAX_MOVES] SERVAL_EWRAM_BSS;
static int root_scores[MAX_MOVES] SERVAL_EWRAM_BSS;
static u32 done_moves[MAX_MOVES] SERVAL_EWRAM_BSS;
static int done_scores[MAX_MOVES] SERVAL_EWRAM_BSS;

static const SearchGame* game;
static const SearchLevel* level;
static void* const pos = pos_buf.bytes;
static bool running, trivial;
static u8 tt_generation; // 1..63
// When the generations run out the table is cleared, TT_CHUNK entries a
// step before the search begins (all of it at once would be most of a
// frame on the GBA): tt_cleared is how far that has come, TT_SIZE when no
// clearing is under way. Nothing is searched until it is done, so the move
// found is the same.
#define TT_CHUNK (TT_SIZE / 16)
static int tt_cleared = TT_SIZE;
static int sp;  // the frame being worked on
static int top; // the move stack's first free place
static int root_count;
static int root_margin; // root moves this close to the best get exact scores
static int iter_depth;  // the iteration under way
static int done_depth, done_score;
static u32 done_best, chosen;
static int chosen_score;
static u32 nodes, rng;
static u32 work; // SEARCH_WORK_* units done

// --- Small helpers ------------------------------------------------------------

static u32 rng_next(void) {
    rng = rng * 1664525u + 1013904223u;
    return rng >> 16; // 16 bits
}

// A random number in [0, n), n at most 65536.
static int rng_below(int n) {
    return (int)((rng_next() * (u32)n) >> 16);
}

// Mate scores are stored relative to the node, read back relative to the root.
static int to_tt(int s, int ply) {
    return s > SEARCH_MATE_BOUND ? s + ply : s < -SEARCH_MATE_BOUND ? s - ply : s;
}

static int from_tt(int s, int ply) {
    return s > SEARCH_MATE_BOUND ? s - ply : s < -SEARCH_MATE_BOUND ? s + ply : s;
}

static void tt_store(ChessKey key, int depth, int flag, int score, u32 move, int ply) {
    TtEntry* e = &tt[(u32)key & (TT_SIZE - 1)];
    e->lock = (u32)(key >> 32);
    e->move = move;
    e->score = (s16)to_tt(score, ply);
    e->depth = (u8)depth;
    e->tag = (u8)(tt_generation << 2 | flag);
}

// Orders a frame's moves: the table's move first, then noisy moves by the
// game's value, the killers, the quiet moves by the game's value.
static void score_moves(Frame* f, int ply, u32 tt_move) {
    for (int i = f->first; i < f->first + f->count; i++) {
        u32 m = stack_moves[i];
        int v;
        if (m == tt_move)
            v = 30000;
        else {
            v = game->order(pos, m);
            if (v < SEARCH_ORDER_NOISY) {
                if (m == killers[ply][0])
                    v = SEARCH_ORDER_KILLER;
                else if (m == killers[ply][1])
                    v = SEARCH_ORDER_KILLER - 1;
            }
        }
        stack_order[i] = (s16)v;
    }
}

// Brings the best-ordered of the frame's untried moves to its next place
// (the first of equals, so the order is the generator's among them).
static u32 pick_next(Frame* f) {
    int at = f->first + f->next, end = f->first + f->count, best = at;
    for (int i = at + 1; i < end; i++)
        if (stack_order[i] > stack_order[best])
            best = i;
    u32 m = stack_moves[best];
    if (best != at) {
        s16 o = stack_order[best];
        // Shift, keeping the others in order (a swap would reorder equals).
        for (int i = best; i > at; i--) {
            stack_moves[i] = stack_moves[i - 1];
            stack_order[i] = stack_order[i - 1];
        }
        stack_moves[at] = m;
        stack_order[at] = o;
    }
    f->next++;
    return m;
}

// The game's moves, its work counted.
static int gen(u32* out, bool noisy) {
    int n = game->gen(pos, out, noisy);
    work += SEARCH_WORK_GEN + (u32)n;
    return n;
}

// --- Nodes --------------------------------------------------------------------

// Sets up the node at frames[sp] just entered. Returns true with *value when
// the node is decided without searching moves.
static bool enter(Frame* f, int* value) {
    int ply = sp;
    f->first = (u16)top;
    f->count = 0;
    f->next = 0;
    f->best_move = NO_MOVE;
    f->alpha_orig = f->alpha;
    if (game->is_draw(pos)) {
        // A side with no legal move is mated (or has lost) before any draw
        // by the clocks or repetition: checkmate takes precedence.
        if (top + MAX_MOVES <= SEARCH_STACK_MOVES && gen(&stack_moves[top], false) == 0)
            *value = game->no_moves_score(pos, ply);
        else
            *value = 0;
        return true;
    }
    if (ply >= SEARCH_PLY_MAX - 1 || top + MAX_MOVES > SEARCH_STACK_MOVES) {
        *value = game->eval(pos);
        return true;
    }
    if (f->depth <= 0) {
        f->quiesce = 1;
        bool all = game->quiesce_in_check_all && game->in_check && game->in_check(pos);
        int n = 0;
        if (all || game->noisy_forced) {
            n = gen(&stack_moves[top], !all);
            if (all && n == 0) {
                *value = game->no_moves_score(pos, ply);
                return true;
            }
        }
        if (!all && (!game->noisy_forced || n == 0)) {
            // Standing pat: the evaluation first, and the moves only if it
            // does not already cut off (most quiescence nodes do).
            int stand = game->eval(pos);
            if (stand >= f->beta) {
                *value = stand;
                return true;
            }
            if (!game->noisy_forced)
                n = gen(&stack_moves[top], true);
            if (n == 0) {
                *value = stand;
                return true;
            }
            f->best = stand;
            if (stand > f->alpha)
                f->alpha = stand;
        } else {
            f->best = -SEARCH_INF;
        }
        f->count = (u16)n;
        top += n;
        score_moves(f, ply, NO_MOVE);
    } else {
        f->quiesce = 0;
        ChessKey key = game->key(pos);
        const TtEntry* e = &tt[(u32)key & (TT_SIZE - 1)];
        u32 tt_move = NO_MOVE;
        if (e->lock == (u32)(key >> 32) && (e->tag >> 2) == tt_generation) {
            tt_move = e->move;
            if (e->depth >= f->depth) {
                int s = from_tt(e->score, ply), flag = e->tag & 3;
                if (flag == TT_EXACT || (flag == TT_LOWER && s >= f->beta) ||
                    (flag == TT_UPPER && s <= f->alpha)) {
                    *value = s;
                    return true;
                }
            }
        }
        int n = gen(&stack_moves[top], false);
        if (n == 0) {
            *value = game->no_moves_score(pos, ply);
            return true;
        }
        f->best = -SEARCH_INF;
        f->count = (u16)n;
        top += n;
        score_moves(f, ply, tt_move);
    }
    f->state = F_NEXT;
    return false;
}

// The value of a node out of moves (or cut off).
static int leave(Frame* f) {
    if (!f->quiesce) {
        int flag = f->best <= f->alpha_orig ? TT_UPPER : f->best >= f->beta ? TT_LOWER : TT_EXACT;
        tt_store(game->key(pos), f->depth, flag, f->best, f->best_move, sp);
    }
    return f->best;
}

// Makes the frame's next move and pushes its child.
static void descend(Frame* f) {
    Frame* c = &frames[sp + 1];
    u32 m;
    if (sp == 0) {
        m = root_moves[f->next++];
        // Moves close to the best get exact scores, for the random choice.
        int lo = f->best - root_margin - 1;
        c->alpha = -SEARCH_INF;
        c->beta = lo < -SEARCH_INF ? SEARCH_INF : -lo;
    } else {
        m = pick_next(f);
        c->alpha = -f->beta;
        c->beta = -f->alpha;
    }
    f->move = m;
    c->depth = (s8)(f->depth - 1);
    c->state = F_ENTER;
    game->make(pos, m);
    sp++;
}

// The frame at sp hands value up. Returns true when the root's iteration is
// complete.
static bool handle_value(int value) {
    for (;;) {
        top = frames[sp].first;
        if (sp == 0)
            return true;
        sp--;
        game->unmake(pos);
        Frame* f = &frames[sp];
        int v = -value;
        if (sp == 0) {
            root_scores[f->next - 1] = v;
            if (v > f->best) {
                f->best = v;
                f->best_move = f->move;
            }
        } else if (v > f->best) {
            f->best = v;
            f->best_move = f->move;
            if (v > f->alpha) {
                f->alpha = v;
                if (v >= f->beta) {
                    if (!f->quiesce && game->order(pos, f->move) < SEARCH_ORDER_NOISY &&
                        killers[sp][0] != f->move) {
                        killers[sp][1] = killers[sp][0];
                        killers[sp][0] = f->move;
                    }
                    value = leave(f);
                    continue;
                }
            }
        }
        if (f->next < f->count)
            return false;
        if (sp == 0)
            return true;
        value = leave(f);
    }
}

// --- Iterations ---------------------------------------------------------------

static void start_iteration(int depth) {
    Frame* f = &frames[0];
    iter_depth = depth;
    sp = 0;
    top = 0;
    f->first = 0;
    f->count = (u16)root_count;
    f->next = 0;
    f->depth = (s8)depth;
    f->best = -SEARCH_INF;
    f->best_move = NO_MOVE;
    f->alpha = -SEARCH_INF;
    f->beta = SEARCH_INF;
    f->quiesce = 0;
    f->state = F_NEXT;
}

// Sorts the root's first count moves by this iteration's scores, best
// first, equals in the order they were tried, into done_*.
static void sort_root(int count) {
    for (int i = 1; i < count; i++) {
        u32 m = root_moves[i];
        int s = root_scores[i], j = i;
        for (; j > 0 && root_scores[j - 1] < s; j--) {
            root_moves[j] = root_moves[j - 1];
            root_scores[j] = root_scores[j - 1];
        }
        root_moves[j] = m;
        root_scores[j] = s;
    }
    memcpy(done_moves, root_moves, sizeof(u32) * (u32)count);
    memcpy(done_scores, root_scores, sizeof(int) * (u32)count);
    done_best = root_moves[0];
    done_score = root_scores[0];
}

// Keeps the iteration just completed as the last whole one.
static void complete_iteration(void) {
    sort_root(root_count);
    done_depth = iter_depth;
}

// The opening rule's margin, if it applies now.
static int opening_margin(void) {
    if (level->opening_plies && game->ply && game->ply(pos) < level->opening_plies)
        return level->opening_margin;
    return 0;
}

// Picks the move from the root's moves in done_moves (count of them, best
// first: the last completed iteration's, or those the first searched) and
// stops.
static void finish(int count) {
    running = false;
    chosen = done_best;
    chosen_score = done_score;
    if (done_score > SEARCH_MATE_BOUND || count < 2)
        return; // a mate is played as found
    int margin = level->random_margin;
    if (opening_margin() > margin)
        margin = opening_margin();
    if (level->blunder_permille && ((rng_next() * 1000u) >> 16) < level->blunder_permille)
        margin = SEARCH_BLUNDER_LOSS;
    if (margin == 0)
        return;
    int k = 1;
    while (k < count && done_scores[k] >= done_score - margin &&
           done_scores[k] > -SEARCH_MATE_BOUND)
        k++;
    if (k > 1) {
        int i = rng_below(k);
        chosen = done_moves[i];
        chosen_score = done_scores[i];
    }
}

// --- The interface ------------------------------------------------------------

void search_start(const SearchGame* g, const void* p, const SearchLevel* lv, u32 seed) {
    game = g;
    level = lv;
    running = true;
    trivial = false;
    nodes = 0;
    work = 0;
    rng = seed;
    done_depth = 0;
    done_score = 0;
    done_best = chosen = 0;
    chosen_score = 0;
    // Refused silently (examples cannot use SERVAL_WARN): it cannot happen with
    // the two games, whose files static-assert their positions into
    // SEARCH_POS_BYTES.
    if (g->pos_size > SEARCH_POS_BYTES) {
        running = false;
        return;
    }
    memcpy(pos_buf.bytes, p, g->pos_size);
    if (tt_cleared < TT_SIZE) {
        tt_cleared = 0; // a clearing left unfinished begins again; the generation stays 1
    } else if (++tt_generation >= 64) {
        tt_generation = 1;
        tt_cleared = 0;
    }
    for (int i = 0; i < SEARCH_PLY_MAX; i++)
        killers[i][0] = killers[i][1] = NO_MOVE;

    root_count = g->gen(pos, root_moves, false);
    if (root_count <= 1) {
        done_best = root_count ? root_moves[0] : 0;
        trivial = true;
        return;
    }
    // The first iteration tries the root's moves by the game's order.
    for (int i = 0; i < root_count; i++)
        root_scores[i] = g->order(pos, root_moves[i]);
    complete_iteration();
    done_depth = 0;
    done_score = 0;

    root_margin = lv->random_margin;
    if (opening_margin() > root_margin)
        root_margin = opening_margin();
    if (lv->blunder_permille)
        root_margin = SEARCH_BLUNDER_LOSS;
    start_iteration(1);
}

bool search_step(u32 budget) {
    if (!running)
        return true;
    if (trivial) {
        running = false;
        chosen = done_best;
        return true;
    }
    if (tt_cleared < TT_SIZE) {
        memset(&tt[tt_cleared], 0, sizeof(TtEntry) * TT_CHUNK);
        tt_cleared += TT_CHUNK;
        return false;
    }
    u32 start = work;
    for (;;) {
        Frame* f = &frames[sp];
        int value;
        if (f->state == F_ENTER) {
            if (work - start >= budget)
                return false;
            work += SEARCH_WORK_NODE;
            nodes++;
            if (nodes > level->max_nodes && (done_depth > 0 || frames[0].best_move != NO_MOVE)) {
                // Out of nodes: back to the root, keep the last whole iteration
                // or, if the first is not whole yet, the best of the root's
                // moves it has searched (so that a position whose first
                // iteration is long still answers in time).
                for (; sp > 0; sp--)
                    game->unmake(pos);
                if (done_depth > 0) {
                    finish(root_count);
                } else {
                    // The first iteration's root moves searched to the end
                    // (all but the one under way), chosen among as a whole
                    // iteration's are.
                    int searched = frames[0].next - 1;
                    sort_root(searched);
                    finish(searched);
                }
                return true;
            }
            if (!enter(f, &value))
                continue;
        } else if (f->next < f->count) {
            descend(f);
            continue;
        } else {
            value = sp == 0 ? f->best : leave(f);
        }
        if (!handle_value(value))
            continue;
        complete_iteration();
        int s = done_score < 0 ? -done_score : done_score;
        if (iter_depth >= level->max_depth || iter_depth >= SEARCH_PLY_MAX - 1 ||
            s >= SEARCH_MATE - iter_depth) {
            finish(root_count);
            return true;
        }
        start_iteration(iter_depth + 1);
    }
}

u32 search_best(void) {
    return running ? done_best : chosen;
}

int search_score(void) {
    return running ? done_score : chosen_score;
}

int search_depth(void) {
    return done_depth;
}

u32 search_nodes(void) {
    return nodes;
}

u32 search_work(void) {
    return work;
}

void search_abort(void) {
    if (running && !trivial)
        for (; sp > 0; sp--)
            game->unmake(pos);
    if (running) {
        chosen = done_best;
        chosen_score = done_score;
    }
    running = false;
}

bool search_running(void) {
    return running;
}

// --- The pace -----------------------------------------------------------------

void search_pace_init(SearchPace* pace) {
    pace->cycles_per_unit = 0;
    pace->last_allowance = 0;
}

// A frame costlier per unit than the estimate raises it at once; a cheaper
// one lowers it a sixteenth of the way. So the estimate follows the costliest
// frames lately, and the allowance it gives keeps a frame whose work costs
// more than most near the target: the average frame stays a little under it.
// A frame that did much less work than it was allowed (the step that
// finished a search, a few nodes) teaches nothing: its cycles are mostly the
// frame's other work, and shared among so few units they would make the
// next search start slow.
u32 search_pace(SearchPace* pace, u32 last_cycles, u32 last_work, u32 budget) {
    if (last_work && last_work >= pace->last_allowance / SEARCH_PACE_SAMPLE_MIN) {
        u32 sample = last_cycles / last_work;
        if (sample >= pace->cycles_per_unit)
            pace->cycles_per_unit = sample;
        else
            pace->cycles_per_unit -= (pace->cycles_per_unit - sample) >> 4;
    }
    u32 n = SEARCH_WORK_PER_FRAME;
    if (pace->cycles_per_unit) {
        n = budget / 1000 * SEARCH_PACE_TARGET / pace->cycles_per_unit;
        n = n < 1 ? 1 : n > SEARCH_PACE_MAX ? SEARCH_PACE_MAX : n;
    }
    pace->last_allowance = n;
    return n;
}
