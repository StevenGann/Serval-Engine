#ifndef JUKEBOX_H
#define JUKEBOX_H

// The jukebox's state, shared by main.c (the menu, which plays the music)
// and show.c (the pictures that follow it).

#include "serval/serval.h"

enum { ROW_MUSIC, ROW_VOLUME, ROW_SPEED, ROW_EFFECTS, ROW_COUNT };
enum { TUNE_THEME, TUNE_CALM, TUNE_OFF, TUNE_COUNT };

// The text row of each menu row: ROW_MUSIC's, then every other row.
#define FIRST_ROW 3
#define MENU_ROW(r) (FIRST_ROW + (r) * 2)

// The text styles besides TEXT_NORMAL (white) and TEXT_HIGHLIGHT (gold):
// show_start() gives them their colours.
enum {
    STYLE_DIM = 2, // lavender: the help, the bottom line, the module's place
    STYLE_ALERT,   // pink: PAUSED
};

typedef struct {
    int row;     // the menu row the cursor is on
    int tune;    // TUNE_*
    int volume;  // the music's, 0-255
    int speed;   // the music's tempo, percent
    int effects; // the effects' volume, 0-255
} Jukebox;

#endif // JUKEBOX_H
