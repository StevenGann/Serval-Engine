// Generated from ASCII pixel art by a conversion script (kept outside the
// repository), the same way as art.c (its opening comment says how); the
// previews in the comments are that art, one character per pixel. The bonus
// block, used block, gem and bricks reuse the overworld's pixels (in other
// colors for the bricks), so a stage's bonus block glints with art.c's frames.
//
// Stage 1-2, the underground: the woodlouse and the bat, and the tileset,
// metatiles and parallax layer of a dark cave of blue-grey rock.

#include "stage_underground.h"

// --- Sprites ------------------------------------------------------------------

// The woodlouse (the walker, 'e'): walk 1, walk 2 (SPR_LOUSE), curled up when
// stomped (SPR_LOUSE_BALL). Faces right.
//   ................
//   ................
//   ................
//   ................
//   ................
//   ......KKKKK.....
//   ....KKhlhlhKK...
//   ...KlhlhlhlhlK.a
//   ..KmlmlmlmlmlmKa
//   ..KmlmlmlmlmlKeK
//   ..KdmdmdmdmdmKKK
//   ..KddddddddddK..
//   ...KKKKKKKKKK...
//   ...a.a.a.a.a....
//   ..a.a.a.a.a.....
//   ................
static const u32 louse_tiles[96] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x11000000, 0x45110000, 0x54541000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000111, 0x00011545, 0x70145454,
    0x34343100, 0x34343100, 0x23232100, 0x22222100, 0x11111000, 0x70707000, 0x07070700, 0x00000000,
    0x71343434, 0x16143434, 0x11132323, 0x00122222, 0x00011111, 0x00007070, 0x00000707, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x11000000, 0x45110000, 0x54541000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000111, 0x70011545, 0x70145454,
    0x34343100, 0x34343100, 0x23232100, 0x22222100, 0x11111000, 0x07070000, 0x70707000, 0x00000000,
    0x11343434, 0x16143434, 0x11132323, 0x00122222, 0x00011111, 0x00070707, 0x00007070, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x11000000, 0x45110000, 0x54541000, 0x34343100, 0x32323100, 0x22221000, 0x11110000, 0x00000000,
    0x00000011, 0x00001145, 0x00015454, 0x00143434, 0x00133232, 0x00012222, 0x00001111, 0x00000000,
};

// The curled-up woodlouse.
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ......KKKK......
//   ....KKhlhlKK....
//   ...KlhlhlhlhK...
//   ..KmlmlmlmlmlK..
//   ..KmdmdmdmdmmK..
//   ...KddddddddK...
//   ....KKKKKKKK....
//   ................

// The bat: asleep upside down under the ceiling (SPR_BAT_HANG), then flying,
// wings up and wings down (SPR_BAT_FLY).
//   ......K...K.....
//   .....KdK.KdK....
//   .....KpKKKpK....
//   ....KMpppppMK...
//   ...KMmpPPPpmMK..
//   ...KMmpPPPpmMK..
//   ...KMmppPppmMK..
//   ...KMmpppppmMK..
//   ....KMmpppmMK...
//   ....KKdpppdKK...
//   .....KpepepK....
//   .....KpppppK....
//   ......KWpWK.....
//   .....KdK.KdK....
//   .....K.....K....
//   ................
//
//   ................
//   K..............K
//   KK............KK
//   KmK..K....K..KmK
//   KmmK.KdKKdK.KmmK
//   KMmmKKppppKKmmMK
//   .KMmmKeppeKmmMK.
//   .KMMmKppppKmMMK.
//   ..KMMKpWWpKMMK..
//   ...KKKdppdKKK...
//   ......KddK......
//   .......KK.......
//   ................
//   ................
//   ................
//   ................
static const u32 bat_tiles[96] = {
    0x01000000, 0x12100000, 0x13100000, 0x33810000, 0x43581000, 0x43581000, 0x33581000, 0x33581000,
    0x00000100, 0x00001210, 0x00001311, 0x00018333, 0x00185344, 0x00185344, 0x00185334, 0x00185333,
    0x35810000, 0x32110000, 0x63100000, 0x33100000, 0x71000000, 0x12100000, 0x00100000, 0x00000000,
    0x00018533, 0x00011233, 0x00001363, 0x00001333, 0x00000173, 0x00001210, 0x00001000, 0x00000000,
    0x00000000, 0x00000001, 0x00000011, 0x00100151, 0x12101551, 0x33115581, 0x36155810, 0x33158810,
    0x00000000, 0x10000000, 0x11000000, 0x15100100, 0x15510121, 0x18551133, 0x01855163, 0x01885133,
    0x73188100, 0x32111000, 0x21000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00188137, 0x00011123, 0x00000012, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00100000, 0x12100000, 0x33110000, 0x36151100, 0x33155810,
    0x00000000, 0x00000000, 0x00000000, 0x00000100, 0x00000121, 0x00001133, 0x00115163, 0x01855133,
    0x73185581, 0x32118551, 0x21001851, 0x10000181, 0x00000011, 0x00000001, 0x00000000, 0x00000000,
    0x18558137, 0x15581123, 0x15810012, 0x18100001, 0x11000000, 0x10000000, 0x00000000, 0x00000000,
};

// Palettes of the stage's group: the blocks' (slot PAL_STAGE_BLOCKS, the same
// colors as the tileset's BANK_BLOCKS: blue-grey bricks), the woodlouse's
// and the bat's.
static const u16 sprite_palettes[UG_PALETTE_COUNT][16] = {
    [PAL_STAGE_BLOCKS] = {0, COLOR_RGB(44, 24, 20), COLOR_RGB(56, 66, 104), COLOR_RGB(84, 98, 142),
                          COLOR_RGB(130, 146, 190), COLOR_RGB(26, 30, 52), COLOR_RGB(178, 106, 22),
                          COLOR_RGB(238, 170, 42), COLOR_RGB(255, 228, 120), COLOR_RGB(112, 76, 50),
                          COLOR_RGB(164, 122, 86), COLOR_RGB(78, 50, 30), COLOR_RGB(130, 88, 46),
                          COLOR_RGB(222, 180, 116), COLOR_RGB(176, 128, 70),
                          COLOR_RGB(196, 146, 96)},
    [UG_PAL_LOUSE] = {0, COLOR_RGB(24, 20, 34), COLOR_RGB(66, 62, 88), COLOR_RGB(106, 102, 136),
                      COLOR_RGB(152, 148, 184), COLOR_RGB(204, 200, 228), COLOR_RGB(250, 236, 140),
                      COLOR_RGB(120, 96, 80)},
    [UG_PAL_BAT] = {0, COLOR_RGB(20, 12, 28), COLOR_RGB(56, 30, 76), COLOR_RGB(96, 56, 124),
                    COLOR_RGB(140, 96, 170), COLOR_RGB(180, 92, 140), COLOR_RGB(255, 226, 90),
                    COLOR_RGB(255, 255, 255), COLOR_RGB(120, 56, 96)},
};

// Animation timing for sys_animate.
static const u8 louse_walk_times[2] = {10, 10};
static const u8 bat_flap_times[2] = {5, 5};

// The stage's sprites, IDs SPR_STAGE(STAGE_UNDERGROUND) + UG_* (stage_underground.h).
const SpriteAsset underground_sprites[STAGE_SPRITES] = {
    [UG_LOUSE] = {.size = SPRITE_16x16,
                  .tiles = louse_tiles,
                  .frame_count = 2,
                  .frame_times = louse_walk_times,
                  .palette_slot = UG_PAL_LOUSE,
                  .origin_x = 2,
                  .origin_y = 3},
    [UG_LOUSE_BALL] = {.size = SPRITE_16x16,
                       .tiles = louse_tiles + 2 * 32, // its third frame
                       .palette_slot = UG_PAL_LOUSE,
                       .origin_x = 2,
                       .origin_y = 3},
    [UG_BAT_HANG] = {.size = SPRITE_16x16,
                     .tiles = bat_tiles,
                     .palette_slot = UG_PAL_BAT,
                     .origin_x = 2,
                     .origin_y = 2},
    [UG_BAT_FLY] = {.size = SPRITE_16x16,
                    .tiles = bat_tiles + 32, // its second and third frames
                    .frame_count = 2,
                    .frame_times = bat_flap_times,
                    .palette_slot = UG_PAL_BAT,
                    .origin_x = 2,
                    .origin_y = 3},
};

static const u16 group_sprites[] = {SPR_BLOCK,      SPR_DEBRIS,   SPR_LOUSE,
                                    SPR_LOUSE_BALL, SPR_BAT_HANG, SPR_BAT_FLY};

const SpriteGroup underground_group = {
    .sprite_ids = group_sprites,
    .palettes = &sprite_palettes[0][0],
    .sprite_count = sizeof group_sprites / sizeof group_sprites[0],
    .palette_count = UG_PALETTE_COUNT,
};

// --- Backgrounds --------------------------------------------------------------

// Background palette banks. BANK_FAR is BANK_ROCK's colors dimmed toward the
// backdrop when the stage loads (underground_load_palettes()), so the tileset's
// palettes are in RAM.
enum { BANK_ROCK, BANK_BLOCKS, BANK_GEM, BANK_EXIT, BANK_FAR, BANK_COUNT };

// 136 unique 8x8 tiles: 87 for the playfield and foreground, 49 more for the
// parallax layer. Tile 0 is empty (transparent); tiles 1-4 are the bonus block.
static const u32 bg_tiles[1088] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x11111110, 0x88888881, 0x77777781, 0x77777781, 0x99777781, 0x99777781, 0x77997781, 0x97997781,
    0x01111111, 0x17888888, 0x16777777, 0x16777777, 0x16777997, 0x16777997, 0x16799777, 0x16799799,
    0x99777781, 0x99977781, 0x99977781, 0x99777781, 0x77777781, 0x77777781, 0x66666671, 0x11111110,
    0x16777999, 0x16779999, 0x16779999, 0x16777999, 0x16777777, 0x16777777, 0x16666666, 0x01111111,
    0x11111110, 0xAAAAAAA1, 0xAAAAA9A1, 0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAAAA1,
    0x01111111, 0x19AAAAAA, 0x199AAAAA, 0x19AAAAAA, 0x19AAAAAA, 0x19AAAAAA, 0x19AAAAAA, 0x19AAAAAA,
    0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAA9A1, 0x99999991, 0x11111110,
    0x19AAAAAA, 0x19AAAAAA, 0x19AAAAAA, 0x19AAAAAA, 0x19AAAAAA, 0x199AAAAA, 0x19999999, 0x01111111,
    0x00000000, 0x11000000, 0x54110000, 0x44541000, 0x44454100, 0x34444100, 0x33333100, 0x33331000,
    0x00000000, 0x00000011, 0x00001134, 0x00012333, 0x00123333, 0x00123333, 0x00122333, 0x00012233,
    0x33321000, 0x33210000, 0x32100000, 0x21000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000,
    0x00012233, 0x00001223, 0x00000123, 0x00000012, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
    0x54444444, 0x53333334, 0x52333333, 0x52222222, 0x55555555, 0x44445444, 0x33345333, 0x33335233,
    0x22225222, 0x55555555, 0x54444444, 0x53333334, 0x52333333, 0x52222222, 0x55555555, 0x55555555,
    0x00000A00, 0xA900A9A0, 0x55455555, 0x44244444, 0x34233334, 0x33233333, 0x33223333, 0x22112222,
    0x000A0009, 0x09A9A0A9, 0x55455555, 0x44244444, 0x34233333, 0x33233333, 0x33223333, 0x22122222,
    0x44445244, 0x33334233, 0x43334233, 0x33333233, 0x33334233, 0x33333223, 0x33333223, 0x22222221,
    0x44452444, 0x33342333, 0x33342333, 0x33332333, 0x33332333, 0x33332233, 0x23332233, 0x22221122,
    0x24454445, 0x23333334, 0x23333334, 0x23334334, 0x23333333, 0x23333333, 0x22333332, 0x11222222,
    0x24544445, 0x23333334, 0x23343334, 0x23333333, 0x23333334, 0x22333333, 0x22333333, 0x12222222,
    0x55555555, 0x44444445, 0x44444445, 0x44443445, 0x44444445, 0x44444445, 0x44444445, 0x34444445,
    0x45555555, 0x24444444, 0x23444444, 0x23444444, 0x23443444, 0x23444444, 0x23444444, 0x23444444,
    0x44444445, 0x44444445, 0x44434445, 0x44444445, 0x44444445, 0x44444445, 0x33333335, 0x22222224,
    0x23444444, 0x23444444, 0x23444444, 0x23444444, 0x23434444, 0x23444444, 0x23333333, 0x22222222,
    0x22232222, 0x22343222, 0x12232222, 0x22222212, 0x22222222, 0x22222322, 0x21223432, 0x22222322,
    0x22212222, 0x23222222, 0x34222222, 0x23222432, 0x22223443, 0x12222322, 0x22222222, 0x22223222,
    0x32222222, 0x43212222, 0x32222222, 0x22222232, 0x32222343, 0x42122232, 0x22222222, 0x22222122,
    0x23222222, 0x22221223, 0x22222222, 0x24322222, 0x23222222, 0x22222223, 0x22212223, 0x22222222,
    0x32222222, 0x32212222, 0x42222222, 0x22222242, 0x42222233, 0x22212221, 0x22101210, 0x11000100,
    0x23222222, 0x22221223, 0x22222222, 0x24222322, 0x23322222, 0x21222122, 0x02101201, 0x01001000,
    0x00023420, 0x00023420, 0x00024200, 0x00023200, 0x00024200, 0x00023000, 0x00002000, 0x00002000,
    0x00024320, 0x00024200, 0x00023200, 0x00002000, 0x00002000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x80000000, 0x78000000, 0x77008000, 0x77007800, 0x77067700, 0x77067700, 0x77867780,
    0x00000000, 0x00000000, 0x00000000, 0x00800006, 0x00780006, 0x06778006, 0x06777086, 0x06777076,
    0x77767770, 0x77767770, 0x77767778, 0x77767777, 0x66616661, 0x11121112, 0x22222222, 0x22222222,
    0x06777676, 0x86777676, 0x76777676, 0x76777676, 0x61666161, 0x12111212, 0x22222222, 0x22222222,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00BCB000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00BCCB00, 0x0BCCDCB0,
    0x0BCDCB00, 0x0BBBBB00, 0x000F0000, 0x00EF0000, 0x09EF9000, 0x9AEFA900, 0xA9AA9A90, 0xAA9A9AA9,
    0x0BBCCBB0, 0x000FF000, 0x000EF000, 0x000EF000, 0x009EF900, 0x09AEFA90, 0x9A9AA9A9, 0x9AA9A9A9,
    0x11111111, 0xFFFDFFFD, 0xEEEFEEEF, 0xECEEECEE, 0xCCBBCCCB, 0x11111111, 0x0000C100, 0x0000C100,
    0x11111111, 0xFFFFFDFF, 0xEEEEEFEE, 0xEEECEEEE, 0xCCCBCCCC, 0x11111111, 0x00000000, 0x00000000,
    0x000C1000, 0x000C1000, 0x00C10000, 0x00C10000, 0x00100000, 0x00000000, 0x00000000, 0x00000000,
    0x66660000, 0x88860000, 0x77760000, 0x77760000, 0x66660000, 0x00000000, 0x00000000, 0x00000000,
    0x66666666, 0x88888878, 0x77777777, 0x77777677, 0x66666666, 0xAA677876, 0xAA677876, 0xAA677776,
    0xAA677876, 0xAA677876, 0x99677876, 0x99677876, 0x99677876, 0x99677876, 0x99677876, 0x99677876,
    0x66666666, 0x88788888, 0x77777777, 0x77777777, 0x66666666, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
    0x66666666, 0x88888888, 0x77777777, 0x77677777, 0x66666666, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA,
    0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAA9, 0xAAA99999, 0x99999999, 0x99999999, 0x99999999, 0x99999999,
    0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAA9, 0xAAA99999, 0x99999999, 0x99E99999,
    0x66666666, 0x88888878, 0x77777777, 0x77777777, 0x66666666, 0xAAAAAAAA, 0xAAAAAAAA, 0xBAAAAAAA,
    0x66666666, 0x88788888, 0x77777777, 0x77777777, 0x66666666, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAABB,
    0xBBAAAAAA, 0xBBBAAAAA, 0xBBAAAAAA, 0xBAAAAAAA, 0xAAAAAAAA, 0x9999AAAA, 0x99999999, 0x99999999,
    0xAAAAABBB, 0xAAAABBBB, 0xAAAAABBB, 0x9999AABB, 0x99999999, 0x99999999, 0x99999999, 0x99999999,
    0x66666666, 0x88888888, 0x77777777, 0x77777776, 0x66666666, 0x677876AA, 0x677876AA, 0x677776AA,
    0x32222322, 0x24323222, 0x23222233, 0x33223242, 0x32222422, 0x32232232, 0x22423222, 0x23223223,
    0x677876AA, 0x677876AA, 0x67787699, 0x67787699, 0x67787699, 0x67787699, 0x67787699, 0x67787699,
    0x34222222, 0x32222232, 0x42232232, 0x22233222, 0x24324344, 0x42223223, 0x42223232, 0x23242242,
    0x99677876, 0x99677876, 0x99677876, 0x99677876, 0x99677776, 0x9E677876, 0xEE677876, 0xEE677876,
    0xEE677876, 0xEE677876, 0xEE677876, 0xEE677876, 0xEE677876, 0xFF677876, 0xDD677876, 0xDD677876,
    0x99999999, 0x99999999, 0x99999999, 0x99EE9999, 0x9EEEE999, 0x9EEEEE99, 0xEEEEEEE9, 0xEEEEEEEE,
    0x99E99999, 0x9EEE9999, 0xEEEEE999, 0xEEEEEE99, 0xEEEEEE99, 0xEEEEEE99, 0xEEEEEE99, 0xEEEEEEEE,
    0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE, 0xEEFEEEEF, 0xFFFFFFFF, 0xCDDDDDDD, 0xDDDDDDDC,
    0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE, 0xFEEEEFEE, 0xFFFFFFFF, 0xDDDDDDDD, 0xDDDDDDDD,
    0x99999999, 0x999E9999, 0x999E9999, 0x9EEEE99E, 0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE,
    0x99999999, 0x99999999, 0x99999999, 0xEE999999, 0xEEE99999, 0xEEEE999E, 0xEEEEE9EE, 0xEEEEEEEE,
    0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE, 0xEEEFEEEE, 0xFFFFFFFF, 0xDDDDDDDD, 0xCDDDDDDD,
    0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE, 0xEFEEEEFE, 0xFFFFFFFF, 0xDCDDDDDD, 0xDDDDDDDD,
    0x67787699, 0x67787699, 0x67787699, 0x67787699, 0x6777769E, 0x6778769E, 0x677876EE, 0x677876EE,
    0x22232322, 0x22333324, 0x22224223, 0x23223232, 0x23243232, 0x32232322, 0x24332422, 0x22223223,
    0x677876EE, 0x677876EE, 0x677876EE, 0x677876EE, 0x677876EE, 0x677876FF, 0x677876DD, 0x677876DD,
    0x23224222, 0x23223223, 0x32242422, 0x32432322, 0x22222223, 0x24223223, 0x23223223, 0x32242232,
    0xDD677876, 0xDD677776, 0xDD677876, 0xDD677876, 0xDD677876, 0xDD677876, 0xDD677876, 0xDD677876,
    0xDD677876, 0xCD677876, 0xDD677876, 0xDD677876, 0xDD677876, 0xCC677876, 0xCC677776, 0xCC666666,
    0xDDDDDDDD, 0xDDDDDDDD, 0xDDDDDCDD, 0xDDDDDDDD, 0xDDDDDDDD, 0xDDDCDDDD, 0xDDDDDDDD, 0xDDDDDDDD,
    0xDDDDDDDD, 0xDDDDDDCD, 0xDDDDDDDD, 0xDDDDDDDD, 0xDDDDCDDD, 0xDDDDDDDD, 0xDDDDDDDD, 0xDDCDDDDD,
    0xDCDDDDDD, 0xDDDDDDDD, 0xDDDDDDDD, 0xDDDDCDDD, 0xCDDDDDCC, 0xCCCCCCCC, 0xCCCCCCCC, 0xCCCCCCCC,
    0xDDDDDDDD, 0xDDDDDDDD, 0xCDDDDDDD, 0xDDDDDCDC, 0xDCDDDDDD, 0xCCCCCCCC, 0xCCCCCCCC, 0xCCCCCCCC,
    0xDDDDDDDC, 0xDDDDDDDD, 0xDDDDDDDD, 0xDDDDDCDD, 0xDDDDDDDD, 0xDDDDDDDD, 0xDDDCDDDD, 0xDDDDDDDD,
    0xDDDDDDDD, 0xDDDDDDDD, 0xDDDDDDCD, 0xDDDDDDDD, 0xDDDDDDDD, 0xDDDDCDDD, 0xDDDDDDDD, 0xDDDDDDDD,
    0xDDDDDDDD, 0xDCDDDDDD, 0xDDDDDDDD, 0xDDDDDDCD, 0xDDCDDDDD, 0xCCCCCCCC, 0xCCCCCCCC, 0xCCCCCCCC,
    0xDDCDDDDD, 0xDDDDDDDD, 0xDDDDDDDD, 0xCDDDDDDC, 0xDDDCDDDC, 0xCCCCCCCC, 0xCCCCCCCC, 0xCCCCCCCC,
    0x677876DD, 0x677776DC, 0x677876DD, 0x677876DD, 0x677876DD, 0x677876DD, 0x677876DD, 0x677876DD,
    0x42232222, 0x22332323, 0x24224222, 0x22323224, 0x23222232, 0x42222222, 0x22332322, 0x22322223,
    0x677876DD, 0x677876DD, 0x677876DD, 0x677876DD, 0x677876DD, 0x677876CC, 0x677776CC, 0x666666CC,
    0x22324322, 0x23222233, 0x22222222, 0x22232332, 0x22232324, 0x32322322, 0x23222243, 0x22222232,
    0x42000000, 0x42000000, 0x42000000, 0x42000000, 0x42000000, 0x42000000, 0x42000000, 0x42000000,
    0x23333334, 0x23333333, 0x23333332, 0x23323333, 0x23333333, 0x23333333, 0x23333333, 0x23332333,
    0x20000000, 0x20000000, 0x20000000, 0x20000000, 0x20000000, 0x20000000, 0x20000000, 0x20000000,
    0x23333334, 0x33333344, 0x33333334, 0x33333234, 0x32333334, 0x33333334, 0x33333334, 0x33333324,
    0x00000023, 0x00000023, 0x00000023, 0x00000023, 0x00000023, 0x00000023, 0x00000023, 0x00000023,
    0x23334200, 0x23334200, 0x23334200, 0x23334200, 0x22334200, 0x23334200, 0x23334200, 0x23334200,
    0x02234200, 0x02334200, 0x02334200, 0x02334200, 0x00223420, 0x00233420, 0x00233420, 0x00233420,
    0x34200000, 0x34200000, 0x34200000, 0x34200000, 0x33420000, 0x34420000, 0x33420000, 0x33420000,
    0x33420000, 0x23420000, 0x33420000, 0x33420000, 0x33342000, 0x32342000, 0x33442000, 0x33342000,
    0x00002333, 0x00002323, 0x00002333, 0x00002333, 0x00000233, 0x00000232, 0x00000233, 0x00000233,
    0x00000233, 0x00000233, 0x00000233, 0x00000233, 0x00000023, 0x00000023, 0x00000023, 0x00000023,
    0x00223420, 0x00233420, 0x00233420, 0x00233420, 0x00022342, 0x00023342, 0x00023342, 0x00023342,
    0x00233242, 0x00233342, 0x00233342, 0x00233342, 0x00233342, 0x00233342, 0x00233342, 0x00233342,
    0x20000000, 0x20000000, 0x20000000, 0x20000000, 0x42000000, 0x42000000, 0x42000000, 0x42000000,
    0x33233334, 0x33333334, 0x33333344, 0x33333334, 0x33323333, 0x33333333, 0x33333333, 0x33333333,
    0x42000000, 0x42000000, 0x42000000, 0x42000000, 0x34200000, 0x34200000, 0x34200000, 0x34200000,
    0x23332333, 0x23333333, 0x23333333, 0x23333334, 0x02333233, 0x02333333, 0x02333333, 0x02333333,
    0x00000023, 0x00000023, 0x00000023, 0x00000023, 0x00000002, 0x00000002, 0x00000002, 0x00000002,
    0x00022420, 0x00023420, 0x00023420, 0x00023420, 0x00002242, 0x00002342, 0x00002342, 0x00002342,
    0x00002342, 0x00002342, 0x00002342, 0x00002342, 0x00002342, 0x00002342, 0x00002342, 0x00002342,
    0x33342000, 0x33242000, 0x33342000, 0x33342000, 0x33342000, 0x33342000, 0x33342000, 0x33442000,
    0x33420000, 0x33420000, 0x23420000, 0x33420000, 0x33420000, 0x33420000, 0x32420000, 0x33420000,
    0x00000233, 0x00000233, 0x00000223, 0x00000233, 0x00000233, 0x00000233, 0x00000232, 0x00000233,
    0x00023333, 0x00023333, 0x00023333, 0x00022333, 0x00023333, 0x00023333, 0x00023333, 0x00023233,
    0x00233420, 0x00223420, 0x00233420, 0x00233420, 0x00233420, 0x00232420, 0x00233420, 0x00233420,
    0x23334200, 0x23334200, 0x22334200, 0x23334200, 0x23334200, 0x23334200, 0x23234200, 0x23334200,
    0x34200000, 0x34200000, 0x34200000, 0x34200000, 0x44200000, 0x34200000, 0x34200000, 0x34200000,
    0x00233323, 0x00233333, 0x00233333, 0x00233333, 0x00233332, 0x00223333, 0x00233333, 0x00233333,
    0x02333333, 0x02332333, 0x02333333, 0x02333333, 0x02333333, 0x02333234, 0x02333333, 0x02333333,
    0x00233420, 0x00232420, 0x00233420, 0x00233420, 0x00233420, 0x00233420, 0x00233420, 0x00233420,
    0x23334200, 0x23334200, 0x23234200, 0x23334200, 0x23334200, 0x23334200, 0x23324200, 0x23334200,
    0x44200000, 0x34200000, 0x34200000, 0x34200000, 0x34200000, 0x34200000, 0x34200000, 0x34200000,
    0x33420000, 0x34420000, 0x33420000, 0x23420000, 0x33342000, 0x33342000, 0x33342000, 0x32342000,
    0x00023333, 0x00023333, 0x00023333, 0x00023323, 0x00023333, 0x00023333, 0x00023333, 0x00023332,
    0x00023333, 0x00023333, 0x00023333, 0x00023333, 0x00002333, 0x00002333, 0x00002333, 0x00002333,
    0x02333420, 0x02333420, 0x02323420, 0x02333420, 0x00233342, 0x00233342, 0x00232342, 0x00233342,
    0x00233420, 0x00233420, 0x00233420, 0x00233420, 0x00233420, 0x00233420, 0x00233420, 0x00233420,
    0x33333334, 0x33333324, 0x33233334, 0x33333334, 0x33333334, 0x33333334, 0x33323344, 0x33333334,
    0x42000000, 0x42000000, 0x42000000, 0x42000000, 0x34200000, 0x34200000, 0x34200000, 0x44200000,
    0x33333333, 0x33333333, 0x33332333, 0x23333333, 0x23333333, 0x23333333, 0x23333233, 0x22333333,
    0x00000002, 0x00000002, 0x00000002, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00233420, 0x00233420, 0x00232420, 0x00233420, 0x00023342, 0x00023342, 0x00023242, 0x00023342,
    0x02333420, 0x02333420, 0x02333420, 0x02233420, 0x02333420, 0x02333420, 0x02333420, 0x02323420,
    0x33420000, 0x33420000, 0x34420000, 0x33420000, 0x33420000, 0x33420000, 0x33420000, 0x33420000,
    0x34200000, 0x34200000, 0x34200000, 0x44200000, 0x24200000, 0x34200000, 0x34200000, 0x34200000,
    0x00023233, 0x00023333, 0x00023333, 0x00023333, 0x00023323, 0x00023333, 0x00023333, 0x00023333,
    0x00023332, 0x00023333, 0x00023333, 0x00023333, 0x00023333, 0x00022333, 0x00023333, 0x00023333,
    0x00234200, 0x00234200, 0x00234200, 0x00224200, 0x00234200, 0x00234200, 0x00234200, 0x00234200,
    0x00234200, 0x00234200, 0x00234200, 0x00234200, 0x00023420, 0x00023420, 0x00023420, 0x00023420,
};

static const u16 rom_palettes[BANK_FAR][16] = {
    [BANK_ROCK] = {0, COLOR_RGB(8, 8, 18), COLOR_RGB(28, 32, 56), COLOR_RGB(48, 56, 90),
                   COLOR_RGB(76, 88, 128), COLOR_RGB(118, 132, 176), COLOR_RGB(96, 36, 120),
                   COLOR_RGB(190, 84, 206), COLOR_RGB(250, 206, 255), COLOR_RGB(26, 78, 84),
                   COLOR_RGB(52, 138, 126), COLOR_RGB(34, 86, 160), COLOR_RGB(84, 190, 226),
                   COLOR_RGB(220, 250, 255), COLOR_RGB(150, 140, 124), COLOR_RGB(200, 190, 170)},
    [BANK_BLOCKS] = {0, COLOR_RGB(44, 24, 20), COLOR_RGB(56, 66, 104), COLOR_RGB(84, 98, 142),
                     COLOR_RGB(130, 146, 190), COLOR_RGB(26, 30, 52), COLOR_RGB(178, 106, 22),
                     COLOR_RGB(238, 170, 42), COLOR_RGB(255, 228, 120), COLOR_RGB(112, 76, 50),
                     COLOR_RGB(164, 122, 86), COLOR_RGB(78, 50, 30), COLOR_RGB(130, 88, 46),
                     COLOR_RGB(222, 180, 116), COLOR_RGB(176, 128, 70), COLOR_RGB(196, 146, 96)},
    [BANK_GEM] = {0, COLOR_RGB(16, 40, 96), COLOR_RGB(30, 100, 210), COLOR_RGB(60, 170, 250),
                  COLOR_RGB(160, 236, 255), COLOR_RGB(255, 255, 255)},
    [BANK_EXIT] = {0, COLOR_RGB(8, 8, 18), COLOR_RGB(28, 32, 56), COLOR_RGB(48, 56, 90),
                   COLOR_RGB(76, 88, 128), COLOR_RGB(118, 132, 176), COLOR_RGB(90, 56, 30),
                   COLOR_RGB(150, 100, 50), COLOR_RGB(200, 150, 80), COLOR_RGB(150, 200, 250),
                   COLOR_RGB(210, 236, 255), COLOR_RGB(255, 244, 190), COLOR_RGB(50, 130, 60),
                   COLOR_RGB(100, 190, 80), COLOR_RGB(90, 130, 190), COLOR_RGB(60, 100, 60)},
};
static u16 bg_palettes[BANK_COUNT][16] SERVAL_EWRAM_BSS;

// The far rock is the near rock's colors mixed this far (of 256) toward the
// backdrop: dim, and blue-black like the cave's air.
#define FAR_DIMMING 150

void underground_load_palettes(void) {
    for (int b = 0; b < BANK_FAR; b++) {
        for (int c = 0; c < 16; c++)
            bg_palettes[b][c] = rom_palettes[b][c];
    }
    for (int c = 1; c < 16; c++)
        bg_palettes[BANK_FAR][c] = color_mix(rom_palettes[BANK_ROCK][c], UG_BACKDROP, FAR_DIMMING);
}

const Tileset underground_tileset = {
    .tiles = bg_tiles,
    .tile_count = 136,
    .palettes = &bg_palettes[0][0],
    .palette_count = BANK_COUNT,
};

// The playfield's and foreground's metatiles: screen entries top-left,
// top-right, bottom-left, bottom-right.
//
// The cave floor: moss on a lip of stone (MT_GROUND_TOP), then courses of
// stones (MT_GROUND, two courses, so it repeats downward).
//   ..G.....g...G...
//   .GgG..gGgG.GgGg.
//   hhhhhRhhhhhhhRhh
//   RRRRRdRRRRRRRdRR
//   RrrrrdRrrrrrrdRr
//   rrrrrdrrrrrrrdrr
//   rrrrddrrrrrrddrr
//   ddddKKdddddddKdd
//   RRdhRRRRRRRdhRRR
//   rrdRrrrrrrrdRrrr
//   rrdRrrrRrrrdRrrr
//   rrdrrrrrrrrdrrrr
//   rrdRrrrrrrrdrrrr
//   rddrrrrrrrddrrrr
//   rddrrrrrrrddrrrd
//   KdddddddddKKdddd
//
//   hRRRhRRdhRRRRhRd
//   RrrrrrrdRrrrrrrd
//   RrrrrrrdRrrrRrrd
//   RrrRrrrdrrrrrrrd
//   rrrrrrrdRrrrrrrd
//   rrrrrrrdrrrrrrdd
//   drrrrrddrrrrrrdd
//   ddddddKKdddddddK
//   RRdhRRRRRRRdhRRR
//   rrdRrrrrrrrdRrrr
//   rrdRrrrRrrrdRrrr
//   rrdrrrrrrrrdrrrr
//   rrdRrrrrrrrdrrrr
//   rddrrrrrrrddrrrr
//   rddrrrrrrrddrrrd
//   KdddddddddKKdddd
//
// A hard stone block: staircases and pillars.
//   hhhhhhhhhhhhhhhR
//   hRRRRRRRRRRRRRRd
//   hRRRRRRRRRRRRRrd
//   hRRrRRRRRRRRRRrd
//   hRRRRRRRRRRrRRrd
//   hRRRRRRRRRRRRRrd
//   hRRRRRRRRRRRRRrd
//   hRRRRRRrRRRRRRrd
//   hRRRRRRRRRRRRRrd
//   hRRRRRRRRRRRRRrd
//   hRRRrRRRRRRRRRrd
//   hRRRRRRRRRRRRRrd
//   hRRRRRRRRRRRrRrd
//   hRRRRRRRRRRRRRrd
//   hrrrrrrrrrrrrrrd
//   Rddddddddddddddd
//
// Bricks (TAG_BRICK), the overworld's in blue-grey: a big serval breaks them.
// MT_GEM_BRICK looks the same but holds gems.
//   EEEEEEEmEEEEEEEm
//   ERRRRRRmERRRRRRm
//   RRRRRRrmRRRRRRrm
//   rrrrrrrmrrrrrrrm
//   mmmmmmmmmmmmmmmm
//   EEEmEEEEEEEmEEEE
//   RRRmERRRRRRmERRR
//   RRrmRRRRRRrmRRRR
//   rrrmrrrrrrrmrrrr
//   mmmmmmmmmmmmmmmm
//   EEEEEEEmEEEEEEEm
//   ERRRRRRmERRRRRRm
//   RRRRRRrmRRRRRRrm
//   rrrrrrrmrrrrrrrm
//   mmmmmmmmmmmmmmmm
//   mmmmmmmmmmmmmmmm
//
// The bonus block (TAG_BONUS; MT_BONUS_FISH has a fish) and the used block
// it becomes, and a gem in the air (TAG_GEM): the overworld's.
//   .KKKKKKKKKKKKKK.
//   KYYYYYYYYYYYYYOK
//   KYOOOOOOOOOOOOoK
//   KYOOOOOOOOOOOOoK
//   KYOOOOuuOuuOOOoK
//   KYOOOOuuOuuOOOoK
//   KYOOuuOOOOOuuOoK
//   KYOOuuOuuuOuuOoK
//   KYOOOOuuuuuOOOoK
//   KYOOOuuuuuuuOOoK
//   KYOOOuuuuuuuOOoK
//   KYOOOOuuuuuOOOoK
//   KYOOOOOOOOOOOOoK
//   KYOOOOOOOOOOOOoK
//   KOoooooooooooooK
//   .KKKKKKKKKKKKKK.
//
// A one-way ledge (MT_ONEWAY, MAP_ONEWAY): a plank on a brace. The serval
// jumps up through it and lands on it. Its right half is this one mirrored.
//   KKKKKKKKKKKKKKKK
//   cfffcfffffcfffff
//   feeefeeeeefeeeee
//   eebeeebeeeeebeee
//   abbbaabbbbbbabbb
//   KKKKKKKKKKKKKKKK
//   ..Kb............
//   ..Kb............
//   ...Kb...........
//   ...Kb...........
//   ....Kb..........
//   ....Kb..........
//   .....K..........
//   ................
//   ................
//   ................
//
// The exit (MT_EXIT, 4 x 3): a timbered mouth in the rock wall, with
// daylight and green hills beyond. The serval walks in through the middle.
//   ....aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaddrddddr
//   ....aBBBBbBBBBBBBBBBBbBBBBBBBBBBBbBBBBBBBBBBBbBBBBBBBBBBdddrdrRd
//   ....abbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbrrddddrd
//   ....abbbbbabbbbbbbbbbbbbbbbbbabbbbbbbbbbbbbbbbbbabbbbbbbdRdrddrr
//   ....aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaddRddddr
//   ........abBbbaSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSabBbbadrddrddr
//   ........abBbbaSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSabBbbadddrdRdd
//   ........abbbbaSSSSSSSSSSSSSSSSSSSSSSSSSyyySSSSSSSSabbbbarddrddrd
//   ........abBbbaSSSSSSSSSSSSSSSSSSSSSSSSyyyyySSSSSSSabBbbaddddddRr
//   ........abBbbaSSSSSSSSSSSSSSSSSSSSSSSyyyyyyySSSSSSabBbbadrdddddr
//   ........abBbbasssSSSSSSSSSSSSSSSSSSSSSyyyyySSSSSssabBbbadrddrddR
//   ........abBbbasssssssSSSSSSSSSSSSSSSSSSyyySSssssssabBbbadddrrddd
//   ........abBbbasssssssssssSSSSSSSSSSSSSSSssssssssssabBbbaRRrRdrRd
//   ........abBbbasssssssssssssssSSSSSSSssssssssssssssabBbbarddrdddR
//   ........abBbbassssssssssssssssssssssssssssssssssssabBbbadrdrdddR
//   ........abBbbassssssssssssssslssssssssssssssssssssabBbbadRddRdrd
//   ........abBbbassssssssssssssslssssssssssssssssssssabBbbaddrdrddd
//   ........abBbbasssssssssssssslllssssslsssssssssssssabBbbaRdrrrrdd
//   ........abBbbassssssssssssslllllsssslsssssssssssssabBbbarddRdddd
//   ........abBbbassssssllsssslllllllssllllsssssssllssabBbbadrdrddrd
//   ........abbbbasssssllllsssllllllllllllllsssssllllsabbbbadrdrRdrd
//   ........abBbbalssslllllssslllllllllllllllssslllllsabBbbaddrdrddr
//   ........abBbballslllllllssllllllllllllllllslllllllabBbbaddRdrrRd
//   ........abBbballllllllllllllllllllllllllllllllllllabBbbarddrdddd
//   ........abBbballllllllllllllllllllllllllllllllllllabBbbadddRddrd
//   ........abBbballllllllllllllllllllllllllllllllllllabBbbarddrddrd
//   ........abBbballllllllllllllllllllllllllllllllllllabBbbaddRdRddr
//   ........abBbballllllllllllllllllllllllllllllllllllabBbbaddrdrRdr
//   ........abBbballLllllLllllLllllLllllLllllLllllLlllabBbbarddddddd
//   ........abBbbaLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLabBbbarddrddRd
//   ........abBbbaGGGGGGGGGgGGGGGGGGGGGGGGGGGGGGGGgGGGabBbbarddrddrd
//   ........abBbbaGGgGGGGGGGGGGGGGGGGGGGGGGgGGGGGGGGGGabBbbadrddRddr
//   ........abBbbaGGGGGGGGGGGGGGGGGGgGGGGGGGGGGGGGGGGGabBbbaddddrddR
//   ........abbbbaGGGGGGGGGGGgGGGGGGGGGGGGGGGGGGGGGGgGabbbbardrdrrdd
//   ........abBbbaGGGGgGGGGGGGGGGGGGGGGGGGGGGgGGGGGGGGabBbbadddRddRd
//   ........abBbbaGGGGGGGGGGGGGGGGGGGGgGGGGGGGGGGGGGGGabBbbaRddrdrdd
//   ........abBbbaGGGGGGGGGGGGGgGGGGGGGGGGGGGGGGGGGGGGabBbbadrddddrd
//   ........abBbbaGGGGGGgGGGGGGGGGGGGGGGGGGGGGGgGGGGGGabBbbadddddddR
//   ........abBbbaGGGGGGGGGGGGGGGGGGGGGGgGGGGGGGGGGGGGabBbbaddrdrrdd
//   ........abBbbaGGGGGGGGGGGGGGGgGGGGGGGGGGGGGGGGGGGGabBbbarddddrdd
//   ........abBbbaGGGGGGGGgGGGGGGGGGGGGGGGGGGGGGGgGGGGabBbbaddrRdrdd
//   ........abBbbaGgGGGGGGGGGGGGGGGGGGGGGGgGGGGGGGGGGGabBbbarrddddrd
//   ........abBbbaGGGGGGGGGGGGGGGGGgGGGGGGGGGGGGGGGGGGabBbbadddddddd
//   ........abBbbaGGGGGgGGGGgGgGGGGGGgGGGGGGgGGGGGGgGGabBbbadrrdrddd
//   ........abBbbaGGggGGGGGgGGGGGGgGGGGGGgGGgGGGgGGGGGabBbbaRdrdrddd
//   ........abBbbaggggggggggggggggggggggggggggggggggggabBbbaddrddrdr
//   ........abbbbaggggggggggggggggggggggggggggggggggggabbbbarRddddrd
//   ........aaaaaaggggggggggggggggggggggggggggggggggggaaaaaadrdddddd
//
// The ceiling's rock (MT_CEILING), and its lowest row (MT_CEILING_EDGE).
//   ddddrdddddddKddd
//   dddrRrddddddddrd
//   ddddrddKddddddRr
//   dKdddddddrRdddrd
//   ddddddddrRRrdddd
//   ddrdddddddrddddK
//   drRrddKddddddddd
//   ddrddddddddrdddd
//   dddddddrddddddrd
//   ddddKdrRrddKdddd
//   dddddddrdddddddd
//   drdddddddddddrRd
//   rRrddddrddddddrd
//   drdddKdRrddddddd
//   ddddddddrdddKddd
//   ddKddddddddddddd
//
//   ddddrdddddddKddd
//   dddrRrddddddddrd
//   ddddrddKddddddRr
//   dKdddddddrRdddrd
//   ddddddddrRRrdddd
//   ddrdddddddrddddK
//   drRrddKddddddddd
//   ddrddddddddrdddd
//   dddddddrddddddrd
//   ddddKddrrddKdddd
//   dddddddRdddddddd
//   dRddddddddrdddRd
//   rrdddddRdddddrrd
//   KdddKdddddKdddKd
//   .KdK.KddK.dK.Kd.
//   ..K...KK...K..K.
//
// Crystal spikes (MT_SPIKES): solid, and tagged TAG_HAZARD.
//   ................
//   .......w........
//   ......wC........
//   ...w..CCc....w..
//   ..wC..CCc...wC..
//   ..CCc.CCc..wCCc.
//   ..CCc.CCcw.CCCc.
//   .wCCcwCCcC.CCCc.
//   .CCCcCCCcCcCCCc.
//   .CCCcCCCcCcCCCcw
//   wCCCcCCCcCcCCCcC
//   CCCCcCCCcCcCCCcC
//   KcccKcccKcKcccKc
//   dKKKdKKKdKdKKKdK
//   dddddddddddddddd
//   dddddddddddddddd
//
// Stalactites (MT_STALACTITES): decoration hanging from the ceiling.
//   .dRrd....drRd...
//   .dRrd.....dRd...
//   ..dRd.....drd...
//   ..drd......d....
//   ..dRd......d....
//   ...rd...........
//   ...d............
//   ...d............
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//
// Glowing mushrooms (MT_MUSHROOMS): in front of the sprites (background 1).
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ..........nNNn..
//   ...nNn...nNWNNn.
//   ..nNWNn..nnNNnn.
//   ..nnnnn....SS...
//   ....S......Ss...
//   ....Ss.....Ss...
//   ...gSsg...gSsg..
//   ..gGSsGg.gGSsGg.
//   .gGgGGgGgGgGGgGg
//   gGGgGgGGgGgGgGGg
//
const Metatile underground_metatiles[UG_MT_COUNT] = {
    [MT_EMPTY] = {{0, 0, 0, 0}, MAP_EMPTY},
    [MT_GROUND_TOP] = {{MAP_SE(15, 0, 0), MAP_SE(16, 0, 0), MAP_SE(17, 0, 0), MAP_SE(18, 0, 0)},
                       MAP_SOLID},
    [MT_GROUND] = {{MAP_SE(19, 0, 0), MAP_SE(20, 0, 0), MAP_SE(17, 0, 0), MAP_SE(18, 0, 0)},
                   MAP_SOLID},
    [MT_STONE] = {{MAP_SE(21, 0, 0), MAP_SE(22, 0, 0), MAP_SE(23, 0, 0), MAP_SE(24, 0, 0)},
                  MAP_SOLID},
    [MT_BRICK] = {{MAP_SE(13, 1, 0), MAP_SE(13, 1, 0), MAP_SE(14, 1, 0), MAP_SE(14, 1, 0)},
                  MAP_SOLID | TAG_BRICK},
    [MT_GEM_BRICK] = {{MAP_SE(13, 1, 0), MAP_SE(13, 1, 0), MAP_SE(14, 1, 0), MAP_SE(14, 1, 0)},
                      MAP_SOLID | TAG_BRICK | TAG_BONUS},
    [MT_BONUS] = {{MAP_SE(1, 1, 0), MAP_SE(2, 1, 0), MAP_SE(3, 1, 0), MAP_SE(4, 1, 0)},
                  MAP_SOLID | TAG_BONUS},
    [MT_BONUS_FISH] = {{MAP_SE(1, 1, 0), MAP_SE(2, 1, 0), MAP_SE(3, 1, 0), MAP_SE(4, 1, 0)},
                       MAP_SOLID | TAG_BONUS},
    [MT_USED] = {{MAP_SE(5, 1, 0), MAP_SE(6, 1, 0), MAP_SE(7, 1, 0), MAP_SE(8, 1, 0)}, MAP_SOLID},
    [MT_HIDDEN] = {{0, 0, 0, 0}, MAP_SOLID},
    [MT_GEM] = {{MAP_SE(9, 2, 0), MAP_SE(10, 2, 0), MAP_SE(11, 2, 0), MAP_SE(12, 2, 0)},
                MAP_EMPTY | TAG_GEM},
    [MT_ONEWAY] = {{MAP_SE(41, 1, 0), MAP_SE(42, 1, 0), MAP_SE(43, 1, 0), 0}, MAP_ONEWAY},
    [MT_ONEWAY + 1] = {{MAP_SE(42, 1, MAP_SE_FLIP_H), MAP_SE(41, 1, MAP_SE_FLIP_H), 0,
                        MAP_SE(43, 1, MAP_SE_FLIP_H)},
                       MAP_ONEWAY},
    // No goal pole in the underground: its exit ends the stage.
    [MT_POLE_TOP] = {{0, 0, 0, 0}, MAP_EMPTY},
    [MT_POLE] = {{0, 0, 0, 0}, MAP_EMPTY},
    [MT_EXIT] = {{MAP_SE(44, 3, 0), MAP_SE(45, 3, 0), 0, MAP_SE(46, 3, 0)}, MAP_EMPTY},
    [MT_EXIT + 1] = {{MAP_SE(47, 3, 0), MAP_SE(48, 3, 0), MAP_SE(49, 3, 0), MAP_SE(50, 3, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 2] = {{MAP_SE(51, 3, 0), MAP_SE(52, 3, 0), MAP_SE(53, 3, 0), MAP_SE(54, 3, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 3] = {{MAP_SE(55, 3, 0), MAP_SE(56, 3, 0), MAP_SE(57, 3, 0), MAP_SE(58, 3, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 4] = {{0, MAP_SE(59, 3, 0), 0, MAP_SE(60, 3, 0)}, MAP_EMPTY},
    [MT_EXIT + 5] = {{MAP_SE(61, 3, 0), MAP_SE(62, 3, 0), MAP_SE(63, 3, 0), MAP_SE(64, 3, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 6] = {{MAP_SE(65, 3, 0), MAP_SE(66, 3, 0), MAP_SE(67, 3, 0), MAP_SE(68, 3, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 7] = {{MAP_SE(69, 3, 0), MAP_SE(70, 3, 0), MAP_SE(71, 3, 0), MAP_SE(72, 3, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 8] = {{0, MAP_SE(73, 3, 0), 0, MAP_SE(74, 3, 0)}, MAP_EMPTY},
    [MT_EXIT + 9] = {{MAP_SE(75, 3, 0), MAP_SE(76, 3, 0), MAP_SE(77, 3, 0), MAP_SE(78, 3, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 10] = {{MAP_SE(79, 3, 0), MAP_SE(80, 3, 0), MAP_SE(81, 3, 0), MAP_SE(82, 3, 0)},
                      MAP_EMPTY},
    [MT_EXIT + 11] = {{MAP_SE(83, 3, 0), MAP_SE(84, 3, 0), MAP_SE(85, 3, 0), MAP_SE(86, 3, 0)},
                      MAP_EMPTY},
    [MT_CEILING] = {{MAP_SE(25, 0, 0), MAP_SE(26, 0, 0), MAP_SE(27, 0, 0), MAP_SE(28, 0, 0)},
                    MAP_SOLID},
    [MT_CEILING_EDGE] = {{MAP_SE(25, 0, 0), MAP_SE(26, 0, 0), MAP_SE(29, 0, 0), MAP_SE(30, 0, 0)},
                         MAP_SOLID},
    [MT_SPIKES] = {{MAP_SE(33, 0, 0), MAP_SE(34, 0, 0), MAP_SE(35, 0, 0), MAP_SE(36, 0, 0)},
                   MAP_SOLID | TAG_HAZARD},
    [MT_STALACTITES] = {{MAP_SE(31, 0, 0), MAP_SE(32, 0, 0), 0, 0}, MAP_EMPTY},
    [MT_MUSHROOMS] = {{MAP_SE(37, 0, 0), MAP_SE(38, 0, 0), MAP_SE(39, 0, 0), MAP_SE(40, 0, 0)},
                      MAP_EMPTY},
};

// The parallax layer: columns of rock in the dark, 24 metatiles cut from a
// 128x64 picture that repeats both ways (its gaps show the backdrop), drawn
// in BANK_FAR. The left and right halves:
//   ......dRRrrrrrrd..................dRrrrd.....................dRr
//   ......dRrrrrrrrd..................dRrrrd.....................dRr
//   ......dRdrrrrrrd..................dRrrrd.....................dRr
//   ......dRrrrrdrrd..................dRrrrd.....................dRr
//   ......dRrrrrrrrd..................dRrrdd....................dRrr
//   ......dRrrrrrrrd..................dRrrrd....................dRRr
//   ......dRrrrrrrrd..................dRrrrd....................dRrr
//   ......dRrrrdrrrd..................dRrrrd....................dRrr
//   .......dRrrrrrrdrd................dRrdd.....................dRrr
//   .......dRRrrrrrrrd................dRrrd.....................dRrd
//   .......dRrrrrrrrrd................dRrrd.....................dRrr
//   .......dRrdrrrrrrd................dRrrd.....................dRrr
//   .......dRrrrrrdrrd...............dRrdd.....................dRrrr
//   .......dRrrrrrrrrd...............dRrrd.....................dRrdr
//   .......dRrrrrrrrrd...............dRrrd.....................dRRrr
//   .......dRdrrrrrrrd...............dRrrd.....................dRrrr
//   .......dRrrrrdrrrd...............dRdd......................dRrrr
//   .......dRrrrrrrrrd...............dRrd......................dRdrr
//   .......dRRrrrrrrrd...............dRrd......................dRrrr
//   .......dRrrrrrrrrd...............dRrd......................dRrrr
//   ......dRrrrrdrrrd...............dRdd.......................dRrrr
//   ......dRrrrrrrrrd...............dRrd.......................dRrrr
//   ......dRrrrrrrrrd...............dRrd.......................dRrrr
//   ......dRrrrrrrrrd...............dRrd.......................dRRrr
//   ......dRrrrdrrrd................dRrd........................dRrr
//   ......dRrrrrrrrd................dRrd........................dRrr
//   ......dRrrrrrrrd................dRrd........................dRrd
//   ......dRRrrrrrrd................dRrd........................dRrr
//   .....dRrrrdrrrd.................dRrd........................dRrr
//   .....dRrrrrrrrd.................dRrd........................dRrr
//   .....dRrrrrrrrd.................dRrd........................dRdr
//   .....dRrrrrrrrd.................dRrd........................dRrr
//   .....dRrrdrrrd...................dRrrd.......................dRR
//   .....dRrrrrrrd...................dRdrd.......................dRr
//   .....dRrrrrrrd...................dRrrd.......................dRr
//   .....dRrrrrrrd...................dRrrd.......................dRr
//   .....dRRdrrrrd...................dRrrd.......................dRr
//   .....dRrrrrrdd...................dRrrd.......................dRr
//   .....dRrrrrrrd...................dRrrd.......................dRr
//   .....dRrrrrrrd...................dRrrd.......................dRr
//   ......dRrrrrrrd...................dRrrrd....................dRrr
//   ......dRrrrdrrd...................dRrrrd....................dRRr
//   ......dRrrrrrrd...................dRrdrd....................dRrr
//   ......dRrrrrrrd...................dRrrrd....................dRrd
//   ......dRrrrrrrd...................dRrrrd...................dRrrr
//   ......dRRrdrrrd...................dRrrrd...................dRrrr
//   ......dRrrrrrrd...................dRdrrd...................dRrrr
//   ......dRrrrrrrd...................dRrrrd...................dRrdr
//   .......dRrrrrrrrd................dRrrd......................dRrr
//   .......dRdrrrrrrd................dRrrd......................dRrr
//   .......dRrrrrdrrd................dRdrd......................dRRr
//   .......dRrrrrrrrd................dRrrd......................dRrr
//   .......dRrrrrrrrd...............dRrrd.......................dRrr
//   .......dRrrrrrrrd...............dRrrd.......................dRrr
//   .......dRRrrdrrrd...............dRdrd.......................dRrr
//   .......dRrrrrrrrd...............dRrrd.......................dRrr
//   ......dRrrrrrrrrd................dRrrrd......................dRr
//   ......dRrrrrrrrrd................dRrrrd......................dRr
//   ......dRrrrdrrrrd................dRrrrd......................dRr
//   ......dRrrrrrrrdd................dRrrdd......................dRR
//   .....dRrrrrrrrrd.................dRrrrd......................dRd
//   .....dRrrrrrrrrd.................dRrrrd......................dRr
//   .....dRrrrdrrrrd.................dRrrrd......................dRr
//   .....dRRrrrrrrdd.................dRrdrd......................dRr
//
//   rrrd.............................dRrdd..........................
//   rdrd.............................dRrrd..........................
//   rrrd.............................dRrrd..........................
//   rrrd.............................dRrrd..........................
//   rrd.............................dRrdd...........................
//   drd.............................dRrrd...........................
//   rrd.............................dRrrd...........................
//   rrd.............................dRrrd...........................
//   rrd.............................dRdrrd..........................
//   rrd.............................dRrrrd..........................
//   rrd.............................dRrrrd..........................
//   rrd.............................dRrrrd..........................
//   rd..............................dRrrrd..........................
//   rd..............................dRrrrd..........................
//   rd..............................dRrrrd..........................
//   rd..............................dRrrrd..........................
//   rrd..............................dRrrd..........................
//   rrd..............................dRrdd..........................
//   rdd..............................dRrrd..........................
//   rrd..............................dRrrd..........................
//   rrd..............................dRrrd..........................
//   rrd..............................dRdrd..........................
//   drd..............................dRrrd..........................
//   rrd..............................dRrrd..........................
//   rrrrd.............................dRrrrd........................
//   rrrrd.............................dRrrrd........................
//   rrrrd.............................dRrrdd........................
//   rrrdd.............................dRrrrd........................
//   rrrrd.............................dRrrrd........................
//   rrrrd.............................dRrrrd........................
//   rrrrd.............................dRrdrd........................
//   rrdrd.............................dRrrrd........................
//   rrrrd............................dRrrrd.........................
//   rrrrd............................dRrrrd.........................
//   rrrrd............................dRrdrd.........................
//   rdrrd............................dRrrrd.........................
//   rrrrd...........................dRrrrd..........................
//   rrrrd...........................dRrrrd..........................
//   rrrrd...........................dRrdrd..........................
//   drrrd...........................dRrrrd..........................
//   rrrrd............................dRrrd..........................
//   rrrrd............................dRrrd..........................
//   rrrrd............................dRrrd..........................
//   rrrrd............................dRrrd..........................
//   rrrd.............................dRrrd..........................
//   rrrd.............................dRrrd..........................
//   rrrd.............................dRrrd..........................
//   rrrd.............................dRrrd..........................
//   rrdrd.............................dRrd..........................
//   rrrrd.............................dRrd..........................
//   rrrrd.............................dRrd..........................
//   rrrrd.............................dRdd..........................
//   rdrrd.............................dRrd..........................
//   rrrrd.............................dRrd..........................
//   rrrrd.............................dRrd..........................
//   rrrrd.............................dRrd..........................
//   drrrd.............................dRrd..........................
//   rrrrd.............................dRrd..........................
//   rrrrd.............................dRrd..........................
//   rrrrd.............................dRrd..........................
//   rrrrd............................dRrd...........................
//   rrrdd............................dRrd...........................
//   rrrrd............................dRrd...........................
//   rrrrd............................dRrd...........................
static const Metatile far_metatiles[24] = {
    {{MAP_SE(87, 4, 0), MAP_SE(88, 4, 0), MAP_SE(89, 4, 0), MAP_SE(90, 4, 0)}, MAP_EMPTY},
    {{0, 0, MAP_SE(91, 4, 0), 0}, MAP_EMPTY},
    {{MAP_SE(92, 4, 0), 0, MAP_SE(93, 4, 0), 0}, MAP_EMPTY},
    {{0, MAP_SE(94, 4, 0), 0, MAP_SE(95, 4, 0)}, MAP_EMPTY},
    {{MAP_SE(96, 4, 0), 0, MAP_SE(97, 4, 0), 0}, MAP_EMPTY},
    {{0, 0, 0, 0}, MAP_EMPTY},
    {{MAP_SE(98, 4, 0), 0, MAP_SE(99, 4, 0), 0}, MAP_EMPTY},
    {{MAP_SE(100, 4, 0), MAP_SE(101, 4, 0), MAP_SE(102, 4, 0), MAP_SE(103, 4, 0)}, MAP_EMPTY},
    {{MAP_SE(104, 4, 0), 0, 0, 0}, MAP_EMPTY},
    {{MAP_SE(105, 4, 0), 0, MAP_SE(106, 4, 0), 0}, MAP_EMPTY},
    {{0, MAP_SE(107, 4, 0), 0, MAP_SE(108, 4, 0)}, MAP_EMPTY},
    {{MAP_SE(109, 4, 0), 0, MAP_SE(110, 4, 0), 0}, MAP_EMPTY},
    {{MAP_SE(111, 4, 0), 0, MAP_SE(112, 4, 0), 0}, MAP_EMPTY},
    {{MAP_SE(113, 4, 0), MAP_SE(114, 4, 0), MAP_SE(87, 4, 0), MAP_SE(115, 4, 0)}, MAP_EMPTY},
    {{MAP_SE(116, 4, 0), 0, MAP_SE(117, 4, 0), 0}, MAP_EMPTY},
    {{0, MAP_SE(118, 4, 0), 0, MAP_SE(119, 4, 0)}, MAP_EMPTY},
    {{MAP_SE(120, 4, 0), 0, MAP_SE(121, 4, 0), 0}, MAP_EMPTY},
    {{MAP_SE(122, 4, 0), 0, MAP_SE(123, 4, 0), 0}, MAP_EMPTY},
    {{MAP_SE(89, 4, 0), MAP_SE(124, 4, 0), MAP_SE(125, 4, 0), MAP_SE(126, 4, 0)}, MAP_EMPTY},
    {{MAP_SE(89, 4, MAP_SE_FLIP_H), 0, MAP_SE(127, 4, 0), 0}, MAP_EMPTY},
    {{MAP_SE(128, 4, 0), 0, MAP_SE(129, 4, 0), 0}, MAP_EMPTY},
    {{0, MAP_SE(130, 4, 0), 0, MAP_SE(131, 4, 0)}, MAP_EMPTY},
    {{MAP_SE(132, 4, 0), 0, MAP_SE(133, 4, 0), 0}, MAP_EMPTY},
    {{MAP_SE(134, 4, 0), 0, MAP_SE(135, 4, 0), 0}, MAP_EMPTY},
};

static const u16 far_cells[8 * 4] = {
    0,  1, 2,  3,  4,  5, 6,  5, 7,  8,  9,  10, 11, 5, 12, 5,
    13, 5, 14, 15, 16, 5, 17, 5, 18, 19, 20, 21, 22, 5, 23, 5,
};

const MapLayer underground_far_layer = {
    .width = 8,
    .height = 4,
    .cells = far_cells,
    .metatiles = far_metatiles,
    .metatile_count = 24,
    .bg = 3,
    .flags = MAP_LAYER_WRAP,
    .scroll_factor = FX_ONE / 2,
};
