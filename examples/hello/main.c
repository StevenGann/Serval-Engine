// Moves a square sprite with the D-pad. Hold A to move faster.
//
// Uses only Serval Engine's API. The sprite data below is written by hand in
// the same form Studio Advance's build generates for a game's assets.

#include "serval/serval.h"

// --- Assets ------------------------------------------------------------------

enum { SPR_SQUARE, SPRITE_COUNT };

// One 8x8 tile, 4 bits per pixel: every pixel uses palette color 1.
static const u32 square_tiles[8] = {
    0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111,
};

// Color 0 is transparent.
static const u16 square_palette[16] = {0, COLOR_RGB(255, 200, 0)};

static const SpriteAsset square = {
    .shape = SPRITE_SHAPE_SQUARE,
    .size = 0, // 8x8
    .frame_count = 1,
    .tiles_per_frame = 1,
    .tiles = square_tiles,
};

static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_SQUARE] = &square,
};

static const u16 hello_group_sprites[] = {SPR_SQUARE};

static const SpriteGroup hello_group = {
    .sprite_count = 1,
    .palette_count = 1,
    .flags = SPRITE_GROUP_RESIDENT,
    .sprite_ids = hello_group_sprites,
    .palettes = square_palette,
    .tile_count = 1,
};

// --- Game --------------------------------------------------------------------

#define SQUARE_SIZE 8

// Limits v to [lo, hi], both inclusive.
static int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

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
        x = clamp_int(x, 0, screen_width() - SQUARE_SIZE);
        y = clamp_int(y, 0, screen_height() - SQUARE_SIZE);

        sprite_draw(SPR_SQUARE, 0, x, y, 0);

        frame_end();
    }
}
