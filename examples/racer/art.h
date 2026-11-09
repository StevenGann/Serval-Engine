#ifndef RACER_ART_H
#define RACER_ART_H

// The pictures, built at boot by art_build(): the road picture (background
// 2), the sky (background 3) and the sprites, with their palettes and the
// colour tables view.c writes as the race goes on.

#include "road.h"

#include <serval/serval.h>

// --- Backgrounds ----------------------------------------------------------------

#define ROAD_BG 2 // the road picture: MAP_LAYER_FIXED | MAP_LAYER_WRAP, 16 metatiles wide
#define SKY_BG 3  // the sky and the far savanna: MAP_LAYER_FIXED | MAP_LAYER_WRAP

// Both pictures have a spare strip below their last line (the road's
// transparent, the sky's the colour of its top), so that a bump can shake
// them down by up to SHAKE_MAX lines (map_set_scroll() y of -1 or -2) and the
// lines coming in at the top show nothing new: the road's spare strip shows
// above the sky, the sky's spare strip at the screen's top.
#define SHAKE_MAX 2

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

// --- Time of day ----------------------------------------------------------------

// Lap 1 is late afternoon, lap 2 sunset, lap 3 dusk. Between two times of
// day there are TIME_STEPS stages, each a whole set of colours mixed at
// boot, so changing the time of day is a walk through the stages (view.c),
// and a frame mixes nothing.
enum { TIME_AFTERNOON, TIME_SUNSET, TIME_DUSK, TIMES_OF_DAY };
#define TIME_STEPS 16
#define TIME_STAGES ((TIMES_OF_DAY - 1) * TIME_STEPS + 1)
#define TIME_STAGE(time) ((time) * TIME_STEPS) // the stage of a time of day itself

// The sprite palettes that take the time of day (the first ones of the
// group: the jeep's two, the scenery's, the traffic's); the logo's doesn't.
#define TINTED_PALETTES 4

// Every colour that changes with the time of day.
typedef struct {
    Color road[3][MATERIALS]; // by shade: SHADE_DARK, SHADE_LIGHT, SHADE_HAZE (the mix)
    Color edge;               // ROAD_EDGE_COLOR
    Color sky[15];            // the sky bank's colours 1-15
    Color sprites[TINTED_PALETTES][16];
} TimeColors;

extern TimeColors time_colors[TIME_STAGES];

extern Tileset road_tileset; // the road's and the sky's tiles and banks 0-12
extern const MapLayer road_layer;
extern const MapLayer sky_layer;
extern u16 bg_tiles_used; // BG tiles the pictures took, of MAP_MAX_TILES

// --- Sprites ----------------------------------------------------------------------

// What stands on the road and beside it, drawn by objects.c from where
// road_project() puts it: each in ROAD_SIZE_CLASSES sizes (class c is the
// full-size picture reduced to 1/2^c at boot), each its own sprite,
// OBJ_SPRITE(object, class).
enum {
    OBJ_ACACIA,
    OBJ_MOUND, // a termite mound
    OBJ_ROCK,
    OBJ_SIGN, // its arrow points left; drawn flipped for a right curve
    OBJ_GIRAFFE,
    OBJ_POST,   // half a checkpoint's post (two make one), at each side of the road
    OBJ_BANNER, // a third of a checkpoint's banner, across the road
    OBJ_JEEP,   // traffic: a safari jeep from behind
    OBJ_TRUCK,  // traffic: a safari truck from behind
    OBJECTS,
};

enum {
    SPR_JEEP,  // the player's jeep: JEEP_* frames
    SPR_LOGO,  // 64x32: LOGO_FRAMES pieces of "SAVANNA RUN"
    SPR_DIGIT, // 32x32: "1", "2", "3" (frame n - 1 is n)
    SPR_GO,    // 64x32: "GO"
    SPR_WORDS, // 64x32: WORD_PIECES pieces of "FINISH", then of "TIME UP"
    SPR_DUST,  // 16x16: a puff of dust, DUST_THICK then DUST_THIN
    SPR_OBJECTS,
    SPRITE_COUNT = SPR_OBJECTS + OBJECTS * ROAD_SIZE_CLASSES,
};
#define OBJ_SPRITE(object, size_class) (SPR_OBJECTS + (object) * ROAD_SIZE_CLASSES + (size_class))

// An object's sprite box in each size class, pixels (at least 8x8: the
// smallest classes of the smaller pictures sit in an 8x8 box). Its picture
// stands at the bottom of the box, centred: the middle of the box's bottom
// edge is the object's foot.
typedef struct {
    u8 w, h;
} ObjectBox;
extern ObjectBox object_box[OBJECTS][ROAD_SIZE_CLASSES];

// The player's jeep: 64x32, its origin the middle of its wheels' foot (draw
// it at the car's x and the road line its wheels stand on). JEEP_SIDE is
// side on, nose to the left (flipped: to the right); with JEEP_FRONT and
// JEEP_STRAIGHT, the poses of a spin.
enum { JEEP_STRAIGHT, JEEP_LEFT, JEEP_RIGHT, JEEP_FRONT, JEEP_SIDE, JEEP_FRAMES };
#define JEEP_W 64
#define JEEP_H 32

// The dust's frames: a fresh puff, and one thinning out.
enum { DUST_THICK, DUST_THIN, DUST_FRAMES };
#define DUST_SIDE 16

// The logo: SAVANNA in three 64x32 pieces side by side, RUN in two under
// them; LOGO_W wide in all, drawn by screens.c.
enum { LOGO_SA, LOGO_VAN, LOGO_NA, LOGO_RU, LOGO_UN, LOGO_FRAMES };

// The race's last words in the big letters, each WORD_PIECES 64x32 pieces
// side by side (WORD_PIECES * 64 wide): frame WORD_FINISH * WORD_PIECES + p
// is piece p of FINISH.
enum { WORD_FINISH, WORD_TIME_UP, WORDS };
#define WORD_PIECES 3

// The sprite group's palettes. The first TINTED_PALETTES take the time of
// day. The jeep braking (its tail lights lit) is drawn with
// SPRITE_PALETTE(PAL_JEEP_BRAKING).
enum { PAL_JEEP, PAL_JEEP_BRAKING, PAL_SCENERY, PAL_TRAFFIC, PAL_LOGO, OBJ_PALETTE_COUNT };

extern const SpriteAsset* sprite_table[SPRITE_COUNT]; // filled by art_build()
extern const SpriteGroup sprite_group;
extern u16 obj_tiles_used; // sprite tiles the pictures took, of 1,024

// Builds every picture into EWRAM. Call once at boot, after road_init()
// (the road picture follows road_width()).
void art_build(void);

#endif // RACER_ART_H
