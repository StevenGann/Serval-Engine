#ifndef SERVAL_MAP_H
#define SERVAL_MAP_H

// Tiled backgrounds built from 16x16 metatiles, a camera that scrolls them,
// and bodies that collide with the map. See docs/tilemaps.md.
//
// A room's graphics are one tileset (tileset_load), shared by up to three map
// layers on backgrounds 1-3 (map_load); background 0 is the text layer. Each
// layer is a grid of metatiles: four 8x8 tiles plus a collision byte. Maps may
// be far bigger than the screen: the engine keeps the part around the camera
// in VRAM, redrawing the rows and columns that scroll into view during VBlank.
//
// World coordinates are pixels from the top-left of the map on background 2,
// the playfield, which is also what map collision uses. The camera is the
// world position shown at the screen's top-left. sys_render and
// sys_render_by_depth draw entities at their world position minus the camera;
// sprite_draw() itself takes screen coordinates.

#include "serval/ecs.h"
#include "serval/fixed.h"
#include "serval/platform.h"

// Background tile graphics and palettes for the map layers of a room. Tile 0
// should be blank (all color 0): layers that don't wrap show it outside their
// map.
typedef struct {
    const u32* tiles;    // 8x8 tiles of 4 bits per pixel, 8 words each
    u16 tile_count;      // at most MAP_MAX_TILES
    const u16* palettes; // palette_count banks of 16 colors (color 0 transparent)
    u8 palette_count;    // at most MAP_MAX_PALETTES, loaded into BG banks 0, 1, ...
} Tileset;

#define MAP_MAX_TILES 1024
// Background palette bank 15 belongs to the text layer. Color 0 of bank 0 is
// the backdrop (screen_set_backdrop), which tileset_load leaves alone.
#define MAP_MAX_PALETTES 15

// A screen entry of a metatile: tile index (0-1023) in the tileset, palette
// bank, and flips, e.g. MAP_SE(12, 0, MAP_SE_FLIP_H).
#define MAP_SE_FLIP_H (1u << 10)
#define MAP_SE_FLIP_V (1u << 11)
#define MAP_SE(tile, palette, flips) ((u16)((tile) | (flips) | ((palette) << 12)))

// Metatile collision byte: the type in the low four bits, and four bits the
// game may use to tag metatiles (MAP_TAG(0) to MAP_TAG(3)), e.g. "bonus block"
// or "hazard". Only the type affects sys_map_movement.
#define MAP_EMPTY 0
#define MAP_SOLID 1
#define MAP_ONEWAY 2 // solid only to bodies falling onto it from above
#define MAP_TYPE(collision) ((collision) & 0x0F)
#define MAP_TAG(n) (1u << (4 + (n)))

typedef struct {
    u16 se[4];    // screen entries: top-left, top-right, bottom-left, bottom-right
    u8 collision; // MAP_EMPTY, MAP_SOLID or MAP_ONEWAY, plus MAP_TAG bits
} Metatile;

// MapLayer.flags
#define MAP_LAYER_WRAP (1 << 0) // repeats in both directions (small parallax backgrounds)

typedef struct {
    u16 width, height;         // in metatiles
    const u16* cells;          // width x height metatile indices, row by row
    const Metatile* metatiles; // the definitions cells index
    u16 metatile_count;
    u8 bg;               // background 1-3; its priority is its number (docs/tilemaps.md)
    u8 flags;            // MAP_LAYER_*
    FIXED scroll_factor; // how far it moves per pixel of camera movement,
                         // e.g. FX_ONE / 2 for a distant parallax layer; 0 means FX_ONE
} MapLayer;

// Loads a tileset's tiles into background VRAM (charblocks 1-2) and its
// palettes into background palette banks, skipping each bank's color 0.
// Copies immediately, like sprite_group_load(): call it while loading a room,
// before the layers using it are shown (on a layer already on screen, the
// change may tear for a frame). Returns false (warning in debug builds) if the
// tileset is incomplete or too big; nothing is loaded then.
bool tileset_load(const Tileset* tileset);

// Replaces `count` tiles of the loaded tileset, from tile `first` on, with
// `tiles` (8 words per tile, as in Tileset.tiles), e.g. the next frame of
// shimmering water or a flashing bonus block: every cell showing those tiles
// changes. The copy happens in VBlank at the next frame_end(), so `tiles`
// must stay valid until then (normally it is const data in ROM). Up to
// MAP_MAX_TILE_UPDATES calls per frame; a later call for the same `first`
// replaces the earlier one. Ignored (warning in debug builds) without a
// tileset, for tiles past the tileset's tile_count, or when the frame's
// queue is full. Keep it to a few dozen tiles per frame: VBlank is short.
// tileset_load() drops updates still queued.
void tileset_set_tiles(u16 first, const u32* tiles, u16 count);
#define MAP_MAX_TILE_UPDATES 8

// Shows a map layer on its background (1-3), replacing any layer there. Its
// window around the camera is drawn at once (writing VRAM, like
// tileset_load(): load layers while loading a room, e.g. during a fade to
// black), so the next frame already shows it. Set the camera first: later
// camera moves are drawn at frame_end(). Returns false (warning in
// debug builds) for bad data: no cells or metatiles, a size of 0, a background
// outside 1-3. Cells naming a metatile the layer doesn't have show and collide
// as empty (a warning at load in debug builds). Loading the playfield
// (background 2) clears map_set_cell's changes and clamps the camera to it.
// The data must stay valid while the layer is shown (normally it is const
// data in ROM).
bool map_load(const MapLayer* layer);

// Hides the map layer on background bg (1-3) and forgets it.
void map_unload(u32 bg);

// Camera: the world position shown at the screen's top-left, in pixels.
// Clamped so the view stays inside the playfield's map when one is loaded on
// background 2 (to 0 on an axis where the map is smaller than the screen);
// without one, any position is kept. Backgrounds scroll at the next
// frame_end(); entities drawn by sys_render afterwards in this frame already
// use it, so set it before them.
void camera_set(int x, int y);
int camera_x(void);
int camera_y(void);

// The metatile index of the playfield (background 2) at metatile coordinates
// (mx, my), including runtime changes (map_set_cell); 0 outside the map.
u16 map_cell(int mx, int my);

// Changes a cell of the playfield at runtime: a broken block, a used bonus
// block, an opened door. Kept in a small table in RAM (MAP_MAX_CHANGES cells
// per room; a full table is reported in debug builds and the change ignored)
// and redrawn during VBlank if visible. Setting a cell back to its original
// metatile frees its place in the table. map_load() of background 2 clears
// the table. Ignored (warning in debug builds) outside the playfield, without
// one, or for a metatile it doesn't have.
void map_set_cell(int mx, int my, u16 metatile);
#define MAP_MAX_CHANGES 64

// The collision byte of the playfield at world pixel (x, y). Outside the map:
// MAP_SOLID to the left and right (bodies can't walk off its sides; this
// includes the corners, so the sides are walls above and below the map too),
// MAP_EMPTY above and below (bodies can jump above it and fall out of the
// bottom). MAP_EMPTY everywhere when no playfield is loaded.
u8 map_collision_at(int x, int y);

// Map bodies: entities with C_POS, C_VEL, C_BODY and C_MAPBODY move with
// sys_map_movement() instead of sys_movement() and sys_physics(), which skip
// them. Their size is body_w x body_h (physics.h), up to 255x255; a body of
// size 0 moves as 1x1 (warning in debug builds).
#define C_MAPBODY (1u << 4)

// Which sides of a map body touched the map in the last sys_map_movement()
// (MAP_CONTACT_*), e.g. to allow a jump only while on the floor. A body only
// touches what it moves into: one standing on a floor touches it every frame
// while gravity pulls it down, but one stopped by a wall touches it again only
// when pushed into it again (its velocity toward the wall is zeroed, or
// reversed if it bounces).
extern u8 body_contact[MAX_ENT];
#define MAP_CONTACT_FLOOR (1 << 0)
#define MAP_CONTACT_CEILING (1 << 1)
#define MAP_CONTACT_LEFT (1 << 2)
#define MAP_CONTACT_RIGHT (1 << 3)

// Moves map bodies: adds gravity (physics_set_gravity) to their velocity and
// limits their fall speed (body_max_fall, physics.h), then moves them by it one
// axis at a time (x, then y), stopping flush against MAP_SOLID metatiles (and
// MAP_ONEWAY ones when falling onto them from above). The velocity into each
// side they touch becomes -velocity * body_bounce / 256: 0 (the default) stops
// them, as for a player character; a gem, power-up or knocked-out enemy can
// bounce instead. Unlike sys_physics' bounds, every side of the map (walls and
// ceilings too) uses body_bounce. On a floor (the side gravity pulls toward),
// a rebound too slow to clear twice one frame's gravity is a rest (zero
// speed), and a body touching it loses body_friction/256 of its speed along it
// each frame (rounded up, so any friction stops it). Fast bodies move in
// steps of at most 7 pixels, so they don't pass through metatiles. Only
// metatiles a body's edge moves into stop it, so a body overlapping a solid
// metatile (placed there, or a cell changed under it) can move out of it. Run
// once per frame, where sys_movement() runs.
void sys_map_movement(void);

#endif // SERVAL_MAP_H
