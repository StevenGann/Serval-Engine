// The levels: brick layouts written as text, one character per brick, read
// by play_start_level(). After the last level the game goes back to the
// first, with a faster ball.
//
// Legend:  r red   o orange   y yellow   g green   b blue   p purple
//          s silver (two hits)   X gold (unbreakable)   . no brick
// Each row is 14 bricks (16x8 pixels) wide; up to 10 rows, from y = 32.

#include "game.h"

const Level levels[LEVEL_COUNT] = {
    {
        "STRIPES",
        {
            "pppppppppppppp",
            "bbbbbbbbbbbbbb",
            "gggggggggggggg",
            "yyyyyyyyyyyyyy",
            "oooooooooooooo",
            "rrrrrrrrrrrrrr",
        },
    },
    {
        "THE SERVAL",
        {
            ".ss........ss.",
            ".sos......sos.",
            ".soosyyyysoos.",
            "..yyyyyyyyyy..",
            "..yXyyyyyyXy..",
            "..yyyyyyyyyy..",
            "...yyyrryyy...",
            "....yyyyyy....",
            ".....pppp.....",
        },
    },
    {
        "FORTRESS",
        {
            "XXXX..XX..XXXX",
            "bbbb..ss..bbbb",
            "gggg..ss..gggg",
            "XX..XXXXXX..XX",
            "ssssssssssssss",
            "rrrrrrrrrrrrrr",
        },
    },
    {
        "DIAMOND",
        {
            "......pp......",
            ".....pbbp.....",
            "....pbggbp....",
            "...pbgyygbp...",
            "..pbgyrrygbp..",
            "...pbgyygbp...",
            "....pbggbp....",
            ".....pbbp.....",
            "XX....ss....XX",
        },
    },
};

BrickKind brick_kind_of(char c) {
    switch (c) {
    case 'r':
        return BRICK_RED;
    case 'o':
        return BRICK_ORANGE;
    case 'y':
        return BRICK_YELLOW;
    case 'g':
        return BRICK_GREEN;
    case 'b':
        return BRICK_BLUE;
    case 'p':
        return BRICK_PURPLE;
    case 's':
        return BRICK_SILVER;
    case 'X':
        return BRICK_GOLD;
    default:
        return BRICK_NONE;
    }
}
