// The raster scene: raster effects, raster_scroll(), raster_backdrop() and
// raster_clear() (docs/runtime-systems.md#raster-effects).
//
// A placeholder until that feature's implementation fills it in: it shows
// only the effect's name. The scene's contract (what main.c resets between
// scenes, what leave() must undo) is in effects.h.

#include "effects.h"

static void enter(void) {
    text_print_centered(9, "RASTER EFFECTS");
    text_print_centered(11, "(TO COME)");
}

static void update(void) {}

static void leave(void) {}

const Scene raster_scene = {.name = "RASTER", .enter = enter, .update = update, .leave = leave};
