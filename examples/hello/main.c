// hello: the smallest complete Serval Engine game.
//
// Demonstrates:
//   - Defining a sprite's data by hand (tiles, palette, SpriteAsset, sprite
//     table, SpriteGroup) in the same form Studio Advance's build generates
//   - Loading a sprite group and drawing a sprite every frame
//   - Keeping a position on screen with int_clamp
//   - The frame loop (frame_begin / frame_end) and reading buttons
//   - Setting the backdrop color
//
// What to expect when booting the ROM:
//   - A dark blue screen with a small yellow square (8x8 pixels) in the center.
//   - The D-pad moves the square one pixel per frame; holding A moves it three.
//   - The square stops at the edges of the screen.
//   - No sound and no other graphics.
//   (In mGBA's default keyboard mapping, the D-pad is the arrow keys and A is X.)
//
// Uses only Serval Engine's API; no third-party headers.

#include "serval/serval.h"

// --- Assets ------------------------------------------------------------------

enum { SPR_SQUARE, SPRITE_COUNT };

// One 8x8 tile, 4 bits per pixel: every pixel uses palette color 1.
static const u32 square_tiles[8] = {
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
};

// Color 0 is transparent.
static const u16 square_palette[16] = {0, COLOR_RGB(255, 200, 0)};

static const SpriteAsset square = {.size = SPRITE_8x8, .tiles = square_tiles};

static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_SQUARE] = &square,
};

// Every sprite in the table (no sprite_ids), sharing one palette.
static const SpriteGroup hello_group = {
    .palettes = square_palette,
    .sprite_count = SPRITE_COUNT,
    .palette_count = 1,
};

// --- Game --------------------------------------------------------------------

#define SQUARE_SIZE 8

int main(void) {
    serval_init();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&hello_group);
    screen_set_backdrop(COLOR_RGB(16, 32, 80));

    int x = (screen_width() - SQUARE_SIZE) / 2;
    int y = (screen_height() - SQUARE_SIZE) / 2;

    for (;;) {
        frame_begin();

        int speed = button_down(BUTTON_A) ? 3 : 1;
        if (button_down(BUTTON_LEFT))
            x -= speed;
        if (button_down(BUTTON_RIGHT))
            x += speed;
        if (button_down(BUTTON_UP))
            y -= speed;
        if (button_down(BUTTON_DOWN))
            y += speed;

        // Keep the whole square on screen.
        x = int_clamp(x, 0, screen_width() - SQUARE_SIZE);
        y = int_clamp(y, 0, screen_height() - SQUARE_SIZE);

        sprite_draw(SPR_SQUARE, 0, x, y, 0);

        frame_end();
    }
}
