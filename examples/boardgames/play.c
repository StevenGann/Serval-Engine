// The play screen: see play.h.
//
// A frame: the buttons (the menu's, the result's, or the board's: the
// cursor, picking and moving), then the computer (a search started, a step
// of it, or its move played once the board has stopped moving), then the
// drawing. The computer thinks as much a frame as the frame has time for
// (search_pace(), from the last frame's cycles; a fixed amount on the web),
// but not in a frame that already did something heavy (a move played and
// saved, an undo, the search set up, the screen dimmed): the move found does
// not depend on which frames it thought in, or how much in each, so this
// changes nothing but the time, and keeps every frame inside its budget.

#include "play.h"

#include "chess_rules.h"
#include "records.h"
#include "sound.h"
#include "view.h"

enum { UI_BOARD, UI_PROMOTE, UI_MENU, UI_OVER };
enum { ITEM_RESUME, ITEM_UNDO, ITEM_FLIP, ITEM_NEW, ITEM_QUIT, ITEM_COUNT };

// The text over the board (the menu, the result): columns BOARD_COL to
// BOARD_COL + BOARD_COLS - 1, the board's 16 and one each side over the
// frame.
#define BOARD_COL 1
#define BOARD_COLS 18
#define MENU_ROW 4 // "PAUSED"; the items from two rows under it, two apart
#define BANNER_ROW 7

#define THINK_DOT_FRAMES 10 // the THINKING dots' pace
#define READOUT_FRAMES 15   // the CPU readout shows the busiest of each 15 frames

static const char* const item_names[ITEM_COUNT] = {"RESUME", "UNDO", "FLIP BOARD", "NEW GAME",
                                                   "QUIT TO TITLE"};

// Lists out of the stack (the GBA's is small).
static Move chess_moves[MAX_MOVES] SERVAL_EWRAM_BSS;
static CkMove ck_moves[CK_MAX_MOVES] SERVAL_EWRAM_BSS;

static struct {
    Match* m;
    u8 ui;
    s8 cursor; // a square, chess's numbering in both games
    s8 picked; // the piece picked up (its square), -1 for none
    // The picked piece's destinations (in checkers, its next landings).
    int targets[64];
    bool rings[64];
    int target_count;
    // Checkers: the landings made so far of a jump being landed one by one.
    int landings[CK_JUMPS_MAX];
    int landed;
    // The promotion: where to, and the piece chosen (0 queen ... 3 knight).
    s8 promote_to;
    u8 choice;
    u8 item;
    // The computer: thinking, or its move found and waiting for the board
    // to stop moving.
    bool thinking, pending;
    u32 pending_move;
    // The position the derived state below is for.
    bool known;
    u16 known_count;
    u32 known_last;
    int check_sq;       // chess: the king in check's square, -1
    bool jump_forced;   // checkers: the side to move must jump
    int marks[12];      // checkers: the pieces that can (must) jump
    int mark_count;     //
    bool castle_landed; // the castling being shown: the king has landed
    bool save_pending;  // the save is written in a frame of its own
    u8 settle;          // frames before the computer may start thinking
    bool announced;     // the result shown
    bool readout;
    u32 readout_peak, readout_frames;
    char readout_text[12];
    SearchPace pace; // the work a frame (GBA)
    u32 stepped;     // the search's work this frame
} st SERVAL_EWRAM_BSS;

// --- Small helpers ------------------------------------------------------------------

static bool is_checkers(void) {
    return st.m->game == GAME_CHECKERS;
}

static bool humans_turn(void) {
    const Match* m = st.m;
    return m->result == RESULT_NONE &&
           (m->mode == MODE_TWO_PLAYERS || match_side_to_move(m) == m->human_side);
}

static int ck_to_sq(int s) {
    return SQ(ck_square_file(s), ck_square_rank(s));
}

static int sq_to_ck(int sq) {
    return ck_square_at(SQ_FILE(sq), SQ_RANK(sq));
}

// Whether the piece on sq belongs to the side to move.
static bool own_piece(int sq) {
    const Match* m = st.m;
    if (is_checkers()) {
        int s = sq_to_ck(sq);
        u8 p = s < 0 ? CK_EMPTY : ck_piece(&m->pos.checkers, s);
        return p != CK_EMPTY && CK_COLOR(p) == ck_side(&m->pos.checkers);
    }
    Piece p = pos_piece(&m->pos.chess, sq);
    return p != EMPTY && PIECE_COLOR(p) == pos_side(&m->pos.chess);
}

static void clear_board_text(void) {
    text_clear_area(0, 0, BOARD_COL + BOARD_COLS, TEXT_ROWS);
}

// Prints s centred over the board, on two lines (split at the space nearest
// the middle) if it is too long for one. Returns the lines used.
static int print_over_board(int row, const char* s) {
    int n = 0;
    while (s[n])
        n++;
    if (n <= BOARD_COLS) {
        text_print_centered_in(BOARD_COL, BOARD_COLS, row, s);
        return 1;
    }
    int split = -1, best = n;
    for (int i = 0; i < n; i++) {
        int w = i > n - 1 - i ? i : n - 1 - i;
        if (s[i] == ' ' && w < best)
            split = i, best = w;
    }
    if (split < 0)
        split = BOARD_COLS;
    text_print_centered_in(BOARD_COL, BOARD_COLS, row, text_format("%.*s", split, s));
    text_print_centered_in(BOARD_COL, BOARD_COLS, row + 1, s + split + (s[split] == ' '));
    return 2;
}

// --- What follows from the position ---------------------------------------------------

// Works out the check, the forced jumps and their marks again when the
// position has changed since.
static void know_position(void) {
    Match* m = st.m;
    u32 last = m->count ? m->moves[m->count - 1] : 0;
    if (st.known && st.known_count == m->count && st.known_last == last)
        return;
    st.known = true;
    st.known_count = m->count;
    st.known_last = last;
    st.check_sq = -1;
    st.jump_forced = false;
    st.mark_count = 0;
    if (is_checkers()) {
        int n = m->result == RESULT_NONE ? ck_gen_legal(&m->pos.checkers, ck_moves) : 0;
        st.jump_forced = n > 0 && ck_move_is_jump(ck_moves[0]);
        for (int i = 0; st.jump_forced && i < n; i++) {
            int sq = ck_to_sq(CK_MOVE_FROM(ck_moves[i]));
            bool seen = false;
            for (int j = 0; j < st.mark_count; j++)
                seen |= st.marks[j] == sq;
            if (!seen && st.mark_count < 12)
                st.marks[st.mark_count++] = sq;
        }
    } else if (pos_in_check(&m->pos.chess)) {
        int side = pos_side(&m->pos.chess);
        for (int sq = 0; sq < 64; sq++)
            if (pos_piece(&m->pos.chess, sq) == (Piece)(side | KING))
                st.check_sq = sq;
    }
}

// The last move's two squares, for the tint.
static void last_move_squares(int* from, int* to) {
    const Match* m = st.m;
    *from = *to = -1;
    if (m->count == 0)
        return;
    u32 mv = m->moves[m->count - 1];
    if (is_checkers()) {
        *from = ck_to_sq(CK_MOVE_FROM(mv));
        *to = ck_to_sq(ck_move_to(mv));
    } else {
        *from = MOVE_FROM((Move)mv);
        *to = MOVE_TO((Move)mv);
    }
}

// --- Picking a piece -------------------------------------------------------------------

static void drop(void) {
    st.picked = -1;
    st.landed = 0;
    st.target_count = 0;
    view_set_lift(-1, -1);
}

// The picked piece's destinations into st.targets.
static void find_targets(void) {
    Match* m = st.m;
    st.target_count = 0;
    if (is_checkers()) {
        int next[4];
        int n =
            ck_continuations(&m->pos.checkers, sq_to_ck(st.picked), st.landings, st.landed, next);
        for (int i = 0; i < n; i++) {
            st.targets[st.target_count] = ck_to_sq(next[i]);
            st.rings[st.target_count++] = st.jump_forced;
        }
        return;
    }
    int n = pos_gen_legal(&m->pos.chess, chess_moves);
    for (int i = 0; i < n; i++) {
        Move mv = chess_moves[i];
        if (MOVE_FROM(mv) != st.picked)
            continue;
        int to = MOVE_TO(mv);
        bool seen = false; // a promotion's four pieces go to one square
        for (int j = 0; j < st.target_count; j++)
            seen |= st.targets[j] == to;
        if (seen)
            continue;
        st.targets[st.target_count] = to;
        st.rings[st.target_count++] = (MOVE_FLAGS(mv) & MF_CAPTURE) != 0;
    }
}

static void pick(int sq) {
    drop();
    st.picked = (s8)sq;
    find_targets();
    // A piece with no move can be picked, but shows none (in checkers,
    // when another piece must jump).
    if (st.target_count)
        sound_pick();
    else
        sound_no();
}

static bool is_target(int sq) {
    for (int i = 0; i < st.target_count; i++)
        if (st.targets[i] == sq)
            return true;
    return false;
}

// --- Playing a move --------------------------------------------------------------------

// Plays mv for the side to move (the human's or the computer's), its first
// `done` landings already shown (checkers), and saves.
static void play_move(u32 mv, int done) {
    Match* m = st.m;
    view_animate_from(m, mv, done);
    if (!match_play(m, mv))
        view_animation_stop(); // cannot happen: every move comes from the rules
    st.castle_landed = false;
    drop();
    st.ui = UI_BOARD;
    st.save_pending = true;
}

// The save (over half a frame, SRAM being slow): in a frame with nothing
// else heavy, once the board has stopped moving. A finished game is counted
// in the record.
static void save(void) {
    const Match* m = st.m;
    st.save_pending = false;
    if (m->result == RESULT_NONE)
        board_records_save_game(m);
    else
        board_records_end_game(m);
}

// The sounds of what the animation did this frame.
static void sound_events(u32 events) {
    const Match* m = st.m;
    if (!(events & VIEW_LANDED))
        return;
    u32 mv = m->count ? m->moves[m->count - 1] : 0;
    bool castle = !is_checkers() &&
                  (MOVE_FLAGS((Move)mv) == MF_CASTLE_K || MOVE_FLAGS((Move)mv) == MF_CASTLE_Q);
    if (events & VIEW_CROWNED)
        sound_promote();
    else if (events & VIEW_CAPTURED)
        sound_capture();
    else if (castle) {
        if (!st.castle_landed) // the king: both knocks, the rook's with it
            sound_castle();
        st.castle_landed = true;
    } else
        sound_move();
    if ((events & VIEW_DONE) && st.check_sq >= 0 && m->result == RESULT_NONE)
        sound_check();
}

// --- The board's buttons ------------------------------------------------------------------

// The cursor moves on the board as it is seen: UP is toward the top of the
// screen, whichever way up the board is.
static void move_cursor(void) {
    int file = SQ_FILE(st.cursor), rank = SQ_RANK(st.cursor);
    int up = view_flipped() ? -1 : 1;
    if (button_repeat(BUTTON_UP))
        rank += up;
    if (button_repeat(BUTTON_DOWN))
        rank -= up;
    if (button_repeat(BUTTON_RIGHT))
        file += up;
    if (button_repeat(BUTTON_LEFT))
        file -= up;
    if (file >= 0 && file <= 7 && rank >= 0 && rank <= 7)
        st.cursor = (s8)SQ(file, rank);
}

// A on a target of the picked piece: a chess move (or the chooser, for a
// promotion), or a checkers landing.
static bool choose_target(int sq) {
    Match* m = st.m;
    if (is_checkers()) {
        st.landings[st.landed++] = sq_to_ck(sq);
        CkMove mv = ck_find(&m->pos.checkers, sq_to_ck(st.picked), st.landings, st.landed);
        if (mv != CK_MOVE_NONE && ck_move_steps(mv) == st.landed) {
            play_move(mv, st.landed - 1);
            return true;
        }
        // More jumps to come: the piece waits on its landing.
        view_set_lift(st.picked, sq);
        find_targets();
        sound_pick();
        return false;
    }
    if (chess_is_promotion(&m->pos.chess, st.picked, sq)) {
        st.ui = UI_PROMOTE;
        st.promote_to = (s8)sq;
        st.choice = 0;
        sound_pick();
        return false;
    }
    play_move(chess_find(&m->pos.chess, st.picked, sq, QUEEN), 0);
    return true;
}

// Returns true if a move was played (a heavy frame).
static bool board_buttons(void) {
    move_cursor();
    if (!humans_turn() || view_animating())
        return false;
    if (button_pressed(BUTTON_B)) {
        if (st.landed) { // a jump half landed: back to its start
            st.landed = 0;
            view_set_lift(-1, -1);
            find_targets();
        } else {
            drop();
        }
        return false;
    }
    if (!button_pressed(BUTTON_A))
        return false;
    int sq = st.cursor;
    if (st.picked >= 0 && is_target(sq))
        return choose_target(sq);
    if (st.landed) { // only the landings go on with a jump
        sound_no();
    } else if (sq == st.picked) {
        drop();
    } else if (own_piece(sq)) {
        pick(sq);
    } else if (st.picked >= 0) {
        sound_no();
    }
    return false;
}

static const u8 promote_kinds[4] = {QUEEN, ROOK, BISHOP, KNIGHT};

static bool promote_buttons(void) {
    // LEFT and RIGHT as seen: the chooser runs either way along the rank.
    int x = view_square_x(view_chooser_square(st.choice));
    for (int d = -1; d <= 1; d += 2) {
        if (!button_repeat(d < 0 ? BUTTON_LEFT : BUTTON_RIGHT))
            continue;
        for (int c = 0; c < 4; c++) {
            int cx = view_square_x(view_chooser_square(c));
            if ((c == st.choice + 1 || c == st.choice - 1) && (cx - x) * d > 0) {
                st.choice = (u8)c;
                sound_pick();
                break;
            }
        }
    }
    if (button_pressed(BUTTON_B)) { // the move cancelled; the pawn still picked
        st.ui = UI_BOARD;
        st.cursor = st.promote_to;
        return false;
    }
    if (button_pressed(BUTTON_A)) {
        Match* m = st.m;
        st.cursor = st.promote_to;
        play_move(chess_find(&m->pos.chess, st.picked, st.promote_to, promote_kinds[st.choice]), 0);
        return true;
    }
    return false;
}

// --- The computer -------------------------------------------------------------------------

static void stop_thinking(void) {
    if (st.thinking)
        search_abort();
    st.thinking = st.pending = false;
}

// Starts the search when it is the computer's turn, steps it, and plays its
// move once found and the board is still. Returns true if it did something
// heavy.

static bool computer(bool busy, u32 allowance) {
    Match* m = st.m;
    if (st.settle) { // the screen's first frames redraw everything
        st.settle--;
        return false;
    }
    if (!st.thinking && !st.pending && !busy && match_computer_to_move(m)) {
        search_start(match_search_game(m), match_position(m), match_level(m), random_u32());
        st.thinking = true;
        return true;
    }
    if (st.thinking && !busy) {
        // Half the work while a move is shown moving: those frames draw
        // more.
        u32 before = search_work();
        bool done = search_step(view_animating() ? (allowance + 1) / 2 : allowance);
        st.stepped = search_work() - before;
        if (!done)
            return false;
        st.thinking = false;
        st.pending = true;
        st.pending_move = search_best();
        return false; // the move is played next frame, not on top of a step
    }
    if (st.pending && !busy && !view_animating() && st.ui != UI_MENU) {
        st.pending = false;
        play_move(st.pending_move, 0);
        return true;
    }
    return false;
}

// --- The menu -----------------------------------------------------------------------------

static void draw_menu(void) {
    clear_board_text();
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered_in(BOARD_COL, BOARD_COLS, MENU_ROW, "PAUSED");
    for (int i = 0; i < ITEM_COUNT; i++) {
        bool off = i == ITEM_UNDO && !match_can_undo(st.m);
        text_set_style(off ? STYLE_DIM : i == st.item ? TEXT_HIGHLIGHT : TEXT_NORMAL);
        const char* s = item_names[i];
        text_print_centered_in(BOARD_COL, BOARD_COLS, MENU_ROW + 2 + 2 * i,
                               i == st.item ? text_format("> %s <", s) : s);
    }
    text_set_style(TEXT_NORMAL);
}

static void open_menu(void) {
    st.ui = UI_MENU;
    st.item = ITEM_RESUME;
    button_repeat_reset();
    view_set_dim(true);
    draw_menu();
    sound_pick();
}

static void close_menu(void) {
    st.ui = UI_BOARD;
    view_set_dim(false);
    clear_board_text();
    button_repeat_reset();
}

static void set_view_side(void) {
    const Match* m = st.m;
    // Unflipped, White (chess) and Light (checkers) are at the bottom.
    bool human_on_top =
        m->game == GAME_CHECKERS ? m->human_side == CK_DARK : m->human_side == BLACK;
    view_set_flipped(m->mode == MODE_ONE_PLAYER && human_on_top);
}

// After an undo or a new game: nothing moving, nothing picked, the board
// as it now is.
static void reset_board(void) {
    view_animation_stop();
    drop();
    st.known = false;
    st.announced = false;
    know_position();
}

static bool menu_buttons(PlayExit* exit) {
    Match* m = st.m;
    if (button_pressed(BUTTON_B | BUTTON_START)) {
        close_menu();
        return true;
    }
    int step = button_repeat(BUTTON_DOWN) ? 1 : button_repeat(BUTTON_UP) ? -1 : 0;
    if (step) {
        st.item = (u8)((st.item + ITEM_COUNT + step) % ITEM_COUNT);
        draw_menu();
        sound_pick();
        return true;
    }
    if (!button_pressed(BUTTON_A))
        return false;
    switch (st.item) {
    case ITEM_RESUME:
        close_menu();
        break;
    case ITEM_UNDO:
        if (!match_can_undo(m)) { // nothing of the human's to take back
            sound_no();
            return false;
        }
        // The computer stops thinking first; then the human's last move goes
        // and the computer's reply if it came (one ply with two players).
        stop_thinking();
        match_undo(m);
        reset_board();
        st.save_pending = true;
        close_menu();
        sound_move();
        break;
    case ITEM_FLIP:
        view_set_flipped(!view_flipped());
        close_menu();
        break;
    case ITEM_NEW:
        stop_thinking();
        match_new(m, (GameKind)m->game, (MatchMode)m->mode, m->human_side, m->level);
        reset_board();
        st.save_pending = true; // a game not begun: the saved one is forgotten
        set_view_side();
        close_menu();
        break;
    case ITEM_QUIT:
        if (st.save_pending)
            save();
        stop_thinking();
        view_animation_stop();
        view_set_dim(false);
        clear_board_text();
        *exit = PLAY_QUIT_TO_TITLE;
        break;
    }
    return true;
}

// --- The result ---------------------------------------------------------------------------

// The game's result over the board, and the sound, once the board has
// stopped moving.
static void announce(void) {
    const Match* m = st.m;
    st.announced = true;
    st.ui = UI_OVER;
    drop();
    view_set_dim(true);
    clear_board_text();
    bool checkers = is_checkers();
    const char* what = "";
    bool decisive = false;
    switch (m->result) {
    case RESULT_CHECKMATE:
        what = "CHECKMATE", decisive = true;
        break;
    case RESULT_NO_MOVES:
        what = "NO MOVES LEFT", decisive = true;
        break;
    case RESULT_STALEMATE:
        what = "STALEMATE";
        break;
    case RESULT_REPETITION:
        what = "DRAW BY REPETITION";
        break;
    case RESULT_FIFTY:
        what = "FIFTY-MOVE DRAW";
        break;
    case RESULT_FORTY:
        what = "DRAW: 40 MOVES";
        break;
    case RESULT_MATERIAL:
        what = "DRAW: NOT ENOUGH MATERIAL";
        break;
    default:
        what = "DRAW: GAME TOO LONG";
        break;
    }
    text_set_style(decisive ? TEXT_HIGHLIGHT : TEXT_NORMAL);
    int row = BANNER_ROW + print_over_board(BANNER_ROW, what) + 1;
    text_set_style(TEXT_NORMAL);
    // The side to move has lost a decisive game.
    int loser = match_side_to_move(m);
    static const char* const names[2][2] = {{"WHITE", "BLACK"}, {"DARK", "LIGHT"}};
    if (decisive) {
        const char* winner = names[checkers][loser == 0];
        if (m->mode == MODE_ONE_PLAYER) {
            bool won = loser != m->human_side;
            text_print_centered_in(BOARD_COL, BOARD_COLS, row, won ? "YOU WIN!" : "YOU LOSE");
            if (won)
                sound_win();
            else
                sound_lose();
        } else {
            text_print_centered_in(BOARD_COL, BOARD_COLS, row, text_format("%s WINS", winner));
            sound_win();
        }
    } else {
        text_print_centered_in(BOARD_COL, BOARD_COLS, row, "A DRAW");
        sound_draw();
    }
    text_set_style(STYLE_DIM);
    text_print_centered_in(BOARD_COL, BOARD_COLS, row + 3, "PRESS A");
    text_set_style(TEXT_NORMAL);
}

// The panel's status line: the result, THINKING, CHECK, a forced jump.
static const char* status_line(void) {
    const Match* m = st.m;
    static const char* const names[2][2] = {{"WHITE", "BLACK"}, {"DARK", "LIGHT"}};
    if (m->result != RESULT_NONE) {
        if (!st.announced)
            return "";
        if (m->result != RESULT_CHECKMATE && m->result != RESULT_NO_MOVES)
            return "DRAW";
        int loser = match_side_to_move(m);
        if (m->mode == MODE_ONE_PLAYER)
            return loser != m->human_side ? "YOU WIN" : "YOU LOSE";
        return text_format("%s WINS", names[is_checkers()][loser == 0]);
    }
    if (st.thinking || st.pending) {
        static const char* const dots[3] = {"THINKING o . .", "THINKING . o .", "THINKING . . o"};
        u32 t = frame_count() / THINK_DOT_FRAMES;
        return dots[t % 3];
    }
    if (st.check_sq >= 0)
        return "CHECK";
    if (st.jump_forced)
        return "MUST JUMP";
    return m->mode == MODE_ONE_PLAYER ? "YOUR MOVE" : "";
}

// --- The CPU readout ----------------------------------------------------------------------

static void readout(void) {
    if (button_pressed(BUTTON_L)) {
        st.readout = !st.readout;
        st.readout_peak = st.readout_frames = 0;
        st.readout_text[0] = '\0';
    }
    if (!st.readout) {
        view_footer(NULL);
        return;
    }
    u32 p = frame_cpu_permille();
    if (p > st.readout_peak)
        st.readout_peak = p;
    if (++st.readout_frames >= READOUT_FRAMES) {
        const char* s = text_format("CPU %u.%u%%", st.readout_peak / 10, st.readout_peak % 10);
        int i = 0;
        for (; s[i] && i < 11; i++)
            st.readout_text[i] = s[i];
        st.readout_text[i] = '\0';
        st.readout_peak = st.readout_frames = 0;
    }
    view_footer(st.readout_text);
}

// --- The frame ----------------------------------------------------------------------------

void play_begin(Match* m) {
    st.m = m;
    search_pace_init(&st.pace);
    st.stepped = 0;
    st.ui = UI_BOARD;
    st.thinking = st.pending = st.save_pending = false;
    st.settle = 2;
    set_view_side();
    view_set_chooser(-1, 0);
    view_set_marks(NULL, 0);
    view_set_dim(false);
    reset_board();
    clear_board_text();
    // The cursor starts in front of the human's pieces.
    bool top = m->mode == MODE_ONE_PLAYER && view_flipped();
    if (is_checkers())
        st.cursor = (s8)(top ? SQ(3, 5) : SQ(2, 2)); // d6 or c3, before the front row
    else
        st.cursor = (s8)(top ? SQ(4, 6) : SQ(4, 1));
    button_repeat_reset();
}

PlayExit play_frame(void) {
    Match* m = st.m;
    PlayExit exit = PLAY_CONTINUE;
    bool busy = false;
    readout();
    if (button_pressed(BUTTON_SELECT) && st.ui != UI_MENU)
        view_set_flipped(!view_flipped());

    know_position();
    switch (st.ui) {
    case UI_MENU:
        busy = menu_buttons(&exit);
        break;
    case UI_OVER:
        if (button_pressed(BUTTON_A | BUTTON_START) && !st.save_pending) {
            view_set_dim(false);
            clear_board_text();
            exit = PLAY_QUIT_TO_TITLE;
        }
        break;
    case UI_PROMOTE:
        busy = promote_buttons();
        break;
    default:
        if (button_pressed(BUTTON_START)) {
            open_menu();
            busy = true;
        } else {
            busy = board_buttons();
        }
        break;
    }
    if (exit != PLAY_CONTINUE)
        return exit;
    if (st.save_pending && !busy && !view_animating()) {
        save();
        busy = true;
    }
    // The work this frame may do thinking, from the last frame's (search.h).
#ifdef SERVAL_WEB
    u32 allowance = match_search_game(m)->frame_work;
#else
    u32 allowance = search_pace(&st.pace, frame_cpu_cycles(), st.stepped, frame_budget_cycles());
#endif
    st.stepped = 0;
    if (st.ui != UI_OVER)
        busy |= computer(busy, allowance);
    know_position();

    // The view: highlights, targets, marks, the chooser, the cursor.
    int from, to;
    last_move_squares(&from, &to);
    // The picked piece's square is tinted; a jump half landed, its landing.
    int picked_sq = st.landed ? ck_to_sq(st.landings[st.landed - 1]) : st.picked;
    view_set_highlights(from, to, st.check_sq, picked_sq);
    bool board_ui = st.ui == UI_BOARD && humans_turn();
    view_set_targets(st.targets, st.rings, board_ui ? st.target_count : 0);
    view_set_marks(st.marks, board_ui && st.jump_forced && st.picked < 0 ? st.mark_count : 0);
    bool promote = st.ui == UI_PROMOTE;
    view_set_chooser(promote ? st.promote_to : -1, promote ? pos_side(&m->pos.chess) : WHITE);
    if (promote)
        view_draw_cursor(view_chooser_square(st.choice));
    else if (st.ui == UI_BOARD)
        view_draw_cursor(st.cursor);
    sound_events(view_draw_match(m));
    // A game that ended while the menu was open (START during the last
    // move's slide) is announced once the menu closes.
    if (m->result != RESULT_NONE && !st.announced && !view_animating() && st.ui != UI_MENU)
        announce();
    view_panel(m, status_line());
    return PLAY_CONTINUE;
}
