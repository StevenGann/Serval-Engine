#ifndef RACER_ART_H
#define RACER_ART_H

// The pictures, built at boot by art_build(): the road picture (background
// 2), the sky (background 3) and the sprites, with their palettes and the
// colour tables view.c writes every frame.

#include "road.h"

#include <serval/serval.h>

// --- Backgrounds ----------------------------------------------------------------

#define ROAD_BG 2 // the road picture: MAP_LAYER_FIXED | MAP_LAYER_WRAP, 16 metatiles wide
#define SKY_BG 3  // the sky and the far savanna: MAP_LAYER_FIXED | MAP_LAYER_WRAP

// Background palette banks: road tile row r (lines HORIZON + 8r ..
// HORIZON + 8r + 7) has bank r; the sky has SKY_BANK, all fifteen of its
// colours. Banks 13 and 14 are free.
#define SKY_BANK 12

// The road picture's materials. In a road bank, colour 1 + material * 3 +
// group is that material on the row's lines of that group (road_group()), so
// the stripes are palette writes: view.c gives each group of each row the
// light, dark or haze colours (RoadFrame.shade).
enum { MAT_GRASS, MAT_RUMBLE, MAT_SURFACE, MAT_DASH, MATERIALS };
#define ROAD_STRIPE_COLORS (MATERIALS * ROAD_GROUPS) // 12: colours 1-12 of a road bank
#define ROAD_EDGE_COLOR 13 // the white line inside each rumble strip: never striped

// The road's colours at one time of day: each material in each shade
// (SHADE_DARK, SHADE_LIGHT, SHADE_HAZE: the mix of the other two, made at
// boot so that a frame mixes nothing).
typedef struct {
    Color shade[3][MATERIALS];
} RoadColors;

// The times of day the colour tables are made for (Task 3 adds the rest).
enum { TIME_AFTERNOON, TIMES_OF_DAY };
extern RoadColors road_colors[TIMES_OF_DAY];

extern Tileset road_tileset; // the road's and the sky's tiles and banks 0-12
extern const MapLayer road_layer;
extern const MapLayer sky_layer;
extern u16 bg_tiles_used; // BG tiles the pictures took, of MAP_MAX_TILES

// --- Sprites ----------------------------------------------------------------------

enum { SPR_JEEP, SPRITE_COUNT };

// The jeep: 64x32, seen from behind, its origin the middle of its wheels'
// foot (draw it at the car's x and the road line its wheels stand on).
enum { JEEP_STRAIGHT, JEEP_LEFT, JEEP_RIGHT, JEEP_FRAMES };
#define JEEP_W 64
#define JEEP_H 32

// The sprite group's palettes: the jeep, and the jeep braking (its tail
// lights lit), drawn with SPRITE_PALETTE(PAL_JEEP_BRAKING).
enum { PAL_JEEP, PAL_JEEP_BRAKING, OBJ_PALETTE_COUNT };

extern const SpriteAsset* const sprite_table[SPRITE_COUNT];
extern const SpriteGroup sprite_group;

// Builds every picture into EWRAM. Call once at boot, after road_init()
// (the road picture follows road_width()).
void art_build(void);

#endif // RACER_ART_H
