// Tests for map layers on the GBA (src/gba/map.c): the tileset in VRAM, the
// background registers, and the screenblocks streamed around the camera,
// checked against the layers' data after each frame_end().

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/map.h"
#include "serval/screen.h"
#include "serval/sprites.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/gba/internal.h"

// 16 metatiles whose 64 screen entries all differ (tiles, palettes, flips).
#define METATILES 16
static Metatile metatiles[METATILES];

static void make_metatiles(void) {
    for (u32 k = 0; k < METATILES; k++) {
        for (u32 c = 0; c < 4; c++)
            metatiles[k].se[c] = MAP_SE(4 * k + c + 1, k % 15,
                                        (k & 1 ? MAP_SE_FLIP_H : 0) | (c & 2 ? MAP_SE_FLIP_V : 0));
        metatiles[k].collision = MAP_EMPTY;
    }
}

// The playfield: 64x40 metatiles (1024x640 pixels) of a pattern that doesn't
// repeat within a screen.
#define PLAY_W 64
#define PLAY_H 40
static SERVAL_EWRAM_BSS u16 play_cells[PLAY_W * PLAY_H];
static MapLayer playfield;
// A small wrapping background (3x2 metatiles) at half speed, and a
// foreground of the playfield's size (at full speed) with a different pattern.
static u16 wrap_cells[6] = {1, 2, 3, 4, 5, 6};
static MapLayer backdrop;
static SERVAL_EWRAM_BSS u16 front_cells[PLAY_W * PLAY_H];
static MapLayer foreground;

static void make_layers(void) {
    make_metatiles();
    for (u32 y = 0; y < PLAY_H; y++) {
        for (u32 x = 0; x < PLAY_W; x++) {
            play_cells[y * PLAY_W + x] = (u16)((x * 7 + y * 3 + x / 5) % METATILES);
            front_cells[y * PLAY_W + x] = (u16)((x + y * 5) % METATILES);
        }
    }
    playfield = (MapLayer){.width = PLAY_W,
                           .height = PLAY_H,
                           .cells = play_cells,
                           .metatiles = metatiles,
                           .metatile_count = METATILES,
                           .bg = 2};
    backdrop = (MapLayer){.width = 3,
                          .height = 2,
                          .cells = wrap_cells,
                          .metatiles = metatiles,
                          .metatile_count = METATILES,
                          .bg = 3,
                          .flags = MAP_LAYER_WRAP,
                          .scroll_factor = FX_ONE / 2};
    foreground = (MapLayer){.width = PLAY_W,
                            .height = PLAY_H,
                            .cells = front_cells,
                            .metatiles = metatiles,
                            .metatile_count = METATILES,
                            .bg = 1};
}

static void unload_all(void) {
    map_unload(1);
    map_unload(2);
    map_unload(3);
    camera_set(0, 0);
    frame_begin();
    frame_end();
}

static int floor_mod(int v, int m) {
    int r = v % m;
    return r < 0 ? r + m : r;
}

static int layer_scroll(const MapLayer* layer, int camera) {
    FIXED f = layer->scroll_factor ? layer->scroll_factor : FX_ONE;
    return (camera * f) >> FX_SHIFT; // small values in these tests: no overflow
}

// What tile (tx, ty) of a layer should show.
static u16 expected_entry(const MapLayer* layer, int tx, int ty) {
    int mx = tx >> 1, my = ty >> 1;
    if (layer->flags & MAP_LAYER_WRAP) {
        mx = floor_mod(mx, layer->width);
        my = floor_mod(my, layer->height);
    } else if (mx < 0 || my < 0 || mx >= layer->width || my >= layer->height) {
        return 0;
    }
    u32 cell = layer->bg == 2 ? map_cell(mx, my) : layer->cells[my * layer->width + mx];
    return cell < layer->metatile_count ? layer->metatiles[cell].se[(ty & 1) * 2 + (tx & 1)] : 0;
}

// True if the layer's screenblock holds the right entries for every tile the
// screen shows, and its scroll registers the right values.
static bool window_ok(const MapLayer* layer) {
    int sx = layer_scroll(layer, camera_x()), sy = layer_scroll(layer, camera_y());
    if (serval_map_scroll(layer->bg) != ((u32)(sy & 0x1FF) << 16 | (u32)(sx & 0x1FF)))
        return false;
    const u16* sb = se_mem[27 + layer->bg];
    for (int ty = sy >> 3; ty <= (sy + SCREEN_H - 1) >> 3; ty++) {
        for (int tx = sx >> 3; tx <= (sx + SCREEN_W - 1) >> 3; tx++) {
            if (sb[(ty & 31) * 32 + (tx & 31)] != expected_entry(layer, tx, ty))
                return false;
        }
    }
    return true;
}

static void show_frame(void) {
    frame_begin();
    frame_end();
}

static const u32 tiles[3 * 8] = {[0] = 0, [8] = 0x11111111, [16] = 0x12345678, [23] = 0xFEDCBA98};
static const u16 palettes[2 * 16] = {
    [0] = 0x7FFF, [1] = 0x0011, [15] = 0x0F0F, [16] = 0x1234, [17] = 0x2222, [31] = 0x3333};

static void tileset_load_copies_tiles_and_palettes(void) {
    screen_set_backdrop(0x0123);
    pal_bg_bank[1][0] = 0x0456;
    const Tileset tileset = {
        .tiles = tiles, .tile_count = 3, .palettes = palettes, .palette_count = 2};
    CHECK(tileset_load(&tileset));
    const u32* vram = (const u32*)&tile_mem[1][0];
    bool same = true;
    for (u32 k = 0; k < 24; k++)
        same &= vram[k] == tiles[k];
    CHECK(same);
    CHECK(pal_bg_bank[0][0] == 0x0123); // the backdrop stays
    CHECK(pal_bg_bank[0][1] == 0x0011 && pal_bg_bank[0][15] == 0x0F0F);
    CHECK(pal_bg_bank[1][0] == 0x0456); // color 0 is transparent: not copied
    CHECK(pal_bg_bank[1][1] == 0x2222 && pal_bg_bank[1][15] == 0x3333);
    screen_set_backdrop(0);
}

static void tileset_load_rejects_bad_tilesets(void) {
    u32 before = debug_warning_count();
    CHECK(!tileset_load(NULL));
    const Tileset no_tiles = {.tile_count = 3};
    CHECK(!tileset_load(&no_tiles));
    const Tileset too_many = {.tiles = tiles, .tile_count = MAP_MAX_TILES + 1};
    CHECK(!tileset_load(&too_many));
    const Tileset too_many_palettes = {
        .tiles = tiles, .tile_count = 1, .palettes = palettes, .palette_count = 16};
    CHECK(!tileset_load(&too_many_palettes));
    const Tileset no_palettes = {.tiles = tiles, .tile_count = 1, .palette_count = 1};
    CHECK(!tileset_load(&no_palettes));
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == before + 4); // too many tiles or palettes: one kind
#else
    CHECK(debug_warning_count() == before);
#endif
}

static void map_load_sets_up_the_backgrounds(void) {
    make_layers();
    CHECK(map_load(&playfield));
    CHECK(map_load(&foreground));
    CHECK(map_load(&backdrop));
    // Drawn at load time already, so the next frame shows them.
    CHECK((REG_DISPCNT & (DCNT_BG1 | DCNT_BG2 | DCNT_BG3)) == (DCNT_BG1 | DCNT_BG2 | DCNT_BG3));
    CHECK(window_ok(&playfield) && window_ok(&foreground) && window_ok(&backdrop));
    show_frame();
    CHECK((REG_DISPCNT & (DCNT_BG1 | DCNT_BG2 | DCNT_BG3)) == (DCNT_BG1 | DCNT_BG2 | DCNT_BG3));
    CHECK(REG_BGCNT[1] == (BG_CBB(1) | BG_SBB(28) | BG_PRIO(1)));
    CHECK(REG_BGCNT[2] == (BG_CBB(1) | BG_SBB(29) | BG_PRIO(2)));
    CHECK(REG_BGCNT[3] == (BG_CBB(1) | BG_SBB(30) | BG_PRIO(3)));
    CHECK(window_ok(&playfield));
    CHECK(window_ok(&foreground));
    CHECK(window_ok(&backdrop));
    unload_all();
}

static void small_steps_stream_rows_and_columns(void) {
    make_layers();
    map_load(&playfield);
    map_load(&foreground);
    map_load(&backdrop);
    camera_set(100, 100);
    show_frame();
    // Moves of 1 to 9 pixels in every direction, crossing tile and
    // screenblock-wrap boundaries; the screenblocks must be right every frame.
    static const s8 moves[][2] = {{1, 0},  {3, 0},   {8, 0},  {9, 1}, {0, 5},  {0, 8},  {-1, -1},
                                  {-7, 0}, {-8, -3}, {0, -9}, {4, 4}, {-5, 6}, {7, -7}, {2, 9}};
    bool ok = true;
    int x = camera_x(), y = camera_y();
    for (int round = 0; round < 6; round++) {
        for (u32 m = 0; m < sizeof(moves) / sizeof(moves[0]); m++) {
            int dx = moves[m][0], dy = moves[m][1];
            if (round & 1) { // and back
                dx = -dx;
                dy = -dy;
            }
            x += dx * (round + 1);
            y += dy * (round + 1);
            camera_set(x, y);
            show_frame();
            ok &= window_ok(&playfield) && window_ok(&foreground) && window_ok(&backdrop);
        }
    }
    CHECK(ok);
    // A long walk right and down, then back, one to three pixels a frame.
    for (int f = 0; f < 300 && ok; f++) {
        x += 1 + f % 3;
        y += f % 2;
        camera_set(x, y);
        show_frame();
        ok &= window_ok(&playfield) && window_ok(&foreground) && window_ok(&backdrop);
    }
    CHECK(ok);
    for (int f = 0; f < 300 && ok; f++) {
        x -= 1 + f % 3;
        y -= f % 2;
        camera_set(x, y);
        show_frame();
        ok &= window_ok(&playfield) && window_ok(&foreground) && window_ok(&backdrop);
    }
    CHECK(ok);
    unload_all();
}

static void big_jumps_redraw_the_window(void) {
    make_layers();
    map_load(&playfield);
    map_load(&backdrop);
    show_frame();
    static const int jumps[][2] = {{700, 400}, {0, 0}, {248, 0}, {248, 168}, {10, 170}, {784, 480}};
    bool ok = true;
    for (u32 k = 0; k < sizeof(jumps) / sizeof(jumps[0]); k++) {
        camera_set(jumps[k][0], jumps[k][1]);
        show_frame();
        ok &= window_ok(&playfield) && window_ok(&backdrop);
    }
    CHECK(ok);
    CHECK(camera_x() == PLAY_W * 16 - SCREEN_W && camera_y() == PLAY_H * 16 - SCREEN_H);
    unload_all();
}

static void parallax_layers_scroll_by_their_factor(void) {
    make_layers();
    map_load(&playfield);
    map_load(&backdrop); // half speed, wrapping
    camera_set(101, 37);
    show_frame();
    CHECK(serval_map_scroll(2) == (37u << 16 | 101u));
    CHECK(serval_map_scroll(3) == (18u << 16 | 50u));
    CHECK(window_ok(&backdrop));
    // A layer that doesn't wrap shows entry 0 outside its map.
    const MapLayer far = {.width = 4,
                          .height = 2,
                          .cells = play_cells,
                          .metatiles = metatiles,
                          .metatile_count = METATILES,
                          .bg = 3,
                          .scroll_factor = FX_ONE / 4};
    map_load(&far);
    camera_set(600, 400);
    show_frame();
    CHECK(serval_map_scroll(3) == (100u << 16 | 150u));
    CHECK(window_ok(&far));
    CHECK(se_mem[30][(100 / 8) * 32 + 150 / 8] == 0);
    unload_all();
}

static void changed_cells_are_redrawn(void) {
    make_layers();
    map_load(&playfield);
    camera_set(64, 32);
    show_frame();
    // Visible cell (5, 3): tiles (10-11, 6-7).
    u16 old = map_cell(5, 3);
    u16 cell = (u16)((old + 1) % METATILES);
    map_set_cell(5, 3, cell);
    CHECK(se_mem[29][6 * 32 + 10] == metatiles[old].se[0]); // not before frame_end
    show_frame();
    CHECK(se_mem[29][6 * 32 + 10] == metatiles[cell].se[0]);
    CHECK(se_mem[29][7 * 32 + 11] == metatiles[cell].se[3]);
    CHECK(window_ok(&playfield));
    map_set_cell(5, 3, old); // undone
    show_frame();
    CHECK(se_mem[29][6 * 32 + 10] == metatiles[old].se[0]);
    // A change outside the window shows when it scrolls in.
    map_set_cell(30, 3, cell);
    show_frame();
    CHECK(window_ok(&playfield));
    for (int x = 64; x <= 300; x += 6) {
        camera_set(x, 32);
        show_frame();
    }
    CHECK(window_ok(&playfield));
    CHECK(se_mem[29][6 * 32 + (60 & 31)] == metatiles[cell].se[0]);
    // Many changes in one frame (more than the redraw list holds).
    for (int k = 0; k < 2 * MAP_MAX_CHANGES; k++)
        map_set_cell(20 + k % 16, 2 + (k / 16) % 8, (u16)(k % METATILES));
    show_frame();
    CHECK(window_ok(&playfield));
    unload_all();
}

static void map_unload_hides_the_background(void) {
    make_layers();
    map_load(&playfield);
    map_load(&foreground);
    show_frame();
    map_unload(2);
    CHECK(REG_DISPCNT & DCNT_BG2); // until frame_end
    show_frame();
    CHECK(!(REG_DISPCNT & DCNT_BG2));
    CHECK(REG_DISPCNT & DCNT_BG1);
    CHECK(map_cell(1, 1) == 0); // the playfield is gone
    CHECK(window_ok(&foreground));
    unload_all();
    CHECK(!(REG_DISPCNT & (DCNT_BG1 | DCNT_BG2 | DCNT_BG3)));
}

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

// Cycle budgets only hold for optimized code: Debug builds (-O0) check
// correctness but not timing.
#ifdef __OPTIMIZE__
#define CHECK_TIMING(cond) CHECK(cond)
#else
#define CHECK_TIMING(cond) ((void)(cond))
#endif

// The worst VBlank work: a full redraw of all three layers. VBlank lasts 68
// scanlines (83,776 cycles), and the OAM copy and sound share it.
static void a_full_redraw_fits_in_vblank(void) {
    make_layers();
    map_load(&playfield);
    map_load(&foreground);
    map_load(&backdrop);
    camera_set(300, 200);
    u32 t0 = cycles();
    serval_map_prepare();
    u32 t1 = cycles();
    VBlankIntrWait();
    u32 t2 = cycles();
    serval_map_commit();
    u32 t3 = cycles();
    debug_log(text_format("map: full redraw of 3 layers: %u cycles before VBlank, %u in VBlank",
                          t1 - t0, t3 - t2));
    CHECK_TIMING(t3 - t2 < 30000);
    CHECK(window_ok(&playfield) && window_ok(&foreground) && window_ok(&backdrop));

    // Typical scrolling: a new row and column on every layer.
    camera_set(308, 208);
    t0 = cycles();
    serval_map_prepare();
    t1 = cycles();
    VBlankIntrWait();
    t2 = cycles();
    serval_map_commit();
    t3 = cycles();
    debug_log(text_format("map: one row and column on 3 layers: %u cycles before VBlank, %u in "
                          "VBlank",
                          t1 - t0, t3 - t2));
    CHECK(window_ok(&playfield) && window_ok(&foreground) && window_ok(&backdrop));
    unload_all();
}

static const u32 sprite_tiles[8];
static const SpriteAsset sprite = {.size = SPRITE_8x8, .tiles = sprite_tiles};
static const SpriteAsset* const sprite_table[1] = {&sprite};
static const u16 sprite_palette[16];
static const SpriteGroup sprite_group = {
    .palettes = sprite_palette, .sprite_count = 1, .palette_count = 1};

// The hardware sprite sys_render or sys_render_by_depth made of one entity:
// x in the low halfword, y in the high one.
static u32 rendered_at(bool by_depth) {
    frame_begin();
    if (by_depth)
        sys_render_by_depth();
    else
        sys_render();
    frame_end();
    if (oam_mem[0].attr0 & ATTR0_HIDE)
        return 0xFFFFFFFF;
    return (u32)(oam_mem[0].attr0 & ATTR0_Y_MASK) << 16 | (oam_mem[0].attr1 & ATTR1_X_MASK);
}

static void render_systems_subtract_the_camera(void) {
    sprite_table_set(sprite_table, 1);
    CHECK(sprite_group_load(&sprite_group));
    ecs_reset();
    u32 i = entity_index(entity_create(C_POS | C_SPR));
    pos_x[i] = FX(300);
    pos_y[i] = FX(90);
    for (int by_depth = 0; by_depth < 2; by_depth++) {
        camera_set(0, 0);
        CHECK(rendered_at(by_depth) == 0xFFFFFFFF); // x 300: off screen
        camera_set(250, 40);                        // no playfield: not clamped
        CHECK(rendered_at(by_depth) == (50u << 16 | 50u));
        camera_set(-10, 0);
        pos_x[i] = FX(20);
        CHECK(rendered_at(by_depth) == (90u << 16 | 30u));
        pos_x[i] = FX(300);
    }
    // sprite_draw takes screen coordinates: the camera doesn't apply.
    camera_set(250, 40);
    frame_begin();
    sprite_draw(0, 0, 12, 34, 0);
    frame_end();
    CHECK((oam_mem[0].attr1 & ATTR1_X_MASK) == 12 && (oam_mem[0].attr0 & ATTR0_Y_MASK) == 34);
    camera_set(0, 0);
    ecs_reset();
    sprite_table_set(NULL, 0);
    show_frame();
}

TEST_SUITE(gba_map_tests, "gba map",
           {"tileset_load copies tiles and palettes", tileset_load_copies_tiles_and_palettes},
           {"tileset_load rejects bad tilesets", tileset_load_rejects_bad_tilesets},
           {"map_load sets up the backgrounds", map_load_sets_up_the_backgrounds},
           {"small steps stream rows and columns", small_steps_stream_rows_and_columns},
           {"big jumps redraw the window", big_jumps_redraw_the_window},
           {"parallax layers scroll by their factor", parallax_layers_scroll_by_their_factor},
           {"changed cells are redrawn", changed_cells_are_redrawn},
           {"map_unload hides the background", map_unload_hides_the_background},
           {"a full redraw fits in VBlank", a_full_redraw_fits_in_vblank},
           {"render systems subtract the camera", render_systems_subtract_the_camera}, );
