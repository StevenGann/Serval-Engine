// Raster effects (screen.h): a value per scanline, which DMA channel 0 copies
// into a background's scroll register or the backdrop color at every
// horizontal blank. See docs/runtime-systems.md#raster-effects.
//
// The game's table is read at every frame_end(). Before VBlank, as CPU work of
// the frame, it is copied into the back one of two buffers in EWRAM (DMA 0 can
// read only internal memory, so a table in ROM works like one in RAM), with
// the layer's scroll added for raster_scroll(). In VBlank the buffers swap,
// line 0's value is written to the register, and DMA 0 restarts on entry 1 of
// the new front buffer, copying one entry per line. HBlank DMA runs after
// each of lines 0-159, the last time as VBlank begins, so a buffer has
// SCREEN_H + 1 entries: the last, a copy of line 0's, is what the register
// holds through VBlank.
//
// The DMA's source moves on by an entry per line and never goes back by
// itself. So while an effect is on, the engine's VBlank handler restarts it
// at every VBlank too: a frame the game doesn't finish in time (frame_end()
// reaching VBlank late) shows the effect again, not what lies past the buffer.
//
// raster_scroll() also tells the map streaming (map.c) which layer pixels its
// lines show, so that it keeps them all in VRAM (serval_map_span).
//
// Web builds run this unchanged: the renderer (src/web/ppu.c) does the HBlank
// DMA's transfers between the lines it draws.

#include "serval/map.h"
#include "serval/screen.h"

#include <tonc.h>

#include "../core/map_internal.h"
#include "../core/warn.h"
#include "internal.h"

#define ENTRIES (SCREEN_H + 1)
// The most layer pixels the lines can show along an axis whatever their
// alignment: they touch at most (249 + 7) / 8 = 32 tiles, the screenblock's
// ring. Offsets within 9 of each other horizontally (240 + 9), and within 89
// vertically (160 + 89), always fit.
#define MAX_SPAN 249

#define DMA0_HBLANK                                                                                \
    (DMA_ENABLE | DMA_AT_HBLANK | DMA_REPEAT | DMA_16 | DMA_SRC_INC | DMA_DST_FIXED | 1)
#define BACKDROP_REGISTER ((vu16*)MEM_PAL)

// Out of line, and one copy: GCC would otherwise make one per constant
// argument (clang, for the web, doesn't clone, nor know noclone).
#ifdef SERVAL_GBA
#define ONE_COPY __attribute__((noinline, noclone))
#else
#define ONE_COPY __attribute__((noinline))
#endif

// The effect set, which frame_end() applies: fill() writes its SCREEN_H
// values and returns the register they go to (NULL: no effect).
static vu16* (*fill)(u16* out);
static const void* table;
static u32 scroll_bg;
static bool scroll_vertical;
// Forgets raster_scroll()'s span: set by raster_scroll(), so that games
// without it don't link the map's state.
static void (*forget_span)(void);

// Word-aligned, each (an even number of entries), so that memcpy16() copies a
// backdrop table a word at a time.
static SERVAL_EWRAM_BSS u16 buffers[2][ENTRIES + 1] ALIGN4;
static u32 back;           // the buffer prepared before VBlank
static vu16* prepared;     // where its values go, or NULL
static vu16* active;       // where DMA 0 copies the front buffer's, or NULL
static const u16* current; // the front buffer

#ifdef SERVAL_DEBUG
enum { W_SCROLL_BG, W_SCROLL_TABLE, W_SPREAD_X, W_SPREAD_Y, W_BACKDROP_TABLE };
static u32 warned;
#define WARN_ONCE(kind, ...)                                                                       \
    do {                                                                                           \
        if (!(warned & (1u << (kind)))) {                                                          \
            warned |= 1u << (kind);                                                                \
            SERVAL_WARN(__VA_ARGS__);                                                              \
        }                                                                                          \
    } while (0)
#else
#define WARN_ONCE(kind, ...) ((void)0)
#endif

// Line 0's value, and DMA 0 started over on the others.
static void restart(void) {
    REG_DMA0CNT = 0;
    *active = current[0];
    REG_DMA0SAD = (u32)(uintptr_t)&current[1];
    REG_DMA0DAD = (u32)(uintptr_t)active;
    REG_DMA0CNT = DMA0_HBLANK;
}

// The engine's VBlank handler while an effect is on.
static void vblank(void) {
    if (active)
        restart();
}

// Before VBlank: the frame's values, in the back buffer.
static void prepare(void) {
    if (!fill) {
        prepared = NULL;
        return;
    }
    u16* out = buffers[back];
    prepared = fill(out);
    out[SCREEN_H] = out[0];
}

// In VBlank (flush step 7, after the palette writes and the map's registers,
// whose values line 0's replaces): the prepared values take over, and when
// raster_backdrop() ends, the backdrop gets its one color back, the one last
// set (serval_backdrop: screen_set_backdrop(), or tileset_set_colors() of
// color 0).
static void commit(void) {
    vu16* was = active;
    if (!was && !prepared)
        return;
    REG_DMA0CNT = 0;
    active = NULL;
    if (was == BACKDROP_REGISTER && prepared != BACKDROP_REGISTER)
        pal_bg_mem[0] = serval_backdrop;
    serval_backdrop_raster = prepared == BACKDROP_REGISTER;
    if (!prepared) {
        serval_vblank_raster = NULL; // the VBlank handler's part (core.c)
        serval_vblank_update();
        return;
    }
    current = buffers[back];
    back ^= 1;
    active = prepared;
    restart();
    if (!was) {
        serval_vblank_raster = vblank; // after Maxmod's part, if a bank is registered
        serval_vblank_update();
    }
}

static void attach(void) {
    serval_raster_prepare_hook = prepare;
    serval_raster_commit_hook = commit;
}

// --- raster_scroll ------------------------------------------------------------

// Whether VRAM holds the whole layer along an axis of `size` metatiles: it
// wraps, and its 2 x size tiles divide the ring's 32.
static bool whole_in_ring(const MapLayer* layer, u32 size) {
    return (layer->flags & MAP_LAYER_WRAP) && size && 16 % size == 0;
}

static void clear_spans(void) {
    serval_map_spans = 0;
}

// The scroll register values for the lines, out[y] = base + offsets[y]
// (9 bits), and the least and greatest of offsets[y] + y * step.
//
// ARM code in IWRAM (132 bytes; noclone keeps it one copy), like map.c's
// fill loops: as Thumb code in ROM it took about 55 cycles a line, 8,700 a
// frame, and here about 30.
static SERVAL_IWRAM_TEXT ONE_COPY void scroll_lines(u16* out, const s16* offsets, u32 base,
                                                    int step, int* lo, int* hi) {
    int least = offsets[0], most = least, row = 0;
    for (u32 y = 0; y < SCREEN_H; y++, row += step) {
        int v = offsets[y], at = v + row;
        least = at < least ? at : least;
        most = at > most ? at : most;
        out[y] = (u16)((base + (u32)v) & 0x1FF);
    }
    *lo = least;
    *hi = most;
}

static vu16* fill_scroll(u16* out) {
    const u32 bg = scroll_bg;
    const s16* offsets = table;
    const MapLayer* layer = serval_map_layers[bg];
    const u32 sx = layer ? (u32)serval_map_layer_x(layer) : 0;
    const u32 sy = layer ? (u32)serval_map_layer_y(layer) : 0;
    ServalMapSpan span = {0, 0, 0, 0};
    int lo, hi;
    if (!scroll_vertical) {
        // Line y shows layer x from sx + offsets[y] to sx + offsets[y] + 239.
        scroll_lines(out, offsets, sx, 0, &lo, &hi);
        span.x_lo = lo;
        span.x_hi = hi;
        if (layer && hi - lo + SCREEN_W > MAX_SPAN && !whole_in_ring(layer, layer->width))
            WARN_ONCE(W_SPREAD_X,
                      "raster_scroll: background %u's offsets are %d pixels apart in one frame, "
                      "but VRAM holds 249 pixels of a layer's width, enough for offsets within "
                      "9 of each other; the lines furthest out show wrong tiles at their edges",
                      bg, hi - lo);
    } else {
        // Line y shows layer row sy + y + offsets[y].
        scroll_lines(out, offsets, sy, 1, &lo, &hi);
        span.y_lo = lo;
        span.y_hi = hi - (SCREEN_H - 1);
        if (layer && hi - lo + 1 > MAX_SPAN && !whole_in_ring(layer, layer->height))
            WARN_ONCE(W_SPREAD_Y,
                      "raster_scroll: background %u's lines show layer rows %d pixels apart in "
                      "one frame, but VRAM holds 249 (offsets within 89 of each other always "
                      "fit); the lines furthest out show wrong tiles at their edges",
                      bg, hi - lo + 1);
    }
    serval_map_span[bg] = span;
    serval_map_spans = 1u << bg;
    // BGxHOFS, or BGxVOFS 2 bytes later.
    return (vu16*)(REG_BASE + 0x10 + bg * 4 + (scroll_vertical ? 2 : 0));
}

void raster_scroll(u32 bg, bool vertical, const s16* offsets) {
    if (bg < 1 || bg > 3) {
        WARN_ONCE(W_SCROLL_BG,
                  "raster_scroll(%u, ...): raster scrolling moves map layers, on backgrounds "
                  "1-3; ignored",
                  bg);
        return;
    }
    if (!serval_plausible_pointer(offsets)) {
        WARN_ONCE(W_SCROLL_TABLE,
                  "raster_scroll: the offsets table is NULL or not a valid pointer; ignored");
        return;
    }
    table = offsets;
    scroll_bg = bg;
    scroll_vertical = vertical;
    fill = fill_scroll;
    forget_span = clear_spans;
    attach();
}

// --- raster_backdrop and raster_clear -------------------------------------------

static vu16* fill_backdrop(u16* out) {
    memcpy16(out, table, SCREEN_H);
    return BACKDROP_REGISTER;
}

void raster_backdrop(const Color* colors) {
    if (!serval_plausible_pointer(colors)) {
        WARN_ONCE(W_BACKDROP_TABLE,
                  "raster_backdrop: the colors table is NULL or not a valid pointer; ignored");
        return;
    }
    if (forget_span)
        forget_span();
    table = colors;
    fill = fill_backdrop;
    attach();
}

void raster_clear(void) {
    if (forget_span)
        forget_span();
    fill = NULL;
}

const u16* serval_raster_lines(void) {
    return active ? current : NULL;
}
