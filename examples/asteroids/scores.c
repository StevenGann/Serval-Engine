// asteroids: the high-score table and the initials entry screen.

#include "scores.h"

// --- The saved table ---------------------------------------------------------

// The whole table is one small struct, saved in one slot. Only fixed-size
// fields: no pointers, which would mean nothing after a power-off.
typedef struct {
    u32 score;
    char initials[3]; // letters, '.' or ' '; not NUL-terminated
    u8 unused;        // keeps entries 4 bytes apart; saved as 0
} ScoreEntry;

typedef struct {
    ScoreEntry entries[SCORES_COUNT]; // best first
} ScoreTable;

// Versioning: SCORES_VERSION is a number this game picks for the layout of
// ScoreTable. Raise it whenever the struct changes (more entries, longer
// initials, a new field), and save_read() reports a save in the old layout as
// SAVE_OTHER_VERSION instead of reading it as garbage. A game that wants to
// keep old saves then reads the slot with its old struct and old version
// number (save_slot_version() says which) and converts. This is version 1,
// so there is nothing older to convert yet.
#define SCORES_SLOT 0
#define SCORES_VERSION 1

static ScoreTable table;
static bool save_failed; // the last save_write() didn't succeed

// Made-up players with modest scores, so a first game can make the table.
static const ScoreTable default_table = {{
    {5000, {'S', 'R', 'V'}, 0},
    {4000, {'C', 'A', 'T'}, 0},
    {3000, {'Z', 'A', 'P'}, 0},
    {2500, {'R', 'O', 'K'}, 0},
    {2000, {'P', 'E', 'W'}, 0},
    {1500, {'I', 'O', 'N'}, 0},
    {1000, {'V', 'E', 'X'}, 0},
    {800, {'J', 'E', 'T'}, 0},
    {600, {'D', 'O', 'T'}, 0},
    {400, {'M', 'A', 'X'}, 0},
}};

void scores_load(void) {
    switch (save_read(SCORES_SLOT, &table, sizeof table, SCORES_VERSION)) {
    case SAVE_OK:
        break;
    case SAVE_EMPTY:         // first boot, or the table was reset
    case SAVE_CORRUPT:       // damaged in storage (bad checksum): nothing to rescue
    case SAVE_OTHER_VERSION: // see SCORES_VERSION above
    default:
        // Start from the default table. The slot isn't written now: the
        // first new high score saves over it.
        table = default_table;
        break;
    }
}

void scores_reset(void) {
    save_erase(SCORES_SLOT);
    table = default_table;
    save_failed = false;
}

int scores_rank(int score) {
    if (score <= 0)
        return -1;
    for (int i = 0; i < SCORES_COUNT; i++) {
        if ((u32)score > table.entries[i].score)
            return i; // a tie goes below the score that was there first
    }
    return -1;
}

// Puts a score into the table at place rank, pushing the rest down one.
static void insert(int rank, int score, const char initials[3]) {
    for (int i = SCORES_COUNT - 1; i > rank; i--)
        table.entries[i] = table.entries[i - 1];
    ScoreEntry* e = &table.entries[rank];
    e->score = (u32)score;
    for (int k = 0; k < 3; k++)
        e->initials[k] = initials[k];
    e->unused = 0;
}

// --- Drawing the table -------------------------------------------------------

#define TABLE_ROW 5 // the best score's text row

void scores_draw(int highlight) {
    text_print_centered(2, "HIGH SCORES");
    for (int i = 0; i < SCORES_COUNT; i++) {
        const ScoreEntry* e = &table.entries[i];
        // The new entry stands out in the highlight style (yellow text); the
        // rest stays in the normal one. %.3s prints the three initials, which
        // have no terminating zero, and reads no further.
        text_set_style(i == highlight ? TEXT_HIGHLIGHT : TEXT_NORMAL);
        text_print_centered(TABLE_ROW + i,
                            text_format("%2d  %.3s  %6u", i + 1, e->initials, e->score));
    }
    text_set_style(TEXT_NORMAL);
    if (save_failed)
        text_print_centered(TABLE_ROW + SCORES_COUNT + 1, "(COULD NOT SAVE)");
}

// --- Entering initials -------------------------------------------------------

// The characters a letter cycles through with UP; DOWN goes backward.
static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ. ";
#define LETTER_COUNT ((int)sizeof letters - 1)

static int entry_score, entry_rank;
static int cursor;    // the letter being changed: 0 to 2
static int choice[3]; // each letter's index in letters[]

static void draw_entry(void) {
    // "A  B  C" and a caret under the current letter; both lines have the
    // same length, so they line up when centered. A space shows as '_'.
    char shown[3];
    for (int k = 0; k < 3; k++) {
        char c = letters[choice[k]];
        shown[k] = c == ' ' ? '_' : c;
    }
    text_print_centered(9, text_format("%c  %c  %c", shown[0], shown[1], shown[2]));
    text_print_centered(10, text_format("%c  %c  %c", cursor == 0 ? '^' : ' ',
                                        cursor == 1 ? '^' : ' ', cursor == 2 ? '^' : ' '));
}

int entry_begin(int score) {
    entry_score = score;
    entry_rank = scores_rank(score);
    cursor = 0;
    choice[0] = choice[1] = choice[2] = 0; // "AAA"
    // UP may still be held from thrusting: it changes no letter until it is
    // released and pressed again.
    button_repeat_reset();
    text_clear();
    text_print_centered(3, "NEW HIGH SCORE");
    text_print_centered(5, text_format("SCORE %d   PLACE %d", score, entry_rank + 1));
    text_print_centered(7, "ENTER YOUR INITIALS");
    text_print_centered(13, "UP/DOWN:LETTER");
    text_print_centered(15, "A:NEXT  B:BACK");
    text_print_centered(17, "START:DONE");
    draw_entry();
    return entry_rank;
}

// +1 (UP), -1 (DOWN) or 0: one step per press, then repeating while held
// (button_repeat).
static int letter_step(void) {
    return button_repeat(BUTTON_UP) ? 1 : button_repeat(BUTTON_DOWN) ? -1 : 0;
}

EntryEvent entry_update(void) {
    EntryEvent event = ENTRY_NONE;
    int step = letter_step();
    if (step != 0) {
        choice[cursor] = (choice[cursor] + step + LETTER_COUNT) % LETTER_COUNT;
        event = ENTRY_LETTER;
    }
    bool done = button_pressed(BUTTON_START);
    if (button_pressed(BUTTON_A | BUTTON_RIGHT)) {
        if (cursor < 2) {
            cursor++;
            event = ENTRY_MOVE;
        } else if (button_pressed(BUTTON_A)) {
            done = true; // A on the last letter confirms
        }
    }
    if (button_pressed(BUTTON_B | BUTTON_LEFT) && cursor > 0) {
        cursor--;
        event = ENTRY_MOVE;
    }

    if (done) {
        char initials[3] = {letters[choice[0]], letters[choice[1]], letters[choice[2]]};
        insert(entry_rank, entry_score, initials);
        // Saved once, now, at a pause in the game: a save takes a few
        // milliseconds on the GBA, too long to do every frame.
        save_failed = !save_write(SCORES_SLOT, &table, sizeof table, SCORES_VERSION);
        return ENTRY_DONE;
    }
    if (event != ENTRY_NONE)
        draw_entry();
    return event;
}
