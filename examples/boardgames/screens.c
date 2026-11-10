// The title and the setup: see screens.h.
//
// Both draw over the dimmed, empty board (view.c; the text would be lost
// among pieces), their text on the board's columns, and each game's record
// in the side panel, where play shows its panel. The title shows the logo
// (four 64x32 sprites) and two pieces of the game the cursor is on, its
// board numbered for checkers; the setup's board is seen from the side
// chosen (its labels turn). After ATTRACT_IDLE frames
// with no button held the title gives the board to the attract mode: the
// computer plays itself at CUB, chess and checkers by turns, undimmed and in
// silence, until the game ends (or ATTRACT_PLIES), and any button brings
// the menu back at once. It never writes the save: no record, no game in
// progress.
//
// Code in ROM and state in EWRAM: nothing here is in a hurry.

#include "screens.h"

#include "art.h"
#include "records.h"
#include "sound.h"
#include "view.h"

// Text over the board: columns BOARD_COL to BOARD_COL + BOARD_COLS - 1.
#define BOARD_COL 1
#define BOARD_COLS 18

#define LOGO_X BOARD_X
#define LOGO_Y 12
#define MENU_ROW 10 // the first item's row, under the logo
#define HINT_ROW 17 // "A: CHOOSE", or a message
#define MESSAGE_FRAMES 150
#define DECOR_ROW 7 // the pieces either side of the hint: the board's bottom row

#define ATTRACT_IDLE 1200 // 20 seconds with no button held
#define ATTRACT_PLIES 160 // a demo game stops here if it has not ended
#define ATTRACT_PAUSE 24  // frames between a move landing and the next search
#define ATTRACT_HOLD 240  // the result stays this long
#define ATTRACT_BLINK 128 // PRESS ANY BUTTON and DEMO GAME by turns

// The setup's rows, and the panel's.
enum { ROW_PLAYERS, ROW_SIDE, ROW_LEVEL, SETUP_ROWS };
enum { ITEM_CONTINUE, ITEM_CHESS, ITEM_CHECKERS, TITLE_ITEMS };
enum { SIDE_FIRST, SIDE_SECOND, SIDE_RANDOM, SIDES };

static const char* const game_names[2] = {"CHESS", "CHECKERS"};
static const char* const level_names[LEVEL_COUNT] = {"CUB", "HUNTER", "SERVAL"};
// The human's side as chosen: the side that moves first, the other, random.
static const char* const side_names[2][SIDES] = {{"WHITE", "BLACK", "RANDOM"},
                                                 {"DARK", "LIGHT", "RANDOM"}};

static struct {
    u8 shown; // the screen drawn now, plus one; 0 when play has the screen
    u8 item;  // the title's
    u8 game;  // the game chosen (GameKind)
    u8 row;   // the setup's
    u8 players, side, level;
    u8 panel_game; // the game whose record the panel shows, 0xFF none
    u16 idle;
    u16 message;          // frames left of the hint row's message
    bool asking;          // the erase prompt
    bool came_from_setup; // B on the setup: the title's cursor stays
    // The attract mode.
    bool attract;
    u8 attract_next; // the game the next demo plays
    bool thinking, pending;
    u32 pending_move;
    u16 pause, hold;
    SearchPace pace;
    u32 stepped;
} sc SERVAL_EWRAM_BSS;

// --- Shared ---------------------------------------------------------------------------

static void clear_board_text(void) {
    text_clear_area(0, 0, BOARD_COL + BOARD_COLS, TEXT_ROWS);
}

static void centered(int style, int row, const char* s) {
    text_set_style(style);
    text_print_centered_in(BOARD_COL, BOARD_COLS, row, s);
    text_set_style(TEXT_NORMAL);
}

// The board shows nothing but m's pieces: no tints, targets or marks.
static void plain_board(void) {
    view_set_pieces_shown(true);
    view_animation_stop();
    view_set_highlights(-1, -1, -1, -1);
    view_set_targets(NULL, NULL, 0);
    view_set_marks(NULL, 0);
    view_set_chooser(-1, WHITE);
    view_set_lift(-1, -1);
    view_footer(NULL);
}

// The human's side on the board: chess's WHITE/BLACK or checkers' CK_DARK/
// CK_LIGHT, from the setup's choice (SIDE_FIRST: the side that moves first).
static int side_of(GameKind game, int choice) {
    if (game == GAME_CHECKERS)
        return choice == SIDE_SECOND ? CK_LIGHT : CK_DARK;
    return choice == SIDE_SECOND ? BLACK : WHITE;
}

// A game's record in the panel: wins, draws and losses at each strength,
// and the two-player games. Redrawn only when the game shown changes.
static void records_panel(GameKind game, bool force) {
    if (!force && sc.panel_game == game)
        return;
    sc.panel_game = (u8)game;
    view_panel_clear();
    const GameRecord* rec = board_records_get(game);
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered_in(PANEL_COL, PANEL_COLS, 1, game_names[game]);
    text_set_style(STYLE_DIM);
    text_print(PANEL_COL, 2, "   W  D  L");
    for (int lv = 0; lv < LEVEL_COUNT; lv++) {
        int row = 4 + 3 * lv;
        u32 w = rec->wins[lv] > 999 ? 999 : rec->wins[lv];
        u32 d = rec->draws[lv] > 999 ? 999 : rec->draws[lv];
        u32 l = rec->losses[lv] > 999 ? 999 : rec->losses[lv];
        text_set_style(TEXT_NORMAL);
        text_print(PANEL_COL + 1, row, level_names[lv]);
        text_print(PANEL_COL, row + 1, text_format(" %3u%3u%3u", w, d, l));
    }
    text_set_style(STYLE_DIM);
    text_print(PANEL_COL + 1, 13, "2 PLAYERS");
    text_set_style(TEXT_NORMAL);
    u32 games = rec->two_player_games;
    text_print(PANEL_COL + 1, 14, text_format("%u GAME%s", games, games == 1 ? "" : "S"));
    if (board_records_save_failed()) {
        text_set_style(STYLE_ALERT);
        text_print_centered_in(PANEL_COL, PANEL_COLS, 17, "SAVE");
        text_print_centered_in(PANEL_COL, PANEL_COLS, 18, "FAILED");
    }
    text_set_style(TEXT_NORMAL);
}

// Leaving for play: the board undimmed and the text cleared, play's panel
// to be drawn afresh.
static Screen to_play(void) {
    sc.shown = 0;
    view_set_pieces_shown(true);
    view_set_dim(false);
    clear_board_text();
    view_panel_clear();
    sc.panel_game = 0xFF;
    return SCREEN_PLAY;
}

// --- The title --------------------------------------------------------------------------

static int first_item(void) {
    return board_records_has_game() ? ITEM_CONTINUE : ITEM_CHESS;
}

// The game the title's cursor is on (CONTINUE: the saved game's).
static GameKind title_game(void) {
    if (sc.item == ITEM_CONTINUE)
        return board_records_saved_kind();
    return sc.item == ITEM_CHECKERS ? GAME_CHECKERS : GAME_CHESS;
}

// The rows of the menu: with CONTINUE (and the saved game's name under
// it) from MENU_ROW, without it one row lower.
static int item_row(int item) {
    if (board_records_has_game())
        return item == ITEM_CONTINUE ? MENU_ROW : MENU_ROW + 1 + 2 * item;
    return MENU_ROW + 1 + 2 * (item - 1);
}

static void draw_hint(void) {
    text_clear_area(BOARD_COL, HINT_ROW, BOARD_COLS, 1);
    if (!sc.message)
        centered(STYLE_DIM, HINT_ROW, "A: CHOOSE");
}

static void message(int style, const char* s) {
    sc.message = MESSAGE_FRAMES;
    text_clear_area(BOARD_COL, HINT_ROW, BOARD_COLS, 1);
    centered(style, HINT_ROW, s);
}

static void draw_menu(void) {
    text_clear_area(BOARD_COL, MENU_ROW, BOARD_COLS, HINT_ROW - MENU_ROW);
    static const char* const names[TITLE_ITEMS] = {"CONTINUE", "CHESS", "CHECKERS"};
    for (int i = first_item(); i < TITLE_ITEMS; i++) {
        bool on = i == sc.item;
        centered(on ? TEXT_HIGHLIGHT : TEXT_NORMAL, item_row(i),
                 on ? text_format("> %s <", names[i]) : names[i]);
    }
    if (board_records_has_game())
        centered(STYLE_DIM, MENU_ROW + 1,
                 text_format("%s GAME", game_names[board_records_saved_kind()]));
    if (!sc.message)
        draw_hint();
}

// The board behind the menu: the start of the game the cursor is on.
static void title_board(Match* m) {
    GameKind game = title_game();
    if (m->game != game || m->count != 0)
        match_new(m, game, MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    records_panel(game, false);
}

// keep_item: the cursor stays where it was (after a demo); else it starts on
// CONTINUE if a game is saved, or CHESS.
static void title_enter(Match* m, bool keep_item) {
    sc.shown = SCREEN_TITLE + 1;
    sc.asking = false;
    sc.attract = false;
    sc.idle = 0;
    sc.message = 0;
    if (!keep_item || sc.item < first_item() || sc.item >= TITLE_ITEMS)
        sc.item = (u8)first_item();
    view_set_flipped(false);
    plain_board();
    view_set_pieces_shown(false);
    match_new(m, title_game(), MODE_TWO_PLAYERS, WHITE, LEVEL_CUB);
    view_set_dim(true);
    clear_board_text();
    records_panel(title_game(), true);
    draw_menu();
    button_repeat_reset();
}

static void ask_erase(void) {
    sc.asking = true;
    text_clear_area(BOARD_COL, MENU_ROW, BOARD_COLS, HINT_ROW - MENU_ROW + 1);
    centered(STYLE_ALERT, MENU_ROW + 1, "ERASE RECORDS");
    centered(STYLE_ALERT, MENU_ROW + 2, "AND SAVED GAME?");
    centered(TEXT_NORMAL, MENU_ROW + 4, "A: YES   B: NO");
    sc.message = 0;
    sound_check();
}

static void answer_erase(Match* m, bool yes) {
    sc.asking = false;
    if (yes) {
        board_records_erase();
        sc.item = (u8)first_item();
        title_board(m);
        records_panel(title_game(), true);
    }
    draw_menu();
    if (!yes) {
        sound_pick();
    } else if (board_records_save_failed()) {
        message(STYLE_ALERT, "SAVE FAILED");
        sound_no();
    } else {
        message(TEXT_HIGHLIGHT, "RECORDS ERASED");
        sound_capture();
    }
}

// The logo, and two pieces of the game the cursor is on facing each other
// across the hint (the board's own pieces are hidden on the title and the
// setup: the text is over them).
static void draw_logo(void) {
    for (int f = 0; f < LOGO_FRAMES; f++)
        sprite_draw(SPR_LOGO, (u8)f, LOGO_X + (f & 1) * (LOGO_W / 2),
                    LOGO_Y + (f >> 1) * (LOGO_H / 2), SPRITE_PALETTE(PAL_LOGO));
    if (sc.message) // a message is wider than the hint
        return;
    bool checkers = title_game() == GAME_CHECKERS;
    int frame = checkers ? FRAME_CROWNED : FRAME_KNIGHT;
    int left = checkers ? PAL_EBONY : PAL_IVORY;
    int y = BOARD_Y + DECOR_ROW * SQUARE;
    sprite_draw(SPR_PIECE, (u8)frame, BOARD_X, y, SPRITE_PALETTE(left));
    sprite_draw(SPR_PIECE, (u8)frame, BOARD_X + 7 * SQUARE, y,
                SPRITE_PALETTE(left == PAL_IVORY ? PAL_EBONY : PAL_IVORY) | SPRITE_FLIP_H);
}

// --- The attract mode ---------------------------------------------------------------------

static void attract_start(Match* m) {
    sc.attract = true;
    sc.thinking = sc.pending = false;
    sc.pause = ATTRACT_PAUSE;
    sc.hold = 0;
    sc.stepped = 0;
    search_pace_init(&sc.pace);
    random_seed(random_entropy());
    GameKind game = sc.attract_next ? GAME_CHECKERS : GAME_CHESS;
    sc.attract_next ^= 1;
    view_set_flipped(false);
    plain_board();
    match_new(m, game, MODE_ONE_PLAYER, game == GAME_CHECKERS ? CK_LIGHT : WHITE, LEVEL_CUB);
    view_set_dim(false);
    clear_board_text();
    view_panel_clear();
    sc.panel_game = 0xFF;
}

static void attract_stop(Match* m) {
    if (search_running())
        search_abort();
    sc.thinking = sc.pending = false;
    title_enter(m, true); // the board stops, the menu comes back
}

// The panel's status line in a demo.
static const char* attract_status(const Match* m) {
    if (m->result != RESULT_NONE && !view_animating()) {
        if (m->result == RESULT_CHECKMATE)
            return "CHECKMATE";
        if (m->result == RESULT_NO_MOVES)
            return "NO MOVES LEFT";
        if (m->result == RESULT_STALEMATE)
            return "STALEMATE";
        return "A DRAW";
    }
    return (frame_count() / ATTRACT_BLINK) & 1 ? "DEMO GAME" : "PRESS ANY BUTTON";
}

// The last move's squares and the king in check's, as play shows them.
static void attract_highlights(const Match* m) {
    int from = -1, to = -1, alert = -1;
    if (m->count) {
        u32 mv = m->moves[m->count - 1];
        if (m->game == GAME_CHECKERS) {
            from = SQ(ck_square_file(CK_MOVE_FROM(mv)), ck_square_rank(CK_MOVE_FROM(mv)));
            to = SQ(ck_square_file(ck_move_to(mv)), ck_square_rank(ck_move_to(mv)));
        } else {
            from = MOVE_FROM((Move)mv);
            to = MOVE_TO((Move)mv);
            if (!view_animating() && pos_in_check(&m->pos.chess)) {
                Piece king = (Piece)(pos_side(&m->pos.chess) | KING);
                for (int sq = 0; sq < 64; sq++)
                    if (pos_piece(&m->pos.chess, sq) == king)
                        alert = sq;
            }
        }
    }
    view_set_highlights(from, to, alert, -1);
}

// One frame of the demo: the computer plays both sides, thinking as play's
// computer does (a frame that starts a search or plays a move does not
// also think). Returns false when the demo is over.
static bool attract_frame(Match* m) {
#ifdef SERVAL_WEB
    u32 allowance = match_search_game(m)->frame_work;
#else
    u32 allowance = search_pace(&sc.pace, frame_cpu_cycles(), sc.stepped, frame_budget_cycles());
#endif
    sc.stepped = 0;
    // A move is played only after the search that found it, so a game over
    // (or long enough) has no search running.
    if (m->result != RESULT_NONE || m->count >= ATTRACT_PLIES) {
        if (!view_animating() && ++sc.hold >= ATTRACT_HOLD)
            return false;
    } else if (sc.pause) {
        if (!view_animating())
            sc.pause--;
    } else if (!sc.thinking && !sc.pending) {
        search_start(match_search_game(m), match_position(m), match_level(m), random_u32());
        sc.thinking = true;
    } else if (sc.thinking) {
        u32 before = search_work();
        bool done = search_step(allowance);
        sc.stepped = search_work() - before;
        if (done) {
            sc.thinking = false;
            sc.pending = true;
            sc.pending_move = search_best();
        }
    } else if (!view_animating()) {
        sc.pending = false;
        view_animate(m, sc.pending_move);
        if (!match_play(m, sc.pending_move))
            view_animation_stop(); // cannot happen: the move comes from the rules
        sc.pause = ATTRACT_PAUSE;
    }
    attract_highlights(m);
    view_draw_match(m);
    view_panel(m, attract_status(m));
    return true;
}

// --- The title's frame ------------------------------------------------------------------

Screen screens_title_frame(Match* m) {
    if (sc.shown != SCREEN_TITLE + 1)
        title_enter(m, sc.came_from_setup);
    sc.came_from_setup = false;
    if (sc.attract) {
        if (!button_pressed(BUTTON_ANY) && attract_frame(m))
            return SCREEN_TITLE;
        // Any button (or the demo's end): the menu at once, and that press
        // does nothing else.
        attract_stop(m);
        draw_logo();
        view_draw_match(m);
        return SCREEN_TITLE;
    }
    Screen next = SCREEN_TITLE;
    bool chord = button_down(BUTTON_L) && button_down(BUTTON_R) && button_down(BUTTON_SELECT);
    if (sc.asking) {
        if (button_pressed(BUTTON_A))
            answer_erase(m, true);
        else if (button_pressed(BUTTON_B))
            answer_erase(m, false);
    } else if (chord && button_pressed(BUTTON_L | BUTTON_R | BUTTON_SELECT)) {
        ask_erase();
    } else {
        int step = button_repeat(BUTTON_DOWN) ? 1 : button_repeat(BUTTON_UP) ? -1 : 0;
        if (step && sc.item + step >= first_item() && sc.item + step < TITLE_ITEMS) {
            sc.item = (u8)(sc.item + step);
            draw_menu();
            title_board(m);
            sound_pick();
        }
        if (button_pressed(BUTTON_A | BUTTON_START)) {
            random_seed(random_entropy()); // the computer's choices differ from game to game
            if (sc.item == ITEM_CONTINUE) {
                if (board_records_restore_game(m)) {
                    sound_move();
                    return to_play();
                }
                // The save could not be replayed: it is forgotten, and
                // CONTINUE goes with it.
                sc.item = ITEM_CHESS;
                title_board(m);
                draw_menu();
                message(STYLE_ALERT, "SAVED GAME LOST");
                sound_no();
            } else {
                sc.game = (u8)title_game();
                sound_move();
                next = SCREEN_SETUP;
            }
        }
    }
    if (sc.message && --sc.message == 0)
        draw_hint();
    // The attract mode, after a while with no button held (not while the
    // erase prompt waits).
    if (button_down(BUTTON_ANY) || sc.asking)
        sc.idle = 0;
    else if (++sc.idle >= ATTRACT_IDLE && next == SCREEN_TITLE)
        attract_start(m);
    if (!sc.attract)
        draw_logo();
    view_draw_match(m);
    if (next == SCREEN_SETUP)
        sc.shown = 0; // the setup draws itself
    return next;
}

// --- The setup ------------------------------------------------------------------------------

static bool one_player(void) {
    return sc.players == 0;
}

// The board (its pieces hidden): the game's, seen from the side chosen (a
// random side shows the first side's).
static void setup_board(Match* m) {
    GameKind game = (GameKind)sc.game;
    int side = side_of(game, sc.side);
    match_new(m, game, one_player() ? MODE_ONE_PLAYER : MODE_TWO_PLAYERS, side, sc.level);
    // As play shows it: unflipped, White (chess) and Light (checkers) are at
    // the bottom.
    bool on_top = game == GAME_CHECKERS ? side == CK_DARK : side == BLACK;
    view_set_flipped(one_player() && on_top);
}

static void setup_line(int row, int text_row, const char* label, const char* value) {
    bool on = sc.row == row;
    centered(STYLE_DIM, text_row, label);
    centered(on ? TEXT_HIGHLIGHT : TEXT_NORMAL, text_row + 1,
             on ? text_format("< %s >", value) : value);
}

static void draw_setup(void) {
    clear_board_text();
    centered(TEXT_HIGHLIGHT, 2, game_names[sc.game]);
    setup_line(ROW_PLAYERS, 4, "PLAYERS", one_player() ? "1 PLAYER" : "2 PLAYERS");
    if (one_player()) {
        setup_line(ROW_SIDE, 7, "YOUR SIDE", side_names[sc.game][sc.side]);
        if (sc.game == GAME_CHECKERS)
            centered(STYLE_DIM, 9, "DARK MOVES FIRST");
        setup_line(ROW_LEVEL, 11, "STRENGTH", level_names[sc.level]);
    }
    centered(STYLE_DIM, 15, "A: PLAY");
    centered(STYLE_DIM, 16, "B: BACK");
}

static void setup_enter(Match* m) {
    sc.shown = SCREEN_SETUP + 1;
    sc.row = ROW_PLAYERS;
    if (sc.players > 1)
        sc.players = 0;
    if (sc.side >= SIDES)
        sc.side = SIDE_FIRST;
    if (sc.level >= LEVEL_COUNT)
        sc.level = LEVEL_CUB;
    plain_board();
    view_set_pieces_shown(false);
    setup_board(m);
    view_set_dim(true);
    records_panel((GameKind)sc.game, false);
    draw_setup();
    button_repeat_reset();
}

Screen screens_setup_frame(Match* m) {
    if (sc.shown != SCREEN_SETUP + 1)
        setup_enter(m);
    if (button_pressed(BUTTON_B)) {
        sc.shown = 0;
        sc.came_from_setup = true; // back on the game it was
        sound_pick();
        view_draw_match(m);
        return SCREEN_TITLE;
    }
    if (button_pressed(BUTTON_A | BUTTON_START)) {
        GameKind game = (GameKind)sc.game;
        int choice = sc.side == SIDE_RANDOM ? random_range(SIDE_FIRST, SIDE_SECOND) : sc.side;
        match_new(m, game, one_player() ? MODE_ONE_PLAYER : MODE_TWO_PLAYERS,
                  one_player() ? side_of(game, choice) : side_of(game, SIDE_FIRST), sc.level);
        sound_move();
        return to_play();
    }
    int rows = one_player() ? SETUP_ROWS : 1;
    int step = button_repeat(BUTTON_DOWN) ? 1 : button_repeat(BUTTON_UP) ? -1 : 0;
    int turn = button_repeat(BUTTON_RIGHT) ? 1 : button_repeat(BUTTON_LEFT) ? -1 : 0;
    if (step && sc.row + step >= 0 && sc.row + step < rows) {
        sc.row = (u8)(sc.row + step);
        draw_setup();
        sound_pick();
    } else if (turn) {
        if (sc.row == ROW_PLAYERS)
            sc.players ^= 1;
        else if (sc.row == ROW_SIDE)
            sc.side = (u8)((sc.side + SIDES + turn) % SIDES);
        else
            sc.level = (u8)((sc.level + LEVEL_COUNT + turn) % LEVEL_COUNT);
        setup_board(m);
        draw_setup();
        sound_pick();
    }
    view_draw_match(m);
    return SCREEN_SETUP;
}
