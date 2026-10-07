// Map layers, the camera, runtime cell changes and collision queries: the
// platform-neutral part of map.h. Drawing the layers is the platform's job
// (src/gba/map.c); sys_map_movement is in src/ecs/map_movement.c.

#include "serval/map.h"
#include "serval/screen.h"

#include "map_internal.h"
#include "warn.h"

int serval_camera_x, serval_camera_y;

const MapLayer* serval_map_layers[4];
u8 serval_map_reload;
// Read once per frame: EWRAM is fast enough.
SERVAL_EWRAM_BSS int serval_map_offset_x[4], serval_map_offset_y[4];

// The changes are in IWRAM: every collision query looks through them. The
// redraw list is read once per frame, so it can be in the slower EWRAM.
MapChange serval_map_changes[MAP_MAX_CHANGES];
u32 serval_map_change_count;
SERVAL_EWRAM_BSS MapChange serval_map_redraw[MAP_MAX_CHANGES];
u32 serval_map_redraw_count;
bool serval_map_redraw_all;

#ifdef SERVAL_DEBUG
// Each kind of problem is reported once, not on every call.
enum {
    W_LAYER_POINTER,
    W_LAYER_BG,
    W_LAYER_SIZE,
    W_LAYER_DATA,
    W_LAYER_FLAGS,
    W_LAYER_CELL,
    W_TYPE_LADDER,
    W_TYPE_SLOPE,
    W_TYPE_RESERVED,
    W_UNLOAD_BG,
    W_SCROLL_BG,
    W_SET_NO_MAP,
    W_SET_OUTSIDE,
    W_SET_METATILE,
    W_SET_FULL,
    W_CAMERA_SMALL,
};
static u32 warned;

static bool first_warning(u32 kind) {
    if (warned & (1u << kind))
        return false;
    warned |= 1u << kind;
    return true;
}
#define WARN_ONCE(kind, ...)                                                                       \
    do {                                                                                           \
        if (first_warning(kind))                                                                   \
            SERVAL_WARN(__VA_ARGS__);                                                              \
    } while (0)
#else
#define WARN_ONCE(kind, ...) ((void)0)
#endif

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static void clamp_camera(void) {
    const MapLayer* playfield = serval_map_layers[2];
    if (!playfield)
        return;
    int max_x = playfield->width * 16 - SCREEN_W, max_y = playfield->height * 16 - SCREEN_H;
    serval_camera_x = clamp(serval_camera_x, 0, max_x < 0 ? 0 : max_x);
    serval_camera_y = clamp(serval_camera_y, 0, max_y < 0 ? 0 : max_y);
}

#ifdef SERVAL_DEBUG
// Clamping at the playfield's edges is normal (a camera following the player
// is clamped near every edge), so it is silent. But on an axis where the
// playfield is smaller than the screen the camera can only be 0, and asking
// for anything else there does nothing: a game drifting a small layer with
// the camera sees it stand still. Checked here, on the requested position,
// not in clamp_camera(), which map_load() also calls.
static void check_small_playfield(int x, int y) {
    const MapLayer* playfield = serval_map_layers[2];
    if (!playfield)
        return;
    bool narrow = x != 0 && playfield->width * 16 < SCREEN_W;
    bool low = y != 0 && playfield->height * 16 < SCREEN_H;
    if (narrow || low)
        WARN_ONCE(W_CAMERA_SMALL,
                  "camera_set(%d, %d): the playfield is %s than the screen, so %c stays 0; move "
                  "small layers with map_set_scroll()",
                  x, y, narrow ? "narrower" : "shorter", narrow ? 'x' : 'y');
}
#endif

void camera_set(int x, int y) {
#ifdef SERVAL_DEBUG
    check_small_playfield(x, y);
#endif
    serval_camera_x = x;
    serval_camera_y = y;
    clamp_camera();
}

int camera_x(void) {
    return serval_camera_x;
}

int camera_y(void) {
    return serval_camera_y;
}

// x * factor, for a FIXED factor, rounded down, without overflowing for any
// camera position on a map of up to 65,535 metatiles and factors up to 128.
static int scale(int x, FIXED factor) {
    if (factor == 0 || factor == FX_ONE)
        return x;
    return (x >> FX_SHIFT) * factor + (((x & (FX_ONE - 1)) * factor) >> FX_SHIFT);
}

int serval_map_layer_x(const MapLayer* layer) {
    int camera = layer->flags & MAP_LAYER_FIXED ? 0 : scale(serval_camera_x, layer->scroll_factor);
    return camera + serval_map_offset_x[layer->bg];
}

int serval_map_layer_y(const MapLayer* layer) {
    int camera = layer->flags & MAP_LAYER_FIXED ? 0 : scale(serval_camera_y, layer->scroll_factor);
    return camera + serval_map_offset_y[layer->bg];
}

void map_set_scroll(u32 bg, int x, int y) {
    if (bg < 1 || bg > 3) {
        WARN_ONCE(W_SCROLL_BG, "map_set_scroll(%u, ...): map layers are on backgrounds 1-3", bg);
        return;
    }
    serval_map_offset_x[bg] = x;
    serval_map_offset_y[bg] = y;
}

static void forget_changes(void) {
    serval_map_change_count = 0;
    serval_map_redraw_count = 0;
    serval_map_redraw_all = false;
}

#ifdef SERVAL_DEBUG
// Debug builds check every cell once at load time; at run time, a cell
// naming a metatile the layer doesn't have shows (and collides) as empty.
static void check_cells(const MapLayer* layer) {
    u32 count = (u32)layer->width * layer->height;
    for (u32 k = 0; k < count; k++) {
        if (layer->cells[k] >= layer->metatile_count) {
            WARN_ONCE(W_LAYER_CELL,
                      "map_load: cell (%u, %u) on background %u uses metatile %u, but the layer "
                      "has only %u; it shows as empty",
                      k % layer->width, k / layer->width, layer->bg, layer->cells[k],
                      layer->metatile_count);
            return;
        }
    }
}

// The playfield's collision types that this version doesn't implement:
// ladders (planned) collide as MAP_EMPTY and slopes (planned) as MAP_SOLID
// (src/ecs/map_movement.c), and the reserved types 10-15 as MAP_EMPTY. They
// load, since a game may be written against planned API; debug builds say so
// once per kind, here rather than in sys_map_movement, which would find them
// every frame. Every metatile definition is checked, not only those the
// cells use, as map_set_cell() can bring in any of them.
static void check_collision_types(const MapLayer* layer) {
    for (u32 k = 0; k < layer->metatile_count; k++) {
        u32 type = MAP_TYPE(layer->metatiles[k].collision);
        // Each must stay within 247 characters (SERVAL_WARN_MAX - 1, warn.h), or it is cut off.
        if (type == MAP_LADDER)
            WARN_ONCE(W_TYPE_LADDER,
                      "map_load: playfield metatile %u is a ladder: ladders are planned, not "
                      "implemented in this engine version; acts as MAP_EMPTY",
                      k);
        else if (type >= MAP_SLOPE_R && type <= MAP_SLOPE_L_LOW)
            WARN_ONCE(W_TYPE_SLOPE,
                      "map_load: playfield metatile %u is a slope: slopes are planned, not "
                      "implemented in this engine version; acts as MAP_SOLID",
                      k);
        else if (type > MAP_SLOPE_L_LOW)
            WARN_ONCE(W_TYPE_RESERVED,
                      "map_load: playfield metatile %u has collision type %u, reserved for later "
                      "engine versions; it collides as MAP_EMPTY",
                      k, type);
    }
}
#endif

bool map_load(const MapLayer* layer) {
    if (!serval_plausible_pointer(layer)) {
        WARN_ONCE(W_LAYER_POINTER, "map_load: the layer pointer is NULL or not valid");
        return false;
    }
    if (layer->bg < 1 || layer->bg > 3) {
        WARN_ONCE(W_LAYER_BG,
                  "map_load: .bg is %u, but map layers go on backgrounds 1-3 (background 0 "
                  "is the text layer)",
                  layer->bg);
        return false;
    }
    if (layer->width == 0 || layer->height == 0) {
        WARN_ONCE(W_LAYER_SIZE,
                  "map_load: the layer for background %u is %ux%u metatiles; set .width and "
                  ".height",
                  layer->bg, layer->width, layer->height);
        return false;
    }
    if (!serval_plausible_pointer(layer->cells) || !serval_plausible_pointer(layer->metatiles) ||
        layer->metatile_count == 0) {
        WARN_ONCE(W_LAYER_DATA,
                  "map_load: the layer for background %u needs .cells, .metatiles and "
                  ".metatile_count",
                  layer->bg);
        return false;
    }
    // Refused rather than ignored: a later version may give these bits a
    // meaning, which would change what such a layer does (docs/releases.md).
    if (layer->flags & ~(MAP_LAYER_WRAP | MAP_LAYER_FIXED)) {
        WARN_ONCE(W_LAYER_FLAGS,
                  "map_load: the layer for background %u has .flags 0x%x; bits 2-7 are reserved "
                  "(MAP_LAYER_WRAP and _FIXED exist); not loaded",
                  layer->bg, layer->flags);
        return false;
    }
#ifdef SERVAL_DEBUG
    check_cells(layer);
    if (layer->bg == 2)
        check_collision_types(layer);
#endif
    serval_map_layers[layer->bg] = layer;
    serval_map_reload |= (u8)(1u << layer->bg);
    if (layer->bg == 2) {
        forget_changes();
        clamp_camera();
    }
    serval_map_attach();
    return true;
}

void map_unload(u32 bg) {
    if (bg < 1 || bg > 3) {
        WARN_ONCE(W_UNLOAD_BG, "map_unload(%u): map layers are on backgrounds 1-3", bg);
        return;
    }
    serval_map_offset_x[bg] = serval_map_offset_y[bg] = 0;
    if (!serval_map_layers[bg])
        return;
    serval_map_layers[bg] = NULL;
    serval_map_reload |= (u8)(1u << bg);
    if (bg == 2)
        forget_changes();
}

u32 serval_map_cell_in(const MapLayer* layer, u32 mx, u32 my) {
    for (u32 k = 0; k < serval_map_change_count; k++) {
        if (serval_map_changes[k].mx == mx && serval_map_changes[k].my == my)
            return serval_map_changes[k].cell;
    }
    return layer->cells[my * layer->width + mx];
}

u16 map_cell(int mx, int my) {
    const MapLayer* playfield = serval_map_layers[2];
    if (!playfield || (u32)mx >= playfield->width || (u32)my >= playfield->height)
        return 0;
    return (u16)serval_map_cell_in(playfield, (u32)mx, (u32)my);
}

void map_set_cell(int mx, int my, u16 metatile) {
    const MapLayer* playfield = serval_map_layers[2];
    if (!playfield) {
        WARN_ONCE(W_SET_NO_MAP, "map_set_cell: no map is loaded on background 2 (the playfield)");
        return;
    }
    if ((u32)mx >= playfield->width || (u32)my >= playfield->height) {
        WARN_ONCE(W_SET_OUTSIDE, "map_set_cell(%d, %d): outside the playfield (%ux%u metatiles)",
                  mx, my, playfield->width, playfield->height);
        return;
    }
    if (metatile >= playfield->metatile_count) {
        WARN_ONCE(W_SET_METATILE, "map_set_cell(%d, %d, %u): the playfield has only %u metatiles",
                  mx, my, metatile, playfield->metatile_count);
        return;
    }

    // A cell set back to its original metatile leaves the table, so changes
    // that are undone (a door closing again) don't use it up.
    bool original = playfield->cells[(u32)my * playfield->width + (u32)mx] == metatile;
    u32 k = 0;
    while (k < serval_map_change_count &&
           (serval_map_changes[k].mx != (u32)mx || serval_map_changes[k].my != (u32)my))
        k++;
    if (k < serval_map_change_count) {
        if (serval_map_changes[k].cell == metatile)
            return;
        if (original)
            serval_map_changes[k] = serval_map_changes[--serval_map_change_count];
        else
            serval_map_changes[k].cell = metatile;
    } else {
        if (original)
            return;
        if (serval_map_change_count == MAP_MAX_CHANGES) {
            WARN_ONCE(W_SET_FULL,
                      "map_set_cell(%d, %d): already %u changed cells in this room; the change "
                      "is ignored",
                      mx, my, MAP_MAX_CHANGES);
            return;
        }
        serval_map_changes[serval_map_change_count++] =
            (MapChange){(u16)mx, (u16)my, (u16)metatile};
    }

    if (serval_map_redraw_count < MAP_MAX_CHANGES)
        serval_map_redraw[serval_map_redraw_count++] = (MapChange){(u16)mx, (u16)my, 0};
    else
        serval_map_redraw_all = true;
}

u8 map_tags_in(int x, int y, int w, int h) {
    const MapLayer* playfield = serval_map_layers[2];
    if (!playfield || w <= 0 || h <= 0)
        return 0;
    // Clipped to the map in pixels first, in an order that can't overflow
    // for any arguments: x + w is only computed with x negative (w is
    // positive), or with 0 <= x < the map's width and w at most what is left
    // of it.
    int map_w = playfield->width * 16, map_h = playfield->height * 16;
    if (x < 0) {
        if (x + w <= 0)
            return 0;
        w += x;
        x = 0;
    }
    if (y < 0) {
        if (y + h <= 0)
            return 0;
        h += y;
        y = 0;
    }
    if (x >= map_w || y >= map_h)
        return 0;
    int right = w > map_w - x ? map_w : x + w; // exclusive
    int bottom = h > map_h - y ? map_h : y + h;
    u32 tags = 0;
    for (u32 my = (u32)y >> 4; my <= (u32)(bottom - 1) >> 4; my++) {
        for (u32 mx = (u32)x >> 4; mx <= (u32)(right - 1) >> 4; mx++) {
            u32 cell = serval_map_cell_in(playfield, mx, my);
            if (cell < playfield->metatile_count)
                tags |= playfield->metatiles[cell].collision;
        }
    }
    return (u8)(tags & (MAP_TAG(0) | MAP_TAG(1) | MAP_TAG(2) | MAP_TAG(3)));
}

u8 map_collision_at(int x, int y) {
    const MapLayer* playfield = serval_map_layers[2];
    if (!playfield)
        return MAP_EMPTY;
    // Arithmetic shifts: pixels -1 to -16 are metatile -1.
    int mx = x >> 4, my = y >> 4;
    if ((u32)mx >= playfield->width)
        return MAP_SOLID;
    if ((u32)my >= playfield->height)
        return MAP_EMPTY;
    u32 cell = serval_map_cell_in(playfield, (u32)mx, (u32)my);
    return cell < playfield->metatile_count ? playfield->metatiles[cell].collision : MAP_EMPTY;
}
