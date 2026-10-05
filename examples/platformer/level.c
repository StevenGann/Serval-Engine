// The level: a first level in the classic side-scroller style, written as
// text, one screen (16 x 13 metatiles) at a time, and converted into map cells
// when the game boots. Also what blocks do when they are hit.

#include "game.h"

// Legend (one character per 16x16 metatile):
//   .  sky                 #  ground (grass on top)   X  stone block
//   B  bricks              G  bricks hiding gems      P  bonus block (a gem)
//   F  bonus block (fish)  o  gem in the air          T  tree stump (2 wide)
//   ^  goal pole's knob    |  goal pole               D  the den (4 x 3)
//   b  bush (2 wide)       w  tall grass (2 wide, in front of sprites)
//   f  flowers (2 wide, in front of sprites)
//   s  the serval's start  c  checkpoint (restart here after losing a life)
//   e  beetle              r  frog
// Row 11 is the ground's surface; the serval stands on it in row 10.
#define SCREENS (LEVEL_W / 16)
static const char level_text[SCREENS][LEVEL_H][16 + 1] = {
    // Screen 0: columns 0-15
    {
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "...s....bb..ww..",
        "################",
        "################",
    },
    // Screen 1: columns 16-31
    {
        "................",
        "................",
        "................",
        ".......P........",
        "................",
        "................",
        "................",
        ".P...BFBP.......",
        "................",
        "................",
        "...ff......e..bb",
        "################",
        "################",
    },
    // Screen 2: columns 32-47
    {
        "................",
        "................",
        "................",
        "................",
        ".........oo.....",
        "................",
        "................",
        "................",
        "...........TT...",
        "..TT.......TT...",
        "..TT...e...TT..w",
        "################",
        "################",
    },
    // Screen 3: columns 48-63
    {
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "......ooo.......",
        "...TT........TT.",
        "...TT........TT.",
        "...TT........TT.",
        "w..TT..e.e...TT.",
        "################",
        "################",
    },
    // Screen 4: columns 64-79
    {
        "................",
        "................",
        "................",
        "................",
        "..............BB",
        "................",
        "................",
        "...........BFB..",
        "................",
        "................",
        "bb..............",
        "#####..#########",
        "#####..#########",
    },
    // Screen 5: columns 80-95
    {
        "................",
        "................",
        "................",
        "e..e............",
        "BBBBBB....BBPB..",
        "................",
        "................",
        "...............G",
        "................",
        "................",
        "................",
        "######...#######",
        "######...#######",
    },
    // Screen 6: columns 96-111
    {
        "................",
        "................",
        "................",
        "...........F....",
        "................",
        "................",
        "................",
        ".....BB.P..P..P.",
        "................",
        "................",
        "..cff..r.....r..",
        "################",
        "################",
    },
    // Screen 7: columns 112-127
    {
        "................",
        "................",
        "................",
        "................",
        "........BBB....B",
        "................",
        "................",
        ".....B..........",
        "................",
        "................",
        ".bb.e.e.....e.e.",
        "################",
        "################",
    },
    // Screen 8: columns 128-143
    {
        "................",
        "................",
        "................",
        "................",
        "PPB.............",
        "................",
        "................",
        "BB.......XXXX...",
        "........XXXXXX..",
        ".......XXXXXXXX.",
        "..r...XXXXXXXXXX",
        "################",
        "################",
    },
    // Screen 9: columns 144-159
    {
        "................",
        "................",
        "................",
        "................",
        "oo..............",
        "................",
        ".........oo.....",
        ".......XX..X....",
        "......XXX..XX...",
        ".....XXXX..XXX..",
        "..wwXXXXX..XXXX.",
        "#########..#####",
        "#########..#####",
    },
    // Screen 10: columns 160-175
    {
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "........BBPB....",
        "................",
        "....TT..........",
        "ff..TT......e.e.",
        "################",
        "################",
    },
    // Screen 11: columns 176-191
    {
        "................",
        "................",
        "................",
        ".............XX.",
        "............XXX.",
        "...........XXXX.",
        "..........XXXXX.",
        ".........XXXXXX.",
        "........XXXXXXX.",
        "..TT...XXXXXXXX.",
        "..TT..XXXXXXXXX.",
        "################",
        "################",
    },
    // Screen 12: columns 192-207
    {
        "................",
        ".......^........",
        ".......|........",
        ".......|........",
        ".......|........",
        ".......|........",
        ".......|........",
        ".......|........",
        ".......|...DDDD.",
        ".......|...DDDD.",
        "bb.....X...DDDD.",
        "################",
        "################",
    },
    // Screen 13: columns 208-223
    {
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        ".ww.bb..ff.bb...",
        "################",
        "################",
    },
};

// The cells of the playfield (background 2) and the foreground (background 1),
// built from the text at boot. In RAM, since they are built at runtime: a
// level made in an editor would be const data in ROM instead.
static u16 playfield_cells[LEVEL_W * LEVEL_H] SERVAL_EWRAM_BSS;
static u16 foreground_cells[LEVEL_W * LEVEL_H] SERVAL_EWRAM_BSS;

static const MapLayer playfield = {
    .width = LEVEL_W,
    .height = LEVEL_H,
    .cells = playfield_cells,
    .metatiles = metatiles,
    .metatile_count = MT_COUNT,
    .bg = 2,
};

// Tall grass and flowers in front of the sprites: the serval runs behind them.
static const MapLayer foreground = {
    .width = LEVEL_W,
    .height = LEVEL_H,
    .cells = foreground_cells,
    .metatiles = metatiles,
    .metatile_count = MT_COUNT,
    .bg = 1,
};

Spawn spawns[MAX_SPAWNS];
int spawn_count;
int start_mx, start_my;
int checkpoint_mx, checkpoint_my;
int pole_mx, pole_top_my, pole_base_my;
int den_door_x;

static char text_at(int mx, int my) {
    if (mx < 0 || mx >= LEVEL_W || my < 0 || my >= LEVEL_H)
        return '.';
    return level_text[mx / 16][my][mx % 16];
}

// How many cells left of (mx, my) hold the same character: for pieces wider
// than a metatile, whether this is their left or right half (or which column).
static int run_left(int mx, int my) {
    char c = text_at(mx, my);
    int n = 0;
    while (n < mx && text_at(mx - n - 1, my) == c)
        n++;
    return n;
}

static int run_up(int mx, int my) {
    char c = text_at(mx, my);
    int n = 0;
    while (n < my && text_at(mx, my - n - 1) == c)
        n++;
    return n;
}

static void add_spawn(SpawnKind kind, int mx, int my) {
    if (spawn_count == MAX_SPAWNS)
        return;
    spawns[spawn_count++] = (Spawn){.kind = (u8)kind, .mx = (u8)mx, .my = (u8)my};
}

void level_build(void) {
    spawn_count = 0;
    // Column by column, so the spawn list comes out sorted from left to right.
    for (int mx = 0; mx < LEVEL_W; mx++) {
        for (int my = 0; my < LEVEL_H; my++) {
            char c = text_at(mx, my);
            u16 cell = MT_EMPTY, front = MT_EMPTY;
            int half = run_left(mx, my) % 2;
            switch (c) {
            case '#':
                cell = text_at(mx, my - 1) == '#' ? MT_GROUND : MT_GROUND_TOP;
                break;
            case 'X':
                cell = MT_STONE;
                break;
            case 'B':
                cell = MT_BRICK;
                break;
            case 'G':
                cell = MT_GEM_BRICK;
                break;
            case 'P':
                cell = MT_BONUS;
                break;
            case 'F':
                cell = MT_BONUS_FISH;
                break;
            case 'o':
                cell = MT_GEM;
                break;
            case 'T':
                if (text_at(mx, my - 1) != 'T')
                    cell = MT_STUMP_TOP;
                else if (text_at(mx, my + 1) != 'T')
                    cell = MT_STUMP_BASE;
                else
                    cell = MT_STUMP;
                cell = (u16)(cell + half);
                break;
            case '^':
                cell = MT_POLE_TOP;
                pole_mx = mx;
                pole_top_my = my;
                break;
            case '|':
                cell = MT_POLE;
                if (text_at(mx, my + 1) != '|')
                    pole_base_my = my + 1;
                break;
            case 'D':
                cell = (u16)(MT_DEN + run_up(mx, my) * 4 + run_left(mx, my));
                if (run_left(mx, my) == 0 && run_up(mx, my) == 0)
                    den_door_x = (mx + 2) * TILE;
                break;
            case 'b':
                cell = (u16)(MT_BUSH + half);
                break;
            case 'w':
                front = (u16)(MT_TALL_GRASS + half);
                break;
            case 'f':
                front = (u16)(MT_FLOWERS + half);
                break;
            case 's':
                start_mx = mx;
                start_my = my;
                break;
            case 'c':
                checkpoint_mx = mx;
                checkpoint_my = my;
                break;
            case 'e':
                add_spawn(SPAWN_BEETLE, mx, my);
                break;
            case 'r':
                add_spawn(SPAWN_FROG, mx, my);
                break;
            default:
                break;
            }
            playfield_cells[my * LEVEL_W + mx] = cell;
            foreground_cells[my * LEVEL_W + mx] = front;
        }
    }
}

// Bricks hiding gems give one per hit, up to GEM_BRICK_GEMS, then are used.
#define GEM_BRICK_GEMS 6
#define MAX_GEM_BRICKS 4
typedef struct {
    u8 mx, my, gems; // where, and how many it gave
} GemBrick;
static GemBrick gem_bricks[MAX_GEM_BRICKS];
static int gem_brick_count;

void level_show(void) {
    gem_brick_count = 0;
    map_load(&far_layer);
    map_load(&playfield); // also forgets earlier map_set_cell changes
    map_load(&foreground);
}

// --- Blocks ------------------------------------------------------------------

static int gem_brick_hit(int mx, int my) {
    for (int k = 0; k < gem_brick_count; k++) {
        if (gem_bricks[k].mx == mx && gem_bricks[k].my == my)
            return ++gem_bricks[k].gems;
    }
    if (gem_brick_count < MAX_GEM_BRICKS) {
        gem_bricks[gem_brick_count++] = (GemBrick){.mx = (u8)mx, .my = (u8)my, .gems = 1};
    }
    return 1;
}

void level_hit_block(int mx, int my, bool big) {
    u8 collision = metatiles[map_cell(mx, my)].collision;
    if (collision & TAG_BONUS) {
        knock_enemies_on(mx, my);
        if (collision & TAG_POWER) {
            spawn_bump(mx, my, BLOCK_FRAME_USED, MT_USED);
            spawn_fish(mx, my);
            psg_play(SND_POWER_APPEARS);
        } else if (collision & TAG_BRICK) {
            bool last = gem_brick_hit(mx, my) >= GEM_BRICK_GEMS;
            spawn_bump(mx, my, last ? BLOCK_FRAME_USED : BLOCK_FRAME_BRICK,
                       last ? MT_USED : MT_GEM_BRICK);
            spawn_gem_pop(mx, my);
        } else {
            spawn_bump(mx, my, BLOCK_FRAME_USED, MT_USED);
            spawn_gem_pop(mx, my);
        }
    } else if (collision & TAG_BRICK) {
        knock_enemies_on(mx, my);
        if (big) {
            map_set_cell(mx, my, MT_EMPTY);
            spawn_debris(mx, my);
            add_score(50);
            psg_play(SND_BREAK);
        } else {
            spawn_bump(mx, my, BLOCK_FRAME_BRICK, MT_BRICK);
            psg_play(SND_BUMP);
        }
    } else {
        psg_play(SND_BUMP);
    }
}

void level_bump_done(int mx, int my, u16 metatile) {
    map_set_cell(mx, my, metatile);
}

void level_collect_gems(int x, int y, int w, int h) {
    for (int my = y >> 4; my <= (y + h - 1) >> 4; my++) {
        for (int mx = x >> 4; mx <= (x + w - 1) >> 4; mx++) {
            if (metatiles[map_cell(mx, my)].collision & TAG_GEM) {
                map_set_cell(mx, my, MT_EMPTY);
                spawn_sparkle(mx * TILE + 4, my * TILE + 4);
                add_gem();
            }
        }
    }
}
