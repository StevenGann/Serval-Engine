// The sprites on the road: level-of-detail sprites from one projection.
//
// Everything that stands on or beside the road, scenery, traffic and the
// checkpoints, is a point on the ground: so far along the track, so far
// across it. road_project() (road.c) says where that point is on screen,
// on which line, and how big a thing standing there looks, as a size class
// and a scale: draw the picture reduced to 1/2^class (art.c made four
// sizes of each), shrunk by the scale, which is between a half and one, in
// sixteenths.
//
// Why that way: the hardware draws a sprite shrunk with sprite_draw_ex() in
// its own box, at half the per-line cost of one grown or turned (which
// needs a box twice as big), and a 64x64 picture shrunk to a few pixels
// would still cost its full box on 64 lines. With four sizes, a sprite is
// never smaller on screen than half its box, so the cost follows its size.
// And because the scales come in sixteenths, all the objects at one scale
// (and the same flip) share one of the hardware's 32 rotation and scale
// matrices: there are at most 8 scales below full size, so 16 matrices
// cover everything, flipped or not, however many objects there are.
//
// The nearest is drawn first, since among sprites the first drawn is in
// front: the objects are collected with their distance ahead, kept in
// order as they come, and drawn from the nearest. The jeep is nearer than
// all of them, so it goes first of all.
//
// A checkpoint is seven sprites at one distance: a post at each side of
// the road (two sprites each, one lifted on the other) and a chequered
// banner across it in three pieces, lifted to the posts' tops. A lift is in
// the full-size picture's pixels, so it shrinks with the rest.

#include "objects.h"

#include "art.h"

// The jeep's wheels stand on this line, a few above the screen's bottom.
#define JEEP_FOOT (SCREEN_H - 3)

// A crash: the jeep spins, slowing, then is gone for its last CRASH_GONE
// frames, and comes back at the road's middle. After e frames it has turned
// e * (SPIN_SPAN - e) >> SPIN_SHIFT quarter turns: three turns in all.
#define CRASH_GONE 20
#define SPIN_SPAN 160
#define SPIN_SHIFT 9

// Objects further ahead than this are never in sight (road.c draws them up
// to 960 ahead).
#define VIEW_AHEAD FX(1024)

// A checkpoint: posts at POST_X out each side, each two halves POST_HALF
// tall, one on the other; the banner's pieces BANNER_STEP apart (a little
// less than their width, so they overlap at every scale), their foot
// BANNER_LIFT pixels up (the post's 64, less the piece's 32).
#define POST_X (FX_ONE * 11 / 10)
#define POST_HALF 32
#define BANNER_STEP (FX_ONE * 7 / 10)
#define BANNER_LIFT 32

// The most sprites one frame's objects take: scenery in sight is about 20,
// traffic 6, a checkpoint 7.
#define ITEMS_MAX 48

typedef struct {
    FIXED ahead; // track units in front of the car: the order of drawing
    FIXED scale;
    s16 x, y; // the sprite's box, top-left
    u16 sprite;
    u16 flags;
} Item;

static Item items[ITEMS_MAX] SERVAL_EWRAM_BSS;
static int item_count;

// Places an object `ahead` in front of the car and `lateral` across the
// road, its foot `lift` pixels (of the full-size picture) above the ground,
// among the items in order of distance (after any as far, so the first
// added of a checkpoint is in front).
static void add(const RoadFrame* f, int object, FIXED ahead, FIXED lateral, u16 flags, int lift,
                int shake) {
    Projection p = road_project(f, ahead, lateral);
    if (!p.visible || item_count == ITEMS_MAX)
        return;
    const ObjectBox* box = &object_box[object][p.size_class];
    // The picture stands at the bottom of its box, which shrinks about its
    // centre: put the box so that its picture's foot is on line p.y.
    int foot = p.y + 1 + shake - ((lift * p.scale) >> (FX_SHIFT + p.size_class));
    int i = item_count++;
    while (i > 0 && items[i - 1].ahead > ahead) {
        items[i] = items[i - 1];
        i--;
    }
    items[i] = (Item){
        .ahead = ahead,
        .scale = p.scale,
        .x = (s16)(p.x - box->w / 2),
        .y = (s16)(foot - box->h / 2 - ((box->h * p.scale) >> (FX_SHIFT + 1))),
        .sprite = (u16)OBJ_SPRITE(object, p.size_class),
        .flags = flags,
    };
}

// The scenery in sight, from the first item at or past the car on.
static void add_scenery(const Track* t, const RoadFrame* f, FIXED distance, FIXED lap, int shake) {
    static const u8 object_of[] = {
        [SCENERY_ACACIA] = OBJ_ACACIA,   [SCENERY_TERMITE_MOUND] = OBJ_MOUND,
        [SCENERY_ROCK] = OBJ_ROCK,       [SCENERY_SIGN_LEFT] = OBJ_SIGN,
        [SCENERY_SIGN_RIGHT] = OBJ_SIGN, [SCENERY_GIRAFFE] = OBJ_GIRAFFE,
    };
    int n = t->scenery_count, lo = 0, hi = n;
    while (lo < hi) { // the first at or past the car
        int mid = (lo + hi) >> 1;
        if (t->scenery[mid].distance < distance)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (int k = 0; k < n; k++) {
        int i = lo + k < n ? lo + k : lo + k - n;
        const Scenery* s = &t->scenery[i];
        FIXED ahead = s->distance - distance;
        if (ahead < 0)
            ahead += lap;
        if (ahead > VIEW_AHEAD)
            break;
        // Signs point the way of their curve; giraffes (drawn facing left)
        // face the road; the rest take turns, for variety.
        u16 flags = (i & 1) ? SPRITE_FLIP_H : 0;
        if (s->kind == SCENERY_SIGN_LEFT)
            flags = 0;
        else if (s->kind == SCENERY_SIGN_RIGHT)
            flags = SPRITE_FLIP_H;
        else if (s->kind == SCENERY_GIRAFFE)
            flags = s->lateral < 0 ? SPRITE_FLIP_H : 0;
        add(f, object_of[s->kind], ahead, s->lateral, flags, 0, shake);
    }
}

static void add_checkpoints(const Track* t, const RoadFrame* f, FIXED distance, FIXED lap,
                            int shake) {
    for (int i = 0; i < t->checkpoint_count; i++) {
        FIXED ahead = t->checkpoints[i].distance - distance;
        if (ahead < 0)
            ahead += lap;
        if (ahead > VIEW_AHEAD)
            continue;
        for (int k = -1; k <= 1; k++)
            add(f, OBJ_BANNER, ahead, k * BANNER_STEP, 0, BANNER_LIFT, shake);
        for (int side = -1; side <= 1; side += 2)
            for (int half = 0; half < 2; half++)
                add(f, OBJ_POST, ahead, side * POST_X, 0, half * POST_HALF, shake);
    }
}

void objects_draw(const Track* t, const RoadFrame* f, FIXED distance, const Vehicle* traffic,
                  int count, int shake) {
    FIXED lap = track_length(t);
    item_count = 0;
    add_scenery(t, f, distance, lap, shake);
    add_checkpoints(t, f, distance, lap, shake);
    for (int i = 0; i < count; i++) {
        const Vehicle* v = &traffic[i];
        FIXED ahead = v->distance - distance;
        if (ahead < 0)
            ahead += lap;
        if (ahead <= VIEW_AHEAD)
            add(f, v->kind == TRAFFIC_TRUCK ? OBJ_TRUCK : OBJ_JEEP, ahead, v->lateral, 0, 0, shake);
    }
    for (int i = 0; i < item_count; i++) {
        const Item* it = &items[i];
        sprite_draw_ex(it->sprite, 0, it->x, it->y, 0, it->scale, it->scale, it->flags);
    }
}

void jeep_draw_race(const Race* r, const RoadFrame* f, int shake) {
    static const s8 jolt[4] = {0, -1, -2, -1};
    int frame = JEEP_STRAIGHT;
    int y = JEEP_FOOT + shake;
    u16 flags = r->braking ? SPRITE_PALETTE(PAL_JEEP_BRAKING) : 0;
    if (r->car_state == CAR_CRASHED) {
        if (r->crash_timer <= CRASH_GONE && !r->over)
            return;
        // Behind, side on (nose right), in front, side on (nose left).
        static const u8 pose_frame[4] = {JEEP_STRAIGHT, JEEP_SIDE, JEEP_FRONT, JEEP_SIDE};
        int e = CAR_CRASH_FRAMES - r->crash_timer;
        int pose = ((e * (SPIN_SPAN - e)) >> SPIN_SHIFT) & 3;
        frame = pose_frame[pose];
        flags = pose == 1 ? SPRITE_FLIP_H : 0;
    } else if (r->speed > 0 && r->steer) {
        frame = r->steer < 0 ? JEEP_LEFT : JEEP_RIGHT;
    }
    if (r->car_state == CAR_OFFROAD && r->speed > 0)
        y += jolt[(r->distance >> 10) & 3];
    sprite_draw(SPR_JEEP, (u8)frame, f->car_x, y, flags);
}

void jeep_draw_cruise(const RoadFrame* f) {
    int frame = f->curve >= FX_ONE / 2    ? JEEP_RIGHT
                : f->curve <= -FX_ONE / 2 ? JEEP_LEFT
                                          : JEEP_STRAIGHT;
    sprite_draw(SPR_JEEP, (u8)frame, f->car_x, JEEP_FOOT, 0);
}
