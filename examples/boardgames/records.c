// The saved records and game in progress: see records.h. One versioned
// struct of fixed-size fields in one slot, read once at boot and written when
// a game is left, ended or a record changes.

#include "records.h"

#include <serval/save.h>

#define RECORDS_SLOT 0
#define RECORDS_VERSION 1

typedef struct SaveData {
    u8 has_game, kind, mode, human_side, level, reserved;
    u16 count;
    GameRecord record[2]; // by GameKind
    u8 moves[GAME_PLY_MAX];
} SaveData;

_Static_assert(sizeof(SaveData) <= SAVE_SLOT_MAX, "the records fit a save slot");

static SaveData data SERVAL_EWRAM_BSS; // read once, written now and then: not IWRAM
static bool save_failed;

static void save(void) {
    save_failed = !save_write(RECORDS_SLOT, &data, sizeof data, RECORDS_VERSION);
}

static void clear(void) {
    SaveData zero = {0};
    data = zero;
}

// Whether the saved game's settings mean a game this version can play.
static bool game_valid(void) {
    if (data.kind > GAME_CHECKERS || data.mode > MODE_TWO_PLAYERS || data.level >= LEVEL_COUNT ||
        data.count > GAME_PLY_MAX || data.reserved)
        return false;
    if (data.kind == GAME_CHESS)
        return data.human_side == WHITE || data.human_side == BLACK;
    return data.human_side == CK_DARK || data.human_side == CK_LIGHT;
}

void board_records_load(void) {
    if (save_read(RECORDS_SLOT, &data, sizeof data, RECORDS_VERSION) != SAVE_OK) {
        clear(); // none, damaged or of another layout: the first write replaces it
        return;
    }
    if (data.has_game > 1 || (data.has_game && !game_valid())) {
        data.has_game = 0; // a game this version cannot play; the records stay
        data.count = 0;
    }
}

const GameRecord* board_records_get(GameKind game) {
    return &data.record[game == GAME_CHECKERS];
}

bool board_records_has_game(void) {
    return data.has_game != 0;
}

GameKind board_records_saved_kind(void) {
    return data.kind == GAME_CHECKERS ? GAME_CHECKERS : GAME_CHESS;
}

void board_records_forget_game(void) {
    if (!data.has_game)
        return;
    data.has_game = 0;
    data.count = 0;
    save();
}

bool board_records_restore_game(Match* m) {
    if (!data.has_game)
        return false;
    if (!match_replay_indices(m, (GameKind)data.kind, (MatchMode)data.mode, data.human_side,
                              data.level, data.moves, data.count)) {
        board_records_forget_game();
        return false;
    }
    return true;
}

void board_records_save_game(const Match* m) {
    if (m->count == 0 || m->result != RESULT_NONE) {
        board_records_forget_game();
        return;
    }
    data.has_game = 1;
    data.kind = m->game;
    data.mode = m->mode;
    data.human_side = m->human_side;
    data.level = m->level;
    data.count = m->count;
    for (int i = 0; i < m->count; i++)
        data.moves[i] = m->move_index[i];
    save();
}

// Whether the side to move lost, for a result that has a loser.
static bool has_loser(GameResult r) {
    return r == RESULT_CHECKMATE || r == RESULT_NO_MOVES;
}

void board_records_end_game(const Match* m) {
    if (m->result == RESULT_NONE) {
        board_records_forget_game();
        return;
    }
    GameRecord* rec = &data.record[m->game == GAME_CHECKERS];
    if (m->mode == MODE_TWO_PLAYERS) {
        if (rec->two_player_games < 0xFFFF)
            rec->two_player_games++;
    } else {
        u16* count;
        if (!has_loser(m->result))
            count = &rec->draws[m->level];
        else if (match_side_to_move(m) == m->human_side) // the human is the one mated
            count = &rec->losses[m->level];
        else
            count = &rec->wins[m->level];
        if (*count < 0xFFFF)
            (*count)++;
    }
    data.has_game = 0;
    data.count = 0;
    save();
}

void board_records_erase(void) {
    clear();
    save();
}

bool board_records_save_failed(void) {
    return save_failed;
}
