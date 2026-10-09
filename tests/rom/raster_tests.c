// Tests for raster effects on the GBA (src/gba/raster.c): DMA 0 set up to
// copy a value per line at every horizontal blank, the values it copies, the
// backdrop read line by line as the hardware draws, the map streaming of what
// the lines show, the VBlank restart, and the warnings.

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/map.h"
#include "serval/math.h"
#include "serval/screen.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/gba/internal.h"

#ifdef SERVAL_DEBUG
#define WARNINGS(n) (n)
#else
#define WARNINGS(n) 0
#endif

// What DMA 0's control register reads while an effect is on: enabled, at
// HBlank, repeating, halfwords, source counting up, destination fixed, and
// one unit per line.
#define DMA0_CONTROL                                                                               \
    ((DMA_ENABLE | DMA_AT_HBLANK | DMA_REPEAT | DMA_16 | DMA_SRC_INC | DMA_DST_FIXED) >> 16)

static void show_frame(void) {
    frame_begin();
    frame_end();
}

static bool dma0_on(void) {
    return REG_DMA0CNT_H & (DMA_ENABLE >> 16);
}

// The VBlank handler libtonc's dispatcher calls, or NULL.
static fnptr vblank_handler(void) {
    for (u32 k = 0; __isr_table[k].flag; k++) {
        if (__isr_table[k].flag == IRQ_VBLANK)
            return __isr_table[k].isr;
    }
    return NULL;
}

// The backdrop color during each line of the next frame the hardware draws,
// read from palette RAM as the line starts: what HBlank DMA put there.
static void read_backdrop_lines(u16* out) {
    while (REG_VCOUNT < SCREEN_H) // into VBlank, if not there yet
        ;
    for (u32 y = 0; y < SCREEN_H; y++) {
        while (REG_VCOUNT != y)
            ;
        out[y] = *(vu16*)MEM_PAL; // volatile: read on each line
    }
}

// A sky gradient: dark blue at the top to orange at the horizon.
static SERVAL_EWRAM_BSS Color sky[SCREEN_H];

static void make_sky(void) {
    for (u32 y = 0; y < SCREEN_H; y++)
        sky[y] = color_mix(COLOR_RGB(16, 24, 96), COLOR_RGB(255, 160, 64), y * 256 / SCREEN_H);
}

static bool lines_match(const u16* lines, const Color* colors) {
    bool ok = true;
    for (u32 y = 0; y < SCREEN_H; y++)
        ok &= lines[y] == colors[y];
    return ok;
}

static void backdrop_per_line_by_dma0(void) {
    make_sky();
    Color plain = COLOR_RGB(8, 8, 8);
    screen_set_backdrop(plain);
    raster_backdrop(sky);
    CHECK(!dma0_on() && pal_bg_mem[0] == plain); // until frame_end()
    show_frame();
    CHECK(REG_DMA0CNT_H == DMA0_CONTROL);
    CHECK(vblank_handler() != NULL);
    CHECK(!(REG_IE & IRQ_HBLANK) && !(REG_DISPSTAT & DSTAT_HBL_IRQ)); // no HBlank interrupt
    const u16* lines = serval_raster_lines();
    CHECK(lines && lines_match(lines, sky) && lines[SCREEN_H] == sky[0]);
    CHECK(pal_bg_mem[0] == sky[0]); // line 0's, written in VBlank
    static SERVAL_EWRAM_BSS u16 seen[SCREEN_H];
    read_backdrop_lines(seen);
    CHECK(lines_match(seen, sky));

    // The table is read at every frame_end(): changing it animates the effect.
    for (u32 y = 0; y < SCREEN_H; y++)
        sky[y] = (Color)(sky[y] ^ 0x0421);
    read_backdrop_lines(seen);
    CHECK(!lines_match(seen, sky)); // not before frame_end()
    show_frame();
    read_backdrop_lines(seen);
    CHECK(lines_match(seen, sky));

    // A frame the game finishes late: the VBlank handler restarts DMA 0, so
    // the frames drawn meanwhile show the effect again.
    VBlankIntrWait();
    VBlankIntrWait();
    read_backdrop_lines(seen);
    CHECK(lines_match(seen, sky));
    raster_clear();
    show_frame();
    screen_set_backdrop(0);
}

// A table in ROM works like one in RAM: frame_end() copies it.
static const Color stripes[SCREEN_H] = {
    [0 ... 39] = 0x001F, [40 ... 79] = 0x03E0, [80 ... 119] = 0x7C00, [120 ... 159] = 0x7FFF};

static void a_table_in_rom_works(void) {
    raster_backdrop(stripes);
    show_frame();
    static SERVAL_EWRAM_BSS u16 seen[SCREEN_H];
    read_backdrop_lines(seen);
    CHECK(lines_match(seen, stripes));
    raster_clear();
    show_frame();
}

static void the_backdrop_comes_back(void) {
    make_sky();
    screen_set_backdrop(COLOR_RGB(255, 0, 0));
    raster_backdrop(sky);
    show_frame();
    // Set while the effect is on: remembered, not shown.
    screen_set_backdrop(COLOR_RGB(0, 255, 0));
    CHECK(pal_bg_mem[0] == sky[0]);
    static SERVAL_EWRAM_BSS u16 seen[SCREEN_H];
    read_backdrop_lines(seen);
    CHECK(lines_match(seen, sky));
    raster_clear();
    CHECK(dma0_on()); // until frame_end()
    show_frame();
    CHECK(!dma0_on() && serval_raster_lines() == NULL);
    CHECK(vblank_handler() == NULL);
    CHECK(pal_bg_mem[0] == COLOR_RGB(0, 255, 0));
    read_backdrop_lines(seen);
    bool plain = true;
    for (u32 y = 0; y < SCREEN_H; y++)
        plain &= seen[y] == COLOR_RGB(0, 255, 0);
    CHECK(plain);
    // And at once again, with the effect off.
    screen_set_backdrop(COLOR_RGB(0, 0, 255));
    CHECK(pal_bg_mem[0] == COLOR_RGB(0, 0, 255));
    // Another effect replacing it puts it back too.
    raster_backdrop(sky);
    show_frame();
    static const s16 none[SCREEN_H];
    raster_scroll(1, false, none);
    show_frame();
    CHECK(pal_bg_mem[0] == COLOR_RGB(0, 0, 255) && REG_DMA0CNT_H == DMA0_CONTROL);
    raster_clear();
    show_frame();
    CHECK(!dma0_on());
    screen_set_backdrop(0);
}

// tileset_set_colors() sets the backdrop too, as color 0 of palette 0
// (map.h): the effect's end puts back whichever color was set last, and a
// palette write to palette 0's other colors leaves the effect's lines alone.
static void palette_writes_set_the_backdrop_too(void) {
    static const Color teal = COLOR_RGB(0, 128, 128), blue = COLOR_RGB(0, 0, 255);
    static const Color gold = COLOR_RGB(255, 200, 0);
    static SERVAL_EWRAM_BSS u16 seen[SCREEN_H];
    make_sky();
    screen_set_backdrop(COLOR_RGB(255, 0, 0));
    raster_backdrop(sky);
    show_frame();
    tileset_set_colors(0, &teal, 1);
    show_frame();
    read_backdrop_lines(seen);
    CHECK(lines_match(seen, sky)); // remembered, not shown
    raster_clear();
    show_frame();
    CHECK(pal_bg_mem[0] == teal);

    // A palette write, then screen_set_backdrop() in the same frame: the
    // later one wins, as without the effect.
    raster_backdrop(sky);
    show_frame();
    tileset_set_colors(0, &teal, 1);
    screen_set_backdrop(blue);
    raster_clear();
    show_frame();
    CHECK(pal_bg_mem[0] == blue);

    // Palette 0's color 1, written while the effect is on.
    Color old = pal_bg_mem[1];
    raster_backdrop(sky);
    show_frame();
    tileset_set_colors(1, &gold, 1);
    show_frame();
    read_backdrop_lines(seen);
    CHECK(lines_match(seen, sky) && pal_bg_mem[1] == gold);
    raster_clear();
    show_frame();
    CHECK(pal_bg_mem[0] == blue);
    tileset_set_colors(1, &old, 1);
    screen_set_backdrop(0);
    show_frame();
}

// --- raster_scroll ------------------------------------------------------------

// Metatiles whose 64 entries all differ, and layers of them (as in
// map_tests.c): a playfield, a foreground and a small wrapping background.
#define METATILES 16
static SERVAL_EWRAM_BSS Metatile metatiles[METATILES];
#define PLAY_W 64
#define PLAY_H 40
static SERVAL_EWRAM_BSS u16 play_cells[PLAY_W * PLAY_H];
static SERVAL_EWRAM_BSS u16 front_cells[PLAY_W * PLAY_H];
static const u16 strip_cells[4 * 2] = {1, 2, 3, 4, 5, 6, 7, 8};
static SERVAL_EWRAM_BSS MapLayer playfield, foreground, strip;

static void make_layers(void) {
    for (u32 k = 0; k < METATILES; k++) {
        for (u32 c = 0; c < 4; c++)
            metatiles[k].se[c] = MAP_SE(4 * k + c + 1, k % 15, k & 1 ? MAP_SE_FLIP_H : 0);
        metatiles[k].collision = MAP_EMPTY;
    }
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
    foreground = (MapLayer){.width = PLAY_W,
                            .height = PLAY_H,
                            .cells = front_cells,
                            .metatiles = metatiles,
                            .metatile_count = METATILES,
                            .bg = 1};
    // 4 x 2 metatiles, wrapping: VRAM holds all of it, any offsets work.
    strip = (MapLayer){.width = 4,
                       .height = 2,
                       .cells = strip_cells,
                       .metatiles = metatiles,
                       .metatile_count = METATILES,
                       .bg = 3,
                       .flags = MAP_LAYER_WRAP,
                       .scroll_factor = FX_ONE / 2};
}

static int floor_mod(int v, int m) {
    int r = v % m;
    return r < 0 ? r + m : r;
}

static int scroll_x(const MapLayer* layer) {
    FIXED f = layer->scroll_factor ? layer->scroll_factor : FX_ONE;
    return (camera_x() * f) >> FX_SHIFT;
}

static int scroll_y(const MapLayer* layer) {
    FIXED f = layer->scroll_factor ? layer->scroll_factor : FX_ONE;
    return (camera_y() * f) >> FX_SHIFT;
}

static u16 expected_entry(const MapLayer* layer, int tx, int ty) {
    int mx = tx >> 1, my = ty >> 1;
    if (layer->flags & MAP_LAYER_WRAP) {
        mx = floor_mod(mx, layer->width);
        my = floor_mod(my, layer->height);
    } else if (mx < 0 || my < 0 || mx >= layer->width || my >= layer->height) {
        return 0;
    }
    u32 cell = layer->cells[my * layer->width + mx];
    return layer->metatiles[cell].se[(ty & 1) * 2 + (tx & 1)];
}

// True if the values DMA 0 copies are the layer's scroll plus the offsets,
// and its screenblock holds the right entry for every tile every line shows.
static bool lines_ok(const MapLayer* layer, bool vertical, const s16* offsets) {
    int sx = scroll_x(layer), sy = scroll_y(layer);
    const u16* lines = serval_raster_lines();
    if (!lines || lines[SCREEN_H] != lines[0])
        return false;
    const u16* sb = se_mem[27 + layer->bg];
    for (int y = 0; y < SCREEN_H; y++) {
        int x0 = vertical ? sx : sx + offsets[y];
        int row = vertical ? sy + y + offsets[y] : sy + y;
        if (lines[y] != ((u32)((vertical ? sy : sx) + offsets[y]) & 0x1FF))
            return false;
        int ty = row >> 3;
        for (int tx = x0 >> 3; tx <= (x0 + SCREEN_W - 1) >> 3; tx++) {
            if (sb[(ty & 31) * 32 + (tx & 31)] != expected_entry(layer, tx, ty))
                return false;
        }
    }
    return true;
}

// A ripple: a sine wave of `amplitude` pixels and `period` lines, `phase`
// turns along.
static void ripple(s16* offsets, int amplitude, u32 period, u32 phase) {
    for (u32 y = 0; y < SCREEN_H; y++) {
        u16 angle = (u16)(phase + y * 65536u / period);
        offsets[y] = (s16)((fx_sin(angle) * amplitude) >> FX_SHIFT);
    }
}

static void unload_all(void) {
    raster_clear();
    map_unload(1);
    map_unload(2);
    map_unload(3);
    camera_set(0, 0);
    show_frame();
}

static void scroll_values_per_line(void) {
    make_layers();
    map_load(&playfield);
    camera_set(203, 117);
    static SERVAL_EWRAM_BSS s16 wave[SCREEN_H];
    ripple(wave, 4, 32, 0);
    raster_scroll(2, false, wave);
    CHECK(serval_raster_lines() == NULL); // until frame_end()
    show_frame();
    CHECK(REG_DMA0CNT_H == DMA0_CONTROL);
    CHECK(lines_ok(&playfield, false, wave));
    // The layer's own scroll register stays as the map sets it (frame_end()
    // writes line 0's value over it), and the camera doesn't move.
    CHECK(serval_map_scroll(2) == (117u << 16 | 203u));
    CHECK(camera_x() == 203 && camera_y() == 117);
    // A moving ripple while the camera moves: each frame's values, and every
    // tile the lines show streamed.
    bool ok = true;
    for (u32 f = 0; f < 120 && ok; f++) {
        ripple(wave, 4, 32, f * 1024);
        camera_set(203 + (int)f * 3 - (int)(f / 40) * 200, 117 + (int)(f % 23) - 11);
        show_frame();
        ok &= lines_ok(&playfield, false, wave);
    }
    CHECK(ok);
    // Vertical, on another background: replaces the first effect.
    map_load(&foreground);
    raster_scroll(1, true, wave);
    show_frame();
    CHECK(lines_ok(&foreground, true, wave));
    CHECK(serval_map_scroll(2) ==
          ((u32)(scroll_y(&playfield) & 0x1FF) << 16 | (u32)(scroll_x(&playfield) & 0x1FF)));
    unload_all();
}

// What the lines show is kept in VRAM: offsets within 9 pixels of each other
// horizontally, rows within 249 pixels vertically (offsets within 89, or a
// mirror image), any offsets on a small wrapping layer; and the window goes
// back to the screen's when the effect ends.
static void the_streaming_follows_the_lines(void) {
    make_layers();
    map_load(&playfield);
    map_load(&strip);
    static SERVAL_EWRAM_BSS s16 offsets[SCREEN_H];
    u32 warnings = debug_warning_count(); // at the limits: no warning
    bool ok = true;
    // -4 to 5 at every alignment, both ways round.
    for (int f = 0; f < 64 && ok; f++) {
        for (u32 y = 0; y < SCREEN_H; y++)
            offsets[y] = (s16)((y & 1) == (u32)(f & 1) ? -4 : 5);
        camera_set(100 + f, 60 + f / 2);
        raster_scroll(2, false, offsets);
        show_frame();
        ok &= lines_ok(&playfield, false, offsets);
    }
    CHECK(ok);
    // Vertically: offsets 0 to 89, and a mirror image (offsets 0 to -159,
    // rows within the screen's), at every alignment.
    for (int f = 0; f < 32 && ok; f++) {
        for (u32 y = 0; y < SCREEN_H; y++)
            offsets[y] = (s16)(f & 1 ? 89 * (int)y / (SCREEN_H - 1) : SCREEN_H - 1 - 2 * (int)y);
        camera_set(150 + f, 200 - f);
        raster_scroll(2, true, offsets);
        show_frame();
        ok &= lines_ok(&playfield, true, offsets);
    }
    CHECK(ok);
    // The small wrapping layer: any offsets, both ways.
    for (int f = 0; f < 16 && ok; f++) {
        for (u32 y = 0; y < SCREEN_H; y++)
            offsets[y] = (s16)((int)y * (f - 8) * 97);
        camera_set(f * 13, f * 5);
        raster_scroll(3, f & 1, offsets);
        show_frame();
        ok &= lines_ok(&strip, f & 1, offsets);
    }
    CHECK(ok);
    CHECK(debug_warning_count() == warnings);
    // Ended: the playfield's window is the screen's again, and streams as
    // the camera moves.
    raster_clear();
    for (int f = 0; f < 40 && ok; f++) {
        camera_set(300 - f * 7, 100 + f * 3);
        show_frame();
        int sx = scroll_x(&playfield), sy = scroll_y(&playfield);
        const u16* sb = se_mem[29];
        for (int ty = sy >> 3; ty <= (sy + SCREEN_H - 1) >> 3; ty++) {
            for (int tx = sx >> 3; tx <= (sx + SCREEN_W - 1) >> 3; tx++)
                ok &= sb[(ty & 31) * 32 + (tx & 31)] == expected_entry(&playfield, tx, ty);
        }
    }
    CHECK(ok && !dma0_on());
    unload_all();
}

static void spread_too_far_warns(void) {
    make_layers();
    map_load(&playfield);
    map_load(&strip);
    camera_set(64, 64);
    static SERVAL_EWRAM_BSS s16 offsets[SCREEN_H];
    u32 before = debug_warning_count();
    // Within the limits, and any spread on the small wrapping layer: quiet.
    for (u32 y = 0; y < SCREEN_H; y++)
        offsets[y] = (s16)(y & 1 ? 9 : 0);
    raster_scroll(2, false, offsets);
    show_frame();
    for (u32 y = 0; y < SCREEN_H; y++)
        offsets[y] = (s16)(y & 1 ? -89 : 0);
    raster_scroll(2, true, offsets);
    show_frame();
    for (u32 y = 0; y < SCREEN_H; y++)
        offsets[y] = (s16)(y & 1 ? 1000 : -1000);
    raster_scroll(3, false, offsets);
    show_frame();
    raster_scroll(3, true, offsets);
    show_frame();
    CHECK(debug_warning_count() == before);
    // One pixel more: a warning per axis, once.
    for (u32 y = 0; y < SCREEN_H; y++)
        offsets[y] = (s16)(y & 1 ? 10 : 0);
    raster_scroll(2, false, offsets);
    show_frame();
    CHECK(debug_warning_count() == before + WARNINGS(1));
    show_frame();
    CHECK(debug_warning_count() == before + WARNINGS(1));
    // Rows 250 pixels apart: line 0 shows row 0, line 159 row 249.
    for (u32 y = 0; y < SCREEN_H; y++)
        offsets[y] = (s16)(y == SCREEN_H - 1 ? 90 : 0);
    raster_scroll(2, true, offsets);
    show_frame();
    CHECK(debug_warning_count() == before + WARNINGS(2));
    show_frame();
    CHECK(debug_warning_count() == before + WARNINGS(2));
    // Past the limit, VRAM keeps the middle of what the lines show: lines 40
    // pixels apart still show the right tiles but for their edges.
    for (u32 y = 0; y < SCREEN_H; y++)
        offsets[y] = (s16)(y & 1 ? 40 : 0);
    raster_scroll(2, false, offsets);
    show_frame();
    int sx = scroll_x(&playfield), sy = scroll_y(&playfield);
    bool middles = true;
    for (int y = 0; y < SCREEN_H; y++) {
        int ty = (sy + y) >> 3, x0 = sx + offsets[y];
        for (int tx = (x0 + 24) >> 3; tx <= (x0 + SCREEN_W - 24) >> 3; tx++)
            middles &= se_mem[29][(ty & 31) * 32 + (tx & 31)] == expected_entry(&playfield, tx, ty);
    }
    CHECK(middles);
    unload_all();
}

static void bad_calls_are_ignored(void) {
    make_sky();
    raster_backdrop(sky);
    show_frame();
    static const s16 none[SCREEN_H];
    u32 before = debug_warning_count();
    raster_scroll(0, false, none);
    CHECK(debug_warning_count() == before + WARNINGS(1));
    raster_scroll(4, false, none);
    CHECK(debug_warning_count() == before + WARNINGS(1)); // once
    raster_scroll(2, false, NULL);
    CHECK(debug_warning_count() == before + WARNINGS(2));
    raster_backdrop(NULL);
    CHECK(debug_warning_count() == before + WARNINGS(3));
    raster_backdrop((const Color*)4);
    CHECK(debug_warning_count() == before + WARNINGS(3));
    show_frame();
    const u16* lines = serval_raster_lines(); // the effect set stays
    CHECK(lines && lines_match(lines, sky));
    raster_clear();
    raster_clear();
    CHECK(debug_warning_count() == before + WARNINGS(3));
    show_frame();
    CHECK(!dma0_on());
    screen_set_backdrop(0);
}

static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

#ifdef __OPTIMIZE__
#define CHECK_TIMING(cond) CHECK(cond)
#else
#define CHECK_TIMING(cond) ((void)(cond))
#endif

// One frame_end()'s raster work, before VBlank and in it.
static void time_raster(u32* prepare, u32* commit) {
    u32 t0 = cycles();
    serval_raster_prepare_hook();
    u32 t1 = cycles();
    VBlankIntrWait();
    u32 t2 = cycles();
    serval_raster_commit_hook();
    u32 t3 = cycles();
    *prepare = t1 - t0;
    *commit = t3 - t2;
}

static void costs(void) {
    make_layers();
    make_sky();
    map_load(&playfield);
    static const s16 table[SCREEN_H] = {[0 ... 79] = 3, [80 ... 159] = -3};
    static SERVAL_EWRAM_BSS s16 wave[SCREEN_H];
    ripple(wave, 4, 40, 0);
    u32 prepare, commit;
    raster_scroll(2, false, table);
    show_frame();
    time_raster(&prepare, &commit);
    debug_log(text_format("raster: scroll from ROM: %u cycles before VBlank, %u in VBlank", prepare,
                          commit));
    CHECK_TIMING(prepare < 6000 && commit < 400);
    raster_scroll(2, true, wave);
    show_frame();
    time_raster(&prepare, &commit);
    debug_log(text_format("raster: vertical scroll from RAM: %u cycles before VBlank, %u in "
                          "VBlank",
                          prepare, commit));
    CHECK_TIMING(prepare < 6000 && commit < 400);
    raster_backdrop(sky);
    show_frame();
    time_raster(&prepare, &commit);
    debug_log(
        text_format("raster: backdrop: %u cycles before VBlank, %u in VBlank", prepare, commit));
    CHECK_TIMING(prepare < 2000 && commit < 400);
    unload_all();
    screen_set_backdrop(0);
}

TEST_SUITE(gba_raster_tests, "gba raster",
           {"backdrop per line by DMA 0", backdrop_per_line_by_dma0},
           {"a table in ROM works", a_table_in_rom_works},
           {"the backdrop comes back", the_backdrop_comes_back},
           {"palette writes set the backdrop too", palette_writes_set_the_backdrop_too},
           {"scroll values per line", scroll_values_per_line},
           {"the streaming follows the lines", the_streaming_follows_the_lines},
           {"spread too far warns", spread_too_far_warns},
           {"bad calls are ignored", bad_calls_are_ignored}, {"costs", costs});
