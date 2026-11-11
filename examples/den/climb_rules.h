#ifndef DEN_CLIMB_RULES_H
#define DEN_CLIMB_RULES_H

// Acacia Climb's rules, portable C (no hardware; tested on the host). The
// serval jumps from bough to bough up an endless acacia; the camera follows
// it upward only, and falling below the screen's bottom ends the round. The
// score is the height in metres: the number of the highest row stood on.
//
// The tree is a function of a seed: row r (a bough every CLIMB_ROW_H pixels,
// row 0 the ledge at the foot) has the same boughs every time it is asked for.
//
// Rulings where the rules left a choice:
//  - Positions are in 1/256 pixel (Q8); x is the screen's, y the world's
//    (down is positive, row r's top is at y = -r * CLIMB_ROW_H); only the
//    camera moves.
//  - The main bough of row r is on the left for even r and on the right
//    for odd r, its inner end 6 to 16 pixels (a little more higher up) from
//    the trunk's middle, 32, 48 or 64 pixels long. From row 2, half the rows
//    have a second, short bough on the other side, further out.
//  - A bough is one of: still, swaying (moves CLIMB_SWAY pixels either way,
//    a triangle wave of CLIMB_SWAY_PERIOD frames; from row 3), dry (snaps
//    CLIMB_SNAP_FRAMES frames after the serval first lands on it, from row 5,
//    and is gone), or, from row 8, still with a weaver bird's nest hanging at
//    its inner end, which knocks the serval down (on a main bough, only
//    every third row).
//  - Boughs are one-way: the serval rises through them and lands on them
//    only falling.
//  - Every row has a bough the previous row's reaches with the jump's arc
//    (climb_jump_reach(), the arc the game plays: climb_step() and the
//    reach use the same step); climb_branch_free() is the part of a bough
//    that a nest leaves, and a swaying bough counts at its nearest swing.
//  - A jump is A: held, it rises to CLIMB_JUMP_V^2 / 2 / CLIMB_GRAV pixels
//    (50, a row and a bit); let go, the rise is cut short. A pressed up to
//    CLIMB_BUFFER frames before landing jumps on landing.
//  - A nest knocks the serval down at CLIMB_KNOCK_V and sideways away from
//    it; then it falls as it falls, to the row below if it lands there.

#include <serval/core.h>

#define CLIMB_ROW_H 40
#define CLIMB_TRUNK_X 120
#define CLIMB_GRAV 64    // 1/4 px/frame^2
#define CLIMB_JUMP_V 1280 // 5 px/frame
#define CLIMB_CUT_V 384
#define CLIMB_FALL_V 1536 // terminal speed, 6 px/frame
#define CLIMB_WALK 384    // 1.5 px/frame
#define CLIMB_AIR 512     // 2 px/frame
#define CLIMB_KNOCK_V 768
#define CLIMB_KNOCK_PUSH 640
#define CLIMB_BUFFER 6
#define CLIMB_SNAP_FRAMES 30
#define CLIMB_SWAY 16
#define CLIMB_SWAY_PERIOD 160
#define CLIMB_NEST_SAFE 9 // a nest keeps the serval this far off its middle
#define CLIMB_CAM_FEET 84 // the screen row the feet stay above, climbing
#define CLIMB_START_FEET 130
#define CLIMB_FALL_LINE 176 // feet below this screen row: the round is over
#define CLIMB_X_MIN 8
#define CLIMB_X_MAX 232
#define CLIMB_FUN_BASE 10
#define CLIMB_FUN_ROWS_MAX 30

enum { CLIMB_SWAYS = 1, CLIMB_SNAPS = 2, CLIMB_NESTS = 4 };

typedef struct {
    s16 x0, x1;   // where the serval can stand, at rest (pixels)
    s16 nest_x;   // a nest's middle, if CLIMB_NESTS
    bool inner_right; // the trunk is at the right end (a bough on the left)
    u8 flags;
} ClimbBranch;

typedef struct {
    u8 count; // 1 or 2; branch 0 is the main one
    ClimbBranch b[2];
} ClimbRow;

// What happened in a step (ClimbState.events), for sounds and effects.
enum {
    CLIMB_EV_JUMP = 1,
    CLIMB_EV_LAND = 2,
    CLIMB_EV_SNAP = 4, // a bough broke: ev_row, ev_idx
    CLIMB_EV_NEST = 8,
    CLIMB_EV_OVER = 16,
    CLIMB_EV_CRACK = 32, // a dry bough was landed on: ev_row, ev_idx
};

typedef enum { CLIMB_PLAYING, CLIMB_OVER } ClimbResult;

#define CLIMB_RING 16

typedef struct {
    u32 seed;
    s32 x, y;   // the feet, Q8
    s32 vx, vy; // Q8 per frame
    s32 cam;    // the world y at the screen's top, pixels
    s32 push;   // sideways knock, Q8 per frame, dying away
    u32 frame;
    u16 rows;   // the highest row stood on
    s16 row;    // the bough stood on (when `standing`)
    u8 idx;
    bool standing;
    u8 buffer;  // frames left of a jump pressed too early
    u8 knock;   // frames before another nest can knock
    u8 events;
    s16 ev_row;
    u8 ev_idx;
    u16 prev_keys;
    s16 tag[CLIMB_RING];  // the row each ring entry holds (the snapped boughs)
    u8 snap[CLIMB_RING][2]; // frames to go before it breaks (0: not landed on)
    u8 broken[CLIMB_RING];  // bit i: bough i is gone
} ClimbState;

// The row `row` of the tree for `seed`.
void climb_row(u32 seed, int row, ClimbRow* out);

// How far the bough is from its rest at `frame` (CLIMB_SWAYS ones only; the
// phase differs by row and bough).
int climb_sway(int row, int idx, u32 frame);

// The part of a bough the serval can use, a nest left aside: its x range (at
// rest), and with `swing` the range a swaying one sweeps.
void climb_branch_free(const ClimbBranch* b, bool swing, int* lo, int* hi);

// The horizontal distance in pixels that a full jump (A held, steering all
// the way) covers while it rises to one row up and comes down to it again:
// the largest gap between two boughs' feet-ranges that the tree allows.
int climb_jump_reach(void);

// Starts a round on the ledge.
void climb_start(ClimbState* s, u32 seed);

// One frame; `keys` are the buttons held (LEFT, RIGHT, A).
ClimbResult climb_step(ClimbState* s, u16 keys);

// Puts the serval standing on a bough (for the start, and for tests).
void climb_stand_on(ClimbState* s, int row, int idx, int x);

// Whether bough `idx` of `row` is gone, and how many frames until a dry one
// breaks (0: not landed on).
bool climb_broken(const ClimbState* s, int row, int idx);
int climb_snap_left(const ClimbState* s, int row, int idx);

// The fun a round that reached `rows` earns (pet_played's points).
u8 climb_fun(u32 rows);

#endif // DEN_CLIMB_RULES_H
