// The board view: the table's squares and labels by map_set_cell(), the
// pieces and everything on them as sprites, the moves' animations, and the
// side panel's text and icons.

#include "view.h"

#include "art.h"

static bool flipped;

// --- Squares --------------------------------------------------------------------------------

static int square_at(int col, int row) {
    return flipped ? SQ(7 - col, row) : SQ(col, 7 - row);
}

void view_set_flipped(bool on) {
    flipped = on;
}

bool view_flipped(void) {
    return flipped;
}

int view_square_x(int sq) {
    return BOARD_X + (flipped ? 7 - SQ_FILE(sq) : SQ_FILE(sq)) * SQUARE;
}

int view_square_y(int sq) {
    return BOARD_Y + (flipped ? SQ_RANK(sq) : 7 - SQ_RANK(sq)) * SQUARE;
}

// --- The map: highlights, labels, numbers -----------------------------------------------------

static s8 last_from = -1, last_to = -1, alert = -1, selected = -1;

// What each changeable cell shows now: the squares by row * 8 + col as
// seen, the rank labels and the file labels.
static u16 shown_square[64];
static u16 shown_rank[8], shown_file[8];
static u8 alert_level = 0xFF; // BANK_ALERT's stage now

void view_set_highlights(int from, int to, int alert_sq, int selected_sq) {
    last_from = (s8)from;
    last_to = (s8)to;
    alert = (s8)alert_sq;
    selected = (s8)selected_sq;
}

static void set_cell(u16* shown, int mx, int my, u16 metatile) {
    if (*shown != metatile) {
        *shown = metatile;
        map_set_cell(mx, my, metatile);
    }
}

// Brings the squares and the frame up to date: map_set_cell() only where a
// cell changes (the engine redraws those in VBlank), and the cells back to
// the map's own metatile where nothing is on them, which frees their place
// in the engine's table of changes (64): chess uses 16 for the labels and
// up to 4 for the highlights, checkers 32 for the dark squares' numbers.
static void update_map(int game) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            int sq = square_at(col, row);
            int state = sq == alert                        ? SQ_ALERT
                        : sq == selected                   ? SQ_SELECTED
                        : sq == last_from || sq == last_to ? SQ_LAST
                                                           : SQ_PLAIN;
            int numbers = NUM_NONE;
            if (game == GAME_CHECKERS && ((col + row) & 1))
                numbers = flipped ? NUM_FLIPPED : NUM_UPRIGHT;
            u16 mt = state == SQ_PLAIN && numbers == NUM_NONE
                         ? (u16)MT_CELL(col + 1, row + 1)
                         : (u16)MT_SQUARE(state, numbers, col, row);
            set_cell(&shown_square[row * 8 + col], col + 1, row + 1, mt);
        }
    }
    for (int i = 0; i < 8; i++) {
        bool chess = game == GAME_CHESS;
        set_cell(&shown_rank[i], 0, i + 1,
                 chess ? (u16)MT_RANK(flipped, i) : (u16)MT_CELL(0, i + 1));
        set_cell(&shown_file[i], i + 1, 9,
                 chess ? (u16)MT_FILE(flipped, i) : (u16)MT_CELL(i + 1, 9));
    }
    // The king in check's square flashes: BANK_ALERT's colours step up and
    // down through their stages, two frames a stage.
    if (alert >= 0) {
        u32 phase = (frame_count() >> 1) & 15;
        u8 level = (u8)(phase < ALERT_LEVELS ? phase : 15 - phase);
        if (level != alert_level) {
            alert_level = level;
            tileset_set_colors(BANK_ALERT * 16 + 1, alert_colors[level], SQUARE_COLORS);
        }
    }
}

// --- Targets, marks, the cursor
// -------------------------------------------------------------------

static u8 target_sq[64];
static bool target_ring[64];
static int target_count;
static u8 mark_sq[64];
static int mark_count;

void view_set_targets(const int* squares, const bool* captures, int count) {
    if (count > 64)
        count = 64;
    for (int i = 0; i < count; i++) {
        target_sq[i] = (u8)squares[i];
        target_ring[i] = captures && captures[i];
    }
    target_count = count;
}

void view_set_marks(const int* squares, int count) {
    if (count > 64)
        count = 64;
    for (int i = 0; i < count; i++)
        mark_sq[i] = (u8)squares[i];
    mark_count = count;
}

void view_draw_cursor(int sq) {
    sprite_draw(SPR_CURSOR, (u8)((frame_count() >> 4) & 1), view_square_x(sq), view_square_y(sq),
                0);
}

// --- Pieces
// -----------------------------------------------------------------------------------------

// A piece as the view keeps it: 0 for none, else its frame + 1 and its
// palette in bit 4.
#define CODE(frame, palette) ((u8)(((palette) << 4) | ((frame) + 1)))
#define CODE_FRAME(c) (((c) & 15) - 1)
#define CODE_PALETTE(c) ((c) >> 4)
#define NO_SQUARE 0xFF

static void board_of(const Match* m, u8 out[64]) {
    if (m->game == GAME_CHECKERS) {
        for (int sq = 0; sq < 64; sq++)
            out[sq] = 0;
        for (int s = 0; s < 32; s++) {
            u8 p = ck_piece(&m->pos.checkers, s);
            if (p != CK_EMPTY)
                out[SQ(ck_square_file(s), ck_square_rank(s))] =
                    CODE(CK_KIND(p) == CK_KING ? FRAME_CROWNED : FRAME_MAN,
                         CK_COLOR(p) == CK_DARK ? PAL_EBONY : PAL_IVORY);
        }
        return;
    }
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos_piece(&m->pos.chess, sq);
        out[sq] = p == EMPTY ? 0 : CODE(PIECE_KIND(p) - 1, PIECE_COLOR(p) ? PAL_EBONY : PAL_IVORY);
    }
}

static void draw_piece(u8 code, int x, int y) {
    sprite_draw(SPR_PIECE, (u8)CODE_FRAME(code), x, y, SPRITE_PALETTE(CODE_PALETTE(code)));
}

// --- Animation
// --------------------------------------------------------------------------------------

// One piece's journey: from one square to another (a slide, or a checkers
// hop), the square whose piece goes when it lands (NO_SQUARE for none) and
// what it becomes there (0: itself).
typedef struct {
    u8 from, to, piece, hop, gone, becomes;
} Step;

#define STEP_MAX (CK_JUMPS_MAX + 1)
static Step steps[STEP_MAX];
static int step_count, step_at, step_t;
static u8 anim_board[64]; // the pieces standing still while the animation runs

// Slides ease out over SLIDE_FRAMES, lifted a little; hops go at an even
// pace over HOP_FRAMES in an arc. Shares of the way in 256ths and heights
// in pixels, by frame.
#define SLIDE_FRAMES 12
#define HOP_FRAMES 10
static const u16 slide_ease[SLIDE_FRAMES] = {41,  78,  112, 142, 169, 192,
                                             211, 228, 240, 249, 254, 256};
static const u8 slide_lift[SLIDE_FRAMES] = {1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0};
static const u16 hop_pace[HOP_FRAMES] = {26, 51, 77, 102, 128, 154, 179, 205, 230, 256};
static const u8 hop_lift[HOP_FRAMES] = {2, 4, 5, 6, 6, 6, 5, 4, 2, 0};

// Puffs and sparkles: where, which, and how far along.
typedef struct {
    s16 x, y;
    u8 sprite, t; // t counts up to the effect's length; 0: unused
} Effect;
#define EFFECT_MAX 16
#define PUFF_TICKS 3 // frames each frame of the puff shows
#define SPARKLE_TICKS 5
static Effect effects[EFFECT_MAX];

static void effect(u8 sprite, int sq) {
    int inset = sprite == SPR_PUFF ? (PUFF_SIDE - SQUARE) / 2 : 0; // the puff is bigger
    for (int i = 0; i < EFFECT_MAX; i++) {
        if (effects[i].t == 0) {
            effects[i] = (Effect){(s16)(view_square_x(sq) - inset),
                                  (s16)(view_square_y(sq) - inset), sprite, 1};
            return;
        }
    }
}

static void add_step(int from, int to, u8 piece, bool hop, int gone, u8 becomes) {
    if (step_count < STEP_MAX)
        steps[step_count++] = (Step){(u8)from, (u8)to, piece, hop, (u8)gone, becomes};
}

void view_animate(const Match* before, u32 move) {
    board_of(before, anim_board);
    step_count = step_at = step_t = 0;
    if (before->game == GAME_CHECKERS) {
        CkMove mv = move;
        int s = CK_MOVE_FROM(mv);
        int from = SQ(ck_square_file(s), ck_square_rank(s));
        u8 piece = anim_board[from];
        bool jump = ck_move_is_jump(mv);
        int n = ck_move_steps(mv);
        for (int i = 0; i < n; i++) {
            int l = ck_move_landing(mv, i);
            int to = SQ(ck_square_file(l), ck_square_rank(l));
            int gone = NO_SQUARE;
            if (jump)
                gone = SQ((SQ_FILE(from) + SQ_FILE(to)) >> 1, (SQ_RANK(from) + SQ_RANK(to)) >> 1);
            // A man crowned on the far row: Dark's men move down the board,
            // Light's up.
            u8 becomes = 0;
            int far = CODE_PALETTE(piece) == PAL_EBONY ? 0 : 7;
            if (i == n - 1 && CODE_FRAME(piece) == FRAME_MAN && SQ_RANK(to) == far)
                becomes = CODE(FRAME_CROWNED, CODE_PALETTE(piece));
            add_step(from, to, piece, true, gone, becomes);
            from = to;
        }
    } else {
        Move mv = (Move)move;
        int from = MOVE_FROM(mv), to = MOVE_TO(mv), flags = MOVE_FLAGS(mv);
        u8 piece = anim_board[from];
        int gone = NO_SQUARE;
        if (flags & MF_CAPTURE)
            gone = flags == MF_EP ? SQ(SQ_FILE(to), SQ_RANK(from)) : to;
        u8 becomes = 0;
        if (flags & MF_PROMO)
            becomes = CODE(MOVE_PROMO_KIND(mv) - 1, CODE_PALETTE(piece));
        add_step(from, to, piece, false, gone, becomes);
        if (flags == MF_CASTLE_K || flags == MF_CASTLE_Q) {
            int rank = SQ_RANK(from);
            int rook_from = SQ(flags == MF_CASTLE_K ? 7 : 0, rank);
            int rook_to = SQ(flags == MF_CASTLE_K ? 5 : 3, rank);
            add_step(rook_from, rook_to, anim_board[rook_from], false, NO_SQUARE, 0);
        }
    }
    if (step_count > 0)
        anim_board[steps[0].from] = 0; // the moving piece is drawn on its own
}

bool view_animating(void) {
    if (step_at < step_count)
        return true;
    for (int i = 0; i < EFFECT_MAX; i++)
        if (effects[i].t)
            return true;
    return false;
}

// One frame of the animation: the step under way moves on, and lands.
static void advance(void) {
    if (step_at >= step_count)
        return;
    const Step* s = &steps[step_at];
    if (++step_t < (s->hop ? HOP_FRAMES : SLIDE_FRAMES))
        return;
    if (s->gone != NO_SQUARE) {
        anim_board[s->gone] = 0;
        effect(SPR_PUFF, s->gone);
    }
    anim_board[s->to] = s->becomes ? s->becomes : s->piece;
    if (s->becomes)
        effect(SPR_SPARKLE, s->to);
    step_at++;
    step_t = 0;
    if (step_at < step_count)
        anim_board[steps[step_at].from] = 0;
}

// The moving piece, in front of the others.
static void draw_moving(void) {
    if (step_at >= step_count)
        return;
    const Step* s = &steps[step_at];
    int fx = view_square_x(s->from), fy = view_square_y(s->from);
    int tx = view_square_x(s->to), ty = view_square_y(s->to);
    int k = step_t; // frames done; the frame shown is k
    int share = s->hop ? hop_pace[k] : slide_ease[k];
    int lift = s->hop ? hop_lift[k] : slide_lift[k];
    draw_piece(s->piece, fx + (tx - fx) * share / 256, fy + (ty - fy) * share / 256 - lift);
}

// The sparkles (in front of the pieces: drawn before them) or the puffs
// (behind: after).
static void draw_effects(u8 sprite) {
    for (int i = 0; i < EFFECT_MAX; i++) {
        Effect* e = &effects[i];
        if (e->t == 0 || e->sprite != sprite)
            continue;
        bool puff = e->sprite == SPR_PUFF;
        int ticks = puff ? PUFF_TICKS : SPARKLE_TICKS;
        int frames = puff ? PUFF_FRAMES : SPARKLE_FRAMES;
        int frame = 0, t = e->t - 1;
        while (t >= ticks) // at most a few steps: no division
            t -= ticks, frame++;
        if (frame >= frames) {
            e->t = 0;
            continue;
        }
        sprite_draw(e->sprite, (u8)frame, e->x, e->y, 0);
        e->t++;
    }
}

// --- Drawing
// ----------------------------------------------------------------------------------------

static u8 board_now[64];

void view_draw_match(const Match* m) {
    update_map(m->game);
    draw_effects(SPR_SPARKLE);
    const u8* board = anim_board;
    if (step_at < step_count) {
        draw_moving();
        advance();
    } else {
        board_of(m, board_now);
        board = board_now;
    }
    for (int sq = 0; sq < 64; sq++)
        if (board[sq])
            draw_piece(board[sq], view_square_x(sq), view_square_y(sq));
    // The puffs, the marks and the targets behind the pieces: drawn after
    // them.
    draw_effects(SPR_PUFF);
    u8 mark_frame = (u8)((frame_count() >> 3) & 1);
    for (int i = 0; i < mark_count; i++)
        sprite_draw(SPR_MARK, mark_frame, view_square_x(mark_sq[i]), view_square_y(mark_sq[i]), 0);
    for (int i = 0; i < target_count; i++)
        sprite_draw(SPR_TARGET, target_ring[i] ? TARGET_RING : TARGET_DOT,
                    view_square_x(target_sq[i]), view_square_y(target_sq[i]), SPRITE_BLEND);
}

// --- The panel
// --------------------------------------------------------------------------------------

#define PANEL_PX (PANEL_COL * 8) // the panel's text's left edge, pixels
#define ROW_GAME 1
#define ROW_OPPONENT 2
#define ROW_TURN 4
#define ROW_MOVE 5
#define ROW_TAKEN 7 // and the row under it
#define ROW_PLIES 10
#define PLIES_SHOWN 6
#define ROW_STATUS 17 // and the row under it
#define STATUS_MAX 32
#define PLY_CHARS (PANEL_COLS - 1) // beside the ply's icon

static const char* const level_names[LEVEL_COUNT] = {"VS CUB", "VS HUNTER", "VS SERVAL"};

// What the panel's text shows, to redraw it only when that changes.
static struct {
    bool valid;
    u8 game, mode, level, side, flipped;
    u16 count;
    u32 last_move; // so an undo and another move in one frame still show
    char status[STATUS_MAX];
} shown;
static char plies[PLIES_SHOWN][24];

static bool same_text(const char* a, const char* b) {
    int i = 0;
    while (a[i] && a[i] == b[i])
        i++;
    return a[i] == b[i];
}

static void copy_text(char* out, const char* s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) {
        out[i] = s[i];
        i++;
    }
    out[i] = '\0';
}

static int text_length(const char* s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

// A long checkers jump by its ends only ("1x10x19x28" as "1x28"), as PDN
// allows, so that every ply fits beside its icon.
static void shorten(char* s) {
    int n = text_length(s);
    if (n <= PLY_CHARS)
        return;
    int first = 0, last = n - 1;
    while (s[first] && s[first] != 'x')
        first++;
    while (last > first && s[last] != 'x')
        last--;
    if (first >= last) {
        s[PLY_CHARS] = '\0';
        return;
    }
    int j = first + 1;
    for (int i = last + 1; i <= n; i++) // the terminator too
        s[j++] = s[i];
}

static void draw_status(const char* status) {
    text_set_style(status[0] == 'C' && status[1] == 'H' && status[2] == 'E' && status[3] == 'C' &&
                           status[4] == 'K'
                       ? STYLE_ALERT
                       : TEXT_HIGHLIGHT);
    int n = text_length(status);
    if (n <= PANEL_COLS) {
        text_print_centered_in(PANEL_COL, PANEL_COLS, ROW_STATUS, status);
    } else {
        // Two lines, split at the space that makes the longer line shortest.
        int split = -1, widest = STATUS_MAX;
        for (int i = 0; i < n; i++) {
            int w = i > n - 1 - i ? i : n - 1 - i;
            if (status[i] == ' ' && w < widest)
                split = i, widest = w;
        }
        char line[STATUS_MAX];
        if (split < 0)
            split = PANEL_COLS;
        copy_text(line, status, split + 1);
        text_print_centered_in(PANEL_COL, PANEL_COLS, ROW_STATUS, line);
        text_print_centered_in(PANEL_COL, PANEL_COLS, ROW_STATUS + 1,
                               status + split + (status[split] == ' '));
    }
    text_set_style(TEXT_NORMAL);
}

static void draw_text(Match* m, const char* status) {
    bool checkers = m->game == GAME_CHECKERS;
    text_clear_area(PANEL_COL, 0, PANEL_COLS, TEXT_ROWS);
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered_in(PANEL_COL, PANEL_COLS, ROW_GAME, checkers ? "CHECKERS" : "CHESS");
    text_set_style(STYLE_DIM);
    text_print_centered_in(PANEL_COL, PANEL_COLS, ROW_OPPONENT,
                           m->mode == MODE_TWO_PLAYERS ? "2 PLAYERS"
                                                       : level_names[m->level % LEVEL_COUNT]);
    text_set_style(TEXT_NORMAL);
    int side = match_side_to_move(m);
    static const char* const names[2][2] = {{"WHITE", "BLACK"}, {"DARK", "LIGHT"}};
    text_print(PANEL_COL + 2, ROW_TURN, names[checkers][side != 0]);
    text_set_style(STYLE_DIM);
    text_print(PANEL_COL + 2, ROW_MOVE, text_format("MOVE %d", m->count / 2 + 1));
    int first = m->count > PLIES_SHOWN ? m->count - PLIES_SHOWN : 0;
    for (int i = first; i < m->count; i++) {
        char* s = plies[i - first];
        match_notation(m, i, s);
        shorten(s);
        text_set_style(i == m->count - 1 ? TEXT_NORMAL : STYLE_DIM);
        text_print(PANEL_COL + 1, ROW_PLIES + i - first, s);
    }
    text_set_style(TEXT_NORMAL);
    if (status[0])
        draw_status(status);
}

// The pieces each side has lost, as frames in the order shown (pawns or
// men first): returns how many.
static int taken(const Match* m, int palette, u8* out) {
    int n = 0;
    if (m->game == GAME_CHECKERS) {
        int color = palette == PAL_EBONY ? CK_DARK : CK_LIGHT, left = 0;
        for (int s = 0; s < 32; s++) {
            u8 p = ck_piece(&m->pos.checkers, s);
            if (p != CK_EMPTY && CK_COLOR(p) == color)
                left++;
        }
        for (int i = left; i < 12; i++)
            out[n++] = FRAME_MAN;
        return n;
    }
    // Chess: the start's pieces less those on the board. A promoted piece
    // counts as the pawn it was, so the pawns lost are the eight less those
    // left and those promoted.
    static const u8 start[6] = {8, 2, 2, 2, 1, 1};
    int color = palette == PAL_EBONY ? BLACK : WHITE;
    int have[6] = {0};
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos_piece(&m->pos.chess, sq);
        if (p != EMPTY && PIECE_COLOR(p) == color)
            have[PIECE_KIND(p) - 1]++;
    }
    int promoted = 0;
    for (int k = 1; k < 5; k++)
        if (have[k] > start[k])
            promoted += have[k] - start[k];
    int lost[5];
    lost[0] = 8 - have[0] - promoted;
    for (int k = 1; k < 5; k++)
        lost[k] = start[k] - have[k];
    for (int k = 0; k < 5; k++)
        for (int i = 0; i < lost[k]; i++)
            out[n++] = (u8)k;
    return n;
}

static void draw_icons(const Match* m) {
    bool checkers = m->game == GAME_CHECKERS;
    int side = match_side_to_move(m);
    int side_palette =
        side != 0 ? (checkers ? PAL_IVORY : PAL_EBONY) : (checkers ? PAL_EBONY : PAL_IVORY);
    sprite_draw(SPR_ICON, checkers ? FRAME_MAN : FRAME_KING, PANEL_PX + 4, ROW_TURN * 8,
                SPRITE_PALETTE(side_palette));
    // The pieces taken: on the upper row those the player at the top of
    // the board has taken, so each side's takings are on its side. Black's
    // chess pieces and Dark's men start at the top unless flipped.
    int top = flipped ? PAL_IVORY : PAL_EBONY;
    for (int r = 0; r < 2; r++) {
        int victim = (r == 0) == (top == PAL_EBONY) ? PAL_IVORY : PAL_EBONY;
        u8 frames[16];
        int n = taken(m, victim, frames);
        int pitch = n <= 11 ? 7 : n <= 13 ? 6 : 5; // 15 at most fit
        for (int i = 0; i < n; i++)
            sprite_draw(SPR_ICON, frames[i], PANEL_PX + i * pitch, (ROW_TAKEN + r) * 8,
                        SPRITE_PALETTE(victim));
    }
    // An icon of the side that played each ply shown: chess's White and
    // checkers' Dark play the even plies.
    int first = m->count > PLIES_SHOWN ? m->count - PLIES_SHOWN : 0;
    for (int i = first; i < m->count; i++) {
        bool second = i & 1;
        int palette = second != checkers ? PAL_EBONY : PAL_IVORY;
        sprite_draw(SPR_ICON, ICON_CHIP, PANEL_PX, (ROW_PLIES + i - first) * 8,
                    SPRITE_PALETTE(palette));
    }
}

void view_panel(Match* m, const char* status) {
    if (!status)
        status = "";
    u32 last_move = m->count ? m->moves[m->count - 1] : 0;
    if (!shown.valid || shown.game != m->game || shown.mode != m->mode || shown.level != m->level ||
        shown.count != m->count || shown.side != match_side_to_move(m) ||
        shown.last_move != last_move || !same_text(shown.status, status)) {
        shown.valid = true;
        shown.game = m->game;
        shown.mode = m->mode;
        shown.level = m->level;
        shown.count = m->count;
        shown.last_move = last_move;
        shown.side = (u8)match_side_to_move(m);
        copy_text(shown.status, status, STATUS_MAX);
        draw_text(m, shown.status);
    }
    draw_icons(m);
}

// --- Setting up
// -------------------------------------------------------------------------------------

void view_init(void) {
    art_load();
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            shown_square[row * 8 + col] = (u16)MT_CELL(col + 1, row + 1);
    for (int i = 0; i < 8; i++) {
        shown_rank[i] = (u16)MT_CELL(0, i + 1);
        shown_file[i] = (u16)MT_CELL(i + 1, 9);
    }
    // The dots and rings (SPRITE_BLEND) darken the squares under them.
    screen_set_blend(0, LAYER_PLAYFIELD, 9, 7);
    text_set_shadow(true);
    text_set_style_color(TEXT_NORMAL, COLOR_RGB(246, 236, 210), COLOR_RGB(20, 12, 10));
    text_set_style_color(TEXT_HIGHLIGHT, COLOR_RGB(255, 210, 100), COLOR_RGB(40, 20, 10));
    text_set_style_color(STYLE_DIM, COLOR_RGB(176, 150, 118), COLOR_RGB(20, 12, 10));
    text_set_style_color(STYLE_ALERT, COLOR_RGB(255, 104, 88), COLOR_RGB(40, 8, 8));
    // Each style builds its glyphs the first time it is used: now, while
    // the screen is black.
    text_clear();
    text_set_style(STYLE_ALERT);
    text_set_style(STYLE_DIM);
    text_set_style(TEXT_HIGHLIGHT);
    text_set_style(TEXT_NORMAL);
}
