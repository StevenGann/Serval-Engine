// The blending scene: alpha blending, screen_set_blend() and SPRITE_BLEND
// (docs/runtime-systems.md#alpha-blending, docs/sprites.md#alpha-blending).
//
// A placeholder until alpha blending is implemented: it shows only the
// effect's name. The scene's contract (what main.c resets between scenes,
// what leave() must undo) is in effects.h.

#include "effects.h"

static void enter(void) {
    text_print_centered(9, "ALPHA BLENDING");
    text_print_centered(11, "(TO COME)");
}

static void update(void) {}

static void leave(void) {}

const Scene blend_scene = {.name = "BLENDING", .enter = enter, .update = update, .leave = leave};
