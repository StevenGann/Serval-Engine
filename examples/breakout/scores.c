// The high-score table: the best five scores and the level each reached,
// kept in save data (save.h). A simpler cousin of asteroids' table (no
// initials): a game over that makes the table is saved at once.

#include "game.h"

// The whole table is one small struct in one save slot: fixed-size fields
// only (a pointer would mean nothing after a power-off).
typedef struct {
    u32 score;
    u8 level;     // the level reached, from 1
    u8 unused[3]; // keeps entries 8 bytes apart; saved as 0
} ScoreEntry;

typedef struct {
    ScoreEntry entries[SCORES_COUNT]; // best first
} ScoreTable;

// SCORES_VERSION is this game's number for the layout of ScoreTable: raise it
// when the struct changes, and save_read() reports an old save as
// SAVE_OTHER_VERSION instead of reading garbage (asteroids/scores.c explains
// converting one).
#define SCORES_SLOT 0
#define SCORES_VERSION 1

static ScoreTable table;

// Modest made-up scores, so a first game can make the table.
static const ScoreTable default_table = {{
    {5000, 3, {0, 0, 0}},
    {4000, 2, {0, 0, 0}},
    {3000, 2, {0, 0, 0}},
    {2000, 1, {0, 0, 0}},
    {1000, 1, {0, 0, 0}},
}};

static bool save_failed;

void scores_load(void) {
    if (save_read(SCORES_SLOT, &table, sizeof table, SCORES_VERSION) != SAVE_OK)
        table = default_table; // first boot, erased, damaged, or another layout
}

void scores_reset(void) {
    save_erase(SCORES_SLOT);
    table = default_table;
    save_failed = false;
}

int scores_add(int score, int level) {
    int rank = -1;
    for (int i = 0; i < SCORES_COUNT; i++) {
        if ((u32)score > table.entries[i].score) {
            rank = i; // a tie goes below the score that was there first
            break;
        }
    }
    if (rank < 0)
        return -1;
    for (int i = SCORES_COUNT - 1; i > rank; i--)
        table.entries[i] = table.entries[i - 1];
    table.entries[rank] = (ScoreEntry){(u32)score, (u8)int_min(level, 99), {0, 0, 0}};
    // Saved once, at the game over: a save takes a few milliseconds.
    save_failed = !save_write(SCORES_SLOT, &table, sizeof table, SCORES_VERSION);
    return rank;
}

int scores_best(void) {
    return (int)table.entries[0].score;
}

void scores_draw(int first_row, int highlight) {
    text_print_centered(first_row, "BEST SCORES");
    for (int i = 0; i < SCORES_COUNT; i++) {
        const ScoreEntry* e = &table.entries[i];
        // The new score stands out in the highlight style (yellow).
        text_set_style(i == highlight ? TEXT_HIGHLIGHT : TEXT_NORMAL);
        text_print_centered(first_row + 2 + i,
                            text_format("%d.  %06u  LEVEL %-2d", i + 1, e->score, e->level));
    }
    text_set_style(TEXT_NORMAL);
    if (save_failed)
        text_print_centered(first_row + 3 + SCORES_COUNT, "(COULD NOT SAVE)");
}
