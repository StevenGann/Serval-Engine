// Generated from ASCII pixel art by a conversion script (kept outside the
// repository), the same way as art.c (its opening comment says how); the
// previews in the comments are that art, one character per pixel. The bonus
// block, used block, gem and bricks reuse the overworld's pixels (in other
// colors for the bricks), so a stage's bonus block glints with art.c's frames.
// The bigger pictures (the dragon, the gate, the far wall) were drawn by
// scripts too, from shapes, and are shown here as they came out.
//
// Stage 1-4, the castle: the salamander, fire, the dragon, and the tileset,
// metatiles and parallax layer of grey stone halls over lava.

#include "stage_castle.h"

// --- Sprites ------------------------------------------------------------------

// The salamander (the walker, 'e'): walk 1, walk 2 (SPR_SALAMANDER), flat
// when stomped (SPR_SALAMANDER_FLAT). Faces right.
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ...........KKK..
//   ..........KmwKK.
//   ..KK.KKKKKKmmmmK
//   .KdmKddyddmmdddK
//   .KdddyydddyymdK.
//   ..KdddddyddddKK.
//   ...KoKKooKKoKK..
//   ...K.K..K..K....
//   ..KK.....KK.....
//   ................
//
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ..KKKKKKKKKKKKK.
//   .KdddyddddyymmwK
//   KddyydddyddddmmK
//   .KKKKKKKKKKKKKK.
static const u32 salamander_tiles[96] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00111000, 0x01173100,
    0x11101100, 0x42213210, 0x24422210, 0x22222100, 0x61161000, 0x00101000, 0x00001100, 0x00000000,
    0x13333111, 0x12223322, 0x01234422, 0x01122224, 0x00116116, 0x00001001, 0x00000110, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00111000, 0x01173100,
    0x11100110, 0x42201321, 0x24411210, 0x22222100, 0x61161000, 0x01010000, 0x00110000, 0x00000000,
    0x13333111, 0x12223322, 0x01234422, 0x01122224, 0x00116116, 0x00001010, 0x00000110, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x11111100, 0x22422210, 0x22244221, 0x11111110,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x01111111, 0x17334422, 0x13322224, 0x01111111,
};

// A ball of a fire bar, 8x8: two frames, flickering.
//   ..KKKK..
//   .KroorK.
//   KroyyorK
//   KoywwyoK
//   KoywwyoK
//   KroyyorK
//   .KroorK.
//   ..KKKK..
static const u32 fireball_tiles[16] = {
    0x00111100, 0x01233210, 0x12344321, 0x13455431, 0x13455431, 0x12344321, 0x01233210, 0x00111100,
    0x00011000, 0x01133110, 0x01344310, 0x13455431, 0x13455431, 0x01344310, 0x01133110, 0x00011000,
};

// An ember leaping out of the lava: two frames, flickering.
//   ................
//   .......r........
//   ....r.KKKK.r....
//   .....KooyoK.....
//   ....KoyywyoK....
//   ...rKoywwyoKr...
//   ....KoyywyoK....
//   ....KroyyorK....
//   .....KKrrKK.....
//   .....r....r.....
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
static const u32 ember_tiles[64] = {
    0x00000000, 0x20000000, 0x11020000, 0x33100000, 0x44310000, 0x54312000, 0x44310000, 0x43210000,
    0x00000000, 0x00000000, 0x00002011, 0x00000134, 0x00001345, 0x00021345, 0x00001345, 0x00001234,
    0x21100000, 0x00200000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000112, 0x00000200, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x11200000, 0x33102000, 0x44310000, 0x54310000, 0x44312000, 0x43210000,
    0x00000000, 0x00000000, 0x00000211, 0x00020134, 0x00001345, 0x00001345, 0x00021345, 0x00001234,
    0x21100000, 0x02000000, 0x20000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000112, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

// The dragon's fireball, flying left: two frames.
//   ................
//   ................
//   ................
//   .......KKK......
//   .....KKoorKK.r..
//   ....KoyyyoorrK..
//   ...KyywwyyoorKr.
//   ...KywwwwyyoorK.
//   ...KywwwwyyoorK.
//   ...KyywwyyoorKr.
//   ....KoyyyoorrK..
//   .....KKoorKK.r..
//   .......KKK......
//   ................
//   ................
//   ................
static const u32 breath_tiles[64] = {
    0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x31100000, 0x44310000, 0x55441000, 0x55541000,
    0x00000000, 0x00000000, 0x00000000, 0x00000011, 0x00201123, 0x00122334, 0x02123344, 0x01233445,
    0x55541000, 0x55441000, 0x44310000, 0x31100000, 0x10000000, 0x00000000, 0x00000000, 0x00000000,
    0x01233445, 0x02123344, 0x00122334, 0x00201123, 0x00000011, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x31100000, 0x44310000, 0x55441000, 0x55541000,
    0x00000000, 0x00000000, 0x00000000, 0x00200011, 0x00021123, 0x02122334, 0x01223344, 0x01233445,
    0x55541000, 0x55441000, 0x44310000, 0x31100000, 0x10000000, 0x00000000, 0x00000000, 0x00000000,
    0x01233445, 0x01223344, 0x02122334, 0x00021123, 0x00200011, 0x00000000, 0x00000000, 0x00000000,
};

// The dragon: a metasprite (SPR_DRAGON), 48x48, each frame nine pieces in a
// 3 x 3 grid, frames of SPR_DRAGON_PART: its 50 different 16x16 parts (the
// frames share the parts that don't change). Faces left. Its frames, DRAGON_*
// (stage_castle.h): standing, walking (two), crouching before a hop, hopping
// with its wings spread, breathing fire, roaring, and hurt as it falls.
//
// stand:
//   .....................KK................K........
//   ...................KKK..............KKK.......K.
//   .................KKHK...........KKKKwwK....KKK..
//   ...............KKHHK..KKKKK.....KKwwwK.KKKKWWK..
//   ...............KHHK.KKhKK......KKKKwwK.KKWWWK...
//   ..............KHHKKKhKK.......KKKKwKK.KKddWWK...
//   .............KHHKKhhK.........KKwwKwKKdKdWdK....
//   ............KHhhKhhhK........KKKwwKwwKKWWdWd....
//   .........KKKhhhKhhhK........KKKwwwKKKdKWWdWWd...
//   ......KKKLLKhhhKhhK.........KKKwwwwKdKWWWddWWd..
//   ....KKLLKddKhhhKhKlK.......KKKwwwwwKdKWWWWdWWWd.
//   ...KLLLLLlKKKKKKKlllK......KKKwwwwKdKWWWWWdWWWK.
//   ..KlllllllKeeKlllllmK.....KKKwwwwwKdKWWWWWddWK..
//   ..KKllllllllllmmmmmmK....KKKKwwwwKdKWWWWWWWdK...
//   .KllllllmmmmmmmmmmmmKK...KKKwwwwKddKWWWWWWWd....
//   .KlmmmmmmmmmmmmmmmmdKLK.KKKKwwwwKdKWWWWWWWKdd...
//   .KmmmmmmmmmmmddKKKKdKLK.KKKwwwwKddKWWWWWWWK.d...
//   .KKKmmKKKKKKKKKmmmKKKLLKKKKwwwwKdKWWWWWWWwwKd...
//   ..KKKKKKKKKKKKKmmmmKLLlKKKwwwwKddKWWWWwwwwwwKd..
//   ..KmtmmmtmmmtmdddddKLlllKKwwwwKdKWWWwwwwwwwwKd..
//   ...KKKKmddddddddKKKKllllKwwwwKddKWwwwwwwwwwwwd..
//   .......KKKKKKKKKKKKKKllllKwwKddKwwwwwwwwwwKKK...
//   ...............KBBBBBlllmKwwKddKwwwwwwwKKK......
//   ................KBBBBBlmmmKKddKwwwwwwwK.........
//   ................KKKKKKmmmmmKddKwwwwwwK..........
//   ................KBBBBBBmmmmdKKwwwwwwK...........
//   .................KBBBBBmmmdddKKKwwwK............
//   .................KKKKKKKmddddKllKKKK............
//   ..................KBBBBBdddddKllllllK.......KK..
//   ..................KKBKKKKddddKllllllmK......KK..
//   .................KLKKlmmKddddKlllmmmmmK....KwwK.
//   .................KKKmmmmKddddKlmmmmmmmK....KwwK.
//   ...............KKKmmmmmdKBddKmmmmmmmKBK...KwwwwK
//   ..............KmmmmmdKKKKKKKmmmmmmmBbBK....KwwK.
//   ..............tmmddKKllKKmmmmmmmmBbBBKK....KKKK.
//   ...............KKKKllllmKmmmmmmBbBBKBBK....KmmK.
//   ................KlllllmmmKmmmmbBBKBBbKlK...KmmK.
//   ................KllllmmmmKmmbBBKBBbBKKmmK..KmmK.
//   ................KllmmmmmmKdBBKBBbBKKdKmmmKKmddK.
//   ................KlmmmmmmdKBKBBbBKKKddKmmmmddddK.
//   .................KmmmmmdKKKKKKKKKdddKmmmdddddKK.
//   .................KmmmddKK....KKdddddKKddddddK...
//   ..................KmdKK........KdddK..KKddKK....
//   ................KKKKKK.........KKKKKK...KK......
//   ...............KllmmmK........KmmmmdK...........
//   ..............KmmmdddK.......KmdddddK...........
//   ..............tKKKKKKK.......tKKKKKKK...........
//   ................t..............t................
//
// walk1:
//   .....................KK................K........
//   ...................KKK..............KKK.......K.
//   .................KKHK...........KKKKwwK....KKK..
//   ...............KKHHK..KKKKK.....KKwwwK.KKKKWWK..
//   ...............KHHK.KKhKK......KKKKwwK.KKWWWK...
//   ..............KHHKKKhKK.......KKKKwKK.KKddWWK...
//   .............KHHKKhhK.........KKwwKwKKdKdWdK....
//   ............KHhhKhhhK........KKKwwKwwKKWWdWd....
//   .........KKKhhhKhhhK........KKKwwwKKKdKWWdWWd...
//   ......KKKLLKhhhKhhK.........KKKwwwwKdKWWWddWWd..
//   ....KKLLKddKhhhKhKlK.......KKKwwwwwKdKWWWWdWWWd.
//   ...KLLLLLlKKKKKKKlllK......KKKwwwwKdKWWWWWdWWWK.
//   ..KlllllllKeeKlllllmK.....KKKwwwwwKdKWWWWWddWK..
//   ..KKllllllllllmmmmmmK....KKKKwwwwKdKWWWWWWWdK...
//   .KllllllmmmmmmmmmmmmKK...KKKwwwwKddKWWWWWWWd....
//   .KlmmmmmmmmmmmmmmmmdKLK.KKKKwwwwKdKWWWWWWWKdd...
//   .KmmmmmmmmmmmddKKKKdKLK.KKKwwwwKddKWWWWWWWK.d...
//   .KKKmmKKKKKKKKKmmmKKKLLKKKKwwwwKdKWWWWWWWwwKd...
//   ..KKKKKKKKKKKKKmmmmKLLlKKKwwwwKddKWWWWwwwwwwKd..
//   ..KmtmmmtmmmtmdddddKLlllKKwwwwKdKWWWwwwwwwwwKd..
//   ...KKKKmddddddddKKKKllllKwwwwKddKWwwwwwwwwwwwd..
//   .......KKKKKKKKKKKKKKllllKwwKddKwwwwwwwwwwKKK...
//   ...............KBBBBBlllmKwwKddKwwwwwwwKKK......
//   ................KBBBBBlmmmKKddKwwwwwwwK.........
//   ................KKKKKKmmmmmKddKwwwwwwK..........
//   ................KBBBBBBmmmmdKKwwwwwwK...........
//   .................KBBBBBmmmdddKKKwwwK............
//   .................KKKKKKKmddddKllKKKK............
//   ..................KBBBBBdddddKllllllK.......KK..
//   ..................KKBKKKKddddKllllllmK......KK..
//   .................KLKKlmmKddddKlllmmmmmK....KwwK.
//   .................KKKmmmmKddddKlmmmmmmmK....KwwK.
//   ...............KKKmmmmmdKBddKmmmmmmmKBK...KwwwwK
//   ..............KmmmmmdKKKKKKKmmmmmmmBbBK....KwwK.
//   ..............tmmddKKllKKmmmmmmmmBbBBKK....KKKK.
//   ...............KKKKllllmKmmmmmmBbBBKBBK....KmmK.
//   ................KllllmmmmKmmmmbBBKBBbKlK...KmmK.
//   ................KlllmmmmmKmmbBBKBBbBKKmmK..KmmK.
//   ................KllmmmmmmKdBBKBBbBKKdKmmmKKmddK.
//   ................KmmmmmmmdKBKBBbBKKKddKmmmmddddK.
//   .................KmmmmddKKKKKKKKKdddKmmmdddddKK.
//   ................KmmmmddKK....KKdddddKKddddddK...
//   ................KmmKKKK........KKKddK.KKddKK....
//   .............KKKKKK...............KKKKKKKK......
//   ............KllmmmK..............KmmmmdK........
//   ...........KmmmdddK.............KmdddddK........
//   ...........tKKKKKKK.............tKKKKKKK........
//   .............t....................t.............
//
// walk2:
//   .....................KK................K........
//   ...................KKK..............KKK.......K.
//   .................KKHK...........KKKKwwK....KKK..
//   ...............KKHHK..KKKKK.....KKwwwK.KKKKWWK..
//   ...............KHHK.KKhKK......KKKKwwK.KKWWWK...
//   ..............KHHKKKhKK.......KKKKwKK.KKddWWK...
//   .............KHHKKhhK.........KKwwKwKKdKdWdK....
//   ............KHhhKhhhK........KKKwwKwwKKWWdWd....
//   .........KKKhhhKhhhK........KKKwwwKKKdKWWdWWd...
//   ......KKKLLKhhhKhhK.........KKKwwwwKdKWWWddWWd..
//   ....KKLLKddKhhhKhKlK.......KKKwwwwwKdKWWWWdWWWd.
//   ...KLLLLLlKKKKKKKlllK......KKKwwwwKdKWWWWWdWWWK.
//   ..KlllllllKeeKlllllmK.....KKKwwwwwKdKWWWWWddWK..
//   ..KKllllllllllmmmmmmK....KKKKwwwwKdKWWWWWWWdK...
//   .KllllllmmmmmmmmmmmmKK...KKKwwwwKddKWWWWWWWd....
//   .KlmmmmmmmmmmmmmmmmdKLK.KKKKwwwwKdKWWWWWWWKdd...
//   .KmmmmmmmmmmmddKKKKdKLK.KKKwwwwKddKWWWWWWWK.d...
//   .KKKmmKKKKKKKKKmmmKKKLLKKKKwwwwKdKWWWWWWWwwKd...
//   ..KKKKKKKKKKKKKmmmmKLLlKKKwwwwKddKWWWWwwwwwwKd..
//   ..KmtmmmtmmmtmdddddKLlllKKwwwwKdKWWWwwwwwwwwKd..
//   ...KKKKmddddddddKKKKllllKwwwwKddKWwwwwwwwwwwwd..
//   .......KKKKKKKKKKKKKKllllKwwKddKwwwwwwwwwwKKK...
//   ...............KBBBBBlllmKwwKddKwwwwwwwKKK......
//   ................KBBBBBlmmmKKddKwwwwwwwK.........
//   ................KKKKKKmmmmmKddKwwwwwwK..........
//   ................KBBBBBBmmmmdKKwwwwwwK...........
//   .................KBBBBBmmmdddKKKwwwK............
//   .................KKKKKKKmddddKllKKKK........KK..
//   ..................KBBBBBdddddKllllllK.......KK..
//   ..................KKBKKKKddddKllllllmK.....KwwK.
//   .................KLKKlmmKddddKlllmmmmmK....KwwK.
//   .................KKKmmmmKddddKlmmmmmmmK...KwwwwK
//   ...............KKKmmmmmdKBddKmmmmmmmKBK....KwwK.
//   ..............KmmmmmdKKKKKKKmmmmmmmBbBK....KKKK.
//   ..............tmmddKKllKKmmmmmmmmBbBBKK....KmmK.
//   ...............KKKKllllmKmmmmmmBbBBKBBK....KmmK.
//   ................KlllllmmmKmmmmbBBKBBbKlK...KmmK.
//   ................KllllmmmmKmmbBBKBBbBKKmmK.KmmdK.
//   ................KllmmmmmmKdBBKBBbBKKdKmmmKmdddK.
//   ................KlmmmmmmdKBKBBbBKKKddKmmmddddK..
//   .................KmmmmmdKKKKKKKKKdddKmmdddddK...
//   .................KKmmddKK....KmddddKKKdddddK....
//   ...................KddK.......KdddK...KKddK.....
//   ..................KKKKKK....KKKKKK......KK......
//   .................KllmmmK...KmmmmdK..............
//   ................KmmmdddK..KmdddddK..............
//   ................tKKKKKKK..tKKKKKKK..............
//   ..................t.........t...................
//
// crouch:
//   ................................................
//   ................................................
//   ................................................
//   ................................................
//   .....................KK................K........
//   ...................KKK..............KKK.......K.
//   .................KKHK...........KKKKwwK....KKK..
//   ...............KKHHK..KKKKK.....KKwwwK.KKKKWWK..
//   ...............KHHK.KKhKK......KKKKwwK.KKWWWK...
//   ..............KHHKKKhKK.......KKKKwKK.KKddWWK...
//   .............KHHKKhhK.........KKwwKwKKdKdWdK....
//   ............KHhhKhhhK........KKKwwKwwKKWWdWd....
//   .........KKKhhhKhhhK........KKKwwwKKKdKWWdWWd...
//   ......KKKLLKhhhKhhK.........KKKwwwwKdKWWWddWWd..
//   ....KKLLKddKhhhKhKlK.......KKKwwwwwKdKWWWWdWWWd.
//   ...KLLLLLlKKKKKKKlllK......KKKwwwwKdKWWWWWdWWWK.
//   ..KlllllllKeeKlllllmK.....KKKwwwwwKdKWWWWWddWK..
//   ..KKllllllllllmmmmmmK....KKKKwwwwKdKWWWWWWWdK...
//   .KllllllmmmmmmmmmmmmKK...KKKwwwwKddKWWWWWWWd....
//   .KlmmmmmmmmmmmmmmmmdKLK.KKKKwwwwKdKWWWWWWWKdd...
//   .KmmmmmmmmmmmddKKKKdKLK.KKKwwwwKddKWWWWWWWK.d...
//   .KKKmmKKKKKKKKKmmmKKKLLKKKKwwwwKdKWWWWWWWwwKd...
//   ..KKKKKKKKKKKKKmmmmKLLlKKKwwwwKddKWWWWwwwwwwKd..
//   ..KmtmmmtmmmtmdddddKLlllKKwwwwKdKWWWwwwwwwwwKd..
//   ...KKKKmddddddddKKKKllllKwwwwKddKWwwwwwwwwwwwd..
//   .......KKKKKKKKKBBBBBllllKwwKddKwwwwwwwwwwKKK...
//   ...............KKKKKKlllmKwwKddKwwwwwwwKKK......
//   ................KBBBBBlmmmKKddKwwwwwwwK.........
//   ................KBBBBBmmmmmKddKwwwwwwK..........
//   ................KKKKKKKmmmmdKKwwwwwwK...........
//   .................KBBBBBmmmdddKKKwwwK............
//   .................KBBBBBBmddddKllKKKK............
//   ..................KKKKKKdddddKllllllK.......KK..
//   ..................KKBKKKKddddKllllllmK......KK..
//   .................KLKKlmmKddddKlllmmmmmK....KwwK.
//   .................KKKmmmmKddddKlmmmmmmmK....KwwK.
//   ...............KKKmmmmmdKKddKmmmmmmmbBK...KwwwwK
//   ..............KmmmmmdKKKmKKKmmmmmmmBKbK....KwwK.
//   ..............tmmddKKlmmmmKmmmmmmBKbBBK....KKKK.
//   ...............KKKKllmmmmmKmmmmBKbBBbKK....KmmK.
//   .................KlmmmmmmdKmmmKbBBbKBKKK...KmmK.
//   .................KmmmmmdddKmKbBBbKBbKdKmK..KmmK.
//   ..................KmmmdddKdbBBbKBbBKdKmmmKKmddK.
//   ................KKKKKKddKKBBbKBbBKKKKKmmmmddddK.
//   ...............KllmmmKKKKKKKKKKKKmmmdKmmdddddKK.
//   ..............KmmmdddK........KmdddddKddddddK...
//   ..............tKKKKKKK........tKKKKKKKKKddKK....
//   ................t...............t.......KK......
//
// hop:
//   .....................KK........KKKKwwK.KKWWWK...
//   ...................KKK........KKKKKKK.KKddWWK...
//   .................KKHK.........KKwKKKKKdKdddK....
//   ...............KKHHK..KKKKK..KKKwwKwKKKWdddK....
//   ...............KHHK.KKhKK....KKwwwKwKdKWWdWdK...
//   ..............KHHKKKhKK.....KKKwwwKwKKWWWdWWd...
//   .............KHHKKhhK......KKKwwwwwKdKWWWdWWdd..
//   ............KHhhKhhhK......KKKwwwwKdKWWWWWdWWdd.
//   .........KKKhhhKhhhK......KKKwwwwwKdKWWWWWdWWWd.
//   ......KKKLLKhhhKhhK.......KKKwwwwKdKWWWWWWdWWWK.
//   ....KKLLKddKhhhKhKlK.....KKKKwwwwKdKWWWWWWddWK..
//   ...KLLLLLlKKKKKKKlllK....KKKwwwwKddKWWWWWWWdK...
//   ..KlllllllKeeKlllllmK...KKKKwwwwKdKWWWWWWWWd....
//   ..KKllllllllllmmmmmmK...KKKwwwwKddKWWWWWWWKd....
//   .KllllllmmmmmmmmmmmmKK..KKKwwwwKdKWWWWWWWWK.d...
//   .KlmmmmmmmmmmmmmmmmdKLKKKKKwwwwKdKWWWWWWWWwKd...
//   .KmmmmmmmmmmmddKKKKdKLKKKKwwwwKddKWWWWWWwwwKd...
//   .KKKmmKKKKKKKKKmmmKKKLLKKKwwwwKdKWWWWWwwwwwwdd..
//   ..KKKKKKKKKKKKKmmmmKLLlKKKwwwKddKWWWwwwwwwwwKd..
//   ..KmtmmmtmmmtmdddddKLlllKwwwwKddKWwwwwwwwwwwKd..
//   ...KKKKmddddddddKKKKllllKwwwKddKwwwwwwwwwwwwwd..
//   .......KKKKKKKKKKKKKKllllKwwKddKwwwwwwwwwwKKK...
//   ...............KBBBBBlllmKwKddKwwwwwwwwKKK......
//   ................KBBBBBlmmmKKddKwwwwwwwK.........
//   ................KKKKKKmmmmmKddKwwwwwwK..........
//   ..............KKKBBBBBBmmmmdKKwwwwwwK.......KK..
//   ..............tllKBBBBBmmmdddKKKwwwK........KK..
//   ..............KlllKKKKKKmddddKllKKKK.......KwwK.
//   ...............KllmmKKKBdddddKllllllK......KwwK.
//   ................KmmmmddKKddddKllllllmK....KwwwwK
//   .................KKdddddKddddKlllmmmmmK....KwwK.
//   .................KLKddddKddddKlmmmmmmmK....KKKK.
//   .................KLKKKKKKBddKmmmmmmmKBK....KmmK.
//   .................KKllllKKKKKmmmmmmmBbBK....KmmK.
//   .................KlllllmKmmmmmmmmBbBBKK....KmmK.
//   ................KlllllmmmKmmmmmBbBBKBBK....KmmK.
//   ................KllllmmmmKmmmmbBBKBBbKmK..KmmdK.
//   ................KllmmmmmmKmmbBBKBBbBKKmmKKmddK..
//   ................KlmmmmmmdKdBBKBBbBKKdKmmmmdddK..
//   .................KmmmmmdKBBKBBbBKKKdKmmmddddK...
//   .................KmmmddKKKKKKKKKKdddKmmdddddK...
//   .................KmmKKK........KKdddKKdddddK....
//   ...............KKKKKK............KKKKKKKddK.....
//   ..............KllmmmK...........KmmmmdK.KK......
//   .............KmmmdddK..........KmdddddK.........
//   .............tKKKKKKK..........tKKKKKKK.........
//   ...............t.................t..............
//   ................................................
//
// breathe:
//   ....................KK.................K........
//   ..................KKK...............KKK.......K.
//   ................KKHK............KKKKwwK....KKK..
//   ..............KKHHK..KKKKK......KKwwwK.KKKKWWK..
//   ..............KHHK.KKhKK.......KKKKwwK.KKWWWK...
//   .............KHHKKKhKK........KKKKwKK.KKddWWK...
//   ............KHHKKhhK..........KKwwKwKKdKdWdK....
//   ...........KHhhKhhhK.........KKKwwKwwKKWWdWd....
//   ........KKKhhhKhhhK.........KKKwwwKKKdKWWdWWd...
//   .....KKKLLKhhhKhhK..........KKKwwwwKdKWWWddWWd..
//   ...KKLLKddKhhhKhKlK........KKKwwwwwKdKWWWWdWWWd.
//   ..KLLLLLlKKKKKKKlllK.......KKKwwwwKdKWWWWWdWWWK.
//   .KlllllllKeeKlllllmK......KKKwwwwwKdKWWWWWddWK..
//   .KKllllllllllmmmmmmK.....KKKKwwwwKdKWWWWWWWdK...
//   KllllllmmmmmmmmmmmmK.....KKKwwwwKddKWWWWWWWd....
//   KlmmmmmmmmmmmmmmmmdKK...KKKKwwwwKdKWWWWWWWKdd...
//   KmmmmmmmmmmmdddddddKK...KKKwwwwKddKWWWWWWWK.d...
//   KKKmmmmddddKKKKKKKKKLK.KKKKwwwwKdKWWWWWWWwwKd...
//   .ftfftfftfffflmmmKLLLlKKKKwwwwKddKWWWWwwwwwwKd..
//   ..fFFFFfffmmmmmmmmKLllKKKKwwwwKdKWWWwwwwwwwwKd..
//   ..KfFfmmmmmmmmmdddKllllKKwwwwKddKWwwwwwwwwwwwd..
//   ..KmmtmmmtmdddddKKKlllllKwwwKddKwwwwwwwwwwKKK...
//   ..KmmmmddddddKKKBBBllllmmKwwKddKwwwwwwwKKK......
//   ...KKKKKKKKKK.KBBBBBllmmmmKKddKwwwwwwwK.........
//   ...............KKKKKlmmmmmmKddKwwwwwwK..........
//   ...............KBBBBBmmmmmmdKKwwwwwwK...........
//   ................KBBBBmmmmmdddKKKwwwK............
//   ................KKKKKKmmdddddKllKKKK............
//   .................KBBBBmddddddKllllllK.......KK..
//   ..................KBBKKKKddddKllllllmK......KK..
//   .................KLKKlmmKddddKlllmmmmmK....KwwK.
//   .................KKKmmmmKddddKlmmmmmmmK....KwwK.
//   ...............KKKmmmmmdKdddKmmmmmmmKBK...KwwwwK
//   ..............KmmmmmdKKKKKKKmmmmmmmBbBK....KwwK.
//   ..............tmmddKKllKKmmmmmmmmBbBBKK....KKKK.
//   ...............KKKKllllmKmmmmmmBbBBKBBK....KmmK.
//   ................KlllllmmmKmmmmbBBKBBbKlK...KmmK.
//   ................KllllmmmmKmmbBBKBBbBKKmmK..KmmK.
//   ................KllmmmmmmKdBBKBBbBKKdKmmmKKmddK.
//   ................KlmmmmmmdKBKBBbBKKKddKmmmmddddK.
//   .................KmmmmmdKKKKKKKKKdddKmmmdddddKK.
//   .................KmmmddKK....KKdddddKKddddddK...
//   ..................KmdKK........KdddK..KKddKK....
//   ................KKKKKK.........KKKKKK...KK......
//   ...............KllmmmK........KmmmmdK...........
//   ..............KmmmdddK.......KmdddddK...........
//   ..............tKKKKKKK.......tKKKKKKK...........
//   ................t..............t................
//
// roar:
//   .................KKHK..................K........
//   ...............KKHHK..KKKKK.........KKK.......K.
//   ...............KHHK.KKhKK.......KKKKwwK....KKK..
//   ..............KHHKKKhKK.........KKwwwK.KKKKWWK..
//   .............KHHKKhhK..........KKKKwwK.KKWWWK...
//   ............KHhhKhhhK.........KKKKwKK.KKddWWK...
//   .........KKKhhhKhhhK..........KKwwKwKKdKdWdK....
//   ......KKKLLKhhhKhhK..........KKKwwKwwKKWWdWd....
//   ....KKLLKddKhhhKhKlK........KKKwwwKKKdKWWdWWd...
//   ...KLLLLLlKKKKKKKlllK.......KKKwwwwKdKWWWddWWd..
//   ..KlllllllKeeKlllllmK......KKKwwwwwKdKWWWWdWWWd.
//   ..KKllllllllllmmmmmmK......KKKwwwwKdKWWWWWdWWWK.
//   .KllllllmmmmmmmmmmmmK.....KKKwwwwwKdKWWWWWddWK..
//   .KlmmmmmmmmmmmmmmmmdK....KKKKwwwwKdKWWWWWWWdK...
//   .KmmmmmmmmmmmdddddddK....KKKwwwwKddKWWWWWWWd....
//   .KKKmmmmddddKKKKKKKKK...KKKKwwwwKdKWWWWWWWKdd...
//   ..ftfftfftfffflmmmKLK...KKKwwwwKddKWWWWWWWK.d...
//   ...fFFFFfffmmmmmmmmKLK.KKKKwwwwKdKWWWWWWWwwKd...
//   ...KfFfmmmmmmmmmdddKLlKKKKwwwwKddKWWWWwwwwwwKd..
//   ...KmmtmmmtmdddddKKLllKKKKwwwwKdKWWWwwwwwwwwKd..
//   ...KmmmmddddddKKKBLllllKKwwwwKddKWwwwwwwwwwwwd..
//   ....KKKKKKKKKKKKKKKlllllKwwwKddKwwwwwwwwwwKKK...
//   ..............KBBBBllllmmKwwKddKwwwwwwwKKK......
//   ..............KBBBBBllmmmmKKddKwwwwwwwK.........
//   ...............KKKKKlmmmmmmKddKwwwwwwK..........
//   ..............KKKBBBBmmmmmmdKKwwwwwwK...........
//   ..............tllKBBBmmmmmdddKKKwwwK............
//   ..............KlllKKKKmmdddddKllKKKK............
//   ...............KllmmKKKddddddKllllllK.......KK..
//   ................KmmmmddKKddddKllllllmK......KK..
//   .................KKdddddKddddKlllmmmmmK....KwwK.
//   .................KLKddddKddddKlmmmmmmmK....KwwK.
//   .................KLLKKKKKdddKmmmmmmmKBK...KwwwwK
//   .................KlKKKKKKKKKmmmmmmmBbBK....KwwK.
//   .................KKllllKKmmmmmmmmBbBBKK....KKKK.
//   .................KlllllmKmmmmmmBbBBKBBK....KmmK.
//   ................KlllllmmmKmmmmbBBKBBbKlK...KmmK.
//   ................KllllmmmmKmmbBBKBBbBKKmmK..KmmK.
//   ................KllmmmmmmKdBBKBBbBKKdKmmmKKmddK.
//   ................KlmmmmmmdKBKBBbBKKKddKmmmmddddK.
//   .................KmmmmmdKKKKKKKKKdddKmmmdddddKK.
//   .................KmmmddKK....KKdddddKKddddddK...
//   ..................KmdKK........KdddK..KKddKK....
//   ................KKKKKK.........KKKKKK...KK......
//   ...............KllmmmK........KmmmmdK...........
//   ..............KmmmdddK.......KmdddddK...........
//   ..............tKKKKKKK.......tKKKKKKK...........
//   ................t..............t................
//
// hurt:
//   .................KKHK...........KKwwwK.KKKKWWK..
//   ...............KKHHK..KKKKK....KKKKwwK.KKWWWK...
//   ...............KHHK.KKhKK.....KKKKKKK.KKddWWK...
//   ..............KHHKKKhKK.......KKwKKKKKdKdddK....
//   .............KHHKKhhK........KKKwwKwKKKWdddK....
//   ............KHhhKhhhK........KKwwwKwKdKWWdWdK...
//   .........KKKhhhKhhhK........KKKwwwKKKKWWWdWWd...
//   ......KKKLLKhhhKhhK........KKKwwwwwKdKWWWddWdd..
//   ....KKLLKddKhhhKhKlK.......KKKwwwwKdKWWWWWdWWdd.
//   ...KLLLLLlKKKKKKKlllK.....KKKwwwwwKdKWWWWWdWWWd.
//   ..KlllllllKeeKlllllmK.....KKKwwwwKdKWWWWWWdWWWK.
//   ..KKllllllllllmmmmmmK....KKKwwwwwKdKWWWWWWWdWK..
//   .KllllllmmmmmmmmmmmmK....KKKwwwwKdKWWWWWWWWdK...
//   .KlmmmmmmmmmmmmmmmmdK...KKKKwwwwKdKWWWWWWWWd....
//   .KmmmmmmmmmmmdddddddKK..KKKwwwwKddKWWWWWWWKdd...
//   .KKKmmmmddddKKKKKKKKKLKKKKKwwwwKdKWWWWWWWWK.d...
//   ..ftfftfftfffflmmmKLLLKKKKKwwwKddKWWWWWWwwwKd...
//   ...fFFFFfffmmmmmmmmKLLLKKKwwwwKddKWWWWwwwwwKdd..
//   ...KfFfmmmmmmmmmdddKLLlKKKwwwKddKWWWwwwwwwwwKd..
//   ...KmmtmmmtmdddddKKBLlllKwwwwKddKWwwwwwwwwwwKd..
//   ...KmmmmddddddKKKBBBllllKwwwKddKwwwwwwwwwwwwwd..
//   ....KKKKKKKKKK.KKKKKKllllKwwKddKwwwwwwwwwwKKK...
//   ...............KBBBBBlllmKwKdddKwwwwwwwKKK......
//   ................KBBBBBlmmmKKddKwwwwwwwK.........
//   ................KKKKKKmmmmmKddKwwwwwwK..........
//   ..............KKKBBBBBBmmmmdKKwwwwwwK...........
//   ..............tllKBBBBBmmmdddKKKwwwK............
//   ..............KlllKKKKKKmddddKllKKKK............
//   ...............KllmmKKKBdddddKllllllK.......KK..
//   ................KmmmmddKKddddKllllllmK......KK..
//   .................KKdddddKddddKlllmmmmmK....KwwK.
//   .................KLKddddKddddKlmmmmmmmK....KwwK.
//   .................KLKKKKKKBddKmmmmmmmKBK...KwwwwK
//   .................KKllllKKKKKmmmmmmmBbBK....KwwK.
//   .................KlllllmKmmmmmmmmBbBBKK....KKKK.
//   ................KlllllmmmKmmmmmBbBBKBBK....KmmK.
//   ................KllllmmmmKmmmmbBBKBBbKlK...KmmK.
//   ................KllmmmmmmKmmbBBKBBbBKKmmK..KmmK.
//   ................KlmmmmmmdKdBBKBBbBKKdKmmmKKmddK.
//   .................KmmmmmdKBBKBBbBKKKdKmmmmmddddK.
//   .................KmmmddKKKKKKKKKKdddKmmmdddddKK.
//   .................KmmKKK........KKdddKKddddddK...
//   ...............KKKKKK............KKKKKKKddKK....
//   ..............KllmmmK...........KmmmmdK.KK......
//   .............KmmmdddK..........KmdddddK.........
//   .............tKKKKKKK..........tKKKKKKK.........
//   ...............t.................t..............
//   ................................................
static const u32 dragon_part_tiles[1600] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x10000000, 0xB1000000, 0xBB100000, 0xAAB10000,
    0x00000000, 0x11000000, 0x55110000, 0x55551000, 0x44444100, 0x44441100, 0x44444410, 0x33333410,
    0x1AAA1110, 0x1AAA1551, 0x1AAA1221, 0x11111145, 0x441CC144, 0x33444444, 0x33333333, 0x33333333,
    0x01100000, 0x00111000, 0x0001B110, 0x11001BB1, 0x1A1101BB, 0x011A111B, 0x0001AA11, 0x0001AAA1,
    0x00000000, 0x00000000, 0x00000000, 0x00000111, 0x10000001, 0x11000000, 0x11000000, 0x11100000,
    0x00001AAA, 0x000001AA, 0x0000141A, 0x00014441, 0x00013444, 0x00013333, 0x00113333, 0x01512333,
    0x81110000, 0x81110000, 0x88111000, 0x88111000, 0x88811100, 0x88811110, 0x88881110, 0x88881111,
    0x10000000, 0x01110000, 0x01881111, 0x10188811, 0x10188111, 0x11011811, 0x12118188, 0x91188188,
    0x00000000, 0x01000000, 0x00111000, 0x00199111, 0x00019991, 0x00019922, 0x00001292, 0x00002929,
    0x91211188, 0x99121888, 0x99121888, 0x99912188, 0x99912188, 0x99991218, 0x99991221, 0x99999121,
    0x00029929, 0x00299229, 0x02999299, 0x01999299, 0x00192299, 0x00012999, 0x00002999, 0x00022199,
    0x33333310, 0x11331110, 0x11111100, 0x333F3100, 0x31111000, 0x10000000, 0x00000000, 0x00000000,
    0x12233333, 0x31111111, 0x31111111, 0x223F333F, 0x22222222, 0x11111111, 0x10000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x01512111, 0x15511133, 0x14551333, 0x44451222, 0x44441111, 0x44411111, 0x44477777, 0x34777771,
    0x18888111, 0x18888111, 0x21888811, 0x21888811, 0x22188881, 0x12218814, 0x12218813, 0x81221133,
    0x33111111, 0x37777771, 0x37777710, 0x11111110, 0x77777100, 0x11171100, 0x33411510, 0x33331110,
    0x81221333, 0x88112333, 0x11122233, 0x44122223, 0x44122222, 0x44122221, 0x44122221, 0x34122221,
    0x99999122, 0x99999912, 0x88999912, 0x88889991, 0x88888891, 0x88888888, 0x18888888, 0x01888888,
    0x00020199, 0x00021889, 0x00218888, 0x00218888, 0x00288888, 0x00011188, 0x00000011, 0x00000000,
    0x00188888, 0x00018888, 0x00001888, 0x00001111, 0x00014444, 0x00134444, 0x01333334, 0x01333333,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00110000, 0x00110000, 0x01881000, 0x01881000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x10000000, 0x31000000, 0x3F000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x31000000, 0x1F000000, 0x00000000,
    0x23333311, 0x11123333, 0x14411223, 0x34444111, 0x33444441, 0x33344441, 0x33333441, 0x33333341,
    0x33312271, 0x33331111, 0x33333331, 0x73333331, 0x76333313, 0x17763313, 0x77177213, 0x76771712,
    0x23333310, 0x12233310, 0x01123100, 0x00111111, 0x00133344, 0x00122233, 0x00111111, 0x0000000F,
    0x11111111, 0x21100001, 0x10000000, 0x10000000, 0x31000000, 0x23100000, 0x11F00000, 0xF0000000,
    0x01713333, 0x01767333, 0x01177673, 0x01771776, 0x14167717, 0x33117677, 0x33121176, 0x33122111,
    0x18888100, 0x01881000, 0x01111000, 0x01331000, 0x01331000, 0x01331001, 0x01223113, 0x01222233,
    0x33312221, 0x22112222, 0x11001222, 0x00011111, 0x00012333, 0x00012222, 0x00011111, 0x00000000,
    0x01122222, 0x00012222, 0x00001122, 0x00000011, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x10000000, 0x31000000, 0x3F000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x11100000, 0x34410000, 0x23331000, 0x1111F000, 0x00F00000,
    0x23333311, 0x11123333, 0x14411223, 0x34444111, 0x33344441, 0x33334441, 0x33333441, 0x33333331,
    0x33312271, 0x33331111, 0x33333331, 0x73333331, 0x76333313, 0x17763313, 0x77177213, 0x76771712,
    0x22333310, 0x12233331, 0x01111331, 0x00000111, 0x00000133, 0x00000122, 0x00000111, 0x00000000,
    0x11111111, 0x21100001, 0x10000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x01713333, 0x01767333, 0x01177673, 0x01771776, 0x14167717, 0x33117677, 0x33121176, 0x33122111,
    0x18888100, 0x01881000, 0x01111000, 0x01331000, 0x01331000, 0x01331001, 0x01223113, 0x01222233,
    0x33312221, 0x22112222, 0x11012211, 0x11111100, 0x12333310, 0x12222231, 0x1111111F, 0x00000F00,
    0x01122222, 0x00012222, 0x00001122, 0x00000011, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x99999122, 0x99999912, 0x88999912, 0x88889991, 0x88888891, 0x88888888, 0x18888888, 0x01888888,
    0x00020199, 0x00021889, 0x00218888, 0x00218888, 0x00288888, 0x00011188, 0x00000011, 0x00000000,
    0x00188888, 0x00018888, 0x00001888, 0x00001111, 0x00014444, 0x00134444, 0x01333334, 0x01333333,
    0x00000000, 0x00000000, 0x00000000, 0x00110000, 0x00110000, 0x01881000, 0x01881000, 0x18888100,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x10000000, 0x31000000, 0x3F000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x23333311, 0x11123333, 0x14411223, 0x34444111, 0x33444441, 0x33344441, 0x33333441, 0x33333341,
    0x33312271, 0x33331111, 0x33333331, 0x73333331, 0x76333313, 0x17763313, 0x77177213, 0x76771712,
    0x23333310, 0x12233110, 0x01221000, 0x11111100, 0x13334410, 0x12223331, 0x1111111F, 0x00000F00,
    0x11111111, 0x23100001, 0x21000000, 0x11110000, 0x33331000, 0x22223100, 0x11111F00, 0x000F0000,
    0x01713333, 0x01767333, 0x01177673, 0x01771776, 0x14167717, 0x33117677, 0x33121176, 0x33122111,
    0x01881000, 0x01111000, 0x01331000, 0x01331000, 0x01331000, 0x01233101, 0x01222313, 0x00122223,
    0x23312221, 0x22111222, 0x11000122, 0x00000011, 0x00000012, 0x00000012, 0x00000011, 0x00000000,
    0x00012222, 0x00001222, 0x00000122, 0x00000011, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x11000000, 0x55110000, 0x55551000,
    0x10000000, 0xB1000000, 0xBB100000, 0xAAB10000, 0x1AAA1110, 0x1AAA1551, 0x1AAA1221, 0x11111145,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x01100000, 0x00111000, 0x0001B110, 0x11001BB1,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000111,
    0x1A1101BB, 0x011A111B, 0x0001AA11, 0x0001AAA1, 0x00001AAA, 0x000001AA, 0x0000141A, 0x00014441,
    0x10000001, 0x11000000, 0x11000000, 0x11100000, 0x81110000, 0x81110000, 0x88111000, 0x88111000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x01110000, 0x01881111, 0x10188811,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x01000000, 0x00111000, 0x00199111,
    0x10188111, 0x11011811, 0x12118188, 0x91188188, 0x91211188, 0x99121888, 0x99121888, 0x99912188,
    0x00019991, 0x00019922, 0x00001292, 0x00002929, 0x00029929, 0x00299229, 0x02999299, 0x01999299,
    0x44444100, 0x44441100, 0x44444410, 0x33333410, 0x33333310, 0x11331110, 0x11111100, 0x333F3100,
    0x441CC144, 0x33444444, 0x33333333, 0x33333333, 0x12233333, 0x31111111, 0x31111111, 0x223F333F,
    0x31111000, 0x10000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x22222222, 0x11111111, 0x10000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00013444, 0x00013333, 0x00113333, 0x01512333, 0x01512111, 0x15511133, 0x14551333, 0x44451222,
    0x88811100, 0x88811110, 0x88881110, 0x88881111, 0x18888111, 0x18888111, 0x21888811, 0x21888811,
    0x44441111, 0x44477777, 0x44411111, 0x34777771, 0x33777771, 0x31111111, 0x37777710, 0x77777710,
    0x22188881, 0x12218814, 0x12218813, 0x81221133, 0x81221333, 0x88112333, 0x11122233, 0x44122223,
    0x99912188, 0x99991218, 0x99991221, 0x99999121, 0x99999122, 0x99999912, 0x88999912, 0x88889991,
    0x00192299, 0x00012999, 0x00002999, 0x00022199, 0x00020199, 0x00021889, 0x00218888, 0x00218888,
    0x88888891, 0x88888888, 0x18888888, 0x01888888, 0x00188888, 0x00018888, 0x00001888, 0x00001111,
    0x00288888, 0x00011188, 0x00000011, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x31000000, 0x3F000000, 0x10000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x31000000, 0x1F000000, 0x00000000,
    0x11111100, 0x11171100, 0x33411510, 0x33331110, 0x23333311, 0x11123333, 0x33411223, 0x33344111,
    0x44122222, 0x44122221, 0x44122221, 0x34122221, 0x33312211, 0x33331113, 0x33333133, 0x73333133,
    0x33333410, 0x23333310, 0x22333100, 0x22111111, 0x11133344, 0x00122233, 0x00111111, 0x0000000F,
    0x61333123, 0x77613122, 0x16776212, 0x67167711, 0x11111111, 0x31000000, 0x1F000000, 0x00000000,
    0x00014444, 0x00134444, 0x01333334, 0x01333333, 0x01763333, 0x01617333, 0x01776173, 0x01167761,
    0x00110000, 0x00110000, 0x01881000, 0x01881000, 0x18888100, 0x01881000, 0x01111000, 0x01331000,
    0x11171677, 0x31216716, 0x33121767, 0x33111117, 0x33123331, 0x22122222, 0x11111111, 0x0000000F,
    0x01331000, 0x01331001, 0x01223113, 0x01222233, 0x01122222, 0x00012222, 0x00001122, 0x00000011,
    0x01100000, 0x00111000, 0x0001B110, 0x11001BB1, 0x1A1101BB, 0x011A111B, 0x0001AA11, 0x0001AAA1,
    0x10000000, 0x11000000, 0x11000000, 0x11100111, 0x81100001, 0x81110000, 0x88111000, 0x88111000,
    0x00001AAA, 0x000001AA, 0x0000141A, 0x00014441, 0x00013444, 0x00013333, 0x00113333, 0x11512333,
    0x88811100, 0x88811100, 0x88811110, 0x88881110, 0x88881111, 0x18888111, 0x18888111, 0x18888111,
    0x10188111, 0x11011111, 0x12111118, 0x91118188, 0x91218188, 0x99118188, 0x99121888, 0x99912188,
    0x00019991, 0x00019922, 0x00001222, 0x00001222, 0x00012929, 0x00029929, 0x00229929, 0x02299299,
    0x99912188, 0x99991218, 0x99991218, 0x99991221, 0x99999121, 0x99999122, 0x99999912, 0x99999912,
    0x02999299, 0x01999299, 0x00192299, 0x00012999, 0x00002999, 0x00002199, 0x00020199, 0x00021899,
    0x33333310, 0x11331110, 0x11111100, 0x333F3100, 0x31111000, 0x10000000, 0x00000000, 0x00000000,
    0x12233333, 0x31111111, 0x31111111, 0x223F333F, 0x22222222, 0x11111111, 0x10000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x11000000, 0x4F000000, 0x41000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000,
    0x11512111, 0x15511133, 0x14551333, 0x44451222, 0x44441111, 0x44411111, 0x44477777, 0x34777771,
    0x21888811, 0x21888811, 0x22188811, 0x22188881, 0x12218881, 0x12218814, 0x81221813, 0x81221133,
    0x33111111, 0x37777771, 0x37777714, 0x11111144, 0x71113344, 0x12233331, 0x22222110, 0x22221510,
    0x81221333, 0x88112333, 0x11122233, 0x44122223, 0x44122222, 0x44122221, 0x44122221, 0x34122221,
    0x99999912, 0x88999991, 0x88889991, 0x88888891, 0x88888888, 0x88888888, 0x18888888, 0x01888888,
    0x00021888, 0x00228888, 0x00218888, 0x00218888, 0x00288888, 0x00011188, 0x00000011, 0x00000000,
    0x00188888, 0x00018888, 0x00001888, 0x00001111, 0x00014444, 0x00134444, 0x01333334, 0x01333333,
    0x00000000, 0x00110000, 0x00110000, 0x01881000, 0x01881000, 0x18888100, 0x01881000, 0x01111000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x10000000, 0x41000000, 0x33100000, 0x11F00000, 0xF0000000, 0x00000000,
    0x11111510, 0x14444110, 0x34444410, 0x33444441, 0x33344441, 0x33333441, 0x33333341, 0x23333310,
    0x33312271, 0x33331111, 0x33333331, 0x73333313, 0x76333313, 0x17763313, 0x77177212, 0x76771771,
    0x12233310, 0x01113310, 0x00011111, 0x00013334, 0x00012223, 0x00011111, 0x00000000, 0x00000000,
    0x11111111, 0x10000000, 0x00000000, 0x00000000, 0x10000000, 0xF0000000, 0x00000000, 0x00000000,
    0x01713333, 0x01767333, 0x01177673, 0x01771776, 0x13167717, 0x33117677, 0x33121176, 0x33312111,
    0x01331000, 0x01331000, 0x01331000, 0x01331000, 0x01233100, 0x00122311, 0x00122233, 0x00012222,
    0x23312221, 0x22112221, 0x11111110, 0x01233331, 0x01222223, 0x01111111, 0x000000F0, 0x00000000,
    0x00012222, 0x00001222, 0x00000122, 0x00000011, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x11000000, 0xB1000000, 0xBB100000, 0x1BB10000, 0x1AAB1000,
    0x00000000, 0x11100000, 0x15511000, 0x55555100, 0x44444410, 0x44444110, 0x34444441, 0x33333341,
    0xA1AAA111, 0xA1AAA155, 0xA1AAA122, 0x11111114, 0x4441CC14, 0x33344444, 0x33333333, 0x33333333,
    0x00110000, 0x00011100, 0x00001B11, 0x111001BB, 0x11A1101B, 0x0011A111, 0x00001AA1, 0x00001AAA,
    0x00000000, 0x00000000, 0x00000000, 0x00000011, 0x10000000, 0x11000000, 0x11000000, 0x11100000,
    0x000001AA, 0x0000001A, 0x00000141, 0x00001444, 0x00001344, 0x00001333, 0x00001333, 0x00011233,
    0x81110000, 0x81110000, 0x88111000, 0x88111000, 0x88811100, 0x88811110, 0x88881110, 0x88881111,
    0x33333331, 0x23333111, 0xDDFDDFD0, 0xDEEEED00, 0x33DED100, 0x33F33100, 0x23333100, 0x11111000,
    0x22223333, 0x11111222, 0x334DDDDF, 0x333333DD, 0x23333333, 0x222223F3, 0x11122222, 0x71011111,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x10000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00011222, 0x10151111, 0x11455513, 0x11445133, 0x14444122, 0x44444111, 0x34444777, 0x33447777,
    0x18888111, 0x18888111, 0x21888811, 0x21888811, 0x22188881, 0x12218881, 0x12218813, 0x81221133,
    0x33341111, 0x33377777, 0x33377771, 0x33111111, 0x23777710, 0x11177100, 0x33411510, 0x33331110,
    0x81221333, 0x88112333, 0x11122233, 0x44122222, 0x44122222, 0x44122221, 0x44122221, 0x34122221,
    0x23333311, 0x11123333, 0x14411223, 0x34444111, 0x33444441, 0x33344441, 0x33333441, 0x33333341,
    0x33312221, 0x33331111, 0x33333331, 0x73333331, 0x76333313, 0x17763313, 0x77177213, 0x76771712,
    0x23333310, 0x12233310, 0x01123100, 0x00111111, 0x00133344, 0x00122233, 0x00111111, 0x0000000F,
    0x11111111, 0x21100001, 0x10000000, 0x10000000, 0x31000000, 0x23100000, 0x11F00000, 0xF0000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x11000000,
    0x00000000, 0x10000000, 0x10000000, 0xB1000000, 0xBB100000, 0xAAB10000, 0x1AAA1110, 0x1AAA1551,
    0x55110000, 0x55551000, 0x44444100, 0x44441100, 0x44444410, 0x33333410, 0x33333310, 0x33331110,
    0x1AAA1221, 0x11111145, 0x441CC144, 0x33444444, 0x33333333, 0x33333333, 0x22233333, 0x11112222,
    0x0001B110, 0x11001BB1, 0x1A1101BB, 0x011A111B, 0x0001AA11, 0x0001AAA1, 0x00001AAA, 0x000001AA,
    0x00000000, 0x00000111, 0x00000001, 0x00000000, 0x10000000, 0x11000000, 0x11000000, 0x11100000,
    0x0000141A, 0x00014441, 0x00013444, 0x00013333, 0x00013333, 0x00012333, 0x00012222, 0x00011111,
    0x81110000, 0x81110000, 0x88111000, 0x88111000, 0x88811100, 0x88811110, 0x88881110, 0x88881111,
    0xDFDDFD00, 0xEEEED000, 0x3DED1000, 0x3F331000, 0x33331000, 0x11110000, 0x00000000, 0x00000000,
    0x34DDDDFD, 0x33333DDD, 0x33333333, 0x22223F33, 0x11222222, 0x11111111, 0x71000000, 0x71000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x10000000, 0x11000000, 0x4F000000, 0x41000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000,
    0x00015133, 0x10151333, 0x11451222, 0x11445112, 0x14444571, 0x44444111, 0x34444777, 0x33447777,
    0x18888111, 0x18888111, 0x21888811, 0x21888811, 0x22188881, 0x12218881, 0x12218813, 0x81221133,
    0x33341111, 0x33377771, 0x33377714, 0x33111144, 0x21113344, 0x12233331, 0x22222110, 0x22221510,
    0x81221333, 0x88112333, 0x11122233, 0x44122222, 0x44122222, 0x44122221, 0x44122221, 0x34122221,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x31000000, 0x1F000000, 0x00000000,
    0x11115510, 0x11111410, 0x14444110, 0x34444410, 0x33444441, 0x33344441, 0x33333441, 0x33333341,
    0x33312221, 0x33331111, 0x33333331, 0x73333331, 0x76333313, 0x17763313, 0x77177213, 0x76771712,
    0x23333310, 0x12233310, 0x01123100, 0x00111111, 0x00133344, 0x00122233, 0x00111111, 0x0000000F,
    0x11111111, 0x21100001, 0x10000000, 0x10000000, 0x31000000, 0x23100000, 0x11F00000, 0xF0000000,
    0x0001B110, 0x11001BB1, 0x1A1101BB, 0x011A111B, 0x0001AA11, 0x0001AAA1, 0x00001AAA, 0x000001AA,
    0x00000000, 0x10000111, 0x11000001, 0x11000000, 0x11100000, 0x81100000, 0x81110000, 0x88111000,
    0x0000141A, 0x00014441, 0x00013444, 0x00013333, 0x00013333, 0x00012333, 0x00112222, 0x11511111,
    0x88111000, 0x88811100, 0x88811100, 0x88881110, 0x88881110, 0x88881111, 0x18888111, 0x18888111,
    0x10188811, 0x10188111, 0x11011111, 0x12111118, 0x91118188, 0x91218188, 0x99111188, 0x99121888,
    0x00199111, 0x00019991, 0x00019922, 0x00001222, 0x00001222, 0x00012929, 0x00029929, 0x00229229,
    0x99912188, 0x99912188, 0x99991218, 0x99991218, 0x99999121, 0x99999121, 0x99999122, 0x99999912,
    0x02299299, 0x02999299, 0x01999299, 0x00192999, 0x00012999, 0x00002999, 0x00022199, 0x00020199,
    0xDFDDFD00, 0xEEEED000, 0x3DED1000, 0x3F331000, 0x33331000, 0x11110000, 0x00000000, 0x00000000,
    0x34DDDDFD, 0x33333DDD, 0x33333333, 0x22223F33, 0x11222222, 0x10111111, 0x10000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x11000000, 0x4F000000, 0x41000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000,
    0x11555133, 0x15551333, 0x14551222, 0x44457112, 0x44447771, 0x44411111, 0x44477777, 0x34777771,
    0x21888111, 0x21888811, 0x22188811, 0x22188881, 0x12218881, 0x12218814, 0x12221813, 0x81221133,
    0x33111111, 0x37777771, 0x37777714, 0x11111144, 0x71113344, 0x12233331, 0x22222110, 0x22221510,
    0x81221333, 0x88112333, 0x11122233, 0x44122223, 0x44122222, 0x44122221, 0x44122221, 0x34122221,
    0x99999912, 0x88999912, 0x88889991, 0x88888891, 0x88888888, 0x88888888, 0x18888888, 0x01888888,
    0x00021888, 0x00221888, 0x00218888, 0x00218888, 0x00288888, 0x00011188, 0x00000011, 0x00000000,
    0x00188888, 0x00018888, 0x00001888, 0x00001111, 0x00014444, 0x00134444, 0x01333334, 0x01333333,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00110000, 0x00110000, 0x01881000, 0x01881000,
    0x01713333, 0x01767333, 0x01177673, 0x01771776, 0x14167717, 0x33117677, 0x33121176, 0x33312111,
    0x18888100, 0x01881000, 0x01111000, 0x01331000, 0x01331000, 0x01331001, 0x01223113, 0x01222233,
    0x33312221, 0x22112221, 0x11111110, 0x01233331, 0x01222223, 0x01111111, 0x000000F0, 0x00000000,
    0x01122222, 0x00012222, 0x00001122, 0x00000011, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

static const SpritePiece dragon_pieces[DRAGON_FRAME_COUNT * 9] = {
    // stand
    {.x = -16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 0},
    {.x = 0, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 1},
    {.x = 16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 2},
    {.x = -16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 3},
    {.x = 0, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 4},
    {.x = 16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 5},
    {.x = -16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 6},
    {.x = 0, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 7},
    {.x = 16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 8},
    // walk1
    {.x = -16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 0},
    {.x = 0, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 1},
    {.x = 16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 2},
    {.x = -16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 3},
    {.x = 0, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 4},
    {.x = 16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 5},
    {.x = -16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 9},
    {.x = 0, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 10},
    {.x = 16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 11},
    // walk2
    {.x = -16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 0},
    {.x = 0, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 1},
    {.x = 16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 2},
    {.x = -16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 3},
    {.x = 0, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 4},
    {.x = 16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 12},
    {.x = -16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 13},
    {.x = 0, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 14},
    {.x = 16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 15},
    // crouch
    {.x = -16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 16},
    {.x = 0, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 17},
    {.x = 16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 18},
    {.x = -16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 19},
    {.x = 0, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 20},
    {.x = 16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 21},
    {.x = -16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 22},
    {.x = 0, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 23},
    {.x = 16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 24},
    // hop
    {.x = -16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 0},
    {.x = 0, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 25},
    {.x = 16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 26},
    {.x = -16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 27},
    {.x = 0, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 28},
    {.x = 16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 29},
    {.x = -16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 30},
    {.x = 0, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 31},
    {.x = 16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 32},
    // breathe
    {.x = -16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 33},
    {.x = 0, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 34},
    {.x = 16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 2},
    {.x = -16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 35},
    {.x = 0, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 36},
    {.x = 16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 5},
    {.x = -16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 6},
    {.x = 0, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 37},
    {.x = 16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 8},
    // roar
    {.x = -16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 38},
    {.x = 0, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 39},
    {.x = 16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 2},
    {.x = -16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 40},
    {.x = 0, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 41},
    {.x = 16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 5},
    {.x = -16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 42},
    {.x = 0, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 43},
    {.x = 16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 8},
    // hurt
    {.x = -16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 38},
    {.x = 0, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 44},
    {.x = 16, .y = -16, .sprite = SPR_DRAGON_PART, .frame = 45},
    {.x = -16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 46},
    {.x = 0, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 47},
    {.x = 16, .y = 0, .sprite = SPR_DRAGON_PART, .frame = 48},
    {.x = -16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 30},
    {.x = 0, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 31},
    {.x = 16, .y = 16, .sprite = SPR_DRAGON_PART, .frame = 49},
};

// Palettes of the stage's group: the blocks' (slot PAL_STAGE_BLOCKS, the same
// colors as the tileset's BANK_BLOCKS: grey-violet bricks), fire, the
// salamander's and the dragon's. CS_PAL_DRAGON_FLASH is the dragon's mixed
// toward white when the stage loads (castle_load_palettes()), so the
// palettes are in RAM.
static const u16 rom_sprite_palettes[CS_PAL_DRAGON_FLASH][16] = {
    [PAL_STAGE_BLOCKS] = {0, COLOR_RGB(44, 24, 20), COLOR_RGB(62, 54, 76), COLOR_RGB(92, 82, 110),
                          COLOR_RGB(134, 124, 154), COLOR_RGB(30, 26, 40), COLOR_RGB(178, 106, 22),
                          COLOR_RGB(238, 170, 42), COLOR_RGB(255, 228, 120), COLOR_RGB(112, 76, 50),
                          COLOR_RGB(164, 122, 86), COLOR_RGB(78, 50, 30), COLOR_RGB(130, 88, 46),
                          COLOR_RGB(222, 180, 116), COLOR_RGB(176, 128, 70),
                          COLOR_RGB(196, 146, 96)},
    [CS_PAL_FIRE] = {0, COLOR_RGB(96, 16, 8), COLOR_RGB(200, 40, 20), COLOR_RGB(248, 128, 32),
                     COLOR_RGB(255, 216, 80), COLOR_RGB(255, 250, 220)},
    [CS_PAL_SALAMANDER] = {0, COLOR_RGB(10, 8, 14), COLOR_RGB(44, 40, 54), COLOR_RGB(78, 72, 92),
                           COLOR_RGB(250, 206, 40), COLOR_RGB(255, 244, 150),
                           COLOR_RGB(230, 104, 36), COLOR_RGB(255, 255, 255)},
    [CS_PAL_DRAGON] = {0, COLOR_RGB(10, 24, 30), COLOR_RGB(16, 70, 80), COLOR_RGB(30, 120, 120),
                       COLOR_RGB(70, 176, 160), COLOR_RGB(150, 226, 196), COLOR_RGB(200, 170, 100),
                       COLOR_RGB(246, 222, 150), COLOR_RGB(110, 30, 90), COLOR_RGB(180, 60, 130),
                       COLOR_RGB(170, 150, 120), COLOR_RGB(240, 230, 200), COLOR_RGB(255, 220, 40),
                       COLOR_RGB(200, 30, 30), COLOR_RGB(255, 170, 40), COLOR_RGB(255, 255, 240)},
};
static u16 sprite_palettes[CS_PALETTE_COUNT][16] SERVAL_EWRAM_BSS;

// Animation timing for sys_animate.
static const u8 salamander_walk_times[2] = {9, 9};
static const u8 flicker_times[2] = {4, 4};

// The stage's sprites, IDs SPR_STAGE(STAGE_CASTLE) + CS_* (stage_castle.h).
const SpriteAsset castle_sprites[STAGE_SPRITES] = {
    [CS_SALAMANDER] = {.size = SPRITE_16x16,
                       .tiles = salamander_tiles,
                       .frame_count = 2,
                       .frame_times = salamander_walk_times,
                       .palette_slot = CS_PAL_SALAMANDER,
                       .origin_x = 2,
                       .origin_y = 3},
    [CS_SALAMANDER_FLAT] = {.size = SPRITE_16x16,
                            .tiles = salamander_tiles + 2 * 32, // its third frame
                            .palette_slot = CS_PAL_SALAMANDER,
                            .origin_x = 2,
                            .origin_y = 3},
    // Drawn by its center.
    [CS_FIREBALL] = {.size = SPRITE_8x8,
                     .tiles = fireball_tiles,
                     .frame_count = 2,
                     .palette_slot = CS_PAL_FIRE,
                     .origin_x = 4,
                     .origin_y = 4},
    [CS_EMBER] = {.size = SPRITE_16x16,
                  .tiles = ember_tiles,
                  .frame_count = 2,
                  .frame_times = flicker_times,
                  .palette_slot = CS_PAL_FIRE,
                  .origin_x = 4,
                  .origin_y = 2},
    [CS_BREATH] = {.size = SPRITE_16x16,
                   .tiles = breath_tiles,
                   .frame_count = 2,
                   .frame_times = flicker_times,
                   .palette_slot = CS_PAL_FIRE,
                   .origin_x = 3,
                   .origin_y = 4},
    [CS_DRAGON_PART] = {.size = SPRITE_16x16,
                        .tiles = dragon_part_tiles,
                        .frame_count = 50,
                        .palette_slot = CS_PAL_DRAGON},
    // The pivot is the middle of the 48x48 picture, drawn 14 pixels right of
    // and 12 below the entity's position, the top-left of its 28x36 body.
    [CS_DRAGON] = {.pieces = dragon_pieces,
                   .piece_count = 9,
                   .frame_count = DRAGON_FRAME_COUNT,
                   .origin_x = -14,
                   .origin_y = -12,
                   .flags = SPRITE_ASSET_METASPRITE},
};

static const u16 group_sprites[] = {SPR_BLOCK,           SPR_DEBRIS,      SPR_SALAMANDER,
                                    SPR_SALAMANDER_FLAT, SPR_FIREBALL,    SPR_EMBER,
                                    SPR_BREATH,          SPR_DRAGON_PART, SPR_DRAGON};

const SpriteGroup castle_group = {
    .sprite_ids = group_sprites,
    .palettes = &sprite_palettes[0][0],
    .sprite_count = sizeof group_sprites / sizeof group_sprites[0],
    .palette_count = CS_PALETTE_COUNT,
};

// --- Backgrounds --------------------------------------------------------------

// Background palette banks. BANK_FAR is BANK_STONE's colors dimmed toward the
// backdrop when the stage loads (castle_load_palettes()), so the tileset's
// palettes are in RAM.
enum { BANK_STONE, BANK_BLOCKS, BANK_GEM, BANK_FIRE, BANK_GATE, BANK_FAR, BANK_COUNT };

// 104 unique 8x8 tiles: 89 for the playfield, 15 more for the
// parallax layer. Tile 0 is empty (transparent); tiles 1-4 are the bonus
// block, 5-6 the lava's surface and 7-8 the torch's flame (all three
// animated).
static const u32 bg_tiles[832] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x11111110, 0x88888881, 0x77777781, 0x77777781, 0x99777781, 0x99777781, 0x77997781, 0x97997781,
    0x01111111, 0x17888888, 0x16777777, 0x16777777, 0x16777997, 0x16777997, 0x16799777, 0x16799799,
    0x99777781, 0x99977781, 0x99977781, 0x99777781, 0x77777781, 0x77777781, 0x66666671, 0x11111110,
    0x16777999, 0x16779999, 0x16779999, 0x16777999, 0x16777777, 0x16777777, 0x16666666, 0x01111111,
    0x00000000, 0x60000066, 0x56666655, 0x45555544, 0x34444422, 0x33333332, 0x22333333, 0x22334443,
    0x06666600, 0x65555566, 0x54444455, 0x43333344, 0x23344433, 0x33455433, 0x33455433, 0x33344332,
    0x60000000, 0x56000000, 0x76000000, 0x76500000, 0x76500000, 0x75400000, 0x54000000, 0x00000000,
    0x00000000, 0x00000006, 0x00000005, 0x00000056, 0x00000046, 0x00000045, 0x00000004, 0x00000000,
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
    0x77777777, 0x66666666, 0x44444445, 0x44434445, 0x44444445, 0x44444445, 0x33333333, 0x11111111,
    0x17777777, 0x15666666, 0x13444444, 0x13444444, 0x13444344, 0x13444444, 0x13333333, 0x11111111,
    0x15666666, 0x13444444, 0x13444344, 0x13444444, 0x13444444, 0x13444444, 0x12333333, 0x11111111,
    0x66666666, 0x44444445, 0x44444445, 0x44434445, 0x44444445, 0x44444445, 0x33333333, 0x11111111,
    0x66666666, 0x44444445, 0x44434445, 0x44444445, 0x44444445, 0x44444445, 0x33333333, 0x11111111,
    0x15666666, 0x13444444, 0x13444444, 0x13444344, 0x13444444, 0x13444444, 0x13333333, 0x11111111,
    0x77777777, 0x66666667, 0x55555567, 0x44444567, 0x44444567, 0x44444567, 0x44444567, 0x44444567,
    0x16777777, 0x15666666, 0x13455555, 0x13444444, 0x13444444, 0x13444443, 0x13444444, 0x13444444,
    0x44444567, 0x43444567, 0x44444567, 0x44444567, 0x44444567, 0x44444467, 0x33333356, 0x11111111,
    0x13444444, 0x13444444, 0x13444444, 0x13444444, 0x13434444, 0x13444444, 0x12333333, 0x11111111,
    0x14444444, 0x13444334, 0x12333333, 0x11111111, 0x44441444, 0x34441344, 0x33331223, 0x11111111,
    0x14444444, 0x13443444, 0x12333333, 0x11111111, 0x44441444, 0x34441344, 0x33331233, 0x11111111,
    0x14444444, 0x13443444, 0x12333333, 0x11111111, 0x44441444, 0x44441434, 0x33331233, 0x11111111,
    0x14444444, 0x13444434, 0x12333333, 0x11111111, 0x44441444, 0x43441434, 0x33331233, 0x11111111,
    0x77777777, 0x55555555, 0x33333333, 0x11111111, 0x00013452, 0x00013452, 0x00013332, 0x00001111,
    0x77777777, 0x16666666, 0x13444445, 0x13444344, 0x12333333, 0x11111111, 0x01345610, 0x01344510,
    0x77777777, 0x16666666, 0x13444445, 0x13444445, 0x12333333, 0x11111111, 0x01345610, 0x01344510,
    0x00135100, 0x00134100, 0x00011000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x77777777, 0x66666666, 0x44444445, 0x44443444, 0x33333333, 0x11111111, 0x01344561, 0x01345610,
    0x77777777, 0x66666666, 0x54443444, 0x34444444, 0x33333333, 0x11111111, 0x00000000, 0x00000000,
    0x01345610, 0x01345100, 0x01346100, 0x01341000, 0x01331000, 0x00110000, 0x00000000, 0x00000000,
    0x00000000, 0x00011100, 0x00145410, 0x00134410, 0x00191100, 0x01910000, 0x19100000, 0x91000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,
    0x10000000, 0x00000000, 0x10000000, 0xDD110000, 0xCCCD1000, 0xCBCC1000, 0xBBBB1000, 0x11111000,
    0x00000019, 0x000001A1, 0x000011A1, 0x0011DDDD, 0x01BCCCCC, 0x01BCBCCC, 0x018BBBBB, 0x01111111,
    0x11111111, 0xAAAAAAA1, 0x9999A9A1, 0x99999AA1, 0x199999A1, 0x319999A1, 0x531999A1, 0x653199A1,
    0x11111111, 0x19AAAAAA, 0x189A9999, 0x18999999, 0x18999991, 0x18999915, 0x18999156, 0x18991567,
    0x765199A1, 0x651999A1, 0x519999A1, 0x199999A1, 0x999999A1, 0x9999A9A1, 0x88888891, 0x11111111,
    0x18991356, 0x18999135, 0x18999913, 0x18999991, 0x18999999, 0x189A9999, 0x18888888, 0x11111111,
    0x10000000, 0xA1000000, 0x91000000, 0x91000000, 0x10000000, 0x10000000, 0x10000000, 0x10000000,
    0x00000001, 0x00000019, 0x00000010, 0x00000018, 0x00000001, 0x0000000A, 0x00000008, 0x00000001,
    0xAA100000, 0x99100000, 0x91000000, 0x10000000, 0x10000000, 0x91100000, 0x88100000, 0x00000000,
    0x000001AA, 0x00000189, 0x00000018, 0x00000008, 0x00000009, 0x00000118, 0x00000188, 0x00000000,
    0x22334554, 0x33335554, 0x33334443, 0x53333333, 0x53322333, 0x43322233, 0x33322334, 0x33333334,
    0x33333333, 0x33223333, 0x32223344, 0x32223355, 0x33333455, 0x43333345, 0x55433333, 0x55433333,
    0x33333334, 0x34543333, 0x35554333, 0x34553322, 0x33443322, 0x33333332, 0x22333333, 0x22334443,
    0x55432223, 0x33332223, 0x33333333, 0x23333333, 0x23344433, 0x33455433, 0x33455433, 0x33344332,
    0x14444445, 0x14444444, 0x13333333, 0x11111111, 0x44451444, 0x44441444, 0x33331333, 0x11111111,
    0x14444445, 0x14444444, 0x13333333, 0x11111111, 0x64451444, 0x14441444, 0x61331333, 0x66111111,
    0x14444445, 0x14444444, 0x13333333, 0x11111111, 0x44451444, 0x44441444, 0x61331333, 0x66111111,
    0x14444445, 0x14444444, 0x13333333, 0x11111111, 0x16666444, 0x61666666, 0x66166666, 0x77777766,
    0x66616445, 0x77661644, 0x7F766163, 0x7F776616, 0x7F777661, 0x7E777766, 0x77777776, 0x77777777,
    0x777E7777, 0xDDDDDDDD, 0x777F7777, 0x777F7777, 0x777F7777, 0x777E7777, 0x77777777, 0x77777777,
    0x14444445, 0x14444444, 0x13333333, 0x11111111, 0x44433333, 0x33133333, 0x33313333, 0x33777777,
    0x14444445, 0x14444444, 0x13333333, 0x11111111, 0x44451444, 0x44441444, 0x33331333, 0x11111133,
    0x77777E77, 0xDDDDDDDD, 0x77777F77, 0x77777F77, 0x77777F77, 0x7A777E77, 0x7AA77777, 0x9AA77777,
    0x14433313, 0x14333377, 0x1133377F, 0x3313777F, 0x3337777F, 0x3377777E, 0x37777777, 0x77777777,
    0x14444445, 0x14444444, 0x13333333, 0x11111111, 0x44451443, 0x44441443, 0x33331313, 0x11111131,
    0x66644445, 0x76644444, 0x76663333, 0x76661111, 0x77661444, 0x77661444, 0x88161333, 0x88611111,
    0x88664445, 0x88664444, 0x88663333, 0x88661111, 0x88661444, 0x88161444, 0x88611333, 0x88661111,
    0x77777779, 0x77777977, 0x77777777, 0x77777777, 0x77777777, 0x77777977, 0x88888888, 0x88888888,
    0x77777777, 0x77777777, 0x77777777, 0x77777777, 0x77777777, 0x77777777, 0x88888888, 0x88888888,
    0x88888888, 0x88888888, 0x88888888, 0x88889888, 0x88888888, 0x88888888, 0x88888888, 0x88888888,
    0x88888888, 0x88888888, 0x88888888, 0x88888888, 0x88888888, 0x88888888, 0x88888888, 0x88888888,
    0xAAAA7777, 0xAAA77777, 0xAAA77977, 0xAA777777, 0x77777777, 0x77777777, 0x88888888, 0x88888888,
    0x77777777, 0x7777777A, 0x7777AAAA, 0x77777AAA, 0x7977777A, 0x77777777, 0x88888888, 0x88888888,
    0x88888888, 0x88888888, 0x88888888, 0x88888898, 0x88888888, 0x88888888, 0x88888888, 0x88888888,
    0x14444333, 0x14444337, 0x13333337, 0x11111337, 0x44453177, 0x44443377, 0x33333388, 0x11113388,
    0x14443388, 0x14443388, 0x13331388, 0x11113188, 0x44453388, 0x44443388, 0x33333388, 0x11113388,
    0x88664445, 0x88664444, 0x88663333, 0x88661111, 0x88161444, 0x88611444, 0xC8661333, 0xCC661111,
    0xCC664445, 0xCC664444, 0xBC663333, 0xBB161111, 0xBB611444, 0xBB661444, 0xBB661333, 0xBB661111,
    0x88888888, 0x88888888, 0x88888888, 0x88888888, 0x88888888, 0xCCCCCC88, 0xCCCCCCCC, 0xCCCCCCCC,
    0x88888888, 0x88888888, 0x88888888, 0x88888888, 0x88888888, 0x8CCCCCCC, 0xCCCCCCCC, 0xCCCCCCCC,
    0xCCCCCCCC, 0xBBBBBBCC, 0xBBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB,
    0xCCCCCCCC, 0xCBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB,
    0x88888888, 0x88888888, 0x88888888, 0x88888888, 0x88888888, 0x88888888, 0x888888CC, 0x888CCCCC,
    0xCCCCCCCC, 0xCCCCCCCC, 0xCCCCCCBB, 0xCCCBBBBB, 0xBBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB, 0xBBBBBBBB,
    0x8888888C, 0x88888CCC, 0x88CCCCCC, 0xCCCCCCCC, 0xCCCCCCCB, 0xCCCCCBBB, 0xCCBBBBBB, 0xBBBBBBBB,
    0x14443388, 0x14441388, 0x13333188, 0x11113388, 0x44453388, 0x44443388, 0x33333388, 0x11113388,
    0x14441388, 0x14443188, 0x13333388, 0x111133CC, 0x444533CC, 0x444433CC, 0x333333CC, 0x111113BB,
    0x13333334, 0x13333333, 0x12222222, 0x11111111, 0x33341333, 0x33331333, 0x22221222, 0x11111111,
    0x13333334, 0x13333333, 0x12222222, 0x11111111, 0x33341333, 0x63331333, 0x66221222, 0x66111111,
    0x13333334, 0x13333333, 0x12222222, 0x11111111, 0x33341333, 0x33331333, 0x66666222, 0x66666666,
    0x66333334, 0x66663333, 0x06666622, 0x00066661, 0x00006666, 0x00000666, 0x00000066, 0x00000006,
    0x66666666, 0x00000066, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x13333334, 0x13333333, 0x12222222, 0x11111111, 0x33341333, 0x33331333, 0x22233333, 0x33333333,
    0x33333333, 0x33000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x13333333, 0x13333333, 0x12333330, 0x13333000, 0x33330000, 0x33300000, 0x33000000, 0x30000000,
    0x13333334, 0x13333333, 0x12222222, 0x11111111, 0x33341333, 0x33331333, 0x22221233, 0x11111133,
    0x66633334, 0x06663333, 0x06662222, 0x00666111, 0x00666333, 0x00066333, 0x00066622, 0x00066611,
    0x00006634, 0x00006633, 0x00006622, 0x00006611, 0x00006633, 0x00006633, 0x00006622, 0x00006611,
    0x13333333, 0x13333330, 0x12223330, 0x11133300, 0x33333300, 0x33333000, 0x22333000, 0x11333000,
    0x13330000, 0x13330000, 0x12330000, 0x11330000, 0x33330000, 0x33330000, 0x22330000, 0x11330000,
    0x11111111, 0x22222222, 0x22222222, 0x22222222, 0x22222222, 0x22222222, 0x22222222, 0x22222222,
    0x22222222, 0x22222222, 0x22222222, 0x22222222, 0x22222222, 0x22222222, 0x22222222, 0x22222222,
};

static const u16 rom_palettes[BANK_COUNT][16] = {
    [BANK_STONE] = {0, COLOR_RGB(12, 10, 16), COLOR_RGB(30, 28, 38), COLOR_RGB(50, 48, 62),
                    COLOR_RGB(74, 72, 90), COLOR_RGB(100, 98, 118), COLOR_RGB(132, 130, 150),
                    COLOR_RGB(176, 174, 192)},
    [BANK_BLOCKS] = {0, COLOR_RGB(44, 24, 20), COLOR_RGB(62, 54, 76), COLOR_RGB(92, 82, 110),
                     COLOR_RGB(134, 124, 154), COLOR_RGB(30, 26, 40), COLOR_RGB(178, 106, 22),
                     COLOR_RGB(238, 170, 42), COLOR_RGB(255, 228, 120), COLOR_RGB(112, 76, 50),
                     COLOR_RGB(164, 122, 86), COLOR_RGB(78, 50, 30), COLOR_RGB(130, 88, 46),
                     COLOR_RGB(222, 180, 116), COLOR_RGB(176, 128, 70), COLOR_RGB(196, 146, 96)},
    [BANK_GEM] = {0, COLOR_RGB(16, 40, 96), COLOR_RGB(30, 100, 210), COLOR_RGB(60, 170, 250),
                  COLOR_RGB(160, 236, 255), COLOR_RGB(255, 255, 255)},
    [BANK_FIRE] = {0, COLOR_RGB(20, 8, 8), COLOR_RGB(70, 14, 10), COLOR_RGB(150, 28, 14),
                   COLOR_RGB(214, 64, 20), COLOR_RGB(246, 132, 30), COLOR_RGB(255, 208, 70),
                   COLOR_RGB(255, 248, 200), COLOR_RGB(34, 32, 42), COLOR_RGB(70, 70, 86),
                   COLOR_RGB(124, 124, 144), COLOR_RGB(50, 48, 62), COLOR_RGB(100, 98, 118),
                   COLOR_RGB(132, 130, 150)},
    [BANK_GATE] = {0, COLOR_RGB(12, 10, 16), COLOR_RGB(30, 28, 38), COLOR_RGB(50, 48, 62),
                   COLOR_RGB(74, 72, 90), COLOR_RGB(100, 98, 118), COLOR_RGB(132, 130, 150),
                   COLOR_RGB(14, 18, 48), COLOR_RGB(28, 38, 90), COLOR_RGB(230, 236, 255),
                   COLOR_RGB(250, 244, 200), COLOR_RGB(14, 36, 40), COLOR_RGB(28, 64, 58),
                   COLOR_RGB(34, 32, 42), COLOR_RGB(70, 70, 86), COLOR_RGB(124, 124, 144)},
};
static u16 bg_palettes[BANK_COUNT][16] SERVAL_EWRAM_BSS;

// The far wall is the near stone's colors mixed this far (of 256) toward the
// backdrop: dim, and red-black like the hall's air. The dragon's flash is its
// colors mixed this far toward white.
#define FAR_DIMMING 168
#define FLASH_WHITENING 176

void castle_load_palettes(void) {
    for (int b = 0; b < BANK_COUNT; b++) {
        for (int c = 0; c < 16; c++)
            bg_palettes[b][c] = rom_palettes[b][c];
    }
    for (int c = 1; c < 16; c++)
        bg_palettes[BANK_FAR][c] =
            color_mix(rom_palettes[BANK_STONE][c], CASTLE_BACKDROP, FAR_DIMMING);
    for (int p = 0; p < CS_PAL_DRAGON_FLASH; p++) {
        for (int c = 0; c < 16; c++)
            sprite_palettes[p][c] = rom_sprite_palettes[p][c];
    }
    for (int c = 1; c < 16; c++)
        sprite_palettes[CS_PAL_DRAGON_FLASH][c] = color_mix(
            rom_sprite_palettes[CS_PAL_DRAGON][c], COLOR_RGB(255, 255, 255), FLASH_WHITENING);
}

const Tileset castle_tileset = {
    .tiles = bg_tiles,
    .tile_count = 104,
    .palettes = &bg_palettes[0][0],
    .palette_count = BANK_COUNT,
};

// The lava's surface, frame by frame (LAVA_TILE, two tiles): its wave rolls
// right. The first frame:
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ................
//   ..........yyyyy.
//   yy.....yyyoooooy
//   ooyyyyyoooRRRRRo
//   RRoooooRRRrrrrrR
//   ssRRRRRrrrRRRrrs
//   srrrrrrrrrRooRrr
//   rrrrrrssrrRooRrr
//   rRRRrrsssrrRRrrr
const u32 lava_tiles[LAVA_FRAMES][2 * 8] = {
    {0x00000000, 0x60000066, 0x56666655, 0x45555544, 0x34444422, 0x33333332, 0x22333333, 0x22334443,
     0x06666600, 0x65555566, 0x54444455, 0x43333344, 0x23344433, 0x33455433, 0x33455433,
     0x33344332},
    {0x00000666, 0x00666555, 0x66555444, 0x55444322, 0x44443322, 0x33333332, 0x22333333, 0x22334443,
     0x66000000, 0x55666000, 0x44555666, 0x23444555, 0x23344444, 0x33455433, 0x33455433,
     0x33344332},
    {0x06666600, 0x65555566, 0x54444455, 0x44553344, 0x33443322, 0x33333332, 0x22333333, 0x22334443,
     0x00000000, 0x60000066, 0x56666655, 0x45555544, 0x24444433, 0x33455433, 0x33455433,
     0x33344332},
    {0x66000000, 0x55666000, 0x44555666, 0x34444555, 0x33443444, 0x33333332, 0x22333333, 0x22334443,
     0x00000666, 0x00666555, 0x66555444, 0x55444333, 0x44344433, 0x33455433, 0x33455433,
     0x33344332},
};

// The torch's flame, frame by frame (TORCH_TILE, two tiles).
//   .......y........
//   ......yoy.......
//   ......ywo.......
//   .....oywyo......
//   .....oywyR......
//   .....RowoR......
//   ......RoR.......
//   ................
//
//   ........y.......
//   .......yo.......
//   ......oyw.......
//   .....oywyR......
//   .....RywyoR.....
//   .....RowoR......
//   ......RoR.......
//   ................
//
//   ......y.........
//   ......oy........
//   ......wyo.......
//   .....Rywyo......
//   ....RoywyR......
//   .....RowoR......
//   ......RoR.......
//   ................
const u32 torch_tiles[TORCH_FRAMES][2 * 8] = {
    {0x60000000, 0x56000000, 0x76000000, 0x76500000, 0x76500000, 0x75400000, 0x54000000, 0x00000000,
     0x00000000, 0x00000006, 0x00000005, 0x00000056, 0x00000046, 0x00000045, 0x00000004,
     0x00000000},
    {0x00000000, 0x60000000, 0x65000000, 0x76500000, 0x76400000, 0x75400000, 0x54000000, 0x00000000,
     0x00000006, 0x00000005, 0x00000007, 0x00000046, 0x00000456, 0x00000045, 0x00000004,
     0x00000000},
    {0x06000000, 0x65000000, 0x67000000, 0x76400000, 0x76540000, 0x75400000, 0x54000000, 0x00000000,
     0x00000000, 0x00000000, 0x00000005, 0x00000056, 0x00000046, 0x00000045, 0x00000004,
     0x00000000},
};

// The playfield's and foreground's metatiles: screen entries top-left,
// top-right, bottom-left, bottom-right.
//
// The floor: big stone blocks in staggered courses, a worn bright edge on
// top (MT_GROUND_TOP), then plain courses (MT_GROUND, two, so it repeats
// downward).
//   HHHHHHHHHHHHHHHK
//   hhhhhhhhhhhhhhRK
//   RrrrrrrrrrrrrrdK
//   RrrrdrrrrrrrrrdK
//   RrrrrrrrrrdrrrdK
//   RrrrrrrrrrrrrrdK
//   dddddddddddddddK
//   KKKKKKKKKKKKKKKK
//   hhhhhhRKhhhhhhhh
//   rrrrrrdKRrrrrrrr
//   rrdrrrdKRrrrrrrr
//   rrrrrrdKRrrrdrrr
//   rrrrrrdKRrrrrrrr
//   rrrrrrdKRrrrrrrr
//   ddddddsKdddddddd
//   KKKKKKKKKKKKKKKK
//
//   hhhhhhhhhhhhhhRK
//   RrrrrrrrrrrrrrdK
//   RrrrdrrrrrrrrrdK
//   RrrrrrrrrrdrrrdK
//   RrrrrrrrrrrrrrdK
//   RrrrrrrrrrrrrrdK
//   dddddddddddddddK
//   KKKKKKKKKKKKKKKK
//   hhhhhhRKhhhhhhhh
//   rrrrrrdKRrrrrrrr
//   rrdrrrdKRrrrrrrr
//   rrrrrrdKRrrrdrrr
//   rrrrrrdKRrrrrrrr
//   rrrrrrdKRrrrrrrr
//   ddddddsKdddddddd
//   KKKKKKKKKKKKKKKK
//
// A dressed stone block: staircases and pillars.
//   HHHHHHHHHHHHHHhK
//   HhhhhhhhhhhhhhRK
//   HhRRRRRRRRRRRrdK
//   HhRrrrrrrrrrrrdK
//   HhRrrrrrrrrrrrdK
//   HhRrrrrrdrrrrrdK
//   HhRrrrrrrrrrrrdK
//   HhRrrrrrrrrrrrdK
//   HhRrrrrrrrrrrrdK
//   HhRrrrdrrrrrrrdK
//   HhRrrrrrrrrrrrdK
//   HhRrrrrrrrrrrrdK
//   HhRrrrrrrrrrdrdK
//   HhrrrrrrrrrrrrdK
//   hRddddddddddddsK
//   KKKKKKKKKKKKKKKK
//
// Bricks (TAG_BRICK), the overworld's in grey-violet: a big serval breaks
// them. MT_GEM_BRICK looks the same but holds gems.
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
// The bonus block (TAG_BONUS; MT_BONUS_FISH has a fish), the used block it
// becomes and a gem in the air (TAG_GEM): the overworld's.
//
// A one-way ledge (MT_ONEWAY, MAP_ONEWAY): a slab on a carved corbel. The
// serval jumps up through it and lands on it. Its right half is this one
// mirrored.
//   HHHHHHHHHHHHHHHH
//   hhhhhhhhhhhhhhhh
//   RrrrrrrrrrrdrrrR
//   rrrdrrrrrrrrrrrd
//   dddddddddddddddd
//   KKKKKKKKKKKKKKKK
//   KhRrrdK.........
//   .KhRrdK.........
//   .KhRrdK.........
//   ..KRrdK.........
//   ..KhrdK.........
//   ...KrdK.........
//   ...KddK.........
//   ....KK..........
//   ................
//   ................
//
// The castle's walls and ceilings (MT_WALL), and a ceiling's lowest row with
// a carved edge and hanging teeth (MT_WALL_EDGE).
//   rrrrrrrKrrrrrrrK
//   rddrrrdKrrrdrrdK
//   ddddddsKddddddsK
//   KKKKKKKKKKKKKKKK
//   rrrKrrrrrrrKrrrr
//   rrdKrrrdrrdKrrrd
//   dssKddddddsKdddd
//   KKKKKKKKKKKKKKKK
//   rrrrrrrKrrrrrrrK
//   rrrdrrdKrdrrrrdK
//   ddddddsKddddddsK
//   KKKKKKKKKKKKKKKK
//   rrrKrrrrrrrKrrrr
//   rdrKrrrrrdrKrrdr
//   ddsKddddddsKdddd
//   KKKKKKKKKKKKKKKK
//
//   rrrrrrrKrrrrrrrK
//   rddrrrdKrrrdrrdK
//   ddddddsKddddddsK
//   KKKKKKKKKKKKKKKK
//   rrrKrrrrrrrKrrrr
//   rrdKrrrdrrdKrrrd
//   dssKddddddsKdddd
//   KKKKKKKKKKKKKKKK
//   HHHHHHHHHHHHHHHH
//   RRRRRRRRRRRRRRRR
//   dddddddddddddddd
//   KKKKKKKKKKKKKKKK
//   sRrdK...sRrdK...
//   sRrdK...sRrdK...
//   sdddK...sdddK...
//   KKKK....KKKK....
//
// Lava. The surface (MT_LAVA_TOP, above) is only a picture: the serval
// standing on the edge of a pit over it is safe. Under it MT_LAVA (TAG_HAZARD)
// is the lava that kills, open so the serval sinks into it, and MT_LAVA_DEEP,
// the same picture, is also solid: embers rest on it, out of sight.
//   RooRrrssrrrrrrrr
//   Rooorrrrrrrrssrr
//   rRRRrrrrRRrrsssr
//   rrrrrrrooorrsssr
//   rrrssrroooRrrrrr
//   rrsssrrRoRrrrrrR
//   RrrssrrrrrrrrRoo
//   RrrrrrrrrrrrrRoo
//   RrrrrrrrrsssrRoo
//   rrrrRoRrrsssrrrr
//   rrrRooorrrrrrrrr
//   ssrrooRrrrrrrrrs
//   ssrrRRrrrrRRRrrs
//   srrrrrrrrrRooRrr
//   rrrrrrssrrRooRrr
//   rRRRrrsssrrRRrrr
//
// The dragon's bridge (MT_BRIDGE): stone slabs on brackets over the lava.
//   HHHHHHHHHHHHHHHH
//   hhhhhhhKhhhhhhhK
//   RrrrrrdKRrrrrrdK
//   rrdrrrdKRrrrrrdK
//   ddddddsKddddddsK
//   KKKKKKKKKKKKKKKK
//   .KhRrdK..KhRrdK.
//   .KRrrdK..KRrrdK.
//   ..KRdK....KRdK..
//   ..KrdK....KrdK..
//   ...KK......KK...
//   ................
//   ................
//   ................
//   ................
//   ................
//
// The lever past the dragon (MT_LEVER), and pulled (MT_LEVER_PULLED: mirrored).
//   ................
//   ..KKK...........
//   .KRoRK..........
//   .KRRrK..........
//   ..KKIK..........
//   ....KIK.........
//   .....KIK........
//   ......KIK.......
//   .......KIK......
//   ........KJK.....
//   .......KKJKK....
//   ....KKhhhhhhKK..
//   ...KhGGGGGGGGgK.
//   ...KGGgGGGGgGgK.
//   ...KgggggggggiK.
//   ...KKKKKKKKKKKK.
//
// A fire bar's block (MT_FIREBAR): iron with a glowing socket, at the bar's
// pivot.
//   KKKKKKKKKKKKKKKK
//   KJJJJJJJJJJJJJIK
//   KJIJIIIIIIIIJIiK
//   KJJIIIIIIIIIIIiK
//   KJIIIIIKKIIIIIiK
//   KJIIIIKroKIIIIiK
//   KJIIIKroyoKIIIiK
//   KJIIKroywyoKIIiK
//   KJIIKoywyorKIIiK
//   KJIIIKoyorKIIIiK
//   KJIIIIKorKIIIIiK
//   KJIIIIIKKIIIIIiK
//   KJIIIIIIIIIIIIiK
//   KJIJIIIIIIIIJIiK
//   KIiiiiiiiiiiiiiK
//   KKKKKKKKKKKKKKKK
//
// A torch on the wall (MT_TORCH: its flame above, animated) and a chain
// hanging from the ceiling (MT_CHAIN).
//   .....KJJJJK.....
//   .....KIIIiK.....
//   ......KIiK......
//   .......Ki.......
//   .......KI.......
//   .....KKIiKK.....
//   .....KiiiiK.....
//   ................
//
//   .......KK.......
//   ......KJIK......
//   ......KI.K......
//   ......KIiK......
//   .......KK.......
//   .......KJ.......
//   .......Ki.......
//   .......KK.......
//   .......KK.......
//   ......KJIK......
//   ......KI.K......
//   ......KIiK......
//   .......KK.......
//   .......KJ.......
//   .......Ki.......
//   .......KK.......
//
// The exit (MT_EXIT, 4 x 3): a gateway in the castle wall, its portcullis
// raised, the moonlit night beyond. The serval walks in through the middle.
//   RrrrrrrKRrrrrrrKRrrrrrrKRrrrrrrKRrrrrrrKRrrrrrrKRrrrrrrKRrrrrrrK
//   rrrrrrrKrrrrrrrKrrrrrrrKrrrrrrrKrrrrrrrKrrrrrrrKrrrrrrrKrrrrrrrK
//   dddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddK
//   KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
//   rrrKRrrrrrrKRrrrrrrKRrrrrrrhhhhKdddddrrrrrrKRrrrrrrKRrrrrrrKRrrr
//   rrrKrrrrrrrKrrrrrrrKrrrrhhhhhhKhdddddKddrrrKrrrrrrrKrrrrrrrKrrrr
//   dddKdddddddKdddddddKddKhhhhhhKhhddddKddddddKdddddddKdddddddKdddd
//   KKKKKKKKKKKKKKKKKKKKKKhhhhnnnnnnnnnnnnddddKKKKKKKKKKKKKKKKKKKKKK
//   RrrrrrrKRrrrrrrKRrrhKhhhnnnnInnnnnInnnnndKdddrrKRrrrrrrKRrrrrrrK
//   rrrrrrrKrrrrrrrKrrhKhhnniiiiiiiiiiiiiiiinnddddrKrrrrrrrKrrrrrrrK
//   dddddddKdddddddKdhKhhnJnnnnnJnnnnnJnnnnnJnndddKKdddddddKdddddddK
//   KKKKKKKKKKKKKKKKhKhhnnJnnnnnJnnnnnJnnnnnJnnndKddKKKKKKKKKKKKKKKK
//   rrrKRrrrrrrKRrrhKhhnnnJnnnnnJnnnnnJnnnnnJnnnnddddrrKRrrrrrrKRrrr
//   rrrKrrrrrrrKrrrKhhnnnnInnnnnInnnnnInnnmnInnnnndddrrKrrrrrrrKrrrr
//   dddKdddddddKddKhhnnnnnnnnnnnnnnnnnnnnmmnnnnnnnnddKdKdddddddKdddd
//   KKKKKKKKKKKKKKhhnnnnnnnnnnnnnnnnnnnnnmm*nnnnnnnnKdKKKKKKKKKKKKKK
//   RrrrrrrKRrrrrhhh*nnnnnnnnnnnnnnnnnnnmmmmnnnnnnnndddrrrrKRrrrrrrK
//   rrrrrrrKrrrrrhhnnn*nnnnnnnnnnnnnnnnnnmmmmnnnnnnnnddrrrrKrrrrrrrK
//   dddddddKddddhhhnnnnnnnnnnnnnnnnnnn*nnmmmmmmmnnnnnddddddKdddddddK
//   KKKKKKKKKKKKhhhnnnnnnnnnnnnnnnnnnnnnnnmmmmmnnnnnnddKKKKKKKKKKKKK
//   rrrKRrrrrrrKhhnnnnnnnnnnnnnnnnnnnnnnnnnnmnnnnn*nnnKdRrrrrrrKRrrr
//   rrrKrrrrrrrKhhnnnn*nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnddrrrrrrrKrrrr
//   dddKdddddddKhKNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNdddddddddKdddd
//   KKKKKKKKKKKKKhNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNddKKKKKKKKKKKK
//   RrrrrrrKRrrrhhNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNddrrrKRrrrrrrK
//   rrrrrrrKrrrrhhNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNddrrrKrrrrrrrK
//   dddddddKddddhhNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNdKdddKdddddddK
//   KKKKKKKKKKKKhhNNNNN*NNNNNNNNNNNNNNNNNNNNN*NNNNNNNNKdKKKKKKKKKKKK
//   rrrKRrrrrrrKhhNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNddRrrrrrrKRrrr
//   rrrKrrrrrrrKhKNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNddrrrrrrrKrrrr
//   dddKdddddddKKhNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNdddddddddKdddd
//   KKKKKKKKKKKKhhNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNddKKKKKKKKKKKK
//   RrrrrrrKRrrrhhNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNddrrrKRrrrrrrK
//   rrrrrrrKrrrrhhNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNdKrrrKrrrrrrrK
//   dddddddKddddhhNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNKddddKdddddddK
//   KKKKKKKKKKKKhhNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNddKKKKKKKKKKKK
//   rrrKRrrrrrrKhKNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNddRrrrrrrKRrrr
//   rrrKrrrrrrrKKhNNNNVVVVVVVVVVVVVNNNNNNNNNNNNNNNNNNNddrrrrrrrKrrrr
//   dddKdddddddKhhNVVVVVVVVVVVVVVVVVVVNNNNNNNNNNNNNNNNdddddddddKdddd
//   KKKKKKKKKKKKhhVVVVVVVVVVVVVVVVVVVVVVVNNNNNNNNNNNNNddKKKKKKKKKKKK
//   RrrrrrrKRrrrhhVVVVVVVVVVVVVVVVVVVVVVVVVVVNNNNNNNNNdKrrrKRrrrrrrK
//   rrrrrrrKrrrrhhVVVVvvvvvvvvvvvvvVVVVVVVVVVVVNNNNNNNKdrrrKrrrrrrrK
//   dddddddKddddhhVvvvvvvvvvvvvvvvvvvvVVVVVVVVVVVVNNNNdddddKdddddddK
//   KKKKKKKKKKKKhKvvvvvvvvvvvvvvvvvvvvvvvVVVVVVVVVVVVVddKKKKKKKKKKKK
//   rrrKRrrrrrrKKhvvvvvvvvvvvvvvvvvvvvvvvvvvvVVVVVVVVVddRrrrrrrKRrrr
//   rrrKrrrrrrrKhhvvvvvvvvvvvvvvvvvvvvvvvvvvvvvVVVVVVVddrrrrrrrKrrrr
//   dddKdddddddKhhvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvVVVVdddddddddKdddd
//   KKKKKKKKKKKKhhvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvdKKKKKKKKKKKKK
const Metatile castle_metatiles[CASTLE_MT_COUNT] = {
    [MT_EMPTY] = {{0, 0, 0, 0}, MAP_EMPTY},
    [MT_GROUND_TOP] = {{MAP_SE(19, 0, 0), MAP_SE(20, 0, 0), MAP_SE(21, 0, 0), MAP_SE(22, 0, 0)},
                       MAP_SOLID},
    [MT_GROUND] = {{MAP_SE(23, 0, 0), MAP_SE(24, 0, 0), MAP_SE(21, 0, 0), MAP_SE(22, 0, 0)},
                   MAP_SOLID},
    [MT_STONE] = {{MAP_SE(25, 0, 0), MAP_SE(26, 0, 0), MAP_SE(27, 0, 0), MAP_SE(28, 0, 0)},
                  MAP_SOLID},
    [MT_BRICK] = {{MAP_SE(17, 1, 0), MAP_SE(17, 1, 0), MAP_SE(18, 1, 0), MAP_SE(18, 1, 0)},
                  MAP_SOLID | TAG_BRICK},
    [MT_GEM_BRICK] = {{MAP_SE(17, 1, 0), MAP_SE(17, 1, 0), MAP_SE(18, 1, 0), MAP_SE(18, 1, 0)},
                      MAP_SOLID | TAG_BRICK | TAG_BONUS},
    [MT_BONUS] = {{MAP_SE(1, 1, 0), MAP_SE(2, 1, 0), MAP_SE(3, 1, 0), MAP_SE(4, 1, 0)},
                  MAP_SOLID | TAG_BONUS},
    [MT_BONUS_FISH] = {{MAP_SE(1, 1, 0), MAP_SE(2, 1, 0), MAP_SE(3, 1, 0), MAP_SE(4, 1, 0)},
                       MAP_SOLID | TAG_BONUS},
    [MT_USED] = {{MAP_SE(9, 1, 0), MAP_SE(10, 1, 0), MAP_SE(11, 1, 0), MAP_SE(12, 1, 0)},
                 MAP_SOLID},
    [MT_HIDDEN] = {{0, 0, 0, 0}, MAP_SOLID},
    [MT_GEM] = {{MAP_SE(13, 2, 0), MAP_SE(14, 2, 0), MAP_SE(15, 2, 0), MAP_SE(16, 2, 0)},
                MAP_EMPTY | TAG_GEM},
    [MT_ONEWAY] = {{MAP_SE(37, 0, 0), MAP_SE(38, 0, 0), MAP_SE(39, 0, 0), 0}, MAP_ONEWAY},
    [MT_ONEWAY + 1] = {{MAP_SE(38, 0, MAP_SE_FLIP_H), MAP_SE(37, 0, MAP_SE_FLIP_H), 0,
                        MAP_SE(39, 0, MAP_SE_FLIP_H)},
                       MAP_ONEWAY},
    // No goal pole in the castle: its gate ends the stage.
    [MT_POLE_TOP] = {{0, 0, 0, 0}, MAP_EMPTY},
    [MT_POLE] = {{0, 0, 0, 0}, MAP_EMPTY},
    [MT_EXIT] = {{MAP_SE(56, 4, 0), MAP_SE(56, 4, 0), MAP_SE(56, 4, 0), MAP_SE(57, 4, 0)},
                 MAP_EMPTY},
    [MT_EXIT + 1] = {{MAP_SE(58, 4, 0), MAP_SE(59, 4, 0), MAP_SE(60, 4, 0), MAP_SE(61, 4, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 2] = {{MAP_SE(62, 4, 0), MAP_SE(63, 4, 0), MAP_SE(64, 4, 0), MAP_SE(65, 4, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 3] = {{MAP_SE(56, 4, 0), MAP_SE(56, 4, 0), MAP_SE(66, 4, 0), MAP_SE(56, 4, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 4] = {{MAP_SE(56, 4, 0), MAP_SE(67, 4, 0), MAP_SE(56, 4, 0), MAP_SE(68, 4, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 5] = {{MAP_SE(69, 4, 0), MAP_SE(70, 4, 0), MAP_SE(71, 4, 0), MAP_SE(72, 4, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 6] = {{MAP_SE(73, 4, 0), MAP_SE(74, 4, 0), MAP_SE(72, 4, 0), MAP_SE(75, 4, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 7] = {{MAP_SE(76, 4, 0), MAP_SE(56, 4, 0), MAP_SE(77, 4, 0), MAP_SE(56, 4, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 8] = {{MAP_SE(56, 4, 0), MAP_SE(78, 4, 0), MAP_SE(56, 4, 0), MAP_SE(79, 4, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 9] = {{MAP_SE(80, 4, 0), MAP_SE(81, 4, 0), MAP_SE(82, 4, 0), MAP_SE(83, 4, 0)},
                     MAP_EMPTY},
    [MT_EXIT + 10] = {{MAP_SE(84, 4, 0), MAP_SE(72, 4, 0), MAP_SE(85, 4, 0), MAP_SE(86, 4, 0)},
                      MAP_EMPTY},
    [MT_EXIT + 11] = {{MAP_SE(87, 4, 0), MAP_SE(56, 4, 0), MAP_SE(88, 4, 0), MAP_SE(56, 4, 0)},
                      MAP_EMPTY},
    [MT_WALL] = {{MAP_SE(29, 0, 0), MAP_SE(30, 0, 0), MAP_SE(31, 0, 0), MAP_SE(32, 0, 0)},
                 MAP_SOLID},
    [MT_WALL_EDGE] = {{MAP_SE(29, 0, 0), MAP_SE(30, 0, 0), MAP_SE(33, 0, 0), MAP_SE(33, 0, 0)},
                      MAP_SOLID},
    [MT_LAVA_TOP] = {{0, 0, MAP_SE(5, 3, 0), MAP_SE(6, 3, 0)}, MAP_EMPTY},
    [MT_LAVA] = {{MAP_SE(52, 3, 0), MAP_SE(53, 3, 0), MAP_SE(54, 3, 0), MAP_SE(55, 3, 0)},
                 MAP_EMPTY | TAG_HAZARD},
    [MT_LAVA_DEEP] = {{MAP_SE(52, 3, 0), MAP_SE(53, 3, 0), MAP_SE(54, 3, 0), MAP_SE(55, 3, 0)},
                      MAP_SOLID | TAG_HAZARD},
    [MT_BRIDGE] = {{MAP_SE(34, 0, 0), MAP_SE(35, 0, 0), MAP_SE(36, 0, 0), MAP_SE(36, 0, 0)},
                   MAP_SOLID},
    [MT_LEVER] = {{MAP_SE(40, 3, 0), MAP_SE(41, 3, 0), MAP_SE(42, 3, 0), MAP_SE(43, 3, 0)},
                  MAP_EMPTY},
    [MT_LEVER_PULLED] = {{MAP_SE(41, 3, MAP_SE_FLIP_H), MAP_SE(40, 3, MAP_SE_FLIP_H),
                          MAP_SE(43, 3, MAP_SE_FLIP_H), MAP_SE(42, 3, MAP_SE_FLIP_H)},
                         MAP_EMPTY},
    [MT_FIREBAR] = {{MAP_SE(44, 3, 0), MAP_SE(45, 3, 0), MAP_SE(46, 3, 0), MAP_SE(47, 3, 0)},
                    MAP_SOLID},
    [MT_TORCH] = {{MAP_SE(7, 3, 0), MAP_SE(8, 3, 0), MAP_SE(50, 3, 0), MAP_SE(51, 3, 0)},
                  MAP_EMPTY},
    [MT_CHAIN] = {{MAP_SE(48, 3, 0), MAP_SE(49, 3, 0), MAP_SE(48, 3, 0), MAP_SE(49, 3, 0)},
                  MAP_EMPTY},
};

// The parallax layer: arches of a colonnade on the far side of the hall, the
// lava's glow (the backdrop) showing between its pillars: 11 metatiles
// cut from a 64x192 picture of one arch, which repeats side to side, drawn in
// BANK_FAR.
//   rddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddK
//   dddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddK
//   sssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssK
//   KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
//   dddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddd
//   dddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddd
//   sssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKssss
//   KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
//   rddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddK
//   dddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddK
//   sssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssK
//   KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
//   dddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddd
//   dddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddd
//   sssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKssss
//   KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
//   rddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddK
//   dddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddK
//   sssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssK
//   KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
//   dddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddd
//   dddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddd
//   sssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKssss
//   KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
//   rddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddK
//   dddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddK
//   sssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssK
//   KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
//   dddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddd
//   dddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddd
//   sssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKssss
//   KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
//   rddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddK
//   dddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddK
//   sssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssKsssssssK
//   KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
//   dddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddddddKrddd
//   dddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddddddKdddd
//   sssKsssssssKsssssssKssssssshhhhhdddddssssssKsssssssKsssssssKssss
//   KKKKKKKKKKKKKKKKKKKKKKKKhhhhhhhhddddddddKKKKKKKKKKKKKKKKKKKKKKKK
//   rddddddKrddddddKrdddddhhhhhhhhhhdddddddddddddddKrddddddKrddddddK
//   dddddddKdddddddKddddhhhhhh............dddddddddKdddddddKdddddddK
//   sssssssKsssssssKsshhhhh..................dddddsKsssssssKsssssssK
//   KKKKKKKKKKKKKKKKKhhhh......................ddddKKKKKKKKKKKKKKKKK
//   dddKrddddddKrdddhhhh........................dddddddKrddddddKrddd
//   dddKdddddddKdddhhhh..........................ddddddKdddddddKdddd
//   sssKsssssssKsshhhh............................ddddsKsssssssKssss
//   KKKKKKKKKKKKKKhhh..............................dddKKKKKKKKKKKKKK
//   rddddddKrddddhhh................................dddddddKrddddddK
//   dddddddKddddhhh..................................ddddddKdddddddK
//   sssssssKsssshhh..................................dddsssKsssssssK
//   KKKKKKKKKKKhhh....................................dddKKKKKKKKKKK
//   dddKrddddddhhh....................................dddddddddKrddd
//   dddKdddddddhh......................................ddddddddKdddd
//   sssKsssssshhh......................................dddsssssKssss
//   KKKKKKKKKKhhh......................................dddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   rddddddKrdhh........................................dddKrddddddK
//   dddddddKddhh........................................dddKdddddddK
//   sssssssKsshh........................................ddsKsssssssK
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   dddKrdddddhh........................................dddddddKrddd
//   dddKddddddhh........................................dddddddKdddd
//   sssKsssssshh........................................ddsssssKssss
//   KKKKKKKKKKhh........................................ddKKKKKKKKKK
//   KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
//   ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
static const Metatile far_metatiles[11] = {
    {{MAP_SE(89, 5, 0), MAP_SE(89, 5, 0), MAP_SE(89, 5, 0), MAP_SE(89, 5, 0)}, MAP_EMPTY},
    {{MAP_SE(89, 5, 0), MAP_SE(89, 5, 0), MAP_SE(89, 5, 0), MAP_SE(90, 5, 0)}, MAP_EMPTY},
    {{MAP_SE(89, 5, 0), MAP_SE(91, 5, 0), MAP_SE(92, 5, 0), MAP_SE(93, 5, 0)}, MAP_EMPTY},
    {{MAP_SE(94, 5, 0), MAP_SE(89, 5, 0), MAP_SE(95, 5, 0), MAP_SE(96, 5, 0)}, MAP_EMPTY},
    {{MAP_SE(89, 5, 0), MAP_SE(89, 5, 0), MAP_SE(97, 5, 0), MAP_SE(89, 5, 0)}, MAP_EMPTY},
    {{MAP_SE(89, 5, 0), MAP_SE(98, 5, 0), MAP_SE(89, 5, 0), MAP_SE(99, 5, 0)}, MAP_EMPTY},
    {{0, 0, 0, 0}, MAP_EMPTY},
    {{MAP_SE(100, 5, 0), MAP_SE(89, 5, 0), MAP_SE(101, 5, 0), MAP_SE(89, 5, 0)}, MAP_EMPTY},
    {{MAP_SE(89, 5, 0), MAP_SE(99, 5, 0), MAP_SE(89, 5, 0), MAP_SE(99, 5, 0)}, MAP_EMPTY},
    {{MAP_SE(101, 5, 0), MAP_SE(89, 5, 0), MAP_SE(101, 5, 0), MAP_SE(89, 5, 0)}, MAP_EMPTY},
    {{MAP_SE(102, 5, 0), MAP_SE(102, 5, 0), MAP_SE(103, 5, 0), MAP_SE(103, 5, 0)}, MAP_EMPTY},
};

static const u16 far_cells[4 * 12] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 6, 7, 8, 6, 6, 9, 8,  6,  6,  9,
    8, 6, 6, 9, 8, 6, 6, 9, 8, 6, 6, 9, 8, 6, 6, 9, 8, 6, 6, 9, 10, 10, 10, 10,
};

const MapLayer castle_far_layer = {
    .width = 4,
    .height = 12,
    .cells = far_cells,
    .metatiles = far_metatiles,
    .metatile_count = 11,
    .bg = 3,
    .flags = MAP_LAYER_WRAP,
    .scroll_factor = FX_ONE / 2,
};
