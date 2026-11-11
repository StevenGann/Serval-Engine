#ifndef DEN_YARN_RULES_H
#define DEN_YARN_RULES_H

// Yarn Bat's rules, portable C (no hardware; tested on the host). The kitten
// stands at the left and bats a ball of yarn that bounces off the den's far
// wall at the right, the ceiling and the floor; each bat is a point and
// unravels more of the yarn. A miss ends the round.
//
// Rulings where the rules left a choice:
//  - Positions are in 1/256 pixel (Q8), the court in screen pixels.
//  - The ball speeds up by 1/16 px/frame every 4 bats from 1.5 px/frame to
//    at most 4 (160 bats: the 30 bats that earn fun come well before).
//  - A bat sends the ball off at an angle by where the paw met it: the
//    paddle's middle straight (the speed is kept, only the angle changes),
//    its ends at most 53 degrees up or down.
//  - A missed ball is not stopped: it rolls on past the paddle and off the
//    screen, and the round is over then (YARN_OVER), not before.
//  - The ball waits YARN_SERVE_WAIT frames at the start.

#include <serval/core.h>

#define YARN_WALL_X 224  // the far wall's face
#define YARN_CEIL_Y 16   // the ceiling, the court's top
#define YARN_FLOOR_Y 144 // the floor, its bottom
#define YARN_PAW_X 30    // the paw's face: the ball is batted when its left edge reaches it
#define YARN_BALL_R 4    // the ball's radius
#define YARN_PADDLE_H 24 // the paw's reach (a ball touching its end counts)
#define YARN_PADDLE_SPEED 3
#define YARN_SERVE_WAIT 45

#define YARN_SPEED_START 384 // 1.5 px/frame, in 1/256
#define YARN_SPEED_STEP 16   // 1/16 px/frame
#define YARN_SPEED_MAX 1024  // 4 px/frame
#define YARN_BATS_PER_STEP 4

#define YARN_FUN_BASE 10
#define YARN_FUN_BATS_MAX 30

// What happened in a step (YarnState.events), for sounds.
enum { YARN_EV_BAT = 1, YARN_EV_WALL = 2, YARN_EV_MISS = 4 };

typedef enum { YARN_PLAYING, YARN_OVER } YarnResult;

typedef struct {
    s32 x, y;   // the ball's middle, Q8
    s32 vx, vy; // Q8 per frame
    s16 paw_y;  // the paddle's middle, pixels
    s16 paw_h;  // its reach (YARN_PADDLE_H unless yarn_set_reach())
    u16 bats;
    u16 wait; // frames before the ball moves
    bool missed;
    u8 events;
    u32 rng;
} YarnState;

void yarn_start(YarnState* s, u32 seed);
// The paw's reach in pixels (the pet's size: a bigger pet covers more).
void yarn_set_reach(YarnState* s, int h);
// One frame; `keys` are the buttons held (BUTTON_UP and BUTTON_DOWN move the
// paw).
YarnResult yarn_step(YarnState* s, u16 keys);

// The ball's speed after `bats` bats, Q8.
s32 yarn_speed(u32 bats);
// The fun a round of `bats` bats earns (pet_played's points).
u8 yarn_fun(u32 bats);

#endif // DEN_YARN_RULES_H
