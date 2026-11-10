// Checkers (English draughts): see checkers.h.

#include "checkers.h"

#define MOVE_STEPS_FIELD(m) ((int)(((m) >> 5) & 15))
#define MOVE_DIR(m, i) ((int)(((m) >> (9 + 2 * (i))) & 3))

static const s8 dir_file[4] = {-1, 1, -1, 1};
static const s8 dir_rank[4] = {1, 1, -1, -1};

// --- Squares ------------------------------------------------------------------

int ck_square_file(int s) {
    int row = (s >> 2) & 7; // from the top
    return ((s & 3) << 1) + 1 - (row & 1);
}

int ck_square_rank(int s) {
    return 7 - ((s >> 2) & 7);
}

int ck_square_at(int file, int rank) {
    if (file < 0 || file > 7 || rank < 0 || rank > 7 || ((file + rank) & 1))
        return -1;
    return ((7 - rank) << 2) + (file >> 1);
}

// The square one step (step[s][d]) and two steps (jump[s][d]) from s in
// direction d, -1 off the board. Built at first use.
static s8 step_to[32][4];
static s8 jump_to[32][4];

// --- Zobrist keys -------------------------------------------------------------

// Built at first use from a fixed seed, so every build and platform has the
// same keys. Indexed by piece (colour | kind: 1, 2, 5, 6) and square.
static ChessKey z_piece[8][32] SERVAL_EWRAM_BSS;
static ChessKey z_side;
static bool ready;

static u32 z_next(u32* x) {
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}

static void tables_init(void) {
    if (ready)
        return;
    for (int s = 0; s < 32; s++) {
        int f = ck_square_file(s), r = ck_square_rank(s);
        for (int d = 0; d < 4; d++) {
            step_to[s][d] = (s8)ck_square_at(f + dir_file[d], r + dir_rank[d]);
            jump_to[s][d] = (s8)ck_square_at(f + 2 * dir_file[d], r + 2 * dir_rank[d]);
        }
    }
    u32 x = 0xC4EC7E25u; // xorshift32: 32-bit arithmetic only
    ChessKey* all = &z_piece[0][0];
    for (int i = 0; i < 8 * 32; i++) {
        u32 hi = z_next(&x);
        all[i] = (ChessKey)hi << 32 | z_next(&x);
    }
    u32 hi = z_next(&x);
    z_side = (ChessKey)hi << 32 | z_next(&x);
    ready = true;
}

static ChessKey key_of(const u8* b, int side) {
    ChessKey key = side == CK_LIGHT ? z_side : 0;
    for (int s = 0; s < 32; s++)
        if (b[s])
            key ^= z_piece[b[s]][s];
    return key;
}

// The row where a man of this colour is crowned: Dark's 29-32, Light's 1-4.
static bool crowning_row(int color, int s) {
    return color == CK_DARK ? s >= 28 : s < 4;
}

// --- Setting up -----------------------------------------------------------------

static void set_board(CheckersPosition* p, const u8* board, int side) {
    tables_init();
    for (int s = 0; s < 32; s++)
        p->board[s] = board[s];
    p->side = (u8)side;
    p->clock = 0;
    p->ply = 0;
    p->keys[0] = key_of(p->board, side);
}

void ck_start(CheckersPosition* p) {
    u8 b[32];
    for (int s = 0; s < 32; s++)
        b[s] = s < 12 ? CK_DARK | CK_MAN : s >= 20 ? CK_LIGHT | CK_MAN : CK_EMPTY;
    set_board(p, b, CK_DARK);
}

// Reads a square number 1..32 at *s; -1 if there is none.
static int read_number(const char** s) {
    const char* c = *s;
    if (*c < '0' || *c > '9')
        return -1;
    int n = 0;
    while (*c >= '0' && *c <= '9' && n <= 32)
        n = n * 10 + (*c++ - '0');
    *s = c;
    return n >= 1 && n <= 32 ? n : -1;
}

bool ck_from_text(CheckersPosition* p, const char* s) {
    u8 b[32] = {0};
    int side;
    if (*s == 'B')
        side = CK_DARK;
    else if (*s == 'W')
        side = CK_LIGHT;
    else
        return false;
    s++;
    bool seen[2] = {false, false};
    for (int section = 0; section < 2; section++) {
        if (*s++ != ':')
            return false;
        int color;
        if (*s == 'B')
            color = CK_DARK;
        else if (*s == 'W')
            color = CK_LIGHT;
        else
            return false;
        if (seen[color >> 2])
            return false;
        seen[color >> 2] = true;
        s++;
        if (*s == ':' || *s == '\0' || *s == '.')
            continue; // no pieces
        for (;;) {
            int kind = CK_MAN;
            if (*s == 'K') {
                kind = CK_KING;
                s++;
            }
            int first = read_number(&s), last = first;
            if (first < 0)
                return false;
            if (*s == '-') {
                s++;
                last = read_number(&s);
                if (last < first)
                    return false;
            }
            for (int n = first; n <= last; n++) {
                int sq = n - 1;
                if (b[sq] || (kind == CK_MAN && crowning_row(color, sq)))
                    return false;
                b[sq] = (u8)(color | kind);
            }
            if (*s != ',')
                break;
            s++;
        }
    }
    if (*s == '.')
        s++;
    if (*s != '\0')
        return false;
    set_board(p, b, side);
    return true;
}

// --- Reading ----------------------------------------------------------------

u8 ck_piece(const CheckersPosition* p, int s) {
    return p->board[s & 31];
}

int ck_side(const CheckersPosition* p) {
    return p->side;
}

int ck_clock(const CheckersPosition* p) {
    return p->clock;
}

int ck_ply(const CheckersPosition* p) {
    return p->ply;
}

ChessKey ck_key(const CheckersPosition* p) {
    return p->keys[p->ply];
}

ChessKey ck_key_at(const CheckersPosition* p, int plies_back) {
    if (plies_back < 0 || plies_back > p->ply)
        return 0;
    return p->keys[p->ply - plies_back];
}

// --- Moves ------------------------------------------------------------------

int ck_move_steps(CkMove m) {
    int n = MOVE_STEPS_FIELD(m);
    return n ? n : 1;
}

bool ck_move_is_jump(CkMove m) {
    return MOVE_STEPS_FIELD(m) != 0;
}

int ck_move_landing(CkMove m, int i) {
    int s = CK_MOVE_FROM(m);
    int f = ck_square_file(s), r = ck_square_rank(s);
    int span = ck_move_is_jump(m) ? 2 : 1;
    for (int k = 0; k <= i; k++) {
        int d = MOVE_DIR(m, k);
        f += span * dir_file[d];
        r += span * dir_rank[d];
    }
    return ck_square_at(f, r);
}

int ck_move_to(CkMove m) {
    return ck_move_landing(m, ck_move_steps(m) - 1);
}

// The jump generator's state: one piece's paths being explored.
typedef struct {
    u8* board;
    CkMove* out;
    int n;
    int from;
    int color; // the jumping piece's
    bool king;
    u32 taken; // the squares jumped over so far
} JumpWalk;

static void add(JumpWalk* w, CkMove m) {
    if (w->n < CK_MAX_MOVES)
        w->out[w->n++] = m;
}

// Explores every jump onward from s, steps jumps made along dirs.
static void jumps_from(JumpWalk* w, int s, int steps, u32 dirs) {
    bool went_on = false;
    if (steps < CK_JUMPS_MAX) {
        int d0 = 0, d1 = 4;
        if (!w->king) {
            // A man jumps forward only, so one crowned on the far row has no
            // jump left: its move ends there, a man still.
            d0 = w->color == CK_DARK ? 2 : 0;
            d1 = d0 + 2;
        }
        for (int d = d0; d < d1; d++) {
            int land = jump_to[s][d];
            if (land < 0 || w->board[land] != CK_EMPTY)
                continue;
            int mid = step_to[s][d];
            u8 victim = w->board[mid];
            if (victim == CK_EMPTY || CK_COLOR(victim) == w->color || (w->taken >> mid & 1))
                continue;
            went_on = true;
            w->taken |= 1u << mid;
            jumps_from(w, land, steps + 1, dirs | (u32)d << (2 * steps));
            w->taken &= ~(1u << mid);
        }
    }
    if (!went_on && steps > 0)
        add(w, (CkMove)w->from | (CkMove)steps << 5 | dirs << 9);
}

int ck_gen_jumps(CheckersPosition* p, CkMove* out) {
    JumpWalk w;
    w.board = p->board;
    w.out = out;
    w.n = 0;
    w.color = p->side;
    w.taken = 0;
    for (int s = 0; s < 32; s++) {
        u8 piece = p->board[s];
        if (piece == CK_EMPTY || CK_COLOR(piece) != p->side)
            continue;
        w.from = s;
        w.king = CK_KIND(piece) == CK_KING;
        p->board[s] = CK_EMPTY; // the jumper may pass back over its own square
        jumps_from(&w, s, 0, 0);
        p->board[s] = piece;
    }
    return w.n;
}

int ck_gen_legal(CheckersPosition* p, CkMove* out) {
    int n = ck_gen_jumps(p, out);
    if (n)
        return n;
    for (int s = 0; s < 32; s++) {
        u8 piece = p->board[s];
        if (piece == CK_EMPTY || CK_COLOR(piece) != p->side)
            continue;
        int d0 = 0, d1 = 4;
        if (CK_KIND(piece) == CK_MAN) {
            d0 = p->side == CK_DARK ? 2 : 0;
            d1 = d0 + 2;
        }
        for (int d = d0; d < d1; d++) {
            int to = step_to[s][d];
            if (to >= 0 && p->board[to] == CK_EMPTY && n < CK_MAX_MOVES)
                out[n++] = (CkMove)s | (CkMove)d << 9;
        }
    }
    return n;
}

// --- Make and unmake ----------------------------------------------------------

void ck_make(CheckersPosition* p, CkMove m) {
    if (p->ply >= POS_HISTORY_MAX)
        return;
    u8* b = p->board;
    int from = CK_MOVE_FROM(m), steps = MOVE_STEPS_FIELD(m);
    int us = p->side;
    ChessKey key = p->keys[p->ply];
    CkUndo* u = &p->undo[p->ply];
    u->move = m;
    u->clock = p->clock;
    u->kings = 0;
    u->crowned = 0;

    u8 piece = b[from];
    b[from] = CK_EMPTY;
    key ^= z_piece[piece][from];
    int to;
    if (steps == 0) {
        to = step_to[from][MOVE_DIR(m, 0)];
    } else {
        int s = from;
        for (int i = 0; i < steps; i++) {
            int d = MOVE_DIR(m, i);
            int mid = step_to[s][d];
            u8 victim = b[mid];
            if (CK_KIND(victim) == CK_KING)
                u->kings |= (u16)(1u << i);
            key ^= z_piece[victim][mid];
            b[mid] = CK_EMPTY;
            s = jump_to[s][d];
        }
        to = s;
    }
    bool man = CK_KIND(piece) == CK_MAN;
    if (man && crowning_row(us, to)) {
        piece = (u8)(us | CK_KING);
        u->crowned = 1;
    }
    b[to] = piece;
    key ^= z_piece[piece][to];

    p->clock = (u8)(steps || man ? 0 : p->clock < 255 ? p->clock + 1 : 255);
    p->side = (u8)(us ^ CK_LIGHT);
    key ^= z_side;
    p->ply++;
    p->keys[p->ply] = key;
}

void ck_unmake(CheckersPosition* p) {
    if (p->ply == 0)
        return;
    p->ply--;
    const CkUndo* u = &p->undo[p->ply];
    CkMove m = u->move;
    u8* b = p->board;
    int us = p->side ^ CK_LIGHT, them = p->side;
    p->side = (u8)us;
    p->clock = u->clock;
    int from = CK_MOVE_FROM(m), steps = MOVE_STEPS_FIELD(m);
    int to;
    if (steps == 0) {
        to = step_to[from][MOVE_DIR(m, 0)];
    } else {
        int s = from;
        for (int i = 0; i < steps; i++) {
            int d = MOVE_DIR(m, i);
            b[step_to[s][d]] = (u8)(them | (u->kings >> i & 1 ? CK_KING : CK_MAN));
            s = jump_to[s][d];
        }
        to = s;
    }
    u8 piece = b[to];
    b[to] = CK_EMPTY;
    if (u->crowned)
        piece = (u8)(us | CK_MAN);
    b[from] = piece;
}

// --- Results ----------------------------------------------------------------

// Whether the current position has come up three times since the last
// capture or man move (nothing before one can repeat).
static bool repeated(const CheckersPosition* p) {
    ChessKey key = ck_key(p);
    int reach = p->clock < p->ply ? p->clock : p->ply;
    int seen = 1;
    for (int back = 2; back <= reach; back += 2)
        if (ck_key_at(p, back) == key && ++seen == 3)
            return true;
    return false;
}

GameResult ck_result(CheckersPosition* p) {
    CkMove list[CK_MAX_MOVES];
    if (ck_gen_legal(p, list) == 0)
        return RESULT_NO_MOVES;
    if (p->clock >= 80)
        return RESULT_FORTY;
    if (repeated(p))
        return RESULT_REPETITION;
    if (p->ply >= GAME_PLY_MAX)
        return RESULT_LENGTH;
    return RESULT_NONE;
}

// --- Notation ---------------------------------------------------------------

// Writes the square number of s (1..32) at out; returns the characters written.
static int put_number(char* out, int s) {
    int n = s + 1;
    if (n >= 10) {
        int tens = n >= 30 ? 3 : n >= 20 ? 2 : 1;
        out[0] = (char)('0' + tens);
        out[1] = (char)('0' + n - 10 * tens);
        return 2;
    }
    out[0] = (char)('0' + n);
    return 1;
}

int ck_notation(const CheckersPosition* p, CkMove m, char out[24]) {
    (void)p;
    int steps = MOVE_STEPS_FIELD(m);
    int n = put_number(out, CK_MOVE_FROM(m));
    if (steps == 0) {
        out[n++] = '-';
        n += put_number(out + n, ck_move_to(m));
    } else if (steps > 7) { // "1x10x19x..." would not fit: its ends only
        out[n++] = 'x';
        n += put_number(out + n, ck_move_to(m));
    } else {
        for (int i = 0; i < steps; i++) {
            out[n++] = 'x';
            n += put_number(out + n, ck_move_landing(m, i));
        }
    }
    out[n] = '\0';
    return n;
}

// --- Finding moves ------------------------------------------------------------

// Whether m goes from `from` by these count landings first.
static bool begins(CkMove m, int from, const int* landings, int count) {
    if (CK_MOVE_FROM(m) != from || ck_move_steps(m) < count)
        return false;
    for (int i = 0; i < count; i++)
        if (ck_move_landing(m, i) != landings[i])
            return false;
    return true;
}

CkMove ck_find(CheckersPosition* p, int from, const int* landings, int count) {
    CkMove list[CK_MAX_MOVES];
    int n = ck_gen_legal(p, list);
    CkMove found = CK_MOVE_NONE;
    int matches = 0;
    for (int i = 0; i < n; i++) {
        if (!begins(list[i], from, landings, count))
            continue;
        if (ck_move_steps(list[i]) == count)
            return list[i];
        found = list[i];
        matches++;
    }
    return matches == 1 ? found : CK_MOVE_NONE;
}

int ck_continuations(CheckersPosition* p, int from, const int* landings, int count, int* next) {
    CkMove list[CK_MAX_MOVES];
    int n = ck_gen_legal(p, list);
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (ck_move_steps(list[i]) <= count || !begins(list[i], from, landings, count))
            continue;
        int s = ck_move_landing(list[i], count);
        bool known = false;
        for (int k = 0; k < found; k++)
            if (next[k] == s)
                known = true;
        if (!known && found < 4)
            next[found++] = s;
    }
    return found;
}

// --- Perft ------------------------------------------------------------------

unsigned long long ck_perft(CheckersPosition* p, int depth) {
    if (depth <= 0)
        return 1;
    CkMove list[CK_MAX_MOVES];
    int n = ck_gen_legal(p, list);
    if (depth == 1)
        return (unsigned long long)n;
    unsigned long long nodes = 0;
    for (int i = 0; i < n; i++) {
        ck_make(p, list[i]);
        nodes += ck_perft(p, depth - 1);
        ck_unmake(p);
    }
    return nodes;
}
