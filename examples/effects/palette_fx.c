// The palettes scene: palette writes, sprite_set_colors() and
// tileset_set_colors() (docs/sprites.md#palettes,
// docs/tilemaps.md#palette-writes).
//
// A placeholder until that feature's implementation fills it in: it shows
// only the effect's name. The scene's contract (what main.c resets between
// scenes, what leave() must undo) is in effects.h.

#include "effects.h"

static void enter(void) {
    text_print_centered(9, "PALETTE WRITES");
    text_print_centered(11, "(TO COME)");
}

static void update(void) {}

static void leave(void) {}

const Scene palette_scene = {.name = "PALETTES", .enter = enter, .update = update, .leave = leave};
