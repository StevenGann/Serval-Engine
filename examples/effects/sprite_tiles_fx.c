// The sprite tiles scene: runtime sprite tiles, sprite_set_tiles()
// (docs/sprites.md#runtime-tiles).
//
// A placeholder until that feature's implementation fills it in: it shows
// only the effect's name. The scene's contract (what main.c resets between
// scenes, what leave() must undo) is in effects.h.

#include "effects.h"

static void enter(void) {
    text_print_centered(9, "RUNTIME SPRITE TILES");
    text_print_centered(11, "(TO COME)");
}

static void update(void) {}

static void leave(void) {}

const Scene sprite_tiles_scene = {
    .name = "SPRITE TILES", .enter = enter, .update = update, .leave = leave};
