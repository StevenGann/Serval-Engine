// The level of the stage being played: built from the stage's level text
// into map cells in RAM when the stage starts, then shown; and what blocks,
// gems and hazards do.

#include "game.h"

// Legend (one character per 16x16 metatile), the same in every stage; each
// stage's art draws these its own way, and its cell hook adds characters of
// its own (stage_<name>.c):
//   .  empty (sky, dark)   #  ground (its top row the surface)
//   X  stone block         B  bricks              G  bricks hiding gems
//   P  bonus block (a gem) F  bonus block (fish)  o  gem in the air
//   -  one-way platform (2 halves, alternating)
//   ^  goal pole's knob    |  goal pole
//   D  the exit: the den, or the stage's own (4 x 3)
//   s  the serval's start  c  checkpoint (restart here after losing a life)
//   e  walker (a beetle)   r  hopper (a frog)
// The serval stands on the ground's surface in the row above it.

int level_w, level_h, level_pixel_w, level_pixel_h;
Spawn spawns[MAX_SPAWNS] SERVAL_EWRAM_BSS;
int spawn_count;
int start_mx, start_my;
int checkpoint_mx, checkpoint_my;
int pole_mx, pole_top_my, pole_base_my;
int exit_x, den_door_x;

// The cells of the playfield (background 2) and the foreground (background 1),
// built from the stage's text when it starts: one buffer each, sized for the
// largest stage. In RAM, since they are built at runtime: a level made in an
// editor would be const data in ROM instead. map_load() keeps reading them
// while the layers are shown, so they are rebuilt only with the screen black.
static u16 playfield_cells[LEVEL_MAX_CELLS] SERVAL_EWRAM_BSS;
static u16 foreground_cells[LEVEL_MAX_CELLS] SERVAL_EWRAM_BSS;
static bool has_foreground; // anything in front of the sprites

// The layers point at the cells and at the stage's metatiles; map_load()
// keeps the pointer, so they are static, filled in by level_build(). (In
// EWRAM, like the cells: IWRAM is for what runs every frame.)
static MapLayer playfield SERVAL_EWRAM_BSS;
// Tall grass, flowers and the like in front of the sprites: the serval
// runs behind them.
static MapLayer foreground SERVAL_EWRAM_BSS;

char level_text_at(int mx, int my) {
    if (mx < 0 || mx >= level_w || my < 0 || my >= level_h)
        return '.';
    return stage->text[(mx / 16) * level_h + my][mx % 16];
}

int level_run_left(int mx, int my) {
    char c = level_text_at(mx, my);
    int n = 0;
    while (n < mx && level_text_at(mx - n - 1, my) == c)
        n++;
    return n;
}

int level_run_up(int mx, int my) {
    char c = level_text_at(mx, my);
    int n = 0;
    while (n < my && level_text_at(mx, my - n - 1) == c)
        n++;
    return n;
}

void level_add_spawn(int kind, int mx, int my) {
    if (spawn_count == MAX_SPAWNS)
        return;
    spawns[spawn_count++] = (Spawn){.kind = (u8)kind, .mx = (u16)mx, .my = (u8)my};
}

void level_build(void) {
    level_w = stage->screens * 16;
    level_h = stage->height;
    if (level_w * level_h > LEVEL_MAX_CELLS)
        level_w = LEVEL_MAX_CELLS / level_h; // (a stage too big for the buffers)
    level_pixel_w = level_w * TILE;
    level_pixel_h = level_h * TILE;
    playfield.cells = playfield_cells;
    foreground.cells = foreground_cells;
    playfield.bg = 2;
    foreground.bg = 1;
    playfield.width = foreground.width = (u16)level_w;
    playfield.height = foreground.height = (u16)level_h;
    playfield.metatiles = foreground.metatiles = stage->metatiles;
    playfield.metatile_count = foreground.metatile_count = stage->metatile_count;
    spawn_count = 0;
    pole_mx = -1;
    exit_x = den_door_x = level_pixel_w + SCREEN_W; // none: never reached
    has_foreground = false;
    // Column by column, so the spawn list comes out sorted from left to right.
    for (int mx = 0; mx < level_w; mx++) {
        for (int my = 0; my < level_h; my++) {
            char c = level_text_at(mx, my);
            u16 cell = MT_EMPTY, front = MT_EMPTY;
            switch (c) {
            case '.':
                break;
            case '#':
                cell = level_text_at(mx, my - 1) == '#' ? MT_GROUND : MT_GROUND_TOP;
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
            case '-':
                cell = (u16)(MT_ONEWAY + level_run_left(mx, my) % 2);
                break;
            case '^':
                cell = MT_POLE_TOP;
                pole_mx = mx;
                pole_top_my = my;
                break;
            case '|':
                cell = MT_POLE;
                if (level_text_at(mx, my + 1) != '|')
                    pole_base_my = my + 1;
                break;
            case 'D':
                cell = (u16)(MT_EXIT + level_run_up(mx, my) * 4 + level_run_left(mx, my));
                if (level_run_left(mx, my) == 0 && level_run_up(mx, my) == 0) {
                    exit_x = mx * TILE;
                    den_door_x = (mx + 2) * TILE;
                }
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
                level_add_spawn(SPAWN_WALKER, mx, my);
                break;
            case 'r':
                level_add_spawn(SPAWN_HOPPER, mx, my);
                break;
            default:
                if (stage->cell)
                    cell = stage->cell(c, mx, my, &front);
                break;
            }
            playfield_cells[my * level_w + mx] = cell;
            foreground_cells[my * level_w + mx] = front;
            has_foreground |= front != MT_EMPTY;
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
    if (stage->far_layer)
        map_load(stage->far_layer);
    else
        map_unload(3);
    map_load(&playfield); // also forgets earlier map_set_cell changes
    if (has_foreground)
        map_load(&foreground);
    else
        map_unload(1);
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
    u8 tags = map_tags_in(mx * TILE, my * TILE, 1, 1);
    if (tags & TAG_BONUS) {
        knock_enemies_on(mx, my);
        if (map_cell(mx, my) == MT_BONUS_FISH) { // told apart by its number
            spawn_bump(mx, my, BLOCK_FRAME_USED, MT_USED);
            spawn_fish(mx, my);
            psg_play(SND_POWER_APPEARS);
        } else if (tags & TAG_BRICK) {
            bool last = gem_brick_hit(mx, my) >= GEM_BRICK_GEMS;
            spawn_bump(mx, my, last ? BLOCK_FRAME_USED : BLOCK_FRAME_BRICK,
                       last ? MT_USED : MT_GEM_BRICK);
            spawn_gem_pop(mx, my);
        } else {
            spawn_bump(mx, my, BLOCK_FRAME_USED, MT_USED);
            spawn_gem_pop(mx, my);
        }
    } else if (tags & TAG_BRICK) {
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
    if (!(map_tags_in(x, y, w, h) & TAG_GEM)) // the usual case: none
        return;
    for (int my = y >> 4; my <= (y + h - 1) >> 4; my++) {
        for (int mx = x >> 4; mx <= (x + w - 1) >> 4; mx++) {
            if (map_tags_in(mx * TILE, my * TILE, 1, 1) & TAG_GEM) {
                map_set_cell(mx, my, MT_EMPTY);
                spawn_sparkle(mx * TILE + 4, my * TILE + 4);
                add_gem();
            }
        }
    }
}
