// Host tests for the web backend's renderer (src/web/ppu.c): small synthetic
// video states, checked pixel by pixel against what the GBA shows.

#include "../src/web/web.h"
#include "test.h"

#define RGB15(r, g, b) ((u16)((r) | ((g) << 5) | ((b) << 10)))

static u16 io[512];
static u16 palette[512];
static u8 vram[0x18000];
static u16 oam[512];
static u8 screen[WEB_SCREEN_W * WEB_SCREEN_H * 4];

// I/O registers, as halfword indices.
enum {
    DISPCNT = 0x00 / 2,
    BG0CNT = 0x08 / 2,
    BG0HOFS = 0x10 / 2,
    BG0VOFS = 0x12 / 2,
    BG2PA = 0x20 / 2,
    BG2PD = 0x26 / 2,
    WIN0H = 0x40 / 2,
    WIN0V = 0x44 / 2,
    WININ = 0x48 / 2,
    WINOUT = 0x4A / 2,
    BLDCNT = 0x50 / 2,
    BLDALPHA = 0x52 / 2,
    BLDY = 0x54 / 2,
};

// Power-on-like state: everything zero, all objects hidden.
static void reset(u16 dispcnt) {
    for (u32 i = 0; i < 512; i++) {
        io[i] = 0;
        palette[i] = 0;
        oam[i] = 0;
    }
    for (u32 i = 0; i < sizeof vram; i++)
        vram[i] = 0;
    for (u32 i = 0; i < 128; i++)
        oam[i * 4] = 0x0200; // hidden
    io[DISPCNT] = dispcnt;
}

static void render(void) {
    WebVideoMemory mem = {io, palette, vram, oam};
    web_render(&mem, screen);
}

// The pixel at (x, y) as 0xRRGGBB.
static u32 pixel(u32 x, u32 y) {
    const u8* p = &screen[(y * WEB_SCREEN_W + x) * 4];
    return (u32)p[0] << 16 | (u32)p[1] << 8 | p[2];
}

// A BGR555 color as the renderer shows it.
static u32 rgb(u16 c) {
    u32 r = c & 31u, g = (c >> 5) & 31u, b = (c >> 10) & 31u;
    return ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
}

// Sets pixel (x, y) of a 4bpp tile at byte address base to color index c.
static void tile4_set(u32 base, u32 x, u32 y, u32 c) {
    u8* b = &vram[base + y * 4 + x / 2];
    *b = (u8)((x & 1) ? ((*b & 0x0F) | (c << 4)) : ((*b & 0xF0) | c));
}

// Fills a 4bpp tile with color index c.
static void tile4_fill(u32 base, u32 c) {
    for (u32 i = 0; i < 32; i++)
        vram[base + i] = (u8)(c | c << 4);
}

// Writes a halfword to VRAM (little endian, like the GBA).
static void vram16(u32 addr, u16 v) {
    vram[addr] = (u8)v;
    vram[addr + 1] = (u8)(v >> 8);
}

static void set_obj(u32 i, u16 a0, u16 a1, u16 a2) {
    oam[i * 4] = a0;
    oam[i * 4 + 1] = a1;
    oam[i * 4 + 2] = a2;
}

#define OBJ_TILES 0x10000u // object tiles (charblock 4)
#define OBJ_PAL 256

static const u16 red = RGB15(31, 0, 0), green = RGB15(0, 31, 0), blue = RGB15(0, 0, 31);
static const u16 backdrop = RGB15(2, 4, 8);

static void backdrop_only(void) {
    reset(0x1F00); // every layer enabled, but nothing in VRAM or OAM
    palette[0] = RGB15(31, 16, 1);
    render();
    CHECK(pixel(0, 0) == 0xFF8408); // channels expanded as (c << 3) | (c >> 2)
    CHECK(pixel(239, 159) == 0xFF8408);
    CHECK(screen[3] == 255 && screen[WEB_SCREEN_W * WEB_SCREEN_H * 4 - 1] == 255);
    // The backdrop has nothing behind it to blend with, even as both targets.
    io[BLDCNT] = 0x2020 | 0x40;
    io[BLDALPHA] = 16 | 16 << 8;
    render();
    CHECK(pixel(0, 0) == 0xFF8408);
}

static void forced_blank(void) {
    reset(0x1F80);
    palette[0] = backdrop;
    render();
    CHECK(pixel(0, 0) == 0xFFFFFF);
    CHECK(pixel(120, 80) == 0xFFFFFF);
}

static void regular_background(void) {
    // BG0: 4bpp, char base 0, screen base 8 (0x4000), 512x256 (two screenblocks).
    reset(0x0100);
    io[BG0CNT] = (u16)(8 << 8 | 1 << 14);
    palette[0] = backdrop;
    palette[2 * 16 + 1] = red;
    palette[3 * 16 + 1] = green;
    tile4_set(32, 1, 2, 1);               // tile 1: one pixel at (1, 2)
    vram16(0x4000, 1 | 2 << 12);          // tile (0, 0): tile 1, palette bank 2
    vram16(0x4002, 1 | 3 << 12 | 0x0400); // tile (1, 0): hflip, bank 3
    vram16(0x4004, 1 | 3 << 12 | 0x0800); // tile (2, 0): vflip
    vram16(0x4800, 1 | 3 << 12);          // second screenblock, tile (32, 0)
    render();
    CHECK(pixel(1, 2) == rgb(red));
    CHECK(pixel(0, 0) == rgb(backdrop)); // color 0 is transparent
    CHECK(pixel(8 + 6, 2) == rgb(green));
    CHECK(pixel(8 + 1, 2) == rgb(backdrop));
    CHECK(pixel(16 + 1, 5) == rgb(green));
    CHECK(pixel(16 + 1, 2) == rgb(backdrop));

    // Scrolling: x 256 is the second screenblock; past 512 it wraps to 0.
    io[BG0HOFS] = 256 - 8;
    io[BG0VOFS] = 256 + 1; // 256 high: wraps to row 1
    render();
    CHECK(pixel(8 + 1, 1) == rgb(green));
    io[BG0HOFS] = 512 - 8;
    render();
    CHECK(pixel(8 + 1, 1) == rgb(red));
    CHECK(pixel(8 + 8 + 6, 1) == rgb(green));

    // 8bpp: tile 1 is 64 bytes at 64; the screen entry's palette bank is ignored.
    io[BG0CNT] = (u16)(8 << 8 | 0x80);
    io[BG0HOFS] = io[BG0VOFS] = 0;
    for (u32 i = 0; i < 128; i++)
        vram[i] = 0;
    vram[64 + 2 * 8 + 1] = 200;
    palette[200] = blue;
    render();
    CHECK(pixel(1, 2) == rgb(blue));
}

static void sprite_4bpp_with_flips(void) {
    reset(0x1040); // OBJ, 1D mapping
    palette[0] = backdrop;
    palette[OBJ_PAL + 5 * 16 + 3] = red;
    tile4_set(OBJ_TILES + 4 * 32, 1, 2, 3); // tile 4: pixel at (1, 2)
    set_obj(0, 50, 100, 4 | 5 << 12);       // 8x8 at (100, 50), palette bank 5
    render();
    CHECK(pixel(101, 52) == rgb(red));
    CHECK(pixel(100, 50) == rgb(backdrop));
    set_obj(0, 50, 100 | 0x1000, 4 | 5 << 12); // hflip
    render();
    CHECK(pixel(106, 52) == rgb(red));
    CHECK(pixel(101, 52) == rgb(backdrop));
    set_obj(0, 50, 100 | 0x2000, 4 | 5 << 12); // vflip
    render();
    CHECK(pixel(101, 55) == rgb(red));
    CHECK(pixel(101, 52) == rgb(backdrop));
    set_obj(0, 50, 508, 4 | 5 << 12); // x = -4: 9-bit wrap
    render();
    CHECK(pixel(101, 52) == rgb(backdrop));
    tile4_set(OBJ_TILES + 4 * 32, 5, 2, 3);
    render();
    CHECK(pixel(1, 52) == rgb(red));
    oam[0] |= 0x0200; // hidden
    render();
    CHECK(pixel(1, 52) == rgb(backdrop));
}

static void sprite_8bpp_and_2d_mapping(void) {
    // 16x16 4bpp sprite in 2D mapping: its second tile row starts 32 tiles on.
    reset(0x1000);
    palette[0] = backdrop;
    palette[OBJ_PAL + 1] = red;
    palette[OBJ_PAL + 2] = green;
    tile4_fill(OBJ_TILES + 2 * 32, 1);  // top left tile
    tile4_fill(OBJ_TILES + 34 * 32, 2); // bottom left in 2D
    set_obj(0, 0, 0 | 1 << 14, 2);      // square, size 1 (16x16)
    render();
    CHECK(pixel(0, 0) == rgb(red));
    CHECK(pixel(0, 8) == rgb(green));
    io[DISPCNT] = 0x1040; // 1D: the bottom left tile is tile 4 instead
    render();
    CHECK(pixel(0, 8) == rgb(backdrop));

    // 8bpp: 64-byte tiles, palette index straight into the 256 object colors.
    reset(0x1040);
    palette[OBJ_PAL + 77] = blue;
    vram[OBJ_TILES + 6 * 32 + 3 * 8 + 2] = 77; // tile index 6 (counted in 32-byte units)
    set_obj(0, 10 | 0x2000, 20, 6);
    render();
    CHECK(pixel(22, 13) == rgb(blue));
}

static void sprite_priority_against_backgrounds(void) {
    reset(0x1100); // BG0 + OBJ
    palette[0] = backdrop;
    palette[1] = green; // BG0 is solid green
    palette[OBJ_PAL + 1] = red;
    io[BG0CNT] = (u16)(8 << 8 | 1); // priority 1
    tile4_fill(0, 1);               // BG tile 0, used by the zeroed map
    tile4_fill(OBJ_TILES + 32, 1);
    set_obj(0, 0, 0, 1 | 1 << 10); // priority 1: wins the tie with BG0
    render();
    CHECK(pixel(0, 0) == rgb(red));
    CHECK(pixel(8, 0) == rgb(green));
    set_obj(0, 0, 0, 1 | 2 << 10); // priority 2: behind BG0
    render();
    CHECK(pixel(0, 0) == rgb(green));
}

static void sprite_order_between_sprites(void) {
    reset(0x1100);
    palette[0] = backdrop;
    palette[1] = green;
    palette[OBJ_PAL + 1] = red;
    palette[OBJ_PAL + 2] = blue;
    io[BG0CNT] = (u16)(8 << 8 | 1); // BG0, priority 1, but empty for now
    tile4_fill(OBJ_TILES + 32, 1);  // tile 1: red
    tile4_fill(OBJ_TILES + 64, 2);  // tile 2: blue
    // Same priority: the lower OAM index is in front.
    set_obj(3, 0, 0, 1);
    set_obj(7, 0, 0, 2);
    render();
    CHECK(pixel(0, 0) == rgb(red));
    // Different priorities: the better priority is in front, whatever the
    // OAM order (mGBA and the hardware compare priorities first).
    set_obj(3, 0, 0, 1 | 2 << 10);
    set_obj(7, 0, 0, 2 | 1 << 10);
    render();
    CHECK(pixel(0, 0) == rgb(blue));
    // A transparent pixel of a later, better-priority object lifts the earlier
    // object's pixel to its priority: in front of BG0 (priority 1).
    tile4_fill(0, 1); // BG0 now solid green
    set_obj(3, 0, 0, 1 | 2 << 10);
    set_obj(7, 0, 0, 5 | 0 << 10); // tile 5 is empty
    render();
    CHECK(pixel(0, 0) == rgb(red));
    oam[7 * 4] = 0x0200;
    render();
    CHECK(pixel(0, 0) == rgb(green));
}

static void affine_sprite_rotated_90(void) {
    reset(0x1040);
    palette[0] = backdrop;
    palette[OBJ_PAL + 1] = red;
    tile4_set(OBJ_TILES + 32, 1, 2, 1); // texel (1, 2)
    // Matrix 2: texture x follows screen y, texture y = -screen x (90 degrees).
    oam[2 * 16 + 3] = 0;          // pa
    oam[2 * 16 + 7] = 256;        // pb
    oam[2 * 16 + 11] = (u16)-256; // pc
    oam[2 * 16 + 15] = 0;         // pd
    set_obj(0, 40 | 0x0100, 60 | 2 << 9, 1);
    render();
    // lx = dy + 4, ly = 4 - dx: texel (1, 2) lands at dx = 2, dy = -3.
    CHECK(pixel(60 + 6, 40 + 1) == rgb(red));
    CHECK(pixel(60 + 1, 40 + 2) == rgb(backdrop));
}

static void affine_sprite_double_size(void) {
    reset(0x1040);
    palette[0] = backdrop;
    palette[OBJ_PAL + 1] = red;
    tile4_set(OBJ_TILES + 32, 1, 2, 1);
    oam[3] = 256; // matrix 0: identity
    oam[15] = 256;
    set_obj(0, 40 | 0x0300, 60, 1); // double size: 16x16 box, image centered
    render();
    CHECK(pixel(60 + 4 + 1, 40 + 4 + 2) == rgb(red));
    CHECK(pixel(60 + 1, 40 + 2) == rgb(backdrop));
    set_obj(0, 40 | 0x0100, 60, 1); // not double size: the image fills the box
    render();
    CHECK(pixel(60 + 1, 40 + 2) == rgb(red));
}

static void sprite_y_wrap(void) {
    reset(0x1040);
    palette[0] = backdrop;
    palette[OBJ_PAL + 1] = red;
    tile4_set(OBJ_TILES + 32, 1, 6, 1);
    set_obj(0, 252, 30, 1); // rows 4-7 show at the top of the screen
    render();
    CHECK(pixel(31, 2) == rgb(red));
    set_obj(0, 160, 30, 1); // below the screen: nothing wraps
    render();
    CHECK(pixel(31, 2) == rgb(backdrop));
}

static void sprite_cycle_budget(void) {
    // 64-pixel-wide sprites cost 64 cycles each: of 1210, the 19th (index
    // 18) still draws, the 20th doesn't.
    reset(0x1040);
    palette[0] = backdrop;
    palette[OBJ_PAL + 1] = red;
    tile4_fill(OBJ_TILES + 32, 1);
    for (u32 i = 0; i < 18; i++)
        set_obj(i, 0 | 1 << 14, 0 | 3 << 14, 0); // 64x32, empty tiles
    set_obj(18, 0, 100, 1);                      // 8x8 at x = 100: 8 cycles
    set_obj(19, 0, 170, 1);
    render();
    CHECK(pixel(100, 0) == rgb(red));
    CHECK(pixel(170, 0) == rgb(red));
    set_obj(18, 0 | 1 << 14, 100 | 3 << 14, 1); // now 64 wide
    render();
    CHECK(pixel(170, 0) == rgb(backdrop)); // sprite 19 is past the budget
}

static void brightness_fade(void) {
    reset(0x0100);
    palette[0] = RGB15(10, 10, 10);
    palette[1] = RGB15(31, 16, 0);
    io[BG0CNT] = 8 << 8;
    for (u32 i = 0; i < 32; i++) // only the first map row has tile 1
        vram16(0x4000 + i * 2, 1);
    tile4_fill(32, 1);
    io[BLDCNT] = 0x01 | 0xC0; // BG0, darken
    io[BLDY] = 8;
    render();
    CHECK(pixel(0, 0) == rgb(RGB15(16, 8, 0)));   // c - c * 8 / 16
    CHECK(pixel(0, 8) == rgb(RGB15(10, 10, 10))); // backdrop isn't a target
    io[BLDCNT] = 0x01 | 0x80;                     // brighten
    render();
    CHECK(pixel(0, 0) == rgb(RGB15(31, 23, 15))); // c + (31 - c) * 8 / 16
    io[BLDY] = 31;                                // treated as 16
    render();
    CHECK(pixel(0, 0) == 0xFFFFFF);
    io[BLDCNT] = 0x21 | 0xC0; // BG0 and backdrop, darken fully
    io[BLDY] = 16;
    render();
    CHECK(pixel(0, 0) == 0 && pixel(0, 8) == 0);
}

static void alpha_blend(void) {
    reset(0x1300); // BG0 over BG1, plus objects
    palette[0] = backdrop;
    palette[1] = RGB15(20, 0, 10);
    palette[2] = RGB15(15, 30, 0);
    io[BG0CNT] = 8 << 8;                // priority 0, map at 0x4000 uses tile 1
    io[BG0CNT + 1] = (u16)(9 << 8 | 1); // priority 1, map at 0x4800 uses tile 2
    tile4_fill(32, 1);
    tile4_fill(64, 2);
    for (u32 i = 0; i < 1024; i++) {
        vram16(0x4000 + i * 2, 1);
        vram16(0x4800 + i * 2, 2);
    }
    io[BLDCNT] = 0x01 | 0x40 | 0x0200; // BG0 over BG1, alpha
    io[BLDALPHA] = 8 | 8 << 8;
    render();
    CHECK(pixel(0, 0) == rgb(RGB15(17, 15, 5)));
    io[BLDALPHA] = 16 | 16 << 8; // red saturates at 31
    render();
    CHECK(pixel(0, 0) == rgb(RGB15(31, 30, 10)));
    io[BLDCNT] = 0x01 | 0x40 | 0x2000; // BG1 isn't a second target: no blend
    render();
    CHECK(pixel(0, 0) == rgb(RGB15(20, 0, 10)));

    // A semi-transparent object blends with a second target in any mode.
    io[BLDCNT] = 0x0100; // no mode, no first targets, BG0 second target
    io[BLDALPHA] = 8 | 8 << 8;
    palette[OBJ_PAL + 1] = RGB15(0, 0, 30);
    tile4_fill(OBJ_TILES + 32, 1);
    set_obj(0, 0 | 0x0400, 0, 1);
    render();
    CHECK(pixel(0, 0) == rgb(RGB15(10, 0, 20)));
    CHECK(pixel(8, 0) == rgb(RGB15(20, 0, 10)));
    oam[0] = 0; // not semi-transparent
    render();
    CHECK(pixel(0, 0) == rgb(RGB15(0, 0, 30)));
}

static void window_0(void) {
    reset(0x2100); // BG0, window 0
    palette[0] = backdrop;
    palette[1] = green;
    io[BG0CNT] = 8 << 8;
    tile4_fill(0, 1);
    io[WIN0H] = 10 << 8 | 20; // x 10-19
    io[WIN0V] = 5 << 8 | 15;  // y 5-14
    io[WININ] = 0;            // inside: nothing
    io[WINOUT] = 0x01;        // outside: BG0
    render();
    CHECK(pixel(9, 5) == rgb(green));
    CHECK(pixel(10, 5) == rgb(backdrop));
    CHECK(pixel(19, 14) == rgb(backdrop));
    CHECK(pixel(20, 14) == rgb(green));
    CHECK(pixel(10, 15) == rgb(green));
    io[DISPCNT] = 0x0100; // window off
    render();
    CHECK(pixel(10, 5) == rgb(green));
}

static void bitmap_and_affine_modes(void) {
    // Mode 3: BG2 is a 240x160 direct-color bitmap (with an identity matrix).
    reset(0x0403);
    io[BG2PA] = 256;
    io[BG2PD] = 256;
    vram16((7 * 240 + 5) * 2, RGB15(3, 6, 9));
    render();
    CHECK(pixel(5, 7) == rgb(RGB15(3, 6, 9)));
    // Mode 4: 8-bit indices, second page at 0xA000.
    io[DISPCNT] = 0x0414;
    palette[0] = backdrop;
    palette[9] = red;
    vram[0xA000 + 7 * 240 + 5] = 9;
    render();
    CHECK(pixel(5, 7) == rgb(red));
    CHECK(pixel(6, 7) == rgb(backdrop));
    // Mode 1: BG2 affine, 128x128, 8bpp tiles; 1-byte map entries.
    reset(0x0401);
    io[BG2PA] = 256;
    io[BG2PD] = 256;
    io[BG0CNT + 2] = 8 << 8; // map at 0x4000
    vram[0x4000 + 1] = 3;    // map (1, 0): tile 3
    vram[3 * 64 + 2 * 8 + 4] = 42;
    palette[42] = green;
    render();
    CHECK(pixel(8 + 4, 2) == rgb(green));
    CHECK(pixel(128 + 8 + 4, 2) != rgb(green)); // no wraparound
    io[BG0CNT + 2] |= 0x2000;
    render();
    CHECK(pixel(128 + 8 + 4, 2) == rgb(green));
}

TEST_SUITE(web_ppu_tests, "web_ppu", {"backdrop_only", backdrop_only},
           {"forced_blank", forced_blank}, {"regular_background", regular_background},
           {"sprite_4bpp_with_flips", sprite_4bpp_with_flips},
           {"sprite_8bpp_and_2d_mapping", sprite_8bpp_and_2d_mapping},
           {"sprite_priority_against_backgrounds", sprite_priority_against_backgrounds},
           {"sprite_order_between_sprites", sprite_order_between_sprites},
           {"affine_sprite_rotated_90", affine_sprite_rotated_90},
           {"affine_sprite_double_size", affine_sprite_double_size},
           {"sprite_y_wrap", sprite_y_wrap}, {"sprite_cycle_budget", sprite_cycle_budget},
           {"brightness_fade", brightness_fade}, {"alpha_blend", alpha_blend},
           {"window_0", window_0}, {"bitmap_and_affine_modes", bitmap_and_affine_modes});
