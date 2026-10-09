// effects: the engine's screen effects, one scene each.
//
// Demonstrates:
//   - The engine's splash screen (serval_splash)
//   - Scenes that load their own sprites, tileset and map layers, switched
//     with fades of the screen's brightness (screen_set_brightness)
//   - Blending (blend_fx.c): alpha blending, screen_set_blend() and
//     SPRITE_BLEND (a placeholder for now)
//   - Palettes (palette_fx.c): palette writes (a placeholder for now)
//   - Sprite tiles (sprite_tiles_fx.c): runtime sprite tiles (a placeholder
//     for now)
//   - Raster (raster_fx.c): raster effects (a placeholder for now)
//
// What to expect when booting the ROM:
//   - First the Serval Engine splash: "made with" and the engine's logo fade
//     in on black, a coin-like jingle plays, and they fade out (about 3
//     seconds; any button skips it once the logo is in).
//   - Then the first scene, Blending. The title bar on the top row names the
//     scene ("< BLENDING >", with its number of 4), between "L" and "R".
//   - L and R switch to the previous and the next scene (after the last,
//     the first again): the screen fades to black over 8 frames, the next
//     scene loads, and the screen fades back in.
//   - Blending: for now the words "ALPHA BLENDING (TO COME)" in the middle.
//   - Palettes: for now the words "PALETTE WRITES (TO COME)".
//   - Sprite tiles: for now the words "RUNTIME SPRITE TILES (TO COME)".
//   - Raster: for now the words "RASTER EFFECTS (TO COME)".
//   - No sound after the splash.
//   (In mGBA's default keyboard mapping: L = A key, R = S key.)
//
// Uses only Serval Engine's API; no third-party headers.

#include "effects.h"

static const Scene* const scenes[] = {
    &blend_scene,
    &palette_scene,
    &sprite_tiles_scene,
    &raster_scene,
};
#define SCENE_COUNT ((int)(sizeof(scenes) / sizeof(scenes[0])))

// The title bar: the scene's name and number between the L and R hints.
static void print_title(int index) {
    text_print_line(0, TITLE_ROW, "L");
    text_print_centered_in(
        1, TEXT_COLS - 2, TITLE_ROW,
        text_format("< %s %d/%d >", scenes[index]->name, index + 1, SCENE_COUNT));
    text_print(TEXT_COLS - 1, TITLE_ROW, "R");
}

// Steps the brightness from `from` to `to`, 2 a frame, the scene running.
static void fade(const Scene* scene, int from, int to) {
    int step = to > from ? 2 : -2;
    for (int level = from; level != to;) {
        level += step;
        frame_begin();
        screen_set_brightness(level);
        scene->update();
        frame_end();
    }
}

// What every scene shares, back to how a scene finds it (effects.h).
static void reset_shared(void) {
    sprite_table_set(NULL, 0);
    for (u32 bg = 1; bg <= 3; bg++)
        map_unload(bg);
    camera_set(0, 0);
    ecs_reset();
    text_clear();
    screen_set_backdrop(COLOR_RGB(0, 0, 0));
}

// Shows scene `index`, the screen black: loads it, then fades in.
static void show(int index) {
    reset_shared();
    print_title(index);
    scenes[index]->enter();
    fade(scenes[index], SCREEN_BRIGHTNESS_MIN, 0);
}

int main(void) {
    serval_init();
    serval_splash();

    int current = 0;
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN);
    show(current);
    for (;;) {
        frame_begin();
        scenes[current]->update();
        frame_end();

        // The buttons of the frame just shown: switch after it.
        int step = button_pressed(BUTTON_R) ? 1 : button_pressed(BUTTON_L) ? -1 : 0;
        if (step) {
            fade(scenes[current], 0, SCREEN_BRIGHTNESS_MIN);
            scenes[current]->leave();
            current = (current + step + SCENE_COUNT) % SCENE_COUNT;
            show(current);
        }
    }
}
