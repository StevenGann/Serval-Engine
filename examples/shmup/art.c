// Graphics: sprites, background tiles, metatiles and the star and panel
// layers, all written as ASCII art and converted to tiles when the game boots
// (art_build), the way the platformer converts its level text. One character
// per pixel: '.' is color 0 (transparent), the hex digits 1-F colors 1-15 of
// the palette named above each picture. Frames of an animation lie side by
// side. Symmetric pictures marked "mirrored" give only their left half.
//
// The bigger pictures (boss, pods, gunship, explosions, orbs, items, rocks,
// crystals, the nebula) were drawn by a small script from circles and
// lighting, then pasted here; edit them like the rest.

#include "game.h"

// --- Palettes ----------------------------------------------------------------

// PAL_* are in game.h: enemies.c and boss.c draw with some of them.
static const u16 palettes[PALETTE_COUNT][16] = {
    // 1 outline, 2-4 hull, 5 white, 6-7 canopy, 8-9 ear fins, A-B flame,
    // C-D shot, E bomb, F needle
    [PAL_PLAYER] = {0, COLOR_RGB(16, 20, 44), COLOR_RGB(64, 84, 124), COLOR_RGB(124, 152, 196),
                    COLOR_RGB(196, 216, 240), COLOR_RGB(255, 255, 255), COLOR_RGB(0, 110, 150),
                    COLOR_RGB(90, 230, 255), COLOR_RGB(170, 70, 40), COLOR_RGB(240, 150, 70),
                    COLOR_RGB(255, 120, 30), COLOR_RGB(255, 240, 150), COLOR_RGB(255, 255, 210),
                    COLOR_RGB(110, 255, 190), COLOR_RGB(255, 90, 140), COLOR_RGB(150, 200, 255)},
    // 1 outline, 2-4 purple shell, 5-6 eye, 7-9 teal
    [PAL_ENEMY] = {0, COLOR_RGB(20, 12, 32), COLOR_RGB(76, 36, 104), COLOR_RGB(136, 70, 168),
                   COLOR_RGB(204, 136, 228), COLOR_RGB(255, 64, 64), COLOR_RGB(255, 200, 120),
                   COLOR_RGB(16, 104, 104), COLOR_RGB(40, 184, 164), COLOR_RGB(150, 255, 220)},
    [PAL_CARRIER] = {0, COLOR_RGB(32, 14, 8), COLOR_RGB(140, 60, 20), COLOR_RGB(220, 116, 32),
                     COLOR_RGB(255, 196, 90), COLOR_RGB(255, 255, 255), COLOR_RGB(255, 255, 160),
                     COLOR_RGB(150, 130, 20), COLOR_RGB(230, 210, 40), COLOR_RGB(255, 255, 160)},
    // 1 outline, 2-5 steel, 6-8 green carapace, 9 red, A yellow
    [PAL_HEAVY] = {0, COLOR_RGB(16, 16, 24), COLOR_RGB(56, 58, 76), COLOR_RGB(100, 104, 128),
                   COLOR_RGB(160, 166, 190), COLOR_RGB(226, 230, 244), COLOR_RGB(36, 84, 48),
                   COLOR_RGB(66, 146, 70), COLOR_RGB(140, 210, 110), COLOR_RGB(255, 60, 40),
                   COLOR_RGB(255, 220, 90)},
    // 1 outline, 2-5 shell, 6-8 metal, 9-C core, D lights
    [PAL_BOSS] = {0, COLOR_RGB(16, 10, 24), COLOR_RGB(60, 30, 70), COLOR_RGB(110, 52, 120),
                  COLOR_RGB(166, 92, 168), COLOR_RGB(226, 160, 220), COLOR_RGB(50, 58, 70),
                  COLOR_RGB(100, 114, 130), COLOR_RGB(170, 184, 200), COLOR_RGB(140, 20, 20),
                  COLOR_RGB(230, 72, 30), COLOR_RGB(255, 170, 50), COLOR_RGB(255, 250, 200),
                  COLOR_RGB(80, 240, 255)},
    [PAL_RAGE] = {0, COLOR_RGB(24, 6, 6), COLOR_RGB(90, 16, 16), COLOR_RGB(170, 36, 28),
                  COLOR_RGB(230, 90, 40), COLOR_RGB(255, 180, 120), COLOR_RGB(60, 40, 40),
                  COLOR_RGB(120, 90, 90), COLOR_RGB(200, 160, 150), COLOR_RGB(80, 20, 140),
                  COLOR_RGB(160, 60, 230), COLOR_RGB(220, 150, 255), COLOR_RGB(255, 255, 255),
                  COLOR_RGB(255, 255, 80)},
    // 1-5 fire, dark to white; 6-7 smoke; 8-A pink orb; B-D blue orb
    [PAL_FIRE] = {0, COLOR_RGB(120, 20, 10), COLOR_RGB(224, 56, 24), COLOR_RGB(255, 136, 32),
                  COLOR_RGB(255, 224, 88), COLOR_RGB(255, 255, 224), COLOR_RGB(64, 56, 60),
                  COLOR_RGB(120, 110, 112), COLOR_RGB(150, 0, 80), COLOR_RGB(255, 70, 170),
                  COLOR_RGB(255, 225, 245), COLOR_RGB(0, 60, 170), COLOR_RGB(70, 170, 255),
                  COLOR_RGB(225, 250, 255)},
    // 1 outline, 2-4 blue capsule, 5 letter, 6-8 pink capsule, 9 glow
    [PAL_ITEM] = {0, COLOR_RGB(16, 16, 40), COLOR_RGB(40, 90, 200), COLOR_RGB(80, 150, 255),
                  COLOR_RGB(180, 220, 255), COLOR_RGB(255, 255, 255), COLOR_RGB(160, 30, 80),
                  COLOR_RGB(240, 70, 130), COLOR_RGB(255, 170, 200), COLOR_RGB(255, 255, 120)},
    [PAL_FLASH] = {0, COLOR_RGB(200, 200, 255), COLOR_RGB(255, 255, 255), COLOR_RGB(255, 255, 255),
                   COLOR_RGB(255, 255, 255), COLOR_RGB(255, 255, 255), COLOR_RGB(255, 255, 255),
                   COLOR_RGB(255, 255, 255), COLOR_RGB(255, 255, 255), COLOR_RGB(255, 255, 255),
                   COLOR_RGB(255, 255, 255), COLOR_RGB(255, 255, 255), COLOR_RGB(255, 255, 255),
                   COLOR_RGB(255, 255, 255), COLOR_RGB(255, 255, 255), COLOR_RGB(255, 255, 255)},
};

// Background palette banks (bank 15 is the text layer's).
enum { BANK_STARS, BANK_TERRAIN, BANK_PANEL, BANK_COUNT };

static const u16 bg_palettes[BANK_COUNT][16] = {
    // 1-3 stars dim to bright, 4-6 nebula, 7 blue star, 8 warm star
    [BANK_STARS] = {0, COLOR_RGB(70, 70, 120), COLOR_RGB(150, 150, 200), COLOR_RGB(255, 255, 255),
                    COLOR_RGB(24, 14, 42), COLOR_RGB(38, 22, 64), COLOR_RGB(56, 32, 90),
                    COLOR_RGB(120, 180, 255), COLOR_RGB(255, 210, 150)},
    // Kept dark and muted, so bullets stand out over it. 1 outline, 2-4 rock,
    // 5-7 crystal, 8-A hull, B seams, C-D lamp off and on, E-F crater
    [BANK_TERRAIN] = {0, COLOR_RGB(12, 10, 20), COLOR_RGB(44, 36, 52), COLOR_RGB(70, 60, 78),
                      COLOR_RGB(100, 88, 106), COLOR_RGB(16, 60, 80), COLOR_RGB(34, 120, 140),
                      COLOR_RGB(110, 200, 215), COLOR_RGB(34, 40, 58), COLOR_RGB(52, 62, 84),
                      COLOR_RGB(78, 92, 118), COLOR_RGB(24, 28, 40), COLOR_RGB(80, 26, 30),
                      COLOR_RGB(255, 96, 64), COLOR_RGB(40, 22, 16), COLOR_RGB(200, 90, 30)},
    // 1 edge, 2 bevel, 3 body, 4 groove
    [BANK_PANEL] = {0, COLOR_RGB(8, 8, 16), COLOR_RGB(70, 80, 130), COLOR_RGB(28, 30, 56),
                    COLOR_RGB(42, 46, 82)},
};

// --- Sprites (hand-drawn) ------------------------------------------------------

// Frames side by side, one row of pixels per line:
// clang-format off

// The player's ship, the Caracal, facing up, with tufted fins like a
// caracal's ears. Frames: level (two flame lengths), banking right (two flame
// lengths; flipped for banking left). PAL_PLAYER.
static const char* const ship_art[] = {
    "......1441......" "......1441......" "......1441......" "......1441......",
    ".....134431....." ".....134431....." ".....134431....." ".....134431.....",
    ".....137731....." ".....137731....." ".....137731....." ".....137731.....",
    ".89..136631..98." ".89..136631..98." ".89..136621..9.." ".89..136621..9..",
    ".891.134431.198." ".891.134431.198." ".891.134321.19.." ".891.134321.19..",
    ".89311344311398." ".89311344311398." ".89311344211291." ".89311344211291.",
    "1893323443323981" "1893323443323981" "1894433443222911" "1894433443222911",
    "1933334444333391" "1933334444333391" "1944444443222291" "1944444443222291",
    "1332234554322331" "1332234554322331" "1443344553222221" "1443344553222221",
    "1322113443112231" "1322113443112231" "1433113442111221" "1433113442111221",
    "121..123321..121" "121..123321..121" "131..123221..121" "131..123221..121",
    "11...122221...11" "11...122221...11" "11...122221...11" "11...122221...11",
    "......1AA1......" "......1AA1......" "......1AA1......" "......1AA1......",
    "......ABBA......" ".......BB......." "......ABBA......" ".......BB.......",
    ".......AA......." "................" ".......AA......." "................",
    "................" "................" "................" "................",
};

// The player's shots: twin bolt, heavy twin bolt (full power), needle (the
// side shots). PAL_PLAYER.
static const char* const shot_art[] = {
    ".D....D." "DD....DD" "...55...",
    "DCD..DCD" "DCD..DCD" "...CF...",
    "DCD..DCD" "CCD..DCC" "...CF...",
    "DCD..DCD" "CCD..DCC" "...FF...",
    ".C....C." "DCD..DCD" "...F....",
    ".D....D." "DCD..DCD" "...F....",
    ".D....D." ".D....D." "........",
    "........" ".D....D." "........",
};

// The ship's hitbox (the 4 x 4 pixels inside the dark ring), shown while
// focusing. PAL_PLAYER.
static const char* const hitbox_art[] = {
    "........",
    ".111111.",
    ".1C55C1.",
    ".15EE51.",
    ".15EE51.",
    ".1C55C1.",
    ".111111.",
    "........",
};

// HUD icons: a ship and a bomb. PAL_PLAYER.
static const char* const icons_art[] = {
    "...44..." "....B...",
    "..1771.." "...B....",
    ".913319." "..1EE1..",
    "93344339" ".1E5EE1.",
    "13344331" ".1EEEE1.",
    "1.1221.1" ".1EEEE1.",
    "...AA..." "..1EE1..",
    "........" "........",
};

// The dart, a small fighter flying down (mirrored): wings back, wings
// raised. PAL_ENEMY; the carrier uses the same art with PAL_CARRIER.
static const char* const dart_art[] = {
    "1......." "........",
    "41......" "........",
    "341....." "1.......",
    "2341...1" "41.....1",
    "12341.13" "341...13",
    ".1234134" "2341.134",
    "..123134" "12341134",
    "...12134" ".1234134",
    "....1234" "..123234",
    ".....134" "...12134",
    ".....127" "....1127",
    ".....178" ".....178",
    ".....189" ".....189",
    "......15" "......15",
    "......16" "......16",
    ".......1" ".......1",
};

// Sparks: a bright cross fading to a red dot. PAL_FIRE.
static const char* const spark_art[] = {
    "...4...." "........" "........",
    "..454..." "...3...." "........",
    ".45554.." "..343..." "...2....",
    "..454..." "...3...." "........",
    "...4...." "........" "........",
    "........" "........" "........",
    "........" "........" "........",
    "........" "........" "........",
};

// --- Background tiles (hand-drawn) ---------------------------------------------

// Stars, one 8x8 tile each: dim, mid, bright cross, blue, warm, two dim.
// BANK_STARS.
static const char* const stars_art[] = {
    "........" "........" "........" "........" "........" "........",
    "........" "........" "........" "........" "........" ".....1..",
    "..1....." "........" "...3...." "........" "........" "........",
    "........" "....2..." "..232..." "........" ".....8.." "........",
    "........" "........" "...3...." "........" "........" "........",
    "........" "........" "........" "..7....." "........" "........",
    "........" "........" "........" "........" "........" "1.......",
    "........" "........" "........" "........" "........" "........",
};

// The station hull, 8x8 tiles: plating, plating with rivets, top edge, left
// edge, outer corner (top-left). Other edges and corners are these flipped
// (hull_pieces). BANK_TERRAIN.
static const char* const hull_art[] = {
    "AAAAAAAB" "AAAAAAAB" "11111111" "1A99998B" "..111111",
    "A999999B" "A9A99A9B" "AAAAAAAA" "1A99998B" ".1AAAAAA",
    "A999999B" "A999999B" "99999999" "1A99998B" "1AA99999",
    "A999999B" "A999999B" "99999999" "1A99998B" "1A999999",
    "A999999B" "A999999B" "99999999" "1A99998B" "1A999999",
    "A999999B" "A9A99A9B" "99999999" "1A99998B" "1A999999",
    "A999999B" "A999999B" "88888888" "1A99998B" "1A999988",
    "BBBBBBBB" "BBBBBBBB" "BBBBBBBB" "1A99998B" "1A9998BB",
};

// A quarter of the hull's signal lamp, off and on (the lamp is this tile in
// each corner of its metatile, flipped). stage_animate() swaps them.
// BANK_TERRAIN.
static const char* const lamp_art[] = {
    "AAAAAAAB" "AAAAAAAB",
    "A999999B" "A999999B",
    "A999999B" "A999999B",
    "A999999B" "A99999DD",
    "A99999BB" "A9999DDD",
    "A9999BCC" "A999DDDD",
    "A999BCCC" "A99DDDDD",
    "BBBBBCCC" "BBBDDDDD",
};

// A turret's pad on the hull, and the crater it leaves. BANK_TERRAIN.
static const char* const pad_art[] = {
    "AAAAAAAAAAAAAAAB" "AAAAAAAAAAAAAAAB",
    "A99999999999999B" "A99999999999999B",
    "A99991111119999B" "A99991111119999B",
    "A99118888881199B" "A99112EE2221199B",
    "A99188888888199B" "A9912EEEEEE2199B",
    "A91888888888819B" "A912EEFEEEEE219B",
    "A91888888888819B" "A912EEEEEFEE219B",
    "A91888888888819B" "A912EFEEEEEE219B",
    "A91888888888819B" "A912EEEEEEFE219B",
    "A91888888888819B" "A912EEFEEEEE219B",
    "A91888888888819B" "A912EEEEEEEE219B",
    "A99188888888199B" "A9912EEEFEE2199B",
    "A99118888881199B" "A99112222221199B",
    "A99991111119999B" "A99991111119999B",
    "A99999999999999B" "A99999999999999B",
    "BBBBBBBBBBBBBBBB" "BBBBBBBBBBBBBBBB",
};

// The HUD panel's tiles: its left edge, plain, and grooved. BANK_PANEL.
static const char* const panel_art[] = {
    "12233333" "33333333" "33334333",
    "12233333" "33333333" "33334333",
    "12233333" "33333333" "33334333",
    "12233333" "33333333" "33334333",
    "12233333" "33333333" "33334333",
    "12233333" "33333333" "33334333",
    "12233333" "33333333" "33334333",
    "12233333" "33333333" "33334333",
};

// clang-format on

// --- Sprites and tiles (generated) ---------------------------------------------
// The boss, the Hive Lantern: an armored lantern around a glowing core, a
// crown of spikes and two mandibles. PAL_BOSS (and PAL_RAGE, PAL_FLASH).
static const char* const boss_art[] = {
    "...............................161..............................",
    "...............................161..............................",
    "......................1.......17661.......1.....................",
    ".....................161......17661......161....................",
    ".....................161.....1776661.....161....................",
    "....................1766111111776661111117661...................",
    "....................1766555557776666443337661...................",
    "...................177666555577766664443776661..................",
    "..................1555555555555555444443333221..................",
    "................11555555555555555544444433332211................",
    "...............1555555555555555555544444433332221...............",
    "..............155555555555555555555444444433332221..............",
    "............11555555555555555555DD544444443333322211............",
    "...........155555555555555555555555444444443333322221...........",
    ".........1155555555555555555555555554444444433333322211.........",
    "........155555555555555555555555555544444444433333322221........",
    ".......15555555555555555555555555555444444444333333322221.......",
    ".....111111111111111111111111111111111111111111111111111111.....",
    "....15555555555555525555555555555555444444442443333333222221....",
    "....15555555555555525555555555555555444444442443333333222221....",
    "....15555555555555525555555555555555444444442443333333222221....",
    "....15555555555555525555555566666666444444442443333333222221....",
    "....155555555555555255DD5566CCCCCBBA66444DD42443333333222221....",
    "....1555555555555552555556CCCCCCCBBBAA6444442443333333222221....",
    "....155555555555555255556CCCCCCCCCBBAAA644442443333333222221....",
    "....15555555555555525556CCCCCCCCCBBBAAA964442443333333222221....",
    ".....1111111111111125556CCCCCCCCCBBBAAAA6444211111111111111.....",
    "....1555555555555552556CCCCCCCCCBBBBAAAA96442443333333222221....",
    "....1555555555555552556CCCCCCCCCBBBAAAAA96442443333333222221....",
    "....1555555555555552556CCCCCCCCBBBBAAAA996442443333333222221....",
    "....1555555555555552556BCCCCCBBBBBAAAAA996442443333333222221....",
    "....1555555555555552556BBBBBBBBBBAAAAA9996442443333333222221....",
    "....1555555555555552556BBBBBBBBAAAAAAA9996442443333333222221....",
    "....1555555555555552556ABBBBBBAAAAAAA99996442443333333222221....",
    "....1555555555555552556AAAAAAAAAAAAA999996442443333333222221....",
    "....15555555555555525556AAAAAAAAAAA9999964442443333333222221....",
    "....15555555555555525556AAAAAAAAA999999964442443333333222221....",
    "....1555555555555552555569AAAAA99999999644442443333333222221....",
    ".....111111111111112555556999999999999644444211111111111111.....",
    ".....155555555555552555555669999999966444444243333333222221.....",
    "......1155555555555255555555666666664444444424333333222211......",
    "........155555555552555555555555555544444444233333322221........",
    ".........1555555555255DD55555555555544444DD423333322221.........",
    "..........15555555525555555555555554444444432333322221..........",
    "...........115555552555555555555555444444443233322211...........",
    ".............15555525555555555555554444444332332221.............",
    "..............111111111111111111111111111111111111..............",
    "..............111555555555555555555444444333322111..............",
    ".............16666888855555555555544444433888866661.............",
    ".............16666888855555555555544444333888866661.............",
    ".............1666688885555555555DD44444333888866661.............",
    "..............166688885555555555554444333288886661..............",
    "..............166688881555555555554443332188886661..............",
    "...............1666888115555555554444332118886661...............",
    "...............16668881.1155555554443311.18886661...............",
    "................1666881...111111111111...1886661................",
    ".................166881..................188661.................",
    ".................1668881................1888661.................",
    "..................166881................188661..................",
    "...................16681................18661...................",
    "....................16881..............18861....................",
    ".....................1681..............1861.....................",
    "......................1681............1861......................",
    ".......................11..............11.......................",
};

// The boss's gun pods: a steel ball with a cyan lens and a barrel. PAL_BOSS.
static const char* const pod_art[] = {
    "................................", "................................",
    ".............111111.............", "..........111444333111..........",
    ".........14444443333331.........", "........1444444433333331........",
    ".......144444444333333331.......", "......14444444443333333331......",
    ".....1444444444433333333331.....", ".....1888888888888777777661.....",
    ".....1888888888887777777661.....", "....188888888888777777776661....",
    "....178888888887777777766661....", "....1788888888CCCD7777766661....",
    "....177888887CCCCDD777666661....", "....177777777CCCCDD777666661....",
    "....177777777CCCDDD776666661....", ".....17777777DDDDDD76666661.....",
    ".....177777777DDDD666666661.....", ".....1677777777776666666661.....",
    "......16777777766666666661......", ".......166666666666666661.......",
    "........1666677886666661........", ".........16667788666661.........",
    "..........111778866111..........", "............17788661............",
    "............17788661............", "............17788661............",
    "............17788661............", "............17788661............",
    "...........1666666661...........", "............11111111............",
};

// The gunship: a green armored carapace between two steel cannons, a red
// eye under it. PAL_HEAVY.
static const char* const gunship_art[] = {
    "...........1111.11111...........", ".........11888818777711.........",
    "........1888888188777771........", ".......188888881888777771.......",
    "......18888888818887777761......", ".....1888888888188877777761.....",
    ".....1888888888888777777761.....", "..1118888888888188777777766111..",
    ".144488888888881877777777662221.", ".144788888888881877777777666221.",
    ".144788888888881777777776666221.", ".144788888888881777777776666221.",
    ".144788888888877777777776666221.", ".144778888887771777777766666221.",
    ".144777777777771777777666666221.", ".144777777777771777776666666221.",
    ".144777777777771777776666666221.", ".14447777777777AA77666666662221.",
    ".1444777777777AAA96666666662221.", ".144427777777AAAA99666666642221.",
    ".1444267777779AA999666666642221.", ".144422669666699996666966442221.",
    ".144422266666639926666664442221.", ".144422216666633226666614442221.",
    ".144422211166633226661114442221.", ".14442221..1113322111..14442221.",
    ".14442221....133221....14442221.", ".14442221....133221....14442221.",
    "..111111.....133221.....111111..", ".............133221.............",
    ".............133221.............", "..............1111..............",
};

// An explosion: a white flash swelling to a fireball, then smoke. PAL_FIRE.
static const char* const boom_art[] = {
    ".......................................2...............1........................",
    "....................................2.222.22........111111................6.....",
    "....................................22333322.......11.12.1.1..........666.6.....",
    ".....................333333.......22333333332.2....1..222.2.1.1.......6.66...6..",
    "....................33444433.....2233444444332....1122..32221.1...6.66.66.66....",
    "......4444.........3344554433.....2344444444322...1223333.3.21...66...7.776.6...",
    ".....445544........3445555443....233444444443322.1122333333..111.66667..7.766...",
    ".....455554........3455555543...223344455444332..12.33.3333...11...667.77...6...",
    ".....455554........3455555543.....334445544433221.22.33.3333.21...6..7.....6.6..",
    ".....445544........3445555443....233444444443322.112233.33...1.1...6.7...7.6..6.",
    "......4444.........3344554433...22234444444432221.1.2.3.3332211...6..6....66....",
    "....................33444433.....2233444444332...1.12223322211........6666..6...",
    ".....................333333.......223333333322...111.2.2...111........6666.6....",
    "..................................2222333322.22......1..21..1.........6..6......",
    "....................................2222222........1...1111.........6..6........",
    "......................................22.2.............1........................",
};

// Enemy bullets: a pink orb and a blue orb. PAL_FIRE.
static const char* const bullet_art[] = {
    "................", "..8888....BBBB..", ".89A998..BCDCCB.", ".8AAA98..BDDDCB.",
    ".89A998..BCDCCB.", ".899998..BCCCCB.", "..8888....BBBB..", "................",
};

// The spinner, a four-bladed seed (drawn rotated). PAL_ENEMY.
static const char* const spinner_art[] = {
    "......11........", ".....1441.......", ".....14441......", ".....14441......",
    ".....14441......", ".....1999811111.", "..11199998833331", ".144499558733331",
    "144449955873331.", "14444888877111..", ".1111177771.....", "......13331.....",
    "......13331.....", "......13321.....", ".......1221.....", "........11......",
};

// The hull turret: a steel dome with a red eye, open and shut. PAL_HEAVY.
static const char* const turret_art[] = {
    "....11111111........11111111....", "...1333333331......1333333331...",
    "..133333333321....133333333321..", ".13333555433321..13333555433321.",
    "13335555554432211333555555443221", "13335555544432211333555554443221",
    "13355555544432211335555554443221", "1335555AA44432211335555544443221",
    "133455AAA94432211334555444443221", "133444AA994332211334441111133221",
    "13334499993322211333444444332221", "12334449933322211233444433332221",
    ".12222333322221..12222333322221.", "..122222222221....122222222221..",
    "...1222222221......1222222221...", "....11111111........11111111....",
};

// Power-ups: a blue P capsule and a pink B capsule, each plain and glowing.
// PAL_ITEM.
static const char* const item_art[] = {
    "......................9.9.9...........................9.9.9.....",
    ".....111111........9.1111119.........111111........9.1111119....",
    "....14444431......9.144444319.......18888871......9.188888719...",
    "...1444444331....9.14444443319.....1888888771....9.18888887719..",
    "..144444443331....1444444433319...188888887771....1888888877719.",
    ".14444455433321..144444554333219.18888555877761..188885558777619",
    ".14444544533321.914444544533321..18888588577761.918888588577761.",
    ".14444555333321..144445553333219.18888555777761..188885557777619",
    ".14444533333321.914444533333321..18888577577761.918888577577761.",
    ".13333533333221..133335333332219.17777555777661..177775557776619",
    ".13333333332221.913333333332221..17777777776661.917777777776661.",
    "..133333332221...9133333332221....177777776661...9177777776661..",
    "...1333322221.....91333322221.9....1777766661.....91777766661.9.",
    "....12222221.......912222221.9......16666661.......916666661.9..",
    ".....111111.........9111111.9........111111.........9111111.9...",
    ".....................9.9.9...........................9.9.9......",
};

// A small asteroid. BANK_TERRAIN.
static const char* const rock_small_art[] = {
    "................", "................", "......11111.....", ".....1442231....",
    "....144222431...", "...14442244331..", "..144442422231..", "..1444444222421.",
    "..1444443224421.", "..1344333244421.", "..133333333221..", "...13333322221..",
    "....122222221...", ".....1122211....", ".......111......", "................",
};

// A big asteroid (2 x 2 metatiles). BANK_TERRAIN.
static const char* const rock_big_art[] = {
    "................................", "................................",
    "................................", "................................",
    "............11111111............", "..........114444442211..........",
    ".........144444444444411........", "........14444444444443331.......",
    ".......1444444444444433331......", "......144444444444444333331.....",
    ".....1444444444444444333331.....", "....144444444444444443333321....",
    "....144444444444444433333321....", "....1244444444444444333333221...",
    "...12444444444444443333223221...", "...14444444444444433332224221...",
    "...14444444444444333332244221...", "...13444444444443333332444221...",
    "...13444444444322333333342221...", "....1344444433224433333222241...",
    "....133333333324443333222241....", "....133333333334433333222241....",
    ".....13333333333333322222221....", ".....1333333333333322222221.....",
    "......13333333333222222221......", ".......123223332222222221.......",
    "........1222422222222221........", ".........11442222222211.........",
    "...........1111221111...........", "...............11...............",
    "................................", "................................",
};

// Crystals growing on a rock. BANK_TERRAIN.
static const char* const crystal_art[] = {
    "................", "................", "................", ".......11.......",
    "......1761......", "......1761......", ".....17765111...", ".....177651761..",
    "...11777655761..", "..1767776577651.", "..1767776577651.", ".17765776577651.",
    ".17765776577651.", ".17765776577651.", ".17765776577651.", "..111111111111..",
};

// A patch of nebula for the starfield (2 x 2 metatiles). BANK_STARS.
static const char* const nebula_art[] = {
    "................................", "................................",
    "................................", "................................",
    "................................", "................................",
    "............4.4.4...............", ".........4.4444444.4............",
    "......4.4444555454444...........", "...4..4445555555554444..........",
    "....44445555555565544.4.........", "...444455555666666555544........",
    "..44445455666666666555544.......", ".4.44445566666666666555544......",
    "4.4454556566666666666655544.4...", "..44455556666666666666555544....",
    "..444455656666666666666555544...", "...445555666666666666666555544..",
    "..44545555566666666666666555544.", ".4.44445555556666666666666554444",
    "....445445555566666666666665544.", "...44444454556666666666666554444",
    "....4.4444445566666666666554444.", ".......4.444456666666666655444..",
    "..........4444566666666655544...", ".............445566666655544....",
    "..............4455666555544.4...", "...............44555554444......",
    ".................4444.4.........", "................................",
    "................................", "................................",
};

// --- Conversion ----------------------------------------------------------------

// Where each picture's tiles go in the sprite tile buffer (in tiles).
#define T_SHIP 0     // 4 frames of 16x16
#define T_SHOT 16    // 3 frames of 8x8
#define T_HITBOX 19  // 8x8
#define T_ICONS 20   // 2 of 8x8
#define T_DART 22    // 2 frames of 16x16
#define T_SPINNER 30 // 16x16
#define T_GUNSHIP 34 // 32x32
#define T_TURRET 50  // 2 frames of 16x16
#define T_BULLET 58  // 2 frames of 8x8
#define T_SPARK 60   // 3 frames of 8x8
#define T_BOOM 63    // 5 frames of 16x16
#define T_ITEMS 83   // 4 frames of 16x16: power (2), bomb (2)
#define T_BOSS 99    // 64x64
#define T_POD 163    // 32x32
#define SPRITE_TILES 179

// Background tiles, in tileset order.
#define BT_STARS 1     // 6 tiles: dim, mid, cross, blue, warm, two
#define BT_NEBULA 7    // 32x32: 16 tiles
#define BT_ROCK 23     // 16x16
#define BT_BIG_ROCK 27 // 32x32: 16 tiles
#define BT_CRYSTAL 43  // 16x16
#define BT_HULL 47     // plating, rivets, top edge, left edge, corner
#define BT_LAMP 52
#define BT_PAD 53    // 16x16
#define BT_CRATER 57 // 16x16
#define BT_PANEL 61  // edge, plain, grooved
#define BG_TILES 64
#define BT_LAMP_ON 64 // past the tileset: the lit lamp, copied over BT_LAMP to blink

// Converted at boot into EWRAM (about 8 KB): sprite_group_load and
// tileset_load copy tiles from anywhere.
static u32 sprite_tiles[SPRITE_TILES * 8] SERVAL_EWRAM_BSS;
static u32 bg_tiles[(BG_TILES + 1) * 8] SERVAL_EWRAM_BSS;

static u32 hex(char c) {
    if (c >= '1' && c <= '9')
        return (u32)(c - '0');
    if (c >= 'A' && c <= 'F')
        return (u32)(c - 'A' + 10);
    return 0; // '.' and anything else: transparent
}

// Converts `frames` frames, from frame `first` on, of a picture `w` x `h`
// pixels per frame into 4bpp tiles at `out`: frame by frame, tiles row by
// row within a frame (1D mapping). A mirrored picture's rows hold only the
// left half of each frame. `row_length` is what each row should measure (all
// frames); debug builds report a row that doesn't.
static void convert(u32* out, const char* const* rows, int w, int h, int first, int frames,
                    bool mirror, int row_length) {
#ifdef SERVAL_DEBUG
    for (int y = 0; y < h; y++) {
        int n = 0;
        while (rows[y][n])
            n++;
        if (n != row_length)
            debug_log(text_format("art: row %d is %d characters, not %d", y, n, row_length));
    }
#else
    (void)row_length;
#endif
    int src_w = mirror ? w / 2 : w;
    for (int f = first; f < first + frames; f++) {
        for (int ty = 0; ty < h / 8; ty++) {
            for (int tx = 0; tx < w / 8; tx++) {
                for (int y = 0; y < 8; y++) {
                    const char* row = rows[ty * 8 + y] + f * src_w;
                    u32 word = 0;
                    for (int x = 0; x < 8; x++) {
                        int px = tx * 8 + x;
                        if (px >= src_w)
                            px = w - 1 - px; // the mirrored half
                        word |= hex(row[px]) << (4 * x);
                    }
                    *out++ = word;
                }
            }
        }
    }
}

// --- Sprites -------------------------------------------------------------------

static const u8 dart_times[] = {6, 6};
static const u8 spark_times[] = {4, 4, 4};      // SPARK_FRAMES in all
static const u8 boom_times[] = {3, 4, 4, 4, 5}; // BOOM_FRAMES in all
static const u8 item_times[] = {10, 10};

// Sprites are drawn centered on their hitboxes (game.h: spawn() centers the
// hitbox on a point): origin = (sprite size - hitbox) / 2. The hitboxes, set
// where each thing is spawned, are much smaller than the art for the ship (4 x
// 4 in a 16 x 16 ship) and bullets (4 x 4 in 8 x 8), so near misses miss.
static const SpriteAsset sprites[SPRITE_COUNT] = {
    [SPR_SHIP] = {.size = SPRITE_16x16,
                  .tiles = sprite_tiles + T_SHIP * 8,
                  .frame_count = 4,
                  .palette_slot = PAL_PLAYER,
                  .origin_x = 6,
                  .origin_y = 6}, // hitbox 4 x 4
    [SPR_SHOT] = {.size = SPRITE_8x8,
                  .tiles = sprite_tiles + T_SHOT * 8,
                  .frame_count = 3,
                  .palette_slot = PAL_PLAYER,
                  .origin_x = 1}, // hitbox 6 x 8
    [SPR_HITBOX] = {.size = SPRITE_8x8,
                    .tiles = sprite_tiles + T_HITBOX * 8,
                    .palette_slot = PAL_PLAYER,
                    .origin_x = 2,
                    .origin_y = 2},
    [SPR_ICON_SHIP] = {.size = SPRITE_8x8,
                       .tiles = sprite_tiles + T_ICONS * 8,
                       .palette_slot = PAL_PLAYER},
    [SPR_ICON_BOMB] = {.size = SPRITE_8x8,
                       .tiles = sprite_tiles + (T_ICONS + 1) * 8,
                       .palette_slot = PAL_PLAYER},
    [SPR_DART] = {.size = SPRITE_16x16,
                  .tiles = sprite_tiles + T_DART * 8,
                  .frame_count = 2,
                  .frame_times = dart_times,
                  .palette_slot = PAL_ENEMY,
                  .origin_x = 2,
                  .origin_y = 3}, // hitbox 12 x 10
    [SPR_SPINNER] = {.size = SPRITE_16x16,
                     .tiles = sprite_tiles + T_SPINNER * 8,
                     .palette_slot = PAL_ENEMY,
                     .origin_x = 2,
                     .origin_y = 2}, // hitbox 12 x 12
    [SPR_GUNSHIP] = {.size = SPRITE_32x32,
                     .tiles = sprite_tiles + T_GUNSHIP * 8,
                     .palette_slot = PAL_HEAVY,
                     .origin_x = 3,
                     .origin_y = 4}, // hitbox 26 x 24
    [SPR_TURRET] = {.size = SPRITE_16x16,
                    .tiles = sprite_tiles + T_TURRET * 8,
                    .frame_count = 2,
                    .palette_slot = PAL_HEAVY,
                    .origin_x = 2,
                    .origin_y = 2}, // hitbox 12 x 12
    [SPR_BULLET] = {.size = SPRITE_8x8,
                    .tiles = sprite_tiles + T_BULLET * 8,
                    .frame_count = 2,
                    .palette_slot = PAL_FIRE,
                    .origin_x = 2,
                    .origin_y = 2}, // hitbox 4 x 4
    [SPR_SPARK] = {.size = SPRITE_8x8,
                   .tiles = sprite_tiles + T_SPARK * 8,
                   .frame_count = 3,
                   .frame_times = spark_times,
                   .flags = SPRITE_ASSET_ANIM_ONCE,
                   .palette_slot = PAL_FIRE,
                   .origin_x = 3,
                   .origin_y = 3}, // hitbox 2 x 2
    [SPR_BOOM] = {.size = SPRITE_16x16,
                  .tiles = sprite_tiles + T_BOOM * 8,
                  .frame_count = 5,
                  .frame_times = boom_times,
                  .flags = SPRITE_ASSET_ANIM_ONCE,
                  .palette_slot = PAL_FIRE,
                  .origin_x = 7,
                  .origin_y = 7}, // hitbox 2 x 2
    [SPR_ITEM_POWER] = {.size = SPRITE_16x16,
                        .tiles = sprite_tiles + T_ITEMS * 8,
                        .frame_count = 2,
                        .frame_times = item_times,
                        .palette_slot = PAL_ITEM,
                        .origin_x = 1,
                        .origin_y = 1}, // hitbox 14 x 14
    [SPR_ITEM_BOMB] = {.size = SPRITE_16x16,
                       .tiles = sprite_tiles + (T_ITEMS + 8) * 8,
                       .frame_count = 2,
                       .frame_times = item_times,
                       .palette_slot = PAL_ITEM,
                       .origin_x = 1,
                       .origin_y = 1},
    [SPR_BOSS] = {.size = SPRITE_64x64,
                  .tiles = sprite_tiles + T_BOSS * 8,
                  .palette_slot = PAL_BOSS,
                  .origin_x = 8,
                  .origin_y = 13}, // hitbox 48 x 36 around the core
    [SPR_POD] = {.size = SPRITE_32x32,
                 .tiles = sprite_tiles + T_POD * 8,
                 .palette_slot = PAL_BOSS,
                 .origin_x = 6,
                 .origin_y = 4}, // hitbox 20 x 20
};

const SpriteAsset* const sprite_table[SPRITE_COUNT] = {
    [SPR_SHIP] = &sprites[SPR_SHIP],
    [SPR_SHOT] = &sprites[SPR_SHOT],
    [SPR_HITBOX] = &sprites[SPR_HITBOX],
    [SPR_ICON_SHIP] = &sprites[SPR_ICON_SHIP],
    [SPR_ICON_BOMB] = &sprites[SPR_ICON_BOMB],
    [SPR_DART] = &sprites[SPR_DART],
    [SPR_SPINNER] = &sprites[SPR_SPINNER],
    [SPR_GUNSHIP] = &sprites[SPR_GUNSHIP],
    [SPR_TURRET] = &sprites[SPR_TURRET],
    [SPR_BULLET] = &sprites[SPR_BULLET],
    [SPR_SPARK] = &sprites[SPR_SPARK],
    [SPR_BOOM] = &sprites[SPR_BOOM],
    [SPR_ITEM_POWER] = &sprites[SPR_ITEM_POWER],
    [SPR_ITEM_BOMB] = &sprites[SPR_ITEM_BOMB],
    [SPR_BOSS] = &sprites[SPR_BOSS],
    [SPR_POD] = &sprites[SPR_POD],
};

// One group: 179 tiles. The white "hit" flash, the boss's red phase and the
// orange carrier are the same sprites drawn with another of the group's
// palettes (SPRITE_PALETTE), not copies of their tiles.
const SpriteGroup sprite_group = {
    .palettes = &palettes[0][0],
    .sprite_count = SPRITE_COUNT,
    .palette_count = PALETTE_COUNT,
};

// --- Backgrounds ---------------------------------------------------------------

const Tileset tileset = {
    .tiles = bg_tiles,
    .tile_count = BG_TILES,
    .palettes = &bg_palettes[0][0],
    .palette_count = BANK_COUNT,
};

#define SE(tile, flips) MAP_SE(tile, BANK_TERRAIN, flips)
#define H MAP_SE_FLIP_H
#define V MAP_SE_FLIP_V

// The stage map's metatiles (stage.c); the hull's are filled in by
// art_build() from its neighbors.
Metatile stage_metatiles[MT_COUNT] = {
    [MT_EMPTY] = {{0, 0, 0, 0}, MAP_EMPTY},
    [MT_ROCK] = {{SE(BT_ROCK, 0), SE(BT_ROCK + 1, 0), SE(BT_ROCK + 2, 0), SE(BT_ROCK + 3, 0)}, 0},
    [MT_BIG_ROCK] = {{SE(BT_BIG_ROCK, 0), SE(BT_BIG_ROCK + 1, 0), SE(BT_BIG_ROCK + 4, 0),
                      SE(BT_BIG_ROCK + 5, 0)},
                     0},
    [MT_BIG_ROCK + 1] = {{SE(BT_BIG_ROCK + 2, 0), SE(BT_BIG_ROCK + 3, 0), SE(BT_BIG_ROCK + 6, 0),
                          SE(BT_BIG_ROCK + 7, 0)},
                         0},
    [MT_BIG_ROCK + 2] = {{SE(BT_BIG_ROCK + 8, 0), SE(BT_BIG_ROCK + 9, 0), SE(BT_BIG_ROCK + 12, 0),
                          SE(BT_BIG_ROCK + 13, 0)},
                         0},
    [MT_BIG_ROCK + 3] = {{SE(BT_BIG_ROCK + 10, 0), SE(BT_BIG_ROCK + 11, 0), SE(BT_BIG_ROCK + 14, 0),
                          SE(BT_BIG_ROCK + 15, 0)},
                         0},
    [MT_CRYSTAL] = {{SE(BT_CRYSTAL, 0), SE(BT_CRYSTAL + 1, 0), SE(BT_CRYSTAL + 2, 0),
                     SE(BT_CRYSTAL + 3, 0)},
                    0},
    [MT_PAD] = {{SE(BT_PAD, 0), SE(BT_PAD + 1, 0), SE(BT_PAD + 2, 0), SE(BT_PAD + 3, 0)}, 0},
    [MT_CRATER] = {{SE(BT_CRATER, 0), SE(BT_CRATER + 1, 0), SE(BT_CRATER + 2, 0),
                    SE(BT_CRATER + 3, 0)},
                   0},
    [MT_HULL_LIGHT] = {{SE(BT_LAMP, 0), SE(BT_LAMP, H), SE(BT_LAMP, V), SE(BT_LAMP, H | V)}, 0},
};

// One corner of a hull metatile: plating where the hull goes on both ways,
// an edge where it stops on one side, an outer corner where it stops on
// both. The top edge, left edge and top-left corner are flipped for the
// other sides (`flips`: H for the right half, V for the bottom half).
static u16 hull_corner(bool vertical, bool horizontal, u16 flips, u16 plating) {
    if (vertical && horizontal)
        return SE(plating, 0);
    if (horizontal)
        return SE(BT_HULL + 2, flips & V); // top (or bottom) edge
    if (vertical)
        return SE(BT_HULL + 3, flips & H); // left (or right) edge
    return SE(BT_HULL + 4, flips);         // corner
}

#define STAR(n) MAP_SE(BT_STARS + (n), BANK_STARS, 0)
#define NEB(n) MAP_SE(BT_NEBULA + (n), BANK_STARS, 0)

static const Metatile star_metatiles[] = {
    {{0, 0, 0, 0}, 0},
    {{STAR(0), 0, 0, 0}, 0},       // 1: dim
    {{0, 0, 0, STAR(1)}, 0},       // 2: mid
    {{0, STAR(2), 0, 0}, 0},       // 3: bright cross
    {{0, 0, STAR(3), 0}, 0},       // 4: blue
    {{STAR(4), 0, 0, 0}, 0},       // 5: warm
    {{0, STAR(5), STAR(0), 0}, 0}, // 6: three dim
    // 7-10: the nebula, 2 x 2 metatiles (a b / c d in the map below)
    {{NEB(0), NEB(1), NEB(4), NEB(5)}, 0},
    {{NEB(2), NEB(3), NEB(6), NEB(7)}, 0},
    {{NEB(8), NEB(9), NEB(12), NEB(13)}, 0},
    {{NEB(10), NEB(11), NEB(14), NEB(15)}, 0},
};

// The starfield, 16 x 16 metatiles (256 pixels square), repeating: digits
// are stars (star_metatiles), "ab/cd" a nebula.
static const char star_map[16][16 + 1] = {
    "1...4.....2...6.", "......3.........", "..2.......ab..1.", "5.......1.cd....",
    "...6..........3.", ".1.....5........", "........2...4...", "..3.ab..........",
    "....cd.....6...1", "1.........1.....", "......2.......5.", "..4.........3...",
    "............ab..", ".6.....1....cd..", "....5.....2.....", "..1........4..6.",
};
static u16 star_cells[16 * 16] SERVAL_EWRAM_BSS;

// MAP_LAYER_WRAP repeats it forever, and a scroll factor of a half makes it
// move at half the camera's speed: far away. Once the camera stops at the top
// of the stage, the game scrolls it on by itself (map_set_scroll, game.c).
const MapLayer stars_layer = {
    .width = 16,
    .height = 16,
    .cells = star_cells,
    .metatiles = star_metatiles,
    .metatile_count = sizeof star_metatiles / sizeof star_metatiles[0],
    .bg = 3,
    .flags = MAP_LAYER_WRAP,
    .scroll_factor = FX_ONE / 2,
};

#define PANEL(n) MAP_SE(BT_PANEL + (n), BANK_PANEL, 0)
static const Metatile panel_metatiles[] = {
    {{0, 0, 0, 0}, 0},
    {{PANEL(0), PANEL(1), PANEL(0), PANEL(1)}, 0}, // left edge
    {{PANEL(1), PANEL(1), PANEL(1), PANEL(1)}, 0}, // plain
    {{PANEL(1), PANEL(2), PANEL(1), PANEL(2)}, 0}, // grooved
};
static const u16 panel_cells[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 2, 2};

// The HUD panel on background 1, in front of sprites, so anything passing
// the field's right edge goes under it. MAP_LAYER_FIXED keeps it still while
// the camera climbs the stage; it is one metatile row, repeated down the
// screen (MAP_LAYER_WRAP).
const MapLayer panel_layer = {
    .width = 16,
    .height = 1,
    .cells = panel_cells,
    .metatiles = panel_metatiles,
    .metatile_count = sizeof panel_metatiles / sizeof panel_metatiles[0],
    .bg = 1,
    .flags = MAP_LAYER_FIXED | MAP_LAYER_WRAP,
};

void art_light_frame(u32 frame, const u32** tiles, u16* first) {
    *tiles = bg_tiles + (frame ? BT_LAMP_ON : BT_LAMP) * 8;
    *first = BT_LAMP;
}

void art_build(void) {
    u32* s = sprite_tiles;
    convert(s + T_SHIP * 8, ship_art, 16, 16, 0, 4, false, 64);
    convert(s + T_SHOT * 8, shot_art, 8, 8, 0, 3, false, 24);
    convert(s + T_HITBOX * 8, hitbox_art, 8, 8, 0, 1, false, 8);
    convert(s + T_ICONS * 8, icons_art, 8, 8, 0, 2, false, 16);
    convert(s + T_DART * 8, dart_art, 16, 16, 0, 2, true, 16);
    convert(s + T_SPINNER * 8, spinner_art, 16, 16, 0, 1, false, 16);
    convert(s + T_GUNSHIP * 8, gunship_art, 32, 32, 0, 1, false, 32);
    convert(s + T_TURRET * 8, turret_art, 16, 16, 0, 2, false, 32);
    convert(s + T_BULLET * 8, bullet_art, 8, 8, 0, 2, false, 16);
    convert(s + T_SPARK * 8, spark_art, 8, 8, 0, 3, false, 24);
    convert(s + T_BOOM * 8, boom_art, 16, 16, 0, 5, false, 80);
    convert(s + T_ITEMS * 8, item_art, 16, 16, 0, 4, false, 64);
    convert(s + T_BOSS * 8, boss_art, 64, 64, 0, 1, false, 64);
    convert(s + T_POD * 8, pod_art, 32, 32, 0, 1, false, 32);

    u32* b = bg_tiles; // tile 0 stays blank
    convert(b + BT_STARS * 8, stars_art, 8, 8, 0, 6, false, 48);
    convert(b + BT_NEBULA * 8, nebula_art, 32, 32, 0, 1, false, 32);
    convert(b + BT_ROCK * 8, rock_small_art, 16, 16, 0, 1, false, 16);
    convert(b + BT_BIG_ROCK * 8, rock_big_art, 32, 32, 0, 1, false, 32);
    convert(b + BT_CRYSTAL * 8, crystal_art, 16, 16, 0, 1, false, 16);
    convert(b + BT_HULL * 8, hull_art, 8, 8, 0, 5, false, 40);
    convert(b + BT_LAMP * 8, lamp_art, 8, 8, 0, 1, false, 16);
    convert(b + BT_LAMP_ON * 8, lamp_art, 8, 8, 1, 1, false, 16);
    convert(b + BT_PAD * 8, pad_art, 16, 16, 0, 2, false, 32);
    convert(b + BT_PANEL * 8, panel_art, 8, 8, 0, 3, false, 24);

    // The 16 hull pieces, one per combination of neighbors.
    for (u32 n = 0; n < 16; n++) {
        bool up = n & HULL_UP, down = n & HULL_DOWN, left = n & HULL_LEFT, right = n & HULL_RIGHT;
        Metatile* m = &stage_metatiles[MT_HULL + n];
        m->se[0] = hull_corner(up, left, 0, BT_HULL + 1); // rivets in two corners
        m->se[1] = hull_corner(up, right, H, BT_HULL);
        m->se[2] = hull_corner(down, left, V, BT_HULL);
        m->se[3] = hull_corner(down, right, H | V, BT_HULL + 1);
        m->collision = 0;
    }

    // The starfield's text to cells.
    for (u32 y = 0; y < 16; y++) {
        for (u32 x = 0; x < 16; x++) {
            char c = star_map[y][x];
            u16 cell = 0;
            if (c >= '1' && c <= '6')
                cell = (u16)(c - '0');
            else if (c >= 'a' && c <= 'd')
                cell = (u16)(7 + c - 'a');
            star_cells[y * 16 + x] = cell;
        }
    }
}
