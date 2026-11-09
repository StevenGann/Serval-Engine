// hello: the smallest complete Serval Engine game.
//
// Demonstrates:
//   - Defining sprites' data by hand (tiles, palette, SpriteAsset, sprite
//     table, SpriteGroup) in the same form Studio Advance's build generates
//   - Loading a sprite group and drawing sprites every frame, choosing a
//     sprite's frame (a walk cycle, a blink) and flipping it to face left
//   - Keeping a position on screen with int_clamp, and moving along a curve
//     with fx_sin
//   - The frame loop (frame_begin / frame_end) and reading buttons
//   - Setting the backdrop color and printing text
//
// What to expect when booting the ROM:
//   - A green meadow dotted with grass tufts and white, pink and blue flowers,
//     in two patches and scattered between. "HELLO!" at the top,
//     "D-PAD: WALK   A: RUN" at the bottom. A pink butterfly flutters over
//     the meadow in a slow figure of eight.
//   - A serval kitten with a shadow stands in the center and blinks now and
//     then. The D-pad walks it one pixel per frame, its legs stepping and its
//     body turning to face left or right; holding A runs at three pixels per
//     frame with quicker steps. It stops at the edges of the screen and walks
//     in front of the flowers.
//   - No sound.
//   (In mGBA's default keyboard mapping, the D-pad is the arrow keys and A is X.)
//
// Uses only Serval Engine's API; no third-party headers.

#include "serval/serval.h"

// --- Assets ------------------------------------------------------------------

enum { SPR_KITTEN, SPR_SHADOW, SPR_DECOR, SPR_BUTTERFLY, SPRITE_COUNT };

// Pixels are 4 bits each, an index into the palette below; 0 is transparent.
// A u32 holds one row of an 8x8 tile, its lowest hex digit the leftmost pixel,
// so the rows read mirrored. A 16x16 frame is four tiles: top left, top right,
// bottom left, bottom right. The kitten's frame 0, with 1 drawn as #:
//
//      ......#.....#...       Frames: 0 standing, 1 and 2
//      .....#6#...#6#..       stepping, 3 blinking.
//      .....#66###66#..
//      ....#222222222#.       1 outline    5 white
//      ....#222424222#.       2 fur        6 pink
//      ....#282222282#.       3 light fur  7 nose
//      ....#282222282#.       4 spots      8 eyes
//      ....#633373336#.
//      .##..#3333333#..
//      #4#...#######...
//      #2#.##2222222#..
//      .#4#2422422422#.
//      ..##2224222422#.
//      ...#3333333333#.
//      ...#2#.#2#.#22#.
//      ...#5#.#5#.#55#.
static const u32 kitten_tiles[4 * 32] = {
    0x01000000, 0x16100000, 0x66100000, 0x22210000, 0x22210000, 0x28210000, 0x28210000, 0x33610000,
    0x00010000, 0x00161000, 0x00166111, 0x01222222, 0x01222424, 0x01282222, 0x01282222, 0x01633373,
    0x33100110, 0x11000141, 0x22110121, 0x22421410, 0x42221100, 0x33331000, 0x10121000, 0x10151000,
    0x00133333, 0x00011111, 0x00122222, 0x01224224, 0x01224222, 0x01333333, 0x01221012, 0x01551015,
    0x01000000, 0x16100000, 0x66100000, 0x22210000, 0x22210000, 0x28210000, 0x28210000, 0x33610000,
    0x00010000, 0x00161000, 0x00166111, 0x01222222, 0x01222424, 0x01282222, 0x01282222, 0x01633373,
    0x33100110, 0x11000141, 0x22110121, 0x22421410, 0x42221100, 0x33331000, 0x10121000, 0x00151000,
    0x00133333, 0x00011111, 0x00122222, 0x01224224, 0x01224222, 0x01333333, 0x01221015, 0x01551000,
    0x01000000, 0x16100000, 0x66100000, 0x22210000, 0x22210000, 0x28210000, 0x28210000, 0x33610000,
    0x00010000, 0x00161000, 0x00166111, 0x01222222, 0x01222424, 0x01282222, 0x01282222, 0x01633373,
    0x33100110, 0x11000141, 0x22110121, 0x22421410, 0x42221100, 0x33331000, 0x10151000, 0x10000000,
    0x00133333, 0x00011111, 0x00122222, 0x01224224, 0x01224222, 0x01333333, 0x01551012, 0x00000015,
    0x01000000, 0x16100000, 0x66100000, 0x22210000, 0x22210000, 0x22210000, 0x88210000, 0x33610000,
    0x00010000, 0x00161000, 0x00166111, 0x01222222, 0x01222424, 0x01222222, 0x01288222, 0x01633373,
    0x33100110, 0x11000141, 0x22110121, 0x22421410, 0x42221100, 0x33331000, 0x10121000, 0x10151000,
    0x00133333, 0x00011111, 0x00122222, 0x01224224, 0x01224222, 0x01333333, 0x01221012, 0x01551015,
};

// 16x8: two tiles side by side, an oval in color 9.
static const u32 shadow_tiles[] = {
    0x00000000, 0x00000000, 0x00000000, 0x99990000, 0x99999900, 0x99999900, 0x99990000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00009999, 0x00999999, 0x00999999, 0x00009999, 0x00000000,
};

// 8x8, one tile per frame: a grass tuft and three flowers.
static const u32 decor_tiles[] = {
    0x00000000, 0x00000000, 0x00000000, 0x0000B000, 0x0B0AB0B0, 0x0ABABAB0, 0xAABABABA, 0x0AAAAAA0,
    0x00055000, 0x005CC500, 0x005CC500, 0x00055000, 0x0B0B0000, 0x00BB0B00, 0x00ABB000, 0x000A0000,
    0x00000000, 0x000DD000, 0x00DCCD00, 0x00DCCD00, 0x00BDD000, 0x000B0B00, 0x000BAB00, 0x0000A000,
    0x00000000, 0x00000000, 0x00E00E00, 0xECE0ECE0, 0x00EB0E00, 0x000B0B00, 0x000BB000, 0x0000A000,
};

enum { DECOR_GRASS, DECOR_DAISY, DECOR_ROSE, DECOR_BLUEBELL };

// 8x8, wings open (frame 0) and folded (frame 1).
static const u32 butterfly_tiles[] = {
    0x00000000, 0x0DD000DD, 0x0D5D0D5D, 0x0DDD1DDD, 0x00DD1DD0, 0x00DD0DD0, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x000DDD00, 0x000D1D00, 0x000D1D00, 0x00001000, 0x00000000, 0x00000000,
};

// One palette for every sprite. Color 0 is transparent.
static const u16 meadow_palette[16] = {
    0,
    COLOR_RGB(52, 30, 22),    // 1 outline
    COLOR_RGB(234, 166, 62),  // 2 fur
    COLOR_RGB(250, 222, 160), // 3 light fur
    COLOR_RGB(110, 58, 26),   // 4 spots
    COLOR_RGB(255, 250, 236), // 5 white
    COLOR_RGB(244, 158, 150), // 6 pink
    COLOR_RGB(214, 92, 112),  // 7 nose
    COLOR_RGB(28, 22, 30),    // 8 eyes
    COLOR_RGB(74, 136, 58),   // 9 shadow
    COLOR_RGB(62, 128, 52),   // 10 dark grass
    COLOR_RGB(150, 206, 96),  // 11 light grass
    COLOR_RGB(255, 212, 64),  // 12 flower center
    COLOR_RGB(246, 120, 160), // 13 pink petals
    COLOR_RGB(130, 160, 252), // 14 blue petals
};

static const SpriteAsset kitten = {.size = SPRITE_16x16, .tiles = kitten_tiles, .frame_count = 4};
static const SpriteAsset shadow = {.size = SPRITE_16x8, .tiles = shadow_tiles};
static const SpriteAsset decor = {.size = SPRITE_8x8, .tiles = decor_tiles, .frame_count = 4};
static const SpriteAsset butterfly = {
    .size = SPRITE_8x8, .tiles = butterfly_tiles, .frame_count = 2};

static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_KITTEN] = &kitten,
    [SPR_SHADOW] = &shadow,
    [SPR_DECOR] = &decor,
    [SPR_BUTTERFLY] = &butterfly,
};

// Every sprite in the table (no sprite_ids), sharing one palette.
static const SpriteGroup hello_group = {
    .palettes = meadow_palette,
    .sprite_count = SPRITE_COUNT,
    .palette_count = 1,
};

// Where the meadow's grass and flowers grow (top-left corners, in pixels).
static const struct {
    u8 x, y, frame;
} meadow[] = {
    // A patch of flowers at the top left, another at the bottom right.
    {14, 28, DECOR_DAISY},
    {24, 34, DECOR_ROSE},
    {10, 40, DECOR_GRASS},
    {30, 26, DECOR_GRASS},
    {20, 46, DECOR_BLUEBELL},
    {196, 112, DECOR_ROSE},
    {208, 104, DECOR_DAISY},
    {218, 116, DECOR_BLUEBELL},
    {204, 124, DECOR_GRASS},
    {224, 98, DECOR_GRASS},
    {186, 122, DECOR_DAISY},
    // Single plants scattered between (none on the text's rows).
    {62, 54, DECOR_GRASS},
    {96, 30, DECOR_DAISY},
    {128, 44, DECOR_GRASS},
    {166, 30, DECOR_BLUEBELL},
    {212, 40, DECOR_GRASS},
    {184, 62, DECOR_ROSE},
    {40, 84, DECOR_GRASS},
    {16, 104, DECOR_ROSE},
    {74, 110, DECOR_GRASS},
    {150, 92, DECOR_GRASS},
    {226, 70, DECOR_DAISY},
    {48, 128, DECOR_DAISY},
    {12, 152, DECOR_GRASS},
    {100, 130, DECOR_BLUEBELL},
    {140, 124, DECOR_GRASS},
    {228, 152, DECOR_GRASS},
    {164, 152, DECOR_GRASS},
    {84, 152, DECOR_GRASS},
};

// --- Game --------------------------------------------------------------------

#define KITTEN_SIZE 16

// The walk cycle: step, stand, other step, stand.
static const u8 walk_frames[4] = {1, 0, 2, 0};

int main(void) {
    serval_init();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&hello_group);
    screen_set_backdrop(COLOR_RGB(106, 176, 78));

    text_set_color(COLOR_RGB(255, 250, 236), COLOR_RGB(40, 92, 36));
    text_set_shadow(true);
    text_print_centered(1, "HELLO!");
    text_print_centered(18, "D-PAD: WALK   A: RUN");

    int x = (screen_width() - KITTEN_SIZE) / 2;
    int y = (screen_height() - KITTEN_SIZE) / 2;
    u16 facing = 0; // 0 faces right, SPRITE_FLIP_H left
    int steps = 0;  // counts up while walking, to pick the walk frame

    for (;;) {
        frame_begin();

        int speed = button_down(BUTTON_A) ? 3 : 1;
        int old_x = x, old_y = y;
        if (button_down(BUTTON_LEFT)) {
            x -= speed;
            facing = SPRITE_FLIP_H;
        }
        if (button_down(BUTTON_RIGHT)) {
            x += speed;
            facing = 0;
        }
        if (button_down(BUTTON_UP))
            y -= speed;
        if (button_down(BUTTON_DOWN))
            y += speed;

        // Keep the whole kitten on screen.
        x = int_clamp(x, 0, screen_width() - KITTEN_SIZE);
        y = int_clamp(y, 0, screen_height() - KITTEN_SIZE);

        // Walking: a new walk frame every 8 frames (running: about every 3).
        // Standing still: blink for 8 frames every 3 seconds.
        int frame;
        if (x != old_x || y != old_y) {
            steps += speed;
            frame = walk_frames[(steps / 8) % 4];
        } else {
            steps = 0;
            frame = frame_count() % 180 < 8 ? 3 : 0;
        }

        // A butterfly loops over the meadow in a lazy figure of eight, its
        // wings beating every 6 frames. An angle of 65,536 is a full turn, so
        // the loop takes 512 frames; fx_sin returns fixed point (FX(1) = 1.0).
        u16 t = (u16)(frame_count() * 128);
        int bx = 116 + fx_to_int(80 * fx_sin(t));
        int by = 76 + fx_to_int(30 * fx_sin((u16)(t * 2)));
        sprite_draw(SPR_BUTTERFLY, (frame_count() / 6) % 2, bx, by, 0);

        // Sprites drawn first are in front: the butterfly, the kitten, its
        // shadow, then the meadow.
        sprite_draw(SPR_KITTEN, frame, x, y, facing);
        sprite_draw(SPR_SHADOW, 0, x, y + 11, 0);
        for (int i = 0; i < (int)(sizeof meadow / sizeof meadow[0]); i++)
            sprite_draw(SPR_DECOR, meadow[i].frame, meadow[i].x, meadow[i].y, 0);

        frame_end();
    }
}
