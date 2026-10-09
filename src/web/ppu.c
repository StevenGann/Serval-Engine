// Software renderer for the web backend: draws what the GBA's video hardware
// would show for the state in VRAM, palette RAM, OAM and the I/O registers.
//
// It renders the whole frame from the state at the time of the call (the web
// glue calls it at VBlank), one scanline at a time, following GBATEK. Writes
// made while the hardware draws are therefore not seen, except HBlank DMA's
// (raster effects): between lines, the DMA channels set to start at HBlank
// copy as on the GBA, into the renderer's copy of the I/O registers and
// palette RAM, so each line is drawn from the state the hardware would have
// then. Each frame they start from their source and destination registers, as
// if restarted in VBlank (the engine restarts DMA 0 every VBlank); writes to
// other memory than I/O and palette RAM are dropped. HBlank interrupts are not
// emulated. The affine background reference points advance by PB/PD per line
// from BGxX/BGxY as at the start of a frame (a write between lines doesn't
// reload them).
//
// Supported: forced blank; modes 0-5 (regular backgrounds, affine backgrounds
// with wraparound, bitmap modes with page select); objects (regular and
// affine, double size, 4bpp/8bpp, 1D/2D mapping, flips, wrapping, the
// per-scanline object cycle budget, semi-transparent objects); windows 0, 1,
// outside and the object window; color special effects (alpha blending,
// brightness increase and decrease). Matched pixel for pixel against mGBA
// 0.10.5 on the engine's examples and on randomized states, except where mGBA
// knowingly differs from GBATEK: its blend arithmetic (see blend()), text
// backgrounds in window segments narrower than a tile, effects in the object
// window, and brightness applied to a second target under a semi-transparent
// object. Background tiles that would lie in object VRAM are transparent.
// HBlank DMA (above; raster:) follows GBATEK: lines 0-159 each end with one
// round of copies, channel 0 first (in mGBA, tests/rom/raster_tests.c reads
// the per-line backdrop colors this gives).
//
// Not supported: mosaic (ignored: drawn as if off), the green swap register,
// and the hardware's behavior for prohibited settings (modes 6-7 show only the
// backdrop and objects; object shape 3 draws nothing).

#include "web.h"

// I/O register offsets, in halfwords (byte offset / 2). Names follow libtonc.
#define REG_DISPCNT (0x00 / 2)
#define REG_BG0CNT (0x08 / 2)
#define REG_BG0HOFS (0x10 / 2)
#define REG_BG0VOFS (0x12 / 2)
#define REG_BG2PA (0x20 / 2) // BG3's affine registers follow 0x10 bytes later
#define REG_BG2X (0x28 / 2)
#define REG_BG2Y (0x2C / 2)
#define REG_WIN0H (0x40 / 2)
#define REG_WIN1H (0x42 / 2)
#define REG_WIN0V (0x44 / 2)
#define REG_WIN1V (0x46 / 2)
#define REG_WININ (0x48 / 2)
#define REG_WINOUT (0x4A / 2)
#define REG_BLDCNT (0x50 / 2)
#define REG_BLDALPHA (0x52 / 2)
#define REG_BLDY (0x54 / 2)
#define REG_DMA0SAD (0xB0 / 2) // DMA 1-3's registers follow, 12 bytes apart
#define DMA_REGS (12 / 2)

// DISPCNT bits.
#define DCNT_MODE_MASK 0x0007
#define DCNT_PAGE 0x0010
#define DCNT_OAM_HBL 0x0020
#define DCNT_OBJ_1D 0x0040
#define DCNT_BLANK 0x0080
#define DCNT_BG0 0x0100
#define DCNT_OBJ 0x1000
#define DCNT_WIN0 0x2000
#define DCNT_WIN1 0x4000
#define DCNT_WINOBJ 0x8000

// BGxCNT bits.
#define BG_PRIO_MASK 0x0003
#define BG_CBB_SHIFT 2
#define BG_8BPP 0x0080
#define BG_SBB_SHIFT 8
#define BG_WRAP 0x2000
#define BG_SIZE_SHIFT 14

// Screen entry bits (regular backgrounds).
#define SE_ID_MASK 0x03FF
#define SE_HFLIP 0x0400
#define SE_VFLIP 0x0800
#define SE_PALBANK_SHIFT 12

// OAM attribute bits.
#define ATTR0_Y_MASK 0x00FF
#define ATTR0_AFF 0x0100
#define ATTR0_HIDE 0x0200 // without ATTR0_AFF; with it, double size
#define ATTR0_AFF_DBL_BIT 0x0200
#define ATTR0_MODE_SHIFT 10
#define ATTR0_8BPP 0x2000
#define ATTR0_SHAPE_SHIFT 14
#define ATTR1_X_MASK 0x01FF
#define ATTR1_AFF_ID_SHIFT 9
#define ATTR1_HFLIP 0x1000
#define ATTR1_VFLIP 0x2000
#define ATTR1_SIZE_SHIFT 14
#define ATTR2_ID_MASK 0x03FF
#define ATTR2_PRIO_SHIFT 10
#define ATTR2_PALBANK_SHIFT 12

#define OBJ_MODE_SEMI 1
#define OBJ_MODE_WINDOW 2

// Window control bits (one byte each of WININ and WINOUT): layers 0-4 (BG0-3,
// OBJ) and color special effects.
#define WIN_BLD 0x20
#define WIN_ALL 0x3F

// BLDCNT: first target bits 0-5, second target bits 8-13 (BG0-3, OBJ,
// backdrop), effect in bits 6-7.
#define BLD_MODE_SHIFT 6
#define BLD_BOT_SHIFT 8
#define BLD_MODE_OFF 0
#define BLD_MODE_ALPHA 1
#define BLD_MODE_WHITE 2
#define BLD_MODE_BLACK 3

// DMAxCNT_H bits (raster:).
#define DMA_DST_SHIFT 5 // 0 increment, 1 decrement, 2 fixed, 3 increment and reload
#define DMA_SRC_SHIFT 7 // 0 increment, 1 decrement, 2 fixed
#define DMA_DST_RELOAD 3
#define DMA_REPEAT 0x0200
#define DMA_32 0x0400
#define DMA_TIMING_SHIFT 12
#define DMA_AT_HBLANK 2
#define DMA_ENABLE 0x8000

#define IO_ADDRESS 0x04000000u
#define PALETTE_ADDRESS 0x05000000u

#define LAYER_OBJ 4
#define LAYER_BACKDROP 5

#define OBJ_VRAM 0x10000     // object tiles: charblocks 4-5
#define BG_VRAM_SIZE 0x10000 // background tiles in object VRAM are transparent
#define OBJ_PALETTE 256

// Object rendering cycles available per scanline (GBATEK: 1210, or 954 when
// DISPCNT's "H-Blank interval free" bit is set). Objects past the budget are
// not drawn on that line.
#define OBJ_CYCLES 1210
#define OBJ_CYCLES_HBLANK_FREE 954

// A layer pixel: the BGR555 color with OPAQUE set, or 0 if transparent.
#define OPAQUE 0x8000

static const u8 obj_sizes[4][4][2] = {
    {{8, 8}, {16, 16}, {32, 32}, {64, 64}}, // square
    {{16, 8}, {32, 8}, {32, 16}, {64, 32}}, // wide
    {{8, 16}, {8, 32}, {16, 32}, {32, 64}}, // tall
    {{0, 0}, {0, 0}, {0, 0}, {0, 0}},       // prohibited
};

// One scanline's layers before composition.
typedef struct {
    u16 bg[4][WEB_SCREEN_W];
    u16 obj[WEB_SCREEN_W];       // frontmost object pixel
    u8 obj_prio[WEB_SCREEN_W];   // its priority; 4 = no object pixel yet
    u8 obj_semi[WEB_SCREEN_W];   // semi-transparent
    u8 obj_window[WEB_SCREEN_W]; // inside the object window
    u8 window[WEB_SCREEN_W];     // WIN_ALL-style enable bits per pixel
} Line;

static u16 vram16(const u8* vram, u32 addr) {
    return (u16)(vram[addr] | (vram[addr + 1] << 8));
}

static u16 palette_color(const WebVideoMemory* mem, u32 index) {
    return (u16)((mem->palette[index] & 0x7FFF) | OPAQUE);
}

// --- Backgrounds ---------------------------------------------------------

static void draw_regular_bg(const WebVideoMemory* mem, u32 bg, u32 y, u16* out) {
    u16 cnt = mem->io[REG_BG0CNT + bg];
    u32 char_base = ((cnt >> BG_CBB_SHIFT) & 3u) * 0x4000u;
    u32 screen_base = ((cnt >> BG_SBB_SHIFT) & 31u) * 0x800u;
    u32 size = (u32)cnt >> BG_SIZE_SHIFT;
    u32 width_mask = (size & 1u) ? 511u : 255u;
    u32 height_mask = (size & 2u) ? 511u : 255u;
    u32 blocks_per_row = (size & 1u) ? 2u : 1u;
    u32 hofs = mem->io[REG_BG0HOFS + bg * 2] & 0x1FFu;
    u32 vofs = mem->io[REG_BG0VOFS + bg * 2] & 0x1FFu;
    u32 py = (y + vofs) & height_mask;

    for (u32 x = 0; x < WEB_SCREEN_W; x++) {
        u32 px = (x + hofs) & width_mask;
        u32 block = (px >> 8) + (py >> 8) * blocks_per_row;
        u32 entry_addr =
            screen_base + block * 0x800u + (((py >> 3) & 31u) * 32u + ((px >> 3) & 31u)) * 2u;
        u16 entry = vram16(mem->vram, entry_addr);
        u32 tx = px & 7u, ty = py & 7u;
        if (entry & SE_HFLIP)
            tx = 7u - tx;
        if (entry & SE_VFLIP)
            ty = 7u - ty;
        u32 tile = entry & SE_ID_MASK;
        u32 index;
        if (cnt & BG_8BPP) {
            u32 addr = char_base + tile * 64u + ty * 8u + tx;
            index = addr < BG_VRAM_SIZE ? mem->vram[addr] : 0u;
        } else {
            u32 addr = char_base + tile * 32u + ty * 4u + tx / 2u;
            index = addr < BG_VRAM_SIZE ? (mem->vram[addr] >> ((tx & 1u) * 4u)) & 15u : 0u;
            if (index)
                index += (u32)(entry >> SE_PALBANK_SHIFT) * 16u;
        }
        out[x] = index ? palette_color(mem, index) : 0;
    }
}

// The 28-bit signed reference point register at io[reg] (low) and io[reg + 1].
static s32 read_ref(const u16* io, u32 reg) {
    u32 v = (u32)io[reg] | ((u32)io[reg + 1] << 16);
    v &= 0x0FFFFFFFu;
    if (v & 0x08000000u)
        v |= 0xF0000000u;
    return (s32)v;
}

// Texture coordinates (24.8 fixed) of pixel 0 on line y of affine background
// 2 or 3, and their steps per pixel.
typedef struct {
    s32 x, y, dx, dy;
} AffineLine;

static AffineLine affine_line(const u16* io, u32 bg, u32 y) {
    u32 base = (bg - 2u) * 8u; // halfwords between BG2's and BG3's registers
    s32 pa = (s16)io[REG_BG2PA + base], pb = (s16)io[REG_BG2PA + base + 1];
    s32 pc = (s16)io[REG_BG2PA + base + 2], pd = (s16)io[REG_BG2PA + base + 3];
    AffineLine l;
    l.x = read_ref(io, REG_BG2X + base) + pb * (s32)y;
    l.y = read_ref(io, REG_BG2Y + base) + pd * (s32)y;
    l.dx = pa;
    l.dy = pc;
    return l;
}

static void draw_affine_bg(const WebVideoMemory* mem, u32 bg, u32 y, u16* out) {
    u16 cnt = mem->io[REG_BG0CNT + bg];
    u32 char_base = ((cnt >> BG_CBB_SHIFT) & 3u) * 0x4000u;
    u32 screen_base = ((cnt >> BG_SBB_SHIFT) & 31u) * 0x800u;
    u32 size_shift = (u32)cnt >> BG_SIZE_SHIFT;
    s32 size = 128 << size_shift; // pixels
    AffineLine l = affine_line(mem->io, bg, y);

    for (u32 x = 0; x < WEB_SCREEN_W; x++, l.x += l.dx, l.y += l.dy) {
        s32 tx = l.x >> 8, ty = l.y >> 8;
        if (cnt & BG_WRAP) {
            tx &= size - 1;
            ty &= size - 1;
        } else if (tx < 0 || ty < 0 || tx >= size || ty >= size) {
            out[x] = 0;
            continue;
        }
        u32 map_addr = screen_base + ((u32)ty >> 3) * ((u32)size >> 3) + ((u32)tx >> 3);
        u32 tile = mem->vram[map_addr]; // (a map may extend into object VRAM)
        u32 addr = char_base + tile * 64u + ((u32)ty & 7u) * 8u + ((u32)tx & 7u);
        u32 index = addr < BG_VRAM_SIZE ? mem->vram[addr] : 0u;
        out[x] = index ? palette_color(mem, index) : 0;
    }
}

// Background 2 in modes 3-5 (bitmaps, transformed by BG2's affine registers).
static void draw_bitmap_bg(const WebVideoMemory* mem, u32 mode, u32 y, u16* out) {
    u16 dispcnt = mem->io[REG_DISPCNT];
    u32 page = (mode != 3 && (dispcnt & DCNT_PAGE)) ? 0xA000u : 0u;
    s32 width = mode == 5 ? 160 : 240;
    s32 height = mode == 5 ? 128 : 160;
    AffineLine l = affine_line(mem->io, 2, y);

    for (u32 x = 0; x < WEB_SCREEN_W; x++, l.x += l.dx, l.y += l.dy) {
        s32 tx = l.x >> 8, ty = l.y >> 8;
        if (tx < 0 || ty < 0 || tx >= width || ty >= height) {
            out[x] = 0;
            continue;
        }
        u32 pixel = (u32)ty * (u32)width + (u32)tx;
        if (mode == 4) {
            u32 index = mem->vram[page + pixel];
            out[x] = index ? palette_color(mem, index) : 0;
        } else {
            out[x] = (u16)((vram16(mem->vram, page + pixel * 2u) & 0x7FFF) | OPAQUE);
        }
    }
}

// --- Objects -------------------------------------------------------------

// The palette index of pixel (lx, ly) of an object whose tiles start at tile
// number `tile`, addressed like the hardware (and mGBA): within the 32 KiB of
// object VRAM, and in 2D mapping within rows of 32 tiles (1 KiB).
static u32 obj_texel(const WebVideoMemory* mem, u32 tile, bool bpp8, bool map_1d, u32 width, u32 lx,
                     u32 ly) {
    u32 char_base = (bpp8 && !map_1d ? tile & ~1u : tile) * 32u;
    u32 mask_lo = map_1d ? 0x7FFEu : 0x3FEu;
    u32 mask_hi = map_1d ? 0u : char_base & 0x7C00u;
    u32 stride = map_1d ? (bpp8 ? width : width / 2u) : 0x80u; // bytes per tile row / 8
    u32 x_base, y_base;
    if (bpp8) {
        x_base = (lx & ~7u) * 8u + (lx & 6u);
        y_base = (ly & ~7u) * stride + (ly & 7u) * 8u + mask_hi;
    } else {
        x_base = (lx & ~7u) * 4u + ((lx >> 1) & 2u);
        y_base = (ly & ~7u) * stride + (ly & 7u) * 4u + mask_hi;
    }
    u32 addr = (y_base + ((x_base + char_base) & mask_lo)) & 0x7FFEu;
    u32 data = vram16(mem->vram, OBJ_VRAM + addr);
    return bpp8 ? (data >> ((lx & 1u) * 8u)) & 0xFFu : (data >> ((lx & 3u) * 4u)) & 0xFu;
}

typedef struct {
    u16 attr0, attr1, attr2;
    u32 width, height; // of the sprite's image
    s32 x;             // left edge of its box on screen
    u32 in_y;          // row of its box drawn on this line
    bool bpp8, map_1d;
} ObjLine;

// Puts one object pixel into the line, the way the hardware merges objects:
// they are visited in OAM order, and a pixel replaces the stored one only if
// its priority is strictly better (so equal priority keeps the lower OAM
// index). A transparent pixel with better priority still lowers the stored
// pixel's priority and takes over its semi-transparency (a hardware quirk
// mGBA reproduces: a later, higher-priority object's box can lift an earlier
// object's pixels in front of a background).
static void put_obj_pixel(const WebVideoMemory* mem, Line* line, const ObjLine* o, u32 sx,
                          u32 index) {
    u32 prio = (o->attr2 >> ATTR2_PRIO_SHIFT) & 3u;
    u32 mode = (o->attr0 >> ATTR0_MODE_SHIFT) & 3u;
    if (mode == OBJ_MODE_WINDOW) {
        if (index)
            line->obj_window[sx] = 1;
        return;
    }
    if (prio >= line->obj_prio[sx])
        return;
    if (index) {
        if (!o->bpp8)
            index += (u32)(o->attr2 >> ATTR2_PALBANK_SHIFT) * 16u;
        line->obj[sx] = palette_color(mem, OBJ_PALETTE + index);
    } else if (!line->obj[sx]) {
        return;
    }
    line->obj_prio[sx] = (u8)prio;
    line->obj_semi[sx] = mode == OBJ_MODE_SEMI;
}

static void draw_regular_obj(const WebVideoMemory* mem, Line* line, const ObjLine* o) {
    u32 ly = o->in_y;
    if (o->attr1 & ATTR1_VFLIP)
        ly = o->height - 1u - ly;
    s32 start = o->x < 0 ? 0 : o->x;
    s32 end = o->x + (s32)o->width;
    if (end > WEB_SCREEN_W)
        end = WEB_SCREEN_W;
    for (s32 sx = start; sx < end; sx++) {
        u32 lx = (u32)(sx - o->x);
        if (o->attr1 & ATTR1_HFLIP)
            lx = o->width - 1u - lx;
        u32 index = obj_texel(mem, o->attr2 & ATTR2_ID_MASK, o->bpp8, o->map_1d, o->width, lx, ly);
        put_obj_pixel(mem, line, o, (u32)sx, index);
    }
}

static void draw_affine_obj(const WebVideoMemory* mem, Line* line, const ObjLine* o) {
    u32 shift = (o->attr0 & ATTR0_AFF_DBL_BIT) ? 1u : 0u;
    s32 box_w = (s32)(o->width << shift), box_h = (s32)(o->height << shift);
    const u16* m = &mem->oam[((o->attr1 >> ATTR1_AFF_ID_SHIFT) & 31u) * 16u];
    s32 pa = (s16)m[3], pb = (s16)m[7], pc = (s16)m[11], pd = (s16)m[15];
    s32 dy = (s32)o->in_y - box_h / 2;
    s32 start = o->x < 0 ? 0 : o->x;
    s32 end = o->x + box_w;
    if (end > WEB_SCREEN_W)
        end = WEB_SCREEN_W;
    for (s32 sx = start; sx < end; sx++) {
        s32 dx = sx - o->x - box_w / 2;
        // Texture coordinates relative to the image's top left corner.
        s32 lx = (pa * dx + pb * dy + (s32)(o->width << 7)) >> 8;
        s32 ly = (pc * dx + pd * dy + (s32)(o->height << 7)) >> 8;
        if (lx < 0 || ly < 0 || lx >= (s32)o->width || ly >= (s32)o->height)
            continue;
        u32 index = obj_texel(mem, o->attr2 & ATTR2_ID_MASK, o->bpp8, o->map_1d, o->width, (u32)lx,
                              (u32)ly);
        put_obj_pixel(mem, line, o, (u32)sx, index);
    }
}

static void draw_objs(const WebVideoMemory* mem, Line* line, u32 y) {
    u16 dispcnt = mem->io[REG_DISPCNT];
    bool bitmap_mode = (dispcnt & DCNT_MODE_MASK) >= 3;
    s32 cycles = (dispcnt & DCNT_OAM_HBL) ? OBJ_CYCLES_HBLANK_FREE : OBJ_CYCLES;

    for (u32 i = 0; i < 128; i++) {
        ObjLine o;
        o.attr0 = mem->oam[i * 4];
        o.attr1 = mem->oam[i * 4 + 1];
        o.attr2 = mem->oam[i * 4 + 2];
        bool affine = (o.attr0 & ATTR0_AFF) != 0;
        if (!affine && (o.attr0 & ATTR0_HIDE))
            continue;
        const u8* size = obj_sizes[o.attr0 >> ATTR0_SHAPE_SHIFT][o.attr1 >> ATTR1_SIZE_SHIFT];
        o.width = size[0];
        o.height = size[1];
        u32 box_w = o.width, box_h = o.height;
        s32 cost = (s32)o.width;
        if (affine) {
            if (o.attr0 & ATTR0_AFF_DBL_BIT) {
                box_w *= 2u;
                box_h *= 2u;
            }
            cost = 10 + 2 * (s32)box_w;
        }
        // Objects entirely off screen don't take rendering cycles (as in mGBA).
        u32 top = o.attr0 & ATTR0_Y_MASK, left = o.attr1 & ATTR1_X_MASK;
        if (top >= WEB_SCREEN_H && top + box_h < 228)
            continue;
        if (left >= WEB_SCREEN_W && left + box_w < 512)
            continue;
        // Y wraps at 256: an object starting at y = 250 shows its rows 6 and
        // on at the top of the screen.
        u32 in_y = (y - top) & 0xFFu;
        if (in_y >= box_h)
            continue;
        o.in_y = in_y;
        o.x = left >= 256 ? (s32)left - 512 : (s32)left;
        o.bpp8 = (o.attr0 & ATTR0_8BPP) != 0;
        o.map_1d = (dispcnt & DCNT_OBJ_1D) != 0;
        // In bitmap modes the bitmap takes the first half of object VRAM.
        if (!bitmap_mode || (o.attr2 & ATTR2_ID_MASK) >= 512) {
            if (affine)
                draw_affine_obj(mem, line, &o);
            else
                draw_regular_obj(mem, line, &o);
        }
        cycles -= cost;
        if (cycles <= 0)
            break;
    }
}

// --- Windows -------------------------------------------------------------

// Whether coordinate v is inside [lo, hi); lo > hi wraps around the edge.
static bool in_window_range(u32 v, u16 reg) {
    u32 lo = (u32)reg >> 8, hi = reg & 0xFFu;
    return lo <= hi ? v >= lo && v < hi : v >= lo || v < hi;
}

static void compute_windows(const WebVideoMemory* mem, Line* line, u32 y) {
    const u16* io = mem->io;
    u16 dispcnt = io[REG_DISPCNT];
    if (!(dispcnt & (DCNT_WIN0 | DCNT_WIN1 | DCNT_WINOBJ))) {
        for (u32 x = 0; x < WEB_SCREEN_W; x++)
            line->window[x] = WIN_ALL;
        return;
    }
    bool win0 = (dispcnt & DCNT_WIN0) && in_window_range(y, io[REG_WIN0V]);
    bool win1 = (dispcnt & DCNT_WIN1) && in_window_range(y, io[REG_WIN1V]);
    bool winobj = (dispcnt & DCNT_WINOBJ) && (dispcnt & DCNT_OBJ);
    for (u32 x = 0; x < WEB_SCREEN_W; x++) {
        u32 bits;
        if (win0 && in_window_range(x, io[REG_WIN0H]))
            bits = io[REG_WININ];
        else if (win1 && in_window_range(x, io[REG_WIN1H]))
            bits = (u32)io[REG_WININ] >> 8;
        else if (winobj && line->obj_window[x])
            bits = (u32)io[REG_WINOUT] >> 8;
        else
            bits = io[REG_WINOUT];
        line->window[x] = (u8)(bits & WIN_ALL);
    }
}

// --- Composition ---------------------------------------------------------

static u32 min_u32(u32 a, u32 b) {
    return a < b ? a : b;
}

// Color special effects on BGR555 colors, per GBATEK. (mGBA 0.10.5 applies
// them to colors already expanded to 8 bits per channel, so its result can
// differ from the hardware's by a few 8-bit steps.)
static u16 blend(u16 a, u16 b, u32 eva, u32 evb) {
    u32 out = 0;
    for (u32 shift = 0; shift < 15; shift += 5) {
        u32 ca = ((u32)a >> shift) & 31u, cb = ((u32)b >> shift) & 31u;
        out |= min_u32(31, (ca * eva + cb * evb) >> 4) << shift;
    }
    return (u16)out;
}

static u16 brighten(u16 c, u32 evy) {
    u32 out = 0;
    for (u32 shift = 0; shift < 15; shift += 5) {
        u32 v = ((u32)c >> shift) & 31u;
        out |= (v + (((31u - v) * evy) >> 4)) << shift;
    }
    return (u16)out;
}

static u16 darken(u16 c, u32 evy) {
    u32 out = 0;
    for (u32 shift = 0; shift < 15; shift += 5) {
        u32 v = ((u32)c >> shift) & 31u;
        out |= (v - ((v * evy) >> 4)) << shift;
    }
    return (u16)out;
}

static void composite(const WebVideoMemory* mem, const Line* line, const bool bg_on[4], u8* rgba) {
    const u16* io = mem->io;
    u16 bldcnt = io[REG_BLDCNT];
    u32 mode = (bldcnt >> BLD_MODE_SHIFT) & 3u;
    u32 eva = min_u32(io[REG_BLDALPHA] & 31u, 16);
    u32 evb = min_u32((io[REG_BLDALPHA] >> 8) & 31u, 16);
    u32 evy = min_u32(io[REG_BLDY] & 31u, 16);
    u32 bg_prio[4];
    for (u32 bg = 0; bg < 4; bg++)
        bg_prio[bg] = io[REG_BG0CNT + bg] & BG_PRIO_MASK;
    u16 backdrop = palette_color(mem, 0);

    for (u32 x = 0; x < WEB_SCREEN_W; x++) {
        u32 win = line->window[x];
        // The two frontmost layers: priority 0 first; at equal priority the
        // object, then backgrounds by number.
        u32 layers[2] = {LAYER_BACKDROP, LAYER_BACKDROP};
        u16 colors[2] = {backdrop, backdrop};
        u32 found = 0;
        for (u32 prio = 0; prio < 4 && found < 2; prio++) {
            if (line->obj[x] && line->obj_prio[x] == prio && (win & (1u << LAYER_OBJ))) {
                layers[found] = LAYER_OBJ;
                colors[found++] = line->obj[x];
            }
            for (u32 bg = 0; bg < 4 && found < 2; bg++) {
                if (bg_on[bg] && bg_prio[bg] == prio && line->bg[bg][x] && (win & (1u << bg))) {
                    layers[found] = bg;
                    colors[found++] = line->bg[bg][x];
                }
            }
        }

        u16 c = colors[0];
        // The backdrop has nothing behind it to blend with.
        bool second_is_target = found > 0 && (((u32)bldcnt >> (BLD_BOT_SHIFT + layers[1])) & 1u);
        // A semi-transparent object is a first target whatever BLDCNT says,
        // and blends with a second target whatever the mode, even where a
        // window disables color effects (as in mGBA); over anything else it
        // takes the mode's brightness effect.
        bool semi = layers[0] == LAYER_OBJ && line->obj_semi[x];
        bool first_is_target = semi || (((u32)bldcnt >> layers[0]) & 1u);
        if (semi && second_is_target)
            c = blend(colors[0], colors[1], eva, evb);
        else if (!(win & WIN_BLD) || !first_is_target)
            ; // no effect
        else if (mode == BLD_MODE_ALPHA && second_is_target)
            c = blend(colors[0], colors[1], eva, evb);
        else if (mode == BLD_MODE_WHITE)
            c = brighten(c, evy);
        else if (mode == BLD_MODE_BLACK)
            c = darken(c, evy);

        u32 r = c & 31u, g = (c >> 5) & 31u, b = (c >> 10) & 31u;
        u8* p = &rgba[x * 4];
        p[0] = (u8)((r << 3) | (r >> 2));
        p[1] = (u8)((g << 3) | (g >> 2));
        p[2] = (u8)((b << 3) | (b >> 2));
        p[3] = 255;
    }
}

// --- HBlank DMA (raster:) --------------------------------------------------

// A DMA channel set to start at HBlank, during a frame: the hardware's
// internal addresses, which move on with each unit copied.
typedef struct {
    u32 src, dst, dst_start;
    u32 count; // units per HBlank
    u16 control;
    bool on;
} HblankDma;

// The channels set to start at HBlank, as a frame begins. Returns whether any
// is.
static bool hblank_dma_begin(const WebVideoMemory* mem, HblankDma dma[4]) {
    bool any = false;
    for (u32 ch = 0; ch < 4; ch++) {
        const u16* r = &mem->io[REG_DMA0SAD + ch * DMA_REGS];
        HblankDma* d = &dma[ch];
        d->control = r[5];
        d->on = mem->dma_source && (d->control & DMA_ENABLE) &&
                (((u32)d->control >> DMA_TIMING_SHIFT) & 3u) == DMA_AT_HBLANK;
        if (!d->on)
            continue;
        // Address bits each channel has (GBATEK), aligned to the unit.
        u32 align = d->control & DMA_32 ? ~3u : ~1u;
        u32 src_bits = ch == 0 ? 0x07FFFFFFu : 0x0FFFFFFFu;
        u32 dst_bits = ch == 3 ? 0x0FFFFFFFu : 0x07FFFFFFu;
        d->src = ((u32)r[0] | ((u32)r[1] << 16)) & src_bits & align;
        d->dst = ((u32)r[2] | ((u32)r[3] << 16)) & dst_bits & align;
        d->dst_start = d->dst;
        u32 count = ch == 3 ? r[4] : r[4] & 0x3FFFu;
        d->count = count ? count : (ch == 3 ? 0x10000u : 0x4000u);
        any = true;
    }
    return any;
}

// How far an address moves per unit, for address control `mode`.
static u32 dma_step(u32 mode, u32 size) {
    return mode == 1 ? 0u - size : mode == 2 ? 0u : size;
}

// A halfword DMA writes: to the I/O registers or palette RAM (mirrored every
// KiB); elsewhere it is dropped.
static void dma_write(u16* io, u16* palette, u32 addr, u16 value) {
    if (addr >= IO_ADDRESS && addr < IO_ADDRESS + 0x400u)
        io[(addr - IO_ADDRESS) / 2] = value;
    else if ((addr & 0xFF000000u) == PALETTE_ADDRESS)
        palette[(addr & 0x3FFu) / 2] = value;
}

// One horizontal blank's transfers, channel 0 first (the highest priority).
static void hblank_dma_run(const WebVideoMemory* mem, HblankDma dma[4], u16* io, u16* palette) {
    for (u32 ch = 0; ch < 4; ch++) {
        HblankDma* d = &dma[ch];
        if (!d->on)
            continue;
        u32 size = d->control & DMA_32 ? 4u : 2u;
        u32 dst_mode = ((u32)d->control >> DMA_DST_SHIFT) & 3u;
        u32 src_step = dma_step(((u32)d->control >> DMA_SRC_SHIFT) & 3u, size);
        u32 dst_step = dma_step(dst_mode, size);
        for (u32 n = 0; n < d->count; n++) {
            const u8* from = mem->dma_source(d->src, size);
            if (from) {
                for (u32 k = 0; k < size; k += 2)
                    dma_write(io, palette, d->dst + k, (u16)(from[k] | (from[k + 1] << 8)));
            }
            d->src += src_step;
            d->dst += dst_step;
        }
        if (dst_mode == DMA_DST_RELOAD)
            d->dst = d->dst_start;
        if (!(d->control & DMA_REPEAT))
            d->on = false;
    }
}

// --- Frames ----------------------------------------------------------------

static void render_line(const WebVideoMemory* mem, u32 y, u8* rgba) {
    u16 dispcnt = mem->io[REG_DISPCNT];
    if (dispcnt & DCNT_BLANK) {
        for (u32 i = 0; i < WEB_SCREEN_W * 4; i++)
            rgba[i] = 255;
        return;
    }

    // Which backgrounds each mode has, and how they're drawn.
    u32 mode = dispcnt & DCNT_MODE_MASK;
    bool bg_on[4];
    for (u32 bg = 0; bg < 4; bg++) {
        bool exists = mode == 0 || (mode == 1 && bg <= 2) || (mode == 2 && bg >= 2) ||
                      (mode >= 3 && mode <= 5 && bg == 2);
        bg_on[bg] = exists && (dispcnt & (DCNT_BG0 << bg));
    }

    static Line line;
    for (u32 bg = 0; bg < 4; bg++) {
        if (!bg_on[bg])
            continue;
        if (mode >= 3)
            draw_bitmap_bg(mem, mode, y, line.bg[bg]);
        else if (mode == 0 || (mode == 1 && bg < 2))
            draw_regular_bg(mem, bg, y, line.bg[bg]);
        else
            draw_affine_bg(mem, bg, y, line.bg[bg]);
    }
    for (u32 x = 0; x < WEB_SCREEN_W; x++) {
        line.obj[x] = 0;
        line.obj_prio[x] = 4;
        line.obj_semi[x] = 0;
        line.obj_window[x] = 0;
    }
    if (dispcnt & DCNT_OBJ)
        draw_objs(mem, &line, y);
    compute_windows(mem, &line, y);
    composite(mem, &line, bg_on, rgba);
}

void web_render(const WebVideoMemory* mem, u8* rgba) {
    // raster: with HBlank DMA, lines are drawn from a copy of the registers
    // and palette that it changes between them.
    static HblankDma dma[4];
    static u16 io[512], palette[512];
    WebVideoMemory frame = *mem;
    bool hblank_dma = hblank_dma_begin(mem, dma);
    if (hblank_dma) {
        for (u32 i = 0; i < 512; i++) {
            io[i] = mem->io[i];
            palette[i] = mem->palette[i];
        }
        frame.io = io;
        frame.palette = palette;
    }
    for (u32 y = 0; y < WEB_SCREEN_H; y++) {
        render_line(&frame, y, &rgba[y * WEB_SCREEN_W * 4]);
        if (hblank_dma)
            hblank_dma_run(mem, dma, io, palette);
    }
}
