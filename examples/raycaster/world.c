// The warren: its map, the doors, the serval, the wisps and the things lying
// about (game.h). One step of all of it is a tick, 30 a second: the view
// takes two frames to draw (game.c), and the world moves once per picture.

#include "game.h"

// --- The map --------------------------------------------------------------------
//
// One character per cell, 32 x 32, north at the top:
//   #  stone      m  mossy stone   w  wood        b  brick
//   B  a banner   G  the moon gate: the way out
//   D  a door     L  the locked door (it needs the key)
//   > < ^ v  where the serval starts, and which way it looks
//   a n h  a wisp: on every difficulty, on NORMAL and HARD, on HARD only
//   A N H  a hex wisp, likewise
//   +  a flask    *  crystals      k  the key
//   f  a brazier  u  an urn        i  a pillar
//   .  floor
// A door needs a wall on two opposite sides and floor on the other two. The
// border must be walls: world_start() makes it so, or rays would leave the map.
static const char* const warren[GRID_H] = {
    "################################", //
    "#....#f......f#wwwwwwwww#bbbbbb#", //
    "#.>..D........w..u...u.w#b.u..b#", //
    "#....#..i..i..w.+.....*w#b...kb#", //
    "#B####........D....n...w#b.A..b#", //
    "#m...#..i..i..w.*.....+w#b....b#", //
    "#m.*.#...a....w..u...u.w#b.u..b#", //
    "#m...#f......fwwwwwwwwww#bbDbbb#", //
    "#m...####D####mmmmmmmmmmmmm.mmm#", //
    "#m.h.m.......................nm#", //
    "#m...D..a.........n..........mm#", //
    "#m.+.m......................mmm#", //
    "#mmmmmmmm.mmmmmmDmmmmmmmmmmmm###", //
    "#bbbbbbbm.m#f......f#####B######", //
    "#b.....bm.m#........#f.......f.#", //
    "#b.u.u.bm.m#..i..i..#..........#", //
    "#b.....bm.m#........#....A.....#", //
    "#b..h..D..m#..a..a..L.........N#", //
    "#b.....bm.m#........#....H.....G", //
    "#b.u.u.bm.m#..i..i..#..........#", //
    "#b*...+bm.m#...N....#..........#", //
    "#bbbbbbbm.m#........#f.......f.#", //
    "#mmmmmmmm.m#f..+*..f############", //
    "#m........m##########mmmmmmmmmm#", //
    "#m.mmmmmmmm#....h...D........*m#", //
    "#m.m#######B........#mm.mmmmmmm#", //
    "#m.m#f....f#..u..u..#m..m#######", //
    "#m.D...a...D........#m.+m#######", //
    "#m.m#f....f#...+*...#mmmm#######", //
    "#m.m########B########mmmm#######", //
    "#m.............................#", //
    "################################", //
};

u8 grid_cells[GRID_W * GRID_H] SERVAL_EWRAM_BSS;
Door doors[MAX_DOORS];
Thing things[MAX_THINGS] SERVAL_EWRAM_BSS;
Player player;
Run run;

// Cells a brazier, urn or pillar stands in: bit x of row y. They block the
// way like walls, but not the view.
static u32 furniture[GRID_H];
static int door_count;

// --- Rules ------------------------------------------------------------------------

#define HEALTH_MAX 100
#define SPARKS_MAX 99
#define SPARKS_START 24
#define FLASK_HEALTH 25
#define CRYSTAL_SPARKS 12

#define TURN_SPEED (13 * 64) // per tick: 13 steps of the sine table, 4.6 degrees
#define WALK_SPEED FIX(0.105)
#define BACK_SPEED FIX(0.075)
#define STRAFE_SPEED FIX(0.085)
#define PLAYER_RADIUS FIX(0.22)
#define WISP_RADIUS FIX(0.28)
#define REACH FIX(0.8)        // how far ahead B opens a door
#define PICKUP_RANGE FX(0.55) // FIXED: fx_length() measures it

#define FIRE_WAIT 9       // ticks between sparks
#define FLASH_TICKS 4     // the wand's flash
#define HURT_TICKS 6      // the red screen
#define RECHARGE_TICKS 45 // an empty wand finds a spark again
#define RECHARGE_BELOW 4

#define DOOR_SPEED 4 // texels a tick: open in half a second
#define DOOR_WAIT 90 // ticks it stays open

#define WAKE_RANGE FX(9)     // a wisp notices the serval from here, if it sees it
#define STRIKE_RANGE FX(0.9) // a wisp strikes from here
#define STRIKE_REACH FX(1.3) // and hits if the serval is still this close
#define HEX_RANGE FX(6.5)    // a hex wisp casts from here
#define HEX_NEAR FX(3)       // and comes no closer than this
#define STRIKE_TICKS 10
#define CAST_TICKS 14
#define HURT_WISP_TICKS 6
#define FADE_TICKS 10

// What a difficulty changes, besides which wisps are there at all.
typedef struct {
    u8 strike;      // health a wisp's strike takes
    u8 hex;         // and a hex
    u8 wisp_rest;   // ticks between a wisp's strikes
    u8 hex_rest;    // and a hex wisp's casts
    Fix wisp_speed; // cells a tick
    Fix hex_speed;
} Rules;

static const Rules rules[DIFFICULTY_COUNT] = {
    [EASY] = {.strike = 6,
              .hex = 5,
              .wisp_rest = 40,
              .hex_rest = 90,
              .wisp_speed = FIX(0.040),
              .hex_speed = FIX(0.030)},
    [NORMAL] = {.strike = 10,
                .hex = 8,
                .wisp_rest = 30,
                .hex_rest = 70,
                .wisp_speed = FIX(0.050),
                .hex_speed = FIX(0.035)},
    [HARD] = {.strike = 15,
              .hex = 12,
              .wisp_rest = 22,
              .hex_rest = 50,
              .wisp_speed = FIX(0.062),
              .hex_speed = FIX(0.042)},
};

// --- Building the warren ------------------------------------------------------------

static bool is_wall_char(char c) {
    return c == '#' || c == 'm' || c == 'w' || c == 'b' || c == 'B' || c == 'G';
}

static void add_thing(int x, int y, u8 kind, u8 pic, s8 health, bool glows) {
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing* t = &things[i];
        if (t->kind != THING_NONE)
            continue;
        *t = (Thing){.x = x * FIX_ONE + FIX_ONE / 2,
                     .y = y * FIX_ONE + FIX_ONE / 2,
                     .kind = kind,
                     .pic = pic,
                     .health = health,
                     .glows = glows};
        if (kind == THING_WISP || kind == THING_HEX)
            run.wisps++;
        else if (kind >= THING_BRAZIER)
            furniture[y] |= 1u << x;
        return;
    }
#ifdef SERVAL_DEBUG
    debug_log("warren: more things than MAX_THINGS");
#endif
}

void world_start(u32 difficulty) {
    run = (Run){.difficulty = (u8)difficulty};
    player = (Player){.health = HEALTH_MAX, .sparks = SPARKS_START};
    door_count = 0;
    for (int i = 0; i < MAX_THINGS; i++)
        things[i].kind = THING_NONE;

    for (int y = 0; y < GRID_H; y++) {
        furniture[y] = 0;
        for (int x = 0; x < GRID_W; x++) {
            char c = warren[y][x];
            u8 cell = 0;
            // Which wisps a difficulty wakes: the lowercase letter of the pair.
            char who = (char)(c | 0x20);
            bool here = who == 'a' || (who == 'n' && difficulty >= NORMAL) ||
                        (who == 'h' && difficulty >= HARD);
            switch (c) {
            case '#':
                cell = TEX_STONE + 1;
                break;
            case 'm':
                cell = TEX_MOSS + 1;
                break;
            case 'w':
                cell = TEX_WOOD + 1;
                break;
            case 'b':
                cell = TEX_BRICK + 1;
                break;
            case 'B':
                cell = TEX_BANNER + 1;
                break;
            case 'G':
                cell = TEX_GATE + 1;
                break;
            case 'D':
            case 'L':
                if (door_count < MAX_DOORS) {
                    // It stands across the way: between the walls beside it.
                    doors[door_count] = (Door){
                        .x = (u8)x,
                        .y = (u8)y,
                        .vertical = y > 0 && is_wall_char(warren[y - 1][x]),
                        .locked = c == 'L',
                    };
                    cell = (u8)(CELL_DOOR | door_count++);
                }
                break;
            case '>':
            case 'v':
            case '<':
            case '^':
                player.x = x * FIX_ONE + FIX_ONE / 2;
                player.y = y * FIX_ONE + FIX_ONE / 2;
                player.angle = c == '>'   ? 0
                               : c == 'v' ? ANGLE_DEG(90)
                               : c == '<' ? ANGLE_DEG(180)
                                          : ANGLE_DEG(270);
                break;
            case 'a':
            case 'n':
            case 'h':
                if (here)
                    add_thing(x, y, THING_WISP, PIC_WISP, 2, true);
                break;
            case 'A':
            case 'N':
            case 'H':
                if (here)
                    add_thing(x, y, THING_HEX, PIC_HEX, 4, true);
                break;
            case '+':
                add_thing(x, y, THING_FLASK, PIC_FLASK, 0, false);
                break;
            case '*':
                add_thing(x, y, THING_CRYSTALS, PIC_CRYSTALS, 0, false);
                break;
            case 'k':
                add_thing(x, y, THING_KEY, PIC_KEY, 0, true);
                break;
            case 'f':
                add_thing(x, y, THING_BRAZIER, PIC_BRAZIER, 0, true);
                break;
            case 'u':
                add_thing(x, y, THING_URN, PIC_URN, 0, false);
                break;
            case 'i':
                add_thing(x, y, THING_PILLAR, PIC_PILLAR, 0, false);
                break;
            default:
                break;
            }
            if (!cell && (x == 0 || y == 0 || x == GRID_W - 1 || y == GRID_H - 1))
                cell = TEX_STONE + 1; // a hole in the border: rays must stop somewhere
            grid_cells[y * GRID_W + x] = cell;
        }
    }
}

// --- Getting about ------------------------------------------------------------------

// Whether a cell is in the way: a wall, a door not open enough, or furniture
// (which blocks the way but not `sight`: sparks and looks pass over it).
static bool blocked(int x, int y, bool sight) {
    if ((unsigned)x >= GRID_W || (unsigned)y >= GRID_H)
        return true;
    u32 cell = grid_cell(x, y);
    if (cell & CELL_DOOR)
        return doors[cell & (CELL_DOOR - 1)].open < DOOR_PASSABLE;
    if (cell)
        return true;
    return !sight && (furniture[y] >> x & 1);
}

// Whether a square of half-size `radius` fits at (x, y).
static bool fits(Fix x, Fix y, Fix radius) {
    int x0 = (x - radius) >> 16, x1 = (x + radius) >> 16;
    int y0 = (y - radius) >> 16, y1 = (y + radius) >> 16;
    return !blocked(x0, y0, false) && !blocked(x1, y0, false) && !blocked(x0, y1, false) &&
           !blocked(x1, y1, false);
}

// Moves by (dx, dy) as far as walls allow: each axis on its own, so that
// walking into a wall at an angle slides along it.
static void move(Fix* x, Fix* y, Fix dx, Fix dy, Fix radius) {
    if (fits(*x + dx, *y, radius))
        *x += dx;
    if (fits(*x, *y + dy, radius))
        *y += dy;
}

// Whether a straight line from (x0, y0) to (x1, y1) crosses no wall or shut
// door: walked in steps of a quarter cell or less.
static bool sees(Fix x0, Fix y0, Fix x1, Fix y1) {
    Fix dx = x1 - x0, dy = y1 - y0;
    Fix longest =
        (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    int shift = 2; // 4 steps for up to a cell, doubling as it grows
    while ((FIX_ONE << (shift - 2)) < longest)
        shift++;
    // Multiplying, not shifting: a negative number shifts toward minus infinity.
    for (int i = 1; i < 1 << shift; i++) {
        Fix x = x0 + dx * i / (1 << shift), y = y0 + dy * i / (1 << shift);
        if (blocked(x >> 16, y >> 16, true))
            return false;
    }
    return true;
}

static FIXED distance_to_player(const Thing* t) {
    return fx_length(fix_to_fx(t->x - player.x), fix_to_fx(t->y - player.y));
}

// Whether a thing is within `range` (a FIXED) of the serval. Most things are
// far off on most ticks, and a look at the differences says so without
// measuring the distance (fx_length() is some hundreds of cycles).
static bool near_player(const Thing* t, FIXED range) {
    FIXED dx = fix_to_fx(t->x - player.x), dy = fix_to_fx(t->y - player.y);
    if (dx > range || dx < -range || dy > range || dy < -range)
        return false;
    return fx_length(dx, dy) < range;
}

// --- Doors ---------------------------------------------------------------------------

// Whether a square of half-size `radius` at (x, y) touches the door's cell.
static bool in_doorway(Fix x, Fix y, Fix radius, const Door* d) {
    return ((x - radius) >> 16) <= d->x && ((x + radius) >> 16) >= d->x &&
           ((y - radius) >> 16) <= d->y && ((y + radius) >> 16) >= d->y;
}

// Whether the serval or a wisp is in the doorway: a door doesn't close on
// them (they would be stuck in a cell that had become a wall).
static bool doorway_taken(const Door* d) {
    if (in_doorway(player.x, player.y, PLAYER_RADIUS, d))
        return true;
    for (int i = 0; i < MAX_THINGS; i++)
        if ((things[i].kind == THING_WISP || things[i].kind == THING_HEX) &&
            in_doorway(things[i].x, things[i].y, WISP_RADIUS, d))
            return true;
    return false;
}

static void doors_tick(void) {
    for (int i = 0; i < door_count; i++) {
        Door* d = &doors[i];
        switch (d->state) {
        case DOOR_OPENING:
            d->open = (u8)(d->open + DOOR_SPEED);
            if (d->open >= DOOR_OPEN) {
                d->open = DOOR_OPEN;
                d->state = DOOR_WAITING;
                d->timer = DOOR_WAIT;
            }
            break;
        case DOOR_WAITING:
            if (d->timer)
                d->timer--;
            else if (!doorway_taken(d))
                d->state = DOOR_CLOSING;
            break;
        case DOOR_CLOSING:
            if (d->open >= DOOR_PASSABLE && doorway_taken(d)) {
                d->state = DOOR_OPENING; // someone slipped in: open again
                break;
            }
            d->open = (u8)(d->open - DOOR_SPEED);
            if (d->open == 0)
                d->state = DOOR_SHUT;
            break;
        default:
            break;
        }
    }
}

// B: opens the door ahead, or goes through the moon gate.
static void use(Fix dir_x, Fix dir_y) {
    int x = (player.x + fix_mul(dir_x, REACH)) >> 16;
    int y = (player.y + fix_mul(dir_y, REACH)) >> 16;
    if ((unsigned)x >= GRID_W || (unsigned)y >= GRID_H)
        return;
    u32 cell = grid_cell(x, y);
    if (cell == TEX_GATE + 1) {
        run.won = true;
        run.over = true;
        return;
    }
    if (!(cell & CELL_DOOR))
        return;
    Door* d = &doors[cell & (CELL_DOOR - 1)];
    if (d->locked) {
        if (!player.has_key) {
            psg_play(SND_LOCKED);
            return;
        }
        d->locked = false; // and from now on it is a door like the others
        psg_play(SND_UNLOCK);
    } else if (d->state == DOOR_SHUT || d->state == DOOR_CLOSING) {
        psg_play(SND_DOOR);
    }
    if (d->state == DOOR_SHUT || d->state == DOOR_CLOSING)
        d->state = DOOR_OPENING;
}

// --- The serval -----------------------------------------------------------------------

static void hurt_player(int damage) {
    if (run.over)
        return;
    player.health = (s16)(player.health - damage);
    player.hurt = HURT_TICKS;
    psg_play(SND_HURT);
    if (player.health <= 0) {
        player.health = 0;
        run.over = true;
    }
}

static bool is_wisp(const Thing* t) {
    return (t->kind == THING_WISP || t->kind == THING_HEX) && t->state != WISP_FADING;
}

// The wand: a spark flies straight ahead and stops at the first wisp, if
// there is one before the first wall.
static void fire(Fix dir_x, Fix dir_y) {
    // How far the wall is: walked in eighths of a cell.
    Fix x = player.x, y = player.y;
    Fix reach = 0;
    while (reach < FIX(20)) {
        x += dir_x / 8;
        y += dir_y / 8;
        if (blocked(x >> 16, y >> 16, true))
            break;
        reach += FIX_ONE / 8;
    }
    reach += FIX_ONE / 4; // a wisp against the wall is still in front of it

    Thing* hit = NULL;
    Fix nearest = reach;
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing* t = &things[i];
        if (!is_wisp(t))
            continue;
        Fix dx = t->x - player.x, dy = t->y - player.y;
        Fix ahead = fix_mul(dx, dir_x) + fix_mul(dy, dir_y);
        Fix aside = fix_mul(dy, dir_x) - fix_mul(dx, dir_y);
        if (ahead < FIX(0.1) || ahead >= nearest || aside > FIX(0.32) || aside < -FIX(0.32))
            continue;
        hit = t;
        nearest = ahead;
    }
    if (!hit)
        return;
    hit->health--;
    if (hit->health <= 0) {
        hit->state = WISP_FADING;
        hit->timer = FADE_TICKS;
        run.downed++;
        psg_play(SND_WISP_GONE);
    } else {
        hit->state = WISP_HURT; // which also wakes it
        hit->timer = HURT_WISP_TICKS;
        psg_play(SND_WISP_HURT);
    }
}

static void pick_up(void) {
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing* t = &things[i];
        if (t->kind != THING_FLASK && t->kind != THING_CRYSTALS && t->kind != THING_KEY)
            continue;
        if (!near_player(t, PICKUP_RANGE))
            continue;
        if (t->kind == THING_FLASK) {
            if (player.health >= HEALTH_MAX)
                continue; // left for later
            player.health = (s16)(player.health + FLASK_HEALTH);
            if (player.health > HEALTH_MAX)
                player.health = HEALTH_MAX;
            psg_play(SND_PICKUP);
        } else if (t->kind == THING_CRYSTALS) {
            if (player.sparks >= SPARKS_MAX)
                continue;
            player.sparks = (s16)(player.sparks + CRYSTAL_SPARKS);
            if (player.sparks > SPARKS_MAX)
                player.sparks = SPARKS_MAX;
            psg_play(SND_PICKUP);
        } else {
            player.has_key = true;
            psg_play(SND_KEY);
        }
        t->kind = THING_NONE;
    }
}

static void player_tick(const Controls* input) {
    player.angle = (u16)(player.angle + input->turn * TURN_SPEED);
    Fix dir_x = fix_from_fx(fx_cos(player.angle));
    Fix dir_y = fix_from_fx(fx_sin(player.angle));

    Fix dx = 0, dy = 0;
    if (input->forward) {
        Fix speed = input->forward > 0 ? WALK_SPEED : -BACK_SPEED;
        dx += fix_mul(dir_x, speed);
        dy += fix_mul(dir_y, speed);
    }
    if (input->strafe) { // to the right is (-dir_y, dir_x)
        Fix speed = input->strafe * STRAFE_SPEED;
        dx -= fix_mul(dir_y, speed);
        dy += fix_mul(dir_x, speed);
    }
    move(&player.x, &player.y, dx, dy, PLAYER_RADIUS);

    if (input->use)
        use(dir_x, dir_y);

    if (player.fire_wait)
        player.fire_wait--;
    if (player.flash)
        player.flash--;
    if (player.hurt)
        player.hurt--;
    if (input->fire && !player.fire_wait) {
        if (player.sparks > 0) {
            player.sparks--;
            player.fire_wait = FIRE_WAIT;
            player.flash = FLASH_TICKS;
            psg_play(SND_SPARK);
            fire(dir_x, dir_y);
        } else {
            player.fire_wait = FIRE_WAIT * 2;
            psg_play(SND_DRY);
        }
    }
    // A wand that has run dry finds a spark again by itself, slowly: the
    // serval is never left with nothing.
    if (player.sparks < RECHARGE_BELOW) {
        if (++player.recharge >= RECHARGE_TICKS) {
            player.recharge = 0;
            player.sparks++;
        }
    } else {
        player.recharge = 0;
    }
    pick_up();
}

// --- Wisps ----------------------------------------------------------------------------

// A step toward the serval, if no other wisp is in the way there.
static void approach(Thing* t, int index, Fix speed) {
    u16 heading = angle_of(fix_to_fx(player.x - t->x), fix_to_fx(player.y - t->y));
    Fix x = t->x, y = t->y;
    move(&x, &y, fix_mul(fix_from_fx(fx_cos(heading)), speed),
         fix_mul(fix_from_fx(fx_sin(heading)), speed), WISP_RADIUS);
    // Wisps keep apart: the one with the higher number waits.
    for (int i = 0; i < index; i++) {
        const Thing* other = &things[i];
        if (!is_wisp(other))
            continue;
        Fix dx = other->x - x, dy = other->y - y;
        if (dx < FIX(0.6) && dx > -FIX(0.6) && dy < FIX(0.6) && dy > -FIX(0.6))
            return;
    }
    t->x = x;
    t->y = y;
}

static void wisp_tick(Thing* t, int index) {
    const Rules* r = &rules[run.difficulty];
    bool hex = t->kind == THING_HEX;
    u8 pics = hex ? PIC_HEX : PIC_WISP;
    u8 drift = (u8)(pics + ((run.ticks + (u32)index * 3) >> 3 & 1));
    if (t->rest)
        t->rest--;

    switch (t->state) {
    case WISP_ASLEEP:
        t->pic = drift;
        // Looked for every fourth tick, each wisp in its own.
        if (((run.ticks + (u32)index) & 3) == 0 && near_player(t, WAKE_RANGE) &&
            sees(t->x, t->y, player.x, player.y)) {
            t->state = WISP_CHASING;
            t->rest = 15; // a moment before its first strike
            psg_play(SND_WISP_WAKES);
        }
        break;

    case WISP_CHASING: {
        t->pic = drift;
        FIXED distance = distance_to_player(t);
        if (hex) {
            bool in_sight = distance < HEX_RANGE && sees(t->x, t->y, player.x, player.y);
            if (in_sight && !t->rest) {
                t->state = WISP_STRIKING;
                t->timer = CAST_TICKS;
                psg_play(SND_HEX);
            } else if (!in_sight || distance > HEX_NEAR) {
                approach(t, index, r->hex_speed);
            }
        } else if (distance < STRIKE_RANGE) {
            if (!t->rest) {
                t->state = WISP_STRIKING;
                t->timer = STRIKE_TICKS;
                psg_play(SND_STRIKE);
            }
        } else {
            approach(t, index, r->wisp_speed);
        }
        break;
    }

    case WISP_STRIKING:
        t->pic = (u8)(pics + 2);
        if (--t->timer)
            break;
        // It lands if the serval hasn't got away: out of a wisp's reach, or
        // out of a hex wisp's sight. A hex also misses more the farther it
        // flies: 9 in 10 land from up close, 1 in 2 from as far as it casts.
        if (hex) {
            int chance = 90 - (int)(distance_to_player(t) * 40 / HEX_RANGE);
            if (sees(t->x, t->y, player.x, player.y) && random_range(0, 99) < chance)
                hurt_player(r->hex);
            t->rest = r->hex_rest;
        } else {
            if (near_player(t, STRIKE_REACH))
                hurt_player(r->strike);
            t->rest = r->wisp_rest;
        }
        t->state = WISP_CHASING;
        break;

    case WISP_HURT:
        t->pic = (u8)(pics + 3);
        if (--t->timer == 0)
            t->state = WISP_CHASING;
        break;

    case WISP_FADING:
        t->pic = (u8)(pics + (t->timer > FADE_TICKS / 2 ? 4 : 5));
        if (--t->timer == 0)
            t->kind = THING_NONE;
        break;

    default:
        break;
    }
}

void world_tick(const Controls* input) {
    if (run.over)
        return;
    run.ticks++;
    player_tick(input);
    doors_tick();
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing* t = &things[i];
        if (t->kind == THING_WISP || t->kind == THING_HEX)
            wisp_tick(t, i);
        else if (t->kind == THING_BRAZIER)
            t->pic = (u8)(PIC_BRAZIER + ((run.ticks + (u32)i * 5) >> 2 & 1));
    }
}
