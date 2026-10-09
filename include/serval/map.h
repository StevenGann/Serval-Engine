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
// sys_render_by_depth draw entities at their world position minus the camera
// (or, with SPRITE_SCREEN in spr_flags, at their position on the screen);
// sprite_draw() itself takes screen coordinates.

#include "serval/ecs.h"
#include "serval/fixed.h"
#include "serval/platform.h"
#include "serval/screen.h" // Color, for tileset_set_colors()

// Background tile graphics and palettes for the map layers of a room. Tile 0
// should be blank (all color 0): layers that don't wrap show it outside their
// map.
typedef struct {
    const u32* tiles;    // 8x8 tiles of 4 bits per pixel, 8 words each
    u16 tile_count;      // at most MAP_MAX_TILES
    const u16* palettes; // palette_count banks of 16 colors (color 0 transparent)
    u8 palette_count;    // at most MAP_MAX_PALETTES, loaded into BG banks 0, 1, ...
    u8 flags;            // TILESET_*; 0 (the default): .tiles as above
} Tileset;

// Tileset.flags. tileset_load() refuses a tileset with a flag it doesn't know
// or doesn't implement yet (returns false, warning in debug builds; nothing is
// loaded). Bit 1 is reserved for 8bpp tilesets, bits 2-7 for later use.
enum {
    // .tiles holds the tile_count tiles compressed with LZ77 in the GBA
    // BIOS's format: a header word with 0x10 in its low byte and the unpacked
    // size in bytes, which must be tile_count * 32, in bits 8-31, then the
    // compressed data. It must be "VRAM-safe": no match copies from the byte
    // just before it (distance 1), so the BIOS's VRAM decoder, which writes
    // 16 bits at a time, can unpack it. tileset_load() will unpack it into
    // VRAM. Planned: until implemented, tileset_load() refuses a tileset with
    // this flag (returns false, warns), loading nothing.
    TILESET_LZ77 SERVAL_PLANNED("LZ77-compressed tilesets, docs/tilemaps.md#tilesets") = 1 << 0,
};

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
// or "spikes" (map_tags_in() finds them). Only the type affects
// sys_map_movement; only the playfield's (background 2) collision bytes are
// read.
#define MAP_EMPTY 0
#define MAP_SOLID 1
#define MAP_ONEWAY 2 // solid only to bodies falling onto it from above
#define MAP_TYPE(collision) ((collision) & 0x0F)
#define MAP_TAG(n) (1u << (4 + (n)))

// The other collision types. 3-9 are planned: until they are implemented,
// sys_map_movement() treats the ladder as MAP_EMPTY and the slopes as
// MAP_SOLID (a whole metatile), and map_load() of the playfield warns once
// for ladders and once for slopes among its metatiles. 10-15 are reserved
// for later types (ceiling slopes, for one): they collide as MAP_EMPTY, and
// map_load() of the playfield warns once if one of its metatiles has one.
// Data must not use them, as a later version will give them a meaning.
enum {
    // Climbable, not solid. A map body overlapping a ladder metatile has
    // MAP_CONTACT_LADDER in body_contact, so the game can let it climb: it
    // sets the body's velocity itself, usually with body_gravity at
    // BODY_GRAVITY(0) (physics.h) while it climbs. The top of a ladder (a ladder metatile
    // with none above it) is a floor to bodies with gravity falling onto it,
    // like MAP_ONEWAY; a body without gravity is climbing, and passes through
    // it both ways. Planned: until implemented it collides as MAP_EMPTY and
    // MAP_CONTACT_LADDER is never set.
    MAP_LADDER SERVAL_PLANNED("ladders, docs/tilemaps.md#collision-types") = 3,
    // Floor slopes: solid below a straight line across the metatile, empty
    // above it. The name says which way the floor rises: _R to the right, _L
    // to the left. The line's height above the metatile's bottom edge, in
    // pixels, at the metatile's left and right edges:
    //   MAP_SLOPE_R       0 to 16   45 degrees: the top-right corner is high
    //   MAP_SLOPE_L      16 to 0    45 degrees: the top-left corner is high
    //   MAP_SLOPE_R_LOW   0 to 8    a 1:2 slope (about 27 degrees) rising to
    //   MAP_SLOPE_R_HIGH  8 to 16   the right: _LOW, then _HIGH to its right
    //   MAP_SLOPE_L_HIGH 16 to 8    a 1:2 slope rising to the left: _HIGH,
    //   MAP_SLOPE_L_LOW   8 to 0    then _LOW to its right
    // So each rises 16 or 8 pixels across its 16-pixel width, and slopes
    // placed side by side as above meet at the same height. A map body will
    // walk up and down them, standing on the line, with MAP_CONTACT_FLOOR as
    // on a flat floor; the exact motion is specified when they are
    // implemented. Planned: until then each collides as MAP_SOLID.
    MAP_SLOPE_R SERVAL_PLANNED("floor slopes, docs/tilemaps.md#collision-types") = 4,
    MAP_SLOPE_L SERVAL_PLANNED("floor slopes, docs/tilemaps.md#collision-types") = 5,
    MAP_SLOPE_R_LOW SERVAL_PLANNED("floor slopes, docs/tilemaps.md#collision-types") = 6,
    MAP_SLOPE_R_HIGH SERVAL_PLANNED("floor slopes, docs/tilemaps.md#collision-types") = 7,
    MAP_SLOPE_L_HIGH SERVAL_PLANNED("floor slopes, docs/tilemaps.md#collision-types") = 8,
    MAP_SLOPE_L_LOW SERVAL_PLANNED("floor slopes, docs/tilemaps.md#collision-types") = 9,
};

typedef struct {
    u16 se[4];    // screen entries: top-left, top-right, bottom-left, bottom-right
    u8 collision; // a collision type (MAP_EMPTY, MAP_SOLID, ...), plus MAP_TAG bits
} Metatile;

// MapLayer.flags: the two below. map_load() refuses a layer with another bit
// set (bits 2-7 are reserved for later use): it returns false (warning in
// debug builds) and shows nothing.
//
// Repeats in both directions (small parallax backgrounds). On the playfield
// only the graphics repeat: collision, map_cell() and the camera's clamp use
// the single map.
#define MAP_LAYER_WRAP (1 << 0)
// Ignores the camera: shows the layer's top-left (moved by map_set_scroll()
// only), e.g. a HUD panel or a frame around the playfield.
#define MAP_LAYER_FIXED (1 << 1)

typedef struct {
    u16 width, height;         // in metatiles
    const u16* cells;          // width x height metatile indices, row by row
    const Metatile* metatiles; // the definitions cells index
    u16 metatile_count;
    u8 bg;               // background 1-3; its priority is its number (docs/tilemaps.md)
    u8 flags;            // MAP_LAYER_*
    FIXED scroll_factor; // how far it moves per pixel of camera movement,
                         // e.g. FX_ONE / 2 for a distant parallax layer; 0 means FX_ONE
                         // (MAP_LAYER_FIXED: not used)
} MapLayer;

// Loads a tileset's tiles into background VRAM (charblocks 1-2) and its
// palettes into background palette banks, skipping each bank's color 0.
// Copies immediately, like sprite_group_load(): call it while loading a room,
// before the layers using it are shown (on a layer already on screen, the
// change may tear for a frame). Returns false (warning in debug builds) if the
// tileset is incomplete or too big, or has a flag this version doesn't know
// or implement (TILESET_LZ77, a reserved bit); nothing is loaded then.
bool tileset_load(const Tileset* tileset);

// Replaces `count` tiles of the loaded tileset, from tile `first` on, with
// `tiles` (8 words per tile, as in Tileset.tiles), e.g. the next frame of
// shimmering water or a flashing bonus block: every cell showing those tiles
// changes. The copy happens in VBlank at the next frame_end(), so `tiles`
// must stay valid until then (normally it is const data in ROM). Up to
// MAP_MAX_TILE_UPDATES calls per frame; a later call for the same `first`
// replaces the earlier one. Ignored (warning in debug builds) without a
// tileset, for tiles past the tileset's tile_count, for a NULL or invalid
// `tiles`, or when the frame's queue is full. Keep it to a few dozen tiles
// per frame: VBlank is short. tileset_load() drops updates still queued.
void tileset_set_tiles(u16 first, const u32* tiles, u16 count);
#define MAP_MAX_TILE_UPDATES 8

// Changes `count` background colors from color `index` on: index = palette *
// 16 + color, palettes numbered as in MAP_SE() (0-14). Water whose colors
// cycle, a room faded toward dusk with color_mix() (screen.h), a flash. The
// colors are copied now (`colors` may be a temporary) and reach the screen in
// VBlank at the next frame_end(). They go to the engine's own copy of the
// palettes (the shadow palette), never to the tileset's data, so a palette
// that is const data in ROM, or that another tileset uses too, keeps its
// colors there. tileset_load() puts the tileset's colors back (colors 1-15 of
// its palette_count palettes), also over writes made before it in the same
// frame. Color 0 of palette 0 is the backdrop, which screen_set_backdrop()
// also sets: writing it changes the backdrop (while raster_backdrop() is on,
// it is remembered, and shown when the effect ends: screen.h), and
// tileset_load() leaves it alone. Color 0 of the other palettes is transparent: writes to it are
// kept, not shown. Ignored (warning in debug builds) if the colors reach past
// color 239 (palette 15 is the text layer's) or `colors` is NULL; a count of
// 0 does nothing. screen_set_backdrop() and tileset_load() write at once, so
// in the same frame they win over earlier writes to the colors they set.
// Sprite palettes have sprite_set_colors() (sprites.h).
void tileset_set_colors(u32 index, const Color* colors, u32 count);

// Shows a map layer on its background (1-3), replacing any layer there. Its
// window around the camera is drawn at once (writing VRAM, like
// tileset_load(): load layers while loading a room, e.g. during a fade to
// black), so the next frame already shows it. Set the camera first: later
// camera moves are drawn at frame_end(). Returns false (warning in
// debug builds) for bad data: no cells or metatiles, a size of 0, a background
// outside 1-3, an unknown flag (MapLayer.flags); nothing changes then. Cells
// naming a metatile the layer doesn't have show and collide as empty (a
// warning at load in debug builds). Loading the playfield (background 2)
// clears map_set_cell's changes and clamps the camera to it; debug builds
// also warn (once per kind) if its metatiles use a planned or reserved
// collision type (MAP_LADDER, the slopes, 10-15), which loads anyway. The
// data must stay valid while the layer is shown (normally it is const data
// in ROM).
bool map_load(const MapLayer* layer);

// Hides the map layer on background bg (1-3) and forgets it, and its
// map_set_scroll() offset. Ignored (warning in debug builds) for a background
// outside 1-3.
void map_unload(u32 bg);

// Moves background bg's layer (1-3) by (x, y) pixels from where the camera
// puts it: the screen's top-left shows layer pixel camera * scroll_factor +
// (x, y), or (x, y) on a MAP_LAYER_FIXED layer. Applied at the next
// frame_end(), streamed like camera moves (a few pixels per frame cost one
// row or column). For a layer that scrolls by itself, e.g. a starfield
// drifting past while the camera stays put, add the speed every frame and let
// the offset count on rather than wrapping it back: a jump of a screen or
// more redraws the whole window. On the playfield (background 2) it moves the
// graphics away from collision and entities. Kept per background until
// changed or map_unload(), also when map_load() replaces the layer (set it
// before, like the camera, to have the load draw it). Ignored (warning in
// debug builds) for a background outside 1-3.
void map_set_scroll(u32 bg, int x, int y);

// Camera: the world position shown at the screen's top-left, in pixels.
// Clamped so the view stays inside the playfield's map when one is loaded on
// background 2 (to 0 on an axis where the map is smaller than the screen);
// without one, any position is kept. Backgrounds scroll at the next
// frame_end(); entities drawn by sys_render afterwards in this frame already
// use it, so set it before them.
// Clamping at the playfield's edges is silent (a camera following the
// player is clamped near every edge). But where the playfield, wrapping or
// not, is smaller than the screen on an axis, the camera, and with it every
// layer it scrolls, can only be at 0 on that axis: asking for any other
// value there warns (once, in debug builds). To scroll a small layer (a
// starfield, clouds) on such a screen, move it with map_set_scroll().
void camera_set(int x, int y);
int camera_x(void);
int camera_y(void);

// The metatile index of the playfield (background 2) at metatile coordinates
// (mx, my), including runtime changes (map_set_cell); 0 outside the map, or
// when no playfield is loaded.
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

// The collision byte of the playfield at world pixel (x, y), as the metatile
// has it (type and tags; MAP_TYPE() takes the type). Outside the map:
// MAP_SOLID to the left and right (bodies can't walk off its sides; this
// includes the corners, so the sides are walls above and below the map too),
// MAP_EMPTY above and below (bodies can jump above it and fall out of the
// bottom). MAP_EMPTY everywhere when no playfield is loaded.
u8 map_collision_at(int x, int y);

// The tag bits (MAP_TAG(0) to MAP_TAG(3)) of every playfield metatile that
// the rectangle of w x h world pixels with its top-left at (x, y) overlaps,
// ORed together, including runtime changes (map_set_cell); the type bits of
// the result are 0. For hazards, water, goals and anything else the game
// tags: a tag is a property separate from how a metatile collides (spikes
// may be solid, lava not), which is why there is no hazard collision type.
// What a map body stands on, one pixel below its feet (a body resting on a
// metatile doesn't overlap it), with the game's #define TAG_SPIKES MAP_TAG(0):
//     map_tags_in(fx_to_int(pos_x[i]), fx_to_int(pos_y[i]) + body_h[i],
//                 body_w[i], 1) & TAG_SPIKES
// Metatiles outside the playfield, and cells naming a metatile the layer
// doesn't have, have no tags. Returns 0 without a playfield, or for a
// rectangle with no area (w or h 0 or less). Costs one cell lookup per
// metatile overlapped, each looking through map_set_cell()'s changes, so
// keep the rectangle to a few metatiles.
u8 map_tags_in(int x, int y, int w, int h);

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
// reversed if it bounces). sys_physics() reports bouncing bodies' contacts in
// the same pool (physics.h).
extern u8 body_contact[MAX_ENT];
#define MAP_CONTACT_FLOOR (1 << 0)
#define MAP_CONTACT_CEILING (1 << 1)
#define MAP_CONTACT_LEFT (1 << 2)
#define MAP_CONTACT_RIGHT (1 << 3)
// The other bits of body_contact: 5 is BODY_CONTACT_EXIT (sys_physics(),
// physics.h), 6 MAP_CONTACT_LADDER (planned); 4 and 7 are reserved for the
// engine.
enum {
    // Set by sys_map_movement() while the body overlaps a MAP_LADDER
    // metatile, beside any other contact. Planned: never set until ladders
    // are implemented.
    MAP_CONTACT_LADDER SERVAL_PLANNED("ladders, docs/tilemaps.md#collision-types") = 1 << 6,
};

// Moves map bodies: adds gravity (physics_set_gravity) to their velocity and
// limits their fall speed (body_max_fall, physics.h), then moves them by it one
// axis at a time (x, then y), stopping flush against MAP_SOLID metatiles (and
// MAP_ONEWAY ones when falling onto them from above). In this version the
// slopes (planned) stop bodies as MAP_SOLID does, and ladders (planned) and
// the reserved types 10-15 are MAP_EMPTY. The velocity into each side they
// touch becomes -velocity * body_bounce / 256: 0 (the default) stops them, as
// for a player character; a gem, power-up or knocked-out enemy can bounce
// instead. 255 (a u8 can't hold 256) is a perfect bounce: off a wall or a
// ceiling it keeps the whole speed, and off a floor the body rebounds at the
// speed that brings it back up to the height it fell from (to within a
// pixel), every time, so it bounces for ever. (Keeping the speed would
// rebound it lower, by up to a frame's fall: the body stops flush against the
// floor, short of where the frame's movement would have taken it.) Unlike
// sys_physics' bounds, every side of the map (walls and ceilings too) uses
// body_bounce. On a floor (the side gravity pulls toward), a rebound too slow
// to clear twice one frame's gravity is a rest (zero speed), whatever the
// bounce (a perfect one rebounds that slowly only when the body hit the floor
// slower than that), and a body touching it loses body_friction/256 of its
// speed along it each frame (rounded up, so any friction stops it). Fast
// bodies move in steps of at most 7 pixels, so they don't pass through
// metatiles. Only
// metatiles a body's edge moves into stop it, so a body overlapping a solid
// metatile (placed there, or a cell changed under it) can move out of it. Run
// once per frame, where sys_movement() runs.
// Each body's gravity is scaled by its body_gravity (physics.h), and floors
// are the sides its own gravity pulls toward.
void sys_map_movement(void);

#endif // SERVAL_MAP_H
