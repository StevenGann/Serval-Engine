// Generated from ASCII pixel art by a conversion script (kept outside the
// repository); the previews in the comments are that art, one character per
// pixel. Edit the art and regenerate rather than editing the numbers. The
// script maps each character to a palette index through a legend per
// palette, packs 4 bits per pixel (the low nibble of each word is the leftmost
// pixel; word n of a tile is its row n), and lays sprites out frame by frame,
// each frame's 8x8 tiles row by row. For backgrounds it keeps one copy of
// each 8x8 tile, matching flipped copies too (the screen entries' flip bits).
//
// What every stage shares: the serval, gems, the fish, the goal banner,
// sparkles, HUD icons, bouncing blocks and brick debris, the bonus block's
// glint, and the sprite table. Each stage's own art is in its art_<name>.c.

#include "game.h"

// --- Sprites ------------------------------------------------------------------

// The small serval: stand, run 1, run 2, jump, skid, dead. Faces right.
//   .......K..K.....
//   ......KsKKsK....
//   ......KpsKpsK...
//   ......KOOOOOOK..
//   .....KOsOOsOOK..
//   .....KOOOOOKeOK.
//   ..K..KOOOOOKwOOK
//   .KsK.KYOOOOOOOpK
//   .KK...KYwwwwwYK.
//   ..K..KOsOOsOOK..
//   ..KK.KOOOOOsOK..
//   ...KKOsOOsOOOK..
//   ....KOOOOOOOK...
//   ....KOK..KOOK...
//   ....KYK..KYK....
//   ....KKK..KKK....
static const u32 serval_small_tiles[192] = {
    0x10000000, 0x61000000, 0x71000000, 0x31000000, 0x63100000, 0x33100000, 0x33100100, 0x34101610,
    0x00000100, 0x00001611, 0x00016716, 0x00133333, 0x00133633, 0x01381333, 0x13351333, 0x17333333,
    0x41000110, 0x63100100, 0x33101100, 0x36311000, 0x33310000, 0x01310000, 0x01410000, 0x01110000,
    0x01455555, 0x00133633, 0x00136333, 0x00133363, 0x00013333, 0x00013310, 0x00001410, 0x00001110,
    0x10000000, 0x61000000, 0x71000000, 0x31000000, 0x63100000, 0x33100000, 0x33100000, 0x34101100,
    0x00000100, 0x00001611, 0x00016716, 0x00133333, 0x00133633, 0x01381333, 0x13351333, 0x17333333,
    0x41001610, 0x63100110, 0x33311100, 0x36310000, 0x33331000, 0x01113100, 0x00014100, 0x00001100,
    0x01455555, 0x00133633, 0x00136333, 0x00133363, 0x00133311, 0x00143100, 0x00141000, 0x00110000,
    0x00000000, 0x10000000, 0x61000000, 0x71000000, 0x31000000, 0x63100000, 0x33100000, 0x33100100,
    0x00000000, 0x00000100, 0x00001611, 0x00016716, 0x00133333, 0x00133633, 0x01381333, 0x13351333,
    0x34101610, 0x41000110, 0x63101100, 0x33311000, 0x36310000, 0x33100000, 0x14100000, 0x01100000,
    0x17333333, 0x01455555, 0x00133633, 0x00136333, 0x00013363, 0x00001334, 0x00001431, 0x00001110,
    0x10000000, 0x61000000, 0x71000000, 0x31000000, 0x63100000, 0x33100000, 0x33100000, 0x34100000,
    0x00000100, 0x00001611, 0x00016716, 0x00133333, 0x00133633, 0x01381333, 0x13351333, 0x17333333,
    0x41001100, 0x63101610, 0x33310110, 0x33631100, 0x33331000, 0x11143100, 0x00011100, 0x00000000,
    0x01455555, 0x14133633, 0x01336333, 0x00113336, 0x00001333, 0x00001431, 0x00001410, 0x00000110,
    0x01000000, 0x16100000, 0x17610000, 0x33310000, 0x36331000, 0x33183100, 0x33153310, 0x33333371,
    0x00000010, 0x00000161, 0x00001761, 0x00001333, 0x00013363, 0x00013333, 0x00013333, 0x01001433,
    0x55555410, 0x36331000, 0x33631000, 0x63331000, 0x33310000, 0x11341000, 0x00114100, 0x00001100,
    0x16100014, 0x11001363, 0x01101333, 0x00113633, 0x00013333, 0x00013311, 0x00014100, 0x00011100,
    0x00100000, 0x01610000, 0x16710000, 0x33310000, 0x33631000, 0x31131000, 0x33331000, 0x73341000,
    0x00000100, 0x00001610, 0x00001761, 0x00001333, 0x00013633, 0x00013113, 0x00013333, 0x00014337,
    0x55410010, 0x36310141, 0x33331310, 0x33633100, 0x63331000, 0x33331000, 0x00141000, 0x00011000,
    0x01001455, 0x14101363, 0x01313333, 0x00133633, 0x00013333, 0x00013333, 0x00014100, 0x00011000,
};

// The grown serval: stand, run 1, run 2, jump, skid.
//   ................
//   ................
//   ................
//   ................
//   .......KK..KK...
//   ......KssKKssK..
//   ......KspKKspK..
//   ......KspsKspK..
//   ......KOOOOOOK..
//   .....KOOsOOsOK..
//   .....KOOOOOOOOK.
//   .....KOOOOOKeOK.
//   .....KOOOOOKwOOK
//   .....KYOOOOOOOpK
//   ......KYYwwwwYK.
//   .......KYwwwwK..
//   ......KOOOOOOK..
//   ..K..KOsOOsOOOK.
//   .KsK.KOOOOOOsOK.
//   .KK..KOsOOwwOOK.
//   ..K..KOOOsOwwOK.
//   ..KK.KOOOOOwOOK.
//   ...KKOsOOOsOOK..
//   ....KOOOOOOOK...
//   ....KOOOKOOOK...
//   ....KOOK.KOOK...
//   ....KOsK.KOsK...
//   ....KOOK.KOOK...
//   ....KOOK.KOOK...
//   ....KOsK.KOsK...
//   ....KYYK.KYYK...
//   ....KKKK.KKKK...
static const u32 serval_big_tiles[320] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x61000000, 0x61000000, 0x61000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00011001, 0x00166116, 0x00176117, 0x00176167,
    0x31000000, 0x33100000, 0x33100000, 0x33100000, 0x33100000, 0x34100000, 0x41000000, 0x10000000,
    0x00133333, 0x00136336, 0x01333333, 0x01381333, 0x13351333, 0x17333333, 0x01455554, 0x00155554,
    0x31000000, 0x63100100, 0x33101610, 0x63100110, 0x33100100, 0x33101100, 0x36311000, 0x33310000,
    0x00133333, 0x01333633, 0x01363333, 0x01335533, 0x01355363, 0x01335333, 0x00133633, 0x00013333,
    0x33310000, 0x13310000, 0x16310000, 0x13310000, 0x13310000, 0x16310000, 0x14410000, 0x11110000,
    0x00013331, 0x00013310, 0x00016310, 0x00013310, 0x00013310, 0x00016310, 0x00014410, 0x00011110,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x61000000, 0x61000000, 0x61000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00011001, 0x00166116, 0x00176117, 0x00176167,
    0x31000000, 0x33100000, 0x33100000, 0x33100000, 0x33100000, 0x34100000, 0x41000000, 0x10000000,
    0x00133333, 0x00136336, 0x01333333, 0x01381333, 0x13351333, 0x17333333, 0x01455554, 0x00155554,
    0x31001100, 0x63101610, 0x33100110, 0x63100100, 0x33101100, 0x33311000, 0x36310000, 0x33310000,
    0x00133333, 0x01333633, 0x01363333, 0x01335533, 0x01355363, 0x01335333, 0x00133633, 0x00013333,
    0x13331000, 0x00133100, 0x00016310, 0x00001310, 0x00000131, 0x00000161, 0x00000141, 0x00000011,
    0x00013331, 0x00013310, 0x00163100, 0x00133100, 0x00131000, 0x00161000, 0x01441000, 0x01110000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x61000000, 0x61000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00011001, 0x00166116, 0x00176117,
    0x61000000, 0x31000000, 0x33100000, 0x33100000, 0x33100000, 0x33100000, 0x34100000, 0x41000000,
    0x00176167, 0x00133333, 0x00136336, 0x01333333, 0x01381333, 0x13351333, 0x17333333, 0x01455554,
    0x10000100, 0x31001610, 0x63100110, 0x33100100, 0x63101100, 0x33311000, 0x33310000, 0x36310000,
    0x00155554, 0x00133333, 0x01333633, 0x01363333, 0x01335533, 0x01355363, 0x01335333, 0x00133633,
    0x33100000, 0x33100000, 0x63100000, 0x33100000, 0x33100000, 0x63100000, 0x44100000, 0x11100000,
    0x00013333, 0x00001331, 0x00001631, 0x00001331, 0x00001331, 0x00001631, 0x00001441, 0x00001111,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x61000000, 0x61000000, 0x61000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00011001, 0x00166116, 0x00176117, 0x00176167,
    0x31000000, 0x33100000, 0x33100000, 0x33100000, 0x33100000, 0x34100000, 0x41000000, 0x10000000,
    0x00133333, 0x00136336, 0x01333333, 0x01381333, 0x13351333, 0x17333333, 0x01455554, 0x11155554,
    0x31000000, 0x63101100, 0x33101610, 0x63100110, 0x33311100, 0x36310000, 0x33310000, 0x33331000,
    0x14133333, 0x01333633, 0x00163333, 0x00135533, 0x00155363, 0x00135333, 0x00013336, 0x00001333,
    0x11133100, 0x00016310, 0x00001310, 0x00000131, 0x00000161, 0x00000141, 0x00000011, 0x00000000,
    0x00001331, 0x00001331, 0x00001631, 0x00001331, 0x00001310, 0x00001410, 0x00000110, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00110000, 0x11661000, 0x11671000, 0x61671000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000011, 0x00000166, 0x00000167, 0x00000167,
    0x33331000, 0x33631000, 0x33333100, 0x33183100, 0x33153310, 0x33333371, 0x45555410, 0x45555100,
    0x00000133, 0x00001336, 0x00013333, 0x00013333, 0x00013333, 0x00001433, 0x00000014, 0x01100001,
    0x33331000, 0x36333100, 0x33363100, 0x35533100, 0x63553100, 0x33533100, 0x36331000, 0x33310000,
    0x16100133, 0x11001363, 0x01001333, 0x01101363, 0x00111333, 0x00013333, 0x00013633, 0x00013333,
    0x33310000, 0x13310000, 0x01631000, 0x00133100, 0x00013100, 0x00001610, 0x00001410, 0x00000110,
    0x00133331, 0x00133110, 0x01631000, 0x01331000, 0x01310000, 0x01610000, 0x14410000, 0x11100000,
};

// A gem popping out of a block, spinning: front, turning, edge-on (frames
// 0-2). Its animation plays frame 1 again mirrored, as the gem turns on
// around (gem_spin below), so that frame needs no tiles of its own.
//   ................
//   ......KKKK......
//   ....KKLwLDKK....
//   ...KLwLLDDDdK...
//   ..KLwLLLDDDDdK..
//   ..KLLLLDDDDDdK..
//   ..KDDDDDDDDddK..
//   ...KDDDDDDddK...
//   ...KdDDDDDddK...
//   ....KdDDDddK....
//   .....KdDDdK.....
//   ......KddK......
//   .......KK.......
//   ................
//   ................
//   ................
static const u32 gem_tiles[96] = {
    0x00000000, 0x11000000, 0x54110000, 0x44541000, 0x44454100, 0x34444100, 0x33333100, 0x33331000,
    0x00000000, 0x00000011, 0x00001134, 0x00012333, 0x00123333, 0x00123333, 0x00122333, 0x00012233,
    0x33321000, 0x33210000, 0x32100000, 0x21000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000,
    0x00012233, 0x00001223, 0x00000123, 0x00000012, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x10000000, 0x41000000, 0x54100000, 0x44100000, 0x44100000, 0x33100000, 0x33100000,
    0x00000000, 0x00000001, 0x00000013, 0x00000123, 0x00000123, 0x00000123, 0x00000123, 0x00000123,
    0x32100000, 0x31000000, 0x31000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000123, 0x00000012, 0x00000012, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x10000000, 0x10000000, 0x10000000, 0x10000000, 0x10000000, 0x10000000, 0x10000000,
    0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x10000000, 0x10000000, 0x10000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000001, 0x00000001, 0x00000001, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

// The power-up: a fish that slides out of a block and flops along.
//   ................
//   ................
//   ................
//   .........KKK....
//   .......KKffK....
//   ..KK..KDDDDKKK..
//   .KfFKKDDDDDwKDK.
//   .KffKDDDDDDKKDDK
//   ..KfFKDLDDDDDDKK
//   .KffKLLLLDDDDDK.
//   .KfFKKLLLLLLKK..
//   ..KK..KKKffKK...
//   .........KK.....
//   ................
//   ................
//   ................
static const u32 fish_tiles[32] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10000000, 0x31001100, 0x33117610, 0x33316610,
    0x00000000, 0x00000000, 0x00000000, 0x00001110, 0x00001661, 0x00111333, 0x01315333, 0x13311333,
    0x43176100, 0x44416610, 0x44117610, 0x11001100, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x11333333, 0x01333334, 0x00114444, 0x00011661, 0x00000110, 0x00000000, 0x00000000, 0x00000000,
};

// The goal banner, which slides down the pole.
//   ................
//   .KKKKKKKKKKKKKK.
//   KrrrrrrrrrrrrrK.
//   .KrrrrwrwrrrrrK.
//   ..KrrrrrrrwrrRK.
//   ...KrrwwwrrrrRK.
//   ....KrwwwwrrrRK.
//   .....KrwwrrrRRK.
//   ......KrrrRRRRK.
//   .......KrRRRRRK.
//   ........KRRRRRK.
//   .........KRRRRK.
//   ..........KRRRK.
//   ...........KRRK.
//   ............KKK.
//   ................
static const u32 banner_tiles[32] = {
    0x00000000, 0x11111110, 0x99999991, 0x95999910, 0x99999100, 0x55991000, 0x55910000, 0x59100000,
    0x00000000, 0x01111111, 0x01999999, 0x01999995, 0x01A99599, 0x01A99995, 0x01A99955, 0x01AA9995,
    0x91000000, 0x10000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x01AAAA99, 0x01AAAAA9, 0x01AAAAA1, 0x01AAAA10, 0x01AAA100, 0x01AA1000, 0x01110000, 0x00000000,
};

// A sparkle where a gem was collected.
//   ........
//   ....w...
//   ...wLw..
//   ..wLwLw.
//   ...wLw..
//   ....w...
//   ........
//   ........
static const u32 sparkle_tiles[16] = {
    0x00000000, 0x00050000, 0x00545000, 0x05454500, 0x00545000, 0x00050000, 0x00000000, 0x00000000,
    0x00005000, 0x00005000, 0x50000000, 0x05504055, 0x50000000, 0x00005000, 0x00005000, 0x00000000,
};

// HUD icon: gems.
//   ..KKKK..
//   .KLwDdK.
//   KLLDDDdK
//   KDDDDddK
//   .KdDDdK.
//   ..KddK..
//   ...KK...
//   ........
static const u32 hud_gem_tiles[8] = {
    0x00111100, 0x01235410, 0x12333441, 0x12233331, 0x01233210, 0x00122100, 0x00011000, 0x00000000,
};

// HUD icon: lives.
//   .K....K.
//   KsK..KsK
//   KpOOOOpK
//   KOeOOeOK
//   KOOOOOOK
//   .KYppYK.
//   ..KwwK..
//   ...KK...
static const u32 hud_serval_tiles[8] = {
    0x01000010, 0x16100161, 0x17333371, 0x13833831, 0x13333331, 0x01477410, 0x00155100, 0x00011000,
};

// HUD icon: time.
//   ..KKKK..
//   .KwwwwK.
//   KwwKwwwK
//   KwwKwwwK
//   KwwKKKwK
//   KwwwwwwK
//   .KwwwwK.
//   ..KKKK..
static const u32 hud_clock_tiles[8] = {
    0x00111100, 0x01555510, 0x15551551, 0x15551551, 0x15111551, 0x15555551, 0x01555510, 0x00111100,
};

// A block bouncing after a hit from below: bonus, used, brick (same art as the tiles).
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
static const u32 block_tiles[96] = {
    0x11111110, 0x88888881, 0x77777781, 0x77777781, 0x99777781, 0x99777781, 0x77997781, 0x97997781,
    0x01111111, 0x17888888, 0x16777777, 0x16777777, 0x16777997, 0x16777997, 0x16799777, 0x16799799,
    0x99777781, 0x99977781, 0x99977781, 0x99777781, 0x77777781, 0x77777781, 0x66666671, 0x11111110,
    0x16777999, 0x16779999, 0x16779999, 0x16777999, 0x16777777, 0x16777777, 0x16666666, 0x01111111,
    0x11111110, 0xAAAAAAA1, 0xAAAAA9A1, 0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAAAA1,
    0x01111111, 0x19AAAAAA, 0x199AAAAA, 0x19AAAAAA, 0x19AAAAAA, 0x19AAAAAA, 0x19AAAAAA, 0x19AAAAAA,
    0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAAAA1, 0xAAAAA9A1, 0x99999991, 0x11111110,
    0x19AAAAAA, 0x19AAAAAA, 0x19AAAAAA, 0x19AAAAAA, 0x19AAAAAA, 0x199AAAAA, 0x19999999, 0x01111111,
    0x54444444, 0x53333334, 0x52333333, 0x52222222, 0x55555555, 0x44445444, 0x33345333, 0x33335233,
    0x54444444, 0x53333334, 0x52333333, 0x52222222, 0x55555555, 0x44445444, 0x33345333, 0x33335233,
    0x22225222, 0x55555555, 0x54444444, 0x53333334, 0x52333333, 0x52222222, 0x55555555, 0x55555555,
    0x22225222, 0x55555555, 0x54444444, 0x53333334, 0x52333333, 0x52222222, 0x55555555, 0x55555555,
};

// A piece of a broken brick, tumbling: one frame, which its animation shows
// flipped horizontally, vertically and both ways (debris_tumble below).
//   ........
//   ..KKKK..
//   .KEERRK.
//   .KERRrK.
//   .KRRrrK.
//   ..KKKK..
//   ........
//   ........
static const u32 debris_tiles[8] = {
    0x00000000, 0x00111100, 0x01334410, 0x01233410, 0x01223310, 0x00111100, 0x00000000, 0x00000000,
};

static const u16 sprite_palettes[PALETTE_COUNT][16] = {
    [PAL_SERVAL] = {0, COLOR_RGB(44, 26, 22), COLOR_RGB(190, 104, 36), COLOR_RGB(236, 156, 56),
                    COLOR_RGB(252, 208, 128), COLOR_RGB(255, 250, 236), COLOR_RGB(58, 36, 30),
                    COLOR_RGB(240, 144, 150), COLOR_RGB(150, 190, 60), COLOR_RGB(220, 60, 40),
                    COLOR_RGB(150, 30, 30)},
    [PAL_GEM] = {0, COLOR_RGB(16, 40, 96), COLOR_RGB(30, 100, 210), COLOR_RGB(60, 170, 250),
                 COLOR_RGB(160, 236, 255), COLOR_RGB(255, 255, 255)},
    [PAL_FISH] = {0, COLOR_RGB(40, 30, 60), COLOR_RGB(200, 80, 96), COLOR_RGB(250, 128, 112),
                  COLOR_RGB(255, 196, 164), COLOR_RGB(255, 255, 255), COLOR_RGB(255, 200, 60),
                  COLOR_RGB(220, 140, 30)},
};

// Animation timing for sys_animate: frames (1/60 s) each animation frame (or
// sequence step) shows.
static const u8 gem_spin_times[4] = {3, 3, 3, 3};
static const u8 sparkle_times[2] = {4, 4};
static const u8 debris_times[4] = {4, 4, 4, 4};

// Animation sequences for sys_animate: frames to show, some flipped, so that
// mirror images of a frame need no tiles of their own.
static const u8 gem_spin[4] = {0, 1, 2, 1 | SPRITE_FRAME_FLIP_H};
static const u8 debris_tumble[4] = {0, SPRITE_FRAME_FLIP_H, SPRITE_FRAME_FLIP_V,
                                    SPRITE_FRAME_FLIP_H | SPRITE_FRAME_FLIP_V};

static const SpriteAsset sprites[SPR_SHARED_COUNT] = {
    [SPR_SERVAL_SMALL] = {.size = SPRITE_16x16,
                          .tiles = serval_small_tiles,
                          .frame_count = 6,
                          .origin_x = 2,
                          .origin_y = 1},
    [SPR_SERVAL_BIG] = {.size = SPRITE_16x32,
                        .tiles = serval_big_tiles,
                        .frame_count = 5,
                        .origin_x = 2,
                        .origin_y = 4},
    [SPR_GEM] = {.size = SPRITE_16x16,
                 .tiles = gem_tiles,
                 .frame_count = 3,
                 .frame_times = gem_spin_times,
                 .frame_order = gem_spin,
                 .order_length = 4,
                 .palette_slot = PAL_GEM},
    [SPR_FISH] = {.size = SPRITE_16x16,
                  .tiles = fish_tiles,
                  .palette_slot = PAL_FISH,
                  .origin_x = 2,
                  .origin_y = 3},
    [SPR_BANNER] = {.size = SPRITE_16x16, .tiles = banner_tiles},
    [SPR_SPARKLE] = {.size = SPRITE_8x8,
                     .tiles = sparkle_tiles,
                     .frame_count = 2,
                     .frame_times = sparkle_times,
                     .palette_slot = PAL_GEM},
    [SPR_HUD_GEM] = {.size = SPRITE_8x8, .tiles = hud_gem_tiles, .palette_slot = PAL_GEM},
    [SPR_HUD_SERVAL] = {.size = SPRITE_8x8, .tiles = hud_serval_tiles},
    [SPR_HUD_CLOCK] = {.size = SPRITE_8x8, .tiles = hud_clock_tiles},
    // In each stage's group, with its colors (palette slot PAL_STAGE_BLOCKS).
    [SPR_BLOCK] = {.size = SPRITE_16x16,
                   .tiles = block_tiles,
                   .frame_count = 3,
                   .palette_slot = PAL_STAGE_BLOCKS},
    [SPR_DEBRIS] = {.size = SPRITE_8x8,
                    .tiles = debris_tiles,
                    .frame_times = debris_times,
                    .frame_order = debris_tumble,
                    .order_length = 4,
                    .palette_slot = PAL_STAGE_BLOCKS},
};

// Every sprite ID: the shared sprites, then each stage's block of
// STAGE_SPRITES (art.h).
#define STAGE_SPRITE_IDS(stage, array)                                                             \
    [SPR_STAGE(stage) +                                                                            \
        0] = &(array)[0],                                                                          \
        [SPR_STAGE(stage) + 1] = &(array)[1], [SPR_STAGE(stage) + 2] = &(array)[2],                \
        [SPR_STAGE(stage) + 3] = &(array)[3], [SPR_STAGE(stage) + 4] = &(array)[4],                \
        [SPR_STAGE(stage) + 5] = &(array)[5], [SPR_STAGE(stage) + 6] = &(array)[6],                \
        [SPR_STAGE(stage) + 7] = &(array)[7], [SPR_STAGE(stage) + 8] = &(array)[8],                \
        [SPR_STAGE(stage) + 9] = &(array)[9], [SPR_STAGE(stage) + 10] = &(array)[10],              \
        [SPR_STAGE(stage) + 11] = &(array)[11], [SPR_STAGE(stage) + 12] = &(array)[12],            \
        [SPR_STAGE(stage) + 13] = &(array)[13], [SPR_STAGE(stage) + 14] = &(array)[14],            \
        [SPR_STAGE(stage) + 15] = &(array)[15]

const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_SERVAL_SMALL] = &sprites[SPR_SERVAL_SMALL],
    [SPR_SERVAL_BIG] = &sprites[SPR_SERVAL_BIG],
    [SPR_GEM] = &sprites[SPR_GEM],
    [SPR_FISH] = &sprites[SPR_FISH],
    [SPR_BANNER] = &sprites[SPR_BANNER],
    [SPR_SPARKLE] = &sprites[SPR_SPARKLE],
    [SPR_HUD_GEM] = &sprites[SPR_HUD_GEM],
    [SPR_HUD_SERVAL] = &sprites[SPR_HUD_SERVAL],
    [SPR_HUD_CLOCK] = &sprites[SPR_HUD_CLOCK],
    [SPR_BLOCK] = &sprites[SPR_BLOCK],
    [SPR_DEBRIS] = &sprites[SPR_DEBRIS],
    STAGE_SPRITE_IDS(STAGE_OVERWORLD, overworld_sprites),
    STAGE_SPRITE_IDS(STAGE_UNDERGROUND, underground_sprites),
};

// The sprites every stage uses, loaded once: IDs 0 to SPR_GLOBAL_COUNT - 1.
const SpriteGroup global_group = {
    .palettes = &sprite_palettes[0][0],
    .sprite_count = SPR_GLOBAL_COUNT,
    .palette_count = PALETTE_COUNT,
};

// --- Backgrounds --------------------------------------------------------------

// The bonus block's glint (tileset_set_tiles): its four tiles (a stage's
// bonus_tile to bonus_tile + 3) with a bright diagonal band (Y) sweeping
// across the gold, four steps, then the plain block again. The second step:
//   .KKKKKKKKKKKKKK.
//   KYYYYYYYYYYYYYOK
//   KYOOOOOOYYYYOOoK
//   KYOOOOOYYYYOOOoK
//   KYOOOOuuYuuOOOoK
//   KYOOOYuuYuuOOOoK
//   KYOOuuYYOOOuuOoK
//   KYOYuuYuuuOuuOoK
//   KYYYYYuuuuuOOOoK
//   KYYYYuuuuuuuOOoK
//   KYYYOuuuuuuuOOoK
//   KYYOOOuuuuuOOOoK
//   KYOOOOOOOOOOOOoK
//   KYOOOOOOOOOOOOoK
//   KOoooooooooooooK
//   .KKKKKKKKKKKKKK.
const u32 bonus_glint_tiles[BONUS_GLINT_FRAMES][BONUS_TILE_COUNT * 8] = {
    {
        0x11111110, 0x88888881, 0x77888881, 0x77788881, 0x99778881, 0x99777881, 0x77997781,
        0x97997781, 0x01111111, 0x17888888, 0x16777777, 0x16777777, 0x16777997, 0x16777997,
        0x16799777, 0x16799799, 0x99777781, 0x99977781, 0x99977781, 0x99777781, 0x77777781,
        0x77777781, 0x66666671, 0x11111110, 0x16777999, 0x16779999, 0x16779999, 0x16777999,
        0x16777777, 0x16777777, 0x16666666, 0x01111111,
    },
    {
        0x11111110, 0x88888881, 0x77777781, 0x87777781, 0x99777781, 0x99877781, 0x88997781,
        0x98998781, 0x01111111, 0x17888888, 0x16778888, 0x16777888, 0x16777998, 0x16777998,
        0x16799777, 0x16799799, 0x99888881, 0x99988881, 0x99978881, 0x99777881, 0x77777781,
        0x77777781, 0x66666671, 0x11111110, 0x16777999, 0x16779999, 0x16779999, 0x16777999,
        0x16777777, 0x16777777, 0x16666666, 0x01111111,
    },
    {
        0x11111110, 0x88888881, 0x77777781, 0x77777781, 0x99777781, 0x99777781, 0x77997781,
        0x97997781, 0x01111111, 0x17888888, 0x16777777, 0x16877777, 0x16887997, 0x16888997,
        0x16899877, 0x16799899, 0x99777781, 0x99977781, 0x99977781, 0x99877781, 0x88887781,
        0x78888781, 0x66666671, 0x11111110, 0x16778999, 0x16779999, 0x16779999, 0x16777999,
        0x16777777, 0x16777777, 0x16666666, 0x01111111,
    },
    {
        0x11111110, 0x88888881, 0x77777781, 0x77777781, 0x99777781, 0x99777781, 0x77997781,
        0x97997781, 0x01111111, 0x17888888, 0x16777777, 0x16777777, 0x16777997, 0x16777997,
        0x16799777, 0x16799799, 0x99777781, 0x99977781, 0x99977781, 0x99777781, 0x77777781,
        0x77777781, 0x66666671, 0x11111110, 0x16777999, 0x16879999, 0x16889999, 0x16888999,
        0x16888877, 0x16788887, 0x16666666, 0x01111111,
    },
    {
        0x11111110, 0x88888881, 0x77777781, 0x77777781, 0x99777781, 0x99777781, 0x77997781,
        0x97997781, 0x01111111, 0x17888888, 0x16777777, 0x16777777, 0x16777997, 0x16777997,
        0x16799777, 0x16799799, 0x99777781, 0x99977781, 0x99977781, 0x99777781, 0x77777781,
        0x77777781, 0x66666671, 0x11111110, 0x16777999, 0x16779999, 0x16779999, 0x16777999,
        0x16777777, 0x16777777, 0x16666666, 0x01111111,
    },
};
