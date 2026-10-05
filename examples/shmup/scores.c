// The high-score table and the initials entry screen (see scores.h), built
// like asteroids' scores.c: one small struct in one save slot, tagged with a
// version number to raise whenever the struct changes.

#include "scores.h"

typedef struct {
    u32 score;
    char initials[3]; // letters, '.' or ' '; not NUL-terminated
    u8 cleared;       // 1 if this run beat the boss (shown as a star)
} ScoreEntry;

typedef struct {
    ScoreEntry entries[SCORES_COUNT]; // best first
} ScoreTable;

// Slot 0 of this game's save memory (every game has its own), version 1:
// save_read() reports a save with another layout as SAVE_OTHER_VERSION, and
// the game then starts from the default table.
#define SCORES_SLOT 0
#define SCORES_VERSION 1

static ScoreTable table;
static bool save_failed;

static const ScoreTable default_table = {{
    {30000, {'K', 'I', 'T'}, 0},
    {20000, {'M', 'E', 'W'}, 0},
    {15000, {'S', 'K', 'Y'}, 0},
    {10000, {'O', 'R', 'B'}, 0},
    {5000, {'Z', 'O', 'E'}, 0},
}};

void scores_load(void) {
    if (save_read(SCORES_SLOT, &table, sizeof table, SCORES_VERSION) != SAVE_OK)
        table = default_table; // first boot, an erased or damaged save, or another layout
}

int scores_best(void) {
    return (int)table.entries[0].score;
}

int scores_rank(int score) {
    if (score <= 0)
        return -1;
    for (int i = 0; i < SCORES_COUNT; i++) {
        if ((u32)score > table.entries[i].score)
            return i;
    }
    return -1;
}

void scores_draw(int highlight) {
    text_print_centered(4, "HIGH SCORES");
    for (int i = 0; i < SCORES_COUNT; i++) {
        const ScoreEntry* e = &table.entries[i];
        // The newest entry stands out in yellow; %.3s prints the initials,
        // which have no terminating zero.
        text_set_style(i == highlight ? TEXT_HIGHLIGHT : TEXT_NORMAL);
        text_print_centered(7 + 2 * i, text_format("%d  %.3s  %7u %c", i + 1, e->initials, e->score,
                                                   e->cleared ? '*' : ' '));
    }
    text_set_style(TEXT_NORMAL);
    text_print_centered(18, "* BEAT THE HIVE LANTERN");
    if (save_failed)
        text_print_centered(17, "(COULD NOT SAVE)");
}

// --- Entering initials -------------------------------------------------------

static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ. ";
#define LETTER_COUNT ((int)sizeof letters - 1)

static int entry_score, entry_rank;
static bool entry_cleared;
static int cursor;
static int choice[3];

static void draw_entry(void) {
    char shown[3];
    for (int k = 0; k < 3; k++) {
        char c = letters[choice[k]];
        shown[k] = c == ' ' ? '_' : c;
    }
    text_print_centered(10, text_format("%c  %c  %c", shown[0], shown[1], shown[2]));
    text_print_centered(11, text_format("%c  %c  %c", cursor == 0 ? '^' : ' ',
                                        cursor == 1 ? '^' : ' ', cursor == 2 ? '^' : ' '));
}

void entry_begin(int score, bool cleared) {
    entry_score = score;
    entry_cleared = cleared;
    entry_rank = scores_rank(score);
    cursor = 0;
    choice[0] = choice[1] = choice[2] = 0;
    text_clear();
    text_print_centered(3, "NEW HIGH SCORE");
    text_print_centered(5, text_format("%d   PLACE %d", score, entry_rank + 1));
    text_print_centered(8, "ENTER YOUR INITIALS");
    text_print_centered(14, "UP/DOWN:LETTER  A:NEXT");
    text_print_centered(16, "B:BACK  START:DONE");
    draw_entry();
}

EntryEvent entry_update(void) {
    EntryEvent event = ENTRY_NONE;
    // Held, UP and DOWN run through the letters (button_repeat).
    int step = button_repeat(BUTTON_UP) ? 1 : button_repeat(BUTTON_DOWN) ? -1 : 0;
    if (step) {
        choice[cursor] = (choice[cursor] + step + LETTER_COUNT) % LETTER_COUNT;
        event = ENTRY_LETTER;
    }
    bool done = button_pressed(BUTTON_START);
    if (button_pressed(BUTTON_A)) {
        if (cursor < 2) {
            cursor++;
            event = ENTRY_MOVE;
        } else {
            done = true;
        }
    }
    if (button_pressed(BUTTON_B) && cursor > 0) {
        cursor--;
        event = ENTRY_MOVE;
    }
    if (done) {
        for (int i = SCORES_COUNT - 1; i > entry_rank; i--)
            table.entries[i] = table.entries[i - 1];
        ScoreEntry* e = &table.entries[entry_rank];
        e->score = (u32)entry_score;
        for (int k = 0; k < 3; k++)
            e->initials[k] = letters[choice[k]];
        e->cleared = entry_cleared ? 1 : 0;
        // Saved once, at a pause in the game (a few milliseconds on the GBA).
        save_failed = !save_write(SCORES_SLOT, &table, sizeof table, SCORES_VERSION);
        return ENTRY_DONE;
    }
    if (event != ENTRY_NONE)
        draw_entry();
    return event;
}
