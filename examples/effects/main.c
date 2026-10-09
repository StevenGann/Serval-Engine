// effects: the engine's screen effects, one scene each.
//
// Demonstrates:
//   - The engine's splash screen (serval_splash)
//   - Scenes that load their own sprites, tileset and map layers, switched
//     with fades of the screen's brightness (screen_set_brightness)
//   - Blending (blend_fx.c): alpha blending with screen_set_blend() and
//     SPRITE_BLEND: a see-through foreground, sprites that glow, blended
//     shadows, a metasprite with a blended piece, and blending pausing
//     while screen_set_brightness() dims the screen
//   - Art drawn at boot from ASCII pictures and shapes (art.c), map layers
//     on all three backgrounds, animated background tiles
//     (tileset_set_tiles)
//   - Palettes (palette_fx.c): palette writes, sprite_set_colors() and
//     tileset_set_colors(): a palette cycle, fades mixed with color_mix(),
//     the backdrop written as color 0 of palette 0, a hit flash
//   - Sprite tiles (sprite_tiles_fx.c): runtime sprite tiles,
//     sprite_set_tiles(): card faces composed in RAM when they are dealt and
//     a frame counter rebuilt every frame, each one hardware sprite whose
//     tiles are copied to VRAM in VBlank; a card flip with sprite_draw_ex()
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
//   - Blending: a garden at night. A starry sky with the moon and dark
//     hills; a red brick wall on the right, with a waterfall pouring down
//     over it into a pool along the bottom right; grass on the left, with a
//     street lamp whose lantern has a round warm halo. A serval stands on
//     the grass with a shadow at its feet, and three pale green wisps drift
//     above. Row 1 shows the blend call, row 18 what it does, row 19 the
//     buttons. The waterfall's streaks keep falling.
//     - First MIX, "blend(FOREGROUND, ALL, 8, 8)": the waterfall and the
//       pool are see-through, the bricks and the serval showing through the
//       water; the shadow darkens the ground under the serval; the halo and
//       the wisps are faint and see-through, also in front of the water.
//     - A: GLOW, "blend(0, ALL, 16, 16)": the halo and the wisps glow, their
//       colors added to what is behind them (brightest over the waterfall);
//       the shadow disappears (black adds nothing); the water is opaque.
//     - A again: OFF, "blend(0, 0, 0, 0)": everything opaque: solid water, a
//       black shadow, the halo as solid brown rings, solid green wisps. A
//       again: MIX.
//     - Left and right walk the serval, facing the way it walks; in the
//       pool and behind the waterfall the water covers it (see-through in
//       MIX).
//     - Holding B dims the screen, "DIMMED: BLENDING PAUSED" on row 2:
//       everything turns opaque, then dims (the hardware has one color
//       effect, and the brightness takes it); releasing B brings the
//       blending back. The fades between scenes pause it the same way: the
//       water turns opaque as the screen fades.
//   - Palettes: a sky of four blue bands over wavy blue water, with a dark
//     shore between them and a red gem on each side of it. "SKY: A FADE TO
//     DUSK" on row 1, "< FADE" and "A: FLASH >" pointing at the gems,
//     "WATER: A PALETTE CYCLE" on the water. The water's waves roll up
//     toward the shore, a step every 6 frames, while its tiles stay the
//     same. The sky fades to dusk (purple at the top, orange at the
//     horizon) and the shore to a darker purple, then back, every 8.5
//     seconds or so. The left gem brightens toward white and back about
//     every 2 seconds; A (mGBA: the X key) makes the right gem flash white
//     for 6 frames, the left one unchanged.
//   - Sprite tiles: four playing cards on black under "FACES COMPOSED AT
//     RUN TIME" (the ace of spades, the king of hearts in a gold frame, the
//     seven of diamonds, the ten of clubs), each one sprite. Every 40 frames
//     the next card from the left flips over, narrowing to an edge and
//     widening again as a new random card; A (mGBA: the X key) flips all
//     four at once. Below them, gold digits on a navy bar count the frames,
//     changing every frame, and "HARDWARE SPRITES: 5" (four cards and the
//     counter).
//   - Raster: for now the words "RASTER EFFECTS (TO COME)".
//   - No sound after the splash.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A key, R = S key.)
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
    text_set_shadow(true); // readable over any scene

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
