#ifndef DEN_ART_INTERNAL_H
#define DEN_ART_INTERNAL_H

// What Serval Den's colours (art.c, in the ROM) and its pictures (art_draw.c,
// drawn on the host by art_gen) share: the palette banks, the colour indices
// the pictures are drawn in, and the maps' sizes. A picture's pixel is an
// index; art.c says what colour each index is at each hour.

#include "art.h"

// Background palette banks.
enum { BANK_DEN, BANK_LAND, BANK_SKY, BANK_TREE, BG_BANKS };

// The den's map (15 x 10 metatiles, one per 16x16 cell) and the sky's (16 x
// 10, so it wraps whole in VRAM and can drift any distance).
#define DEN_W 15
#define SKY_W 16
#define MAP_H 10
#define TREE_W 16 // Acacia Climb's trunk and its sky: 16 x 10 metatiles, wrapping

// The den (BANK_DEN).
enum {
    D_CRACK = 1,
    D_ROCK_DARK,
    D_ROCK,
    D_ROCK_LIGHT,
    D_ROCK_GLINT,
    D_EARTH_DARK,
    D_EARTH,
    D_EARTH_LIGHT,
    D_STRAW_DARK,
    D_STRAW,
    D_STRAW_LIGHT,
    D_ROOT_DARK,
    D_ROOT,
    D_HOLLOW, // the deepest shadow: the nest's hollow, under the walls
    D_SAND,   // the floor's lightest, where the mouth's light falls
};

// The savanna through the mouth (BANK_LAND), per time of day.
enum {
    L_HILL_FAR = 1,
    L_HILL,
    L_PLAIN_LIGHT,
    L_PLAIN,
    L_PLAIN_DARK,
    L_GRASS_LIGHT,
    L_GRASS,
    L_GRASS_DARK,
    L_TRUNK,
    L_CANOPY,
    L_CANOPY_LIGHT,
    LAND_COLORS = 11,
};

// The sky (BANK_SKY): five bands from the top, cloud colours, and stars in
// colours that, by day, are their band's (a bright and a dim star for bands
// 1 and 2).
enum {
    S_BAND1 = 1,
    S_BAND2,
    S_BAND3,
    S_BAND4,
    S_BAND5,
    S_CLOUD_LIGHT,
    S_CLOUD,
    S_CLOUD_SHADE,
    S_STAR1,
    S_STAR2,
    S_DIM1,
    S_DIM2,
    SKY_COLORS = 12,
};

// The acacia (BANK_TREE, and PAL_TREE for the boughs and nests).
enum {
    T_OUT = 1,
    T_BARK_DARK,
    T_BARK,
    T_BARK_LIGHT,
    T_LEAF_DARK,
    T_LEAF,
    T_LEAF_LIGHT,
    T_STRAW_DARK,
    T_STRAW,
    T_STRAW_LIGHT,
    T_GLINT,
    TREE_COLORS = 11,
};

// The trunk's middle, in pixels (the trunk is 32 wide there).
#define TREE_X 120

// The coat's colours (PAL_PET).
enum {
    P_OUTLINE = 1,
    P_HI,
    P_MID,
    P_SHADE,
    P_CREAM,
    P_BLACK, // the ears' backs, the tail's rings, the neck's bands
    P_SPOT_A,
    P_SPOT_B,
    P_SPOT_C,
    P_IRIS,
    P_PUPIL,
    P_PINK,
    P_EAR, // the inside of an ear
    P_FAR, // the far legs and ear, in shadow
    P_GLINT,
};

// The pictures, drawn (art_draw.c; the host only: art_gen writes them into
// art_data.c).
typedef struct {
    const u32* bg_tiles; // 8 words a tile; tile 0 blank
    int bg_tile_count;
    const Metatile* den_metatiles;  // DEN_W * MAP_H, cell i is metatile i
    const Metatile* sky_metatiles;  // SKY_W * MAP_H
    const Metatile* tree_metatiles; // TREE_W * MAP_H: the trunk
    const Metatile* csky_metatiles; // TREE_W * MAP_H: the climb's sky, full height
    const u32* obj_tiles;           // every sprite's frames, one after another
    int obj_tile_count;
    const SpriteAsset* sprites; // SPRITE_COUNT, their .tiles in obj_tiles
} ArtDrawing;

void art_draw(ArtDrawing* out);

#endif // DEN_ART_INTERNAL_H
