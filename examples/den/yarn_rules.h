#ifndef DEN_YARN_RULES_H
#define DEN_YARN_RULES_H

// Yarn Bat's rules, portable C (no hardware; tested on the host). The pet runs
// left and right along the den's floor and bats a ball of yarn up with a paw;
// the yarn bounces off the den's ceiling and its two side walls and comes
// down again. Each bat is a point and unravels more of the yarn. A miss (the
// yarn passing the paw's line beside the pet) ends the round when it lands.
//
// Rulings where the rules left a choice:
//  - Positions are in 1/256 pixel (Q8), the court in screen pixels.
//  - The ball speeds up by 1/16 px/frame every 4 bats from 1.5 px/frame to
//    at most 4 (160 bats: the 30 bats that earn fun come well before).
//  - A bat sends the ball off at an angle by where it met the paw: the
//    paw's middle straight up (the speed is kept, only the angle changes),
//    its ends at most 53 degrees to the left or right of up.
//  - A missed ball is not stopped: it falls on past the paw's line to the
//    floor, and the round is over then (YARN_OVER), not before.
//  - The ball waits YARN_SERVE_WAIT frames at the start.

#include <serval/core.h>

#define YARN_LEFT_X 16   // the near wall's face
#define YARN_WALL_X 224  // the far wall's face
#define YARN_CEIL_Y 16   // the ceiling, the court's top
#define YARN_FLOOR_Y 144 // the floor, where a missed ball lands
#define YARN_PAW_Y                                                                                 \
    124                  // the paw's line (the kitten's; yarn_set_reach() moves it): the ball
                         // is batted when its bottom edge reaches it
#define YARN_BALL_R 4    // the ball's radius
#define YARN_PADDLE_W 24 // the paw's reach, across (a ball touching its end counts)
#define YARN_PET_HALF 22 // the paw's middle stays this far from each wall
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
    s16 paw_x;  // the pet's middle, pixels
    s16 paw_w;  // the paw's reach across (YARN_PADDLE_W unless yarn_set_reach())
    s16 paw_y;  // the paw's line, pixels (YARN_PAW_Y unless yarn_set_reach())
    u16 bats;
    u16 wait; // frames before the ball moves
    bool missed;
    u8 events;
    u32 rng;
} YarnState;

void yarn_start(YarnState* s, u32 seed);
// The paw's reach across in pixels, and its line's height (the pet's size: a
// bigger pet covers more, and holds its paw higher).
void yarn_set_reach(YarnState* s, int w, int line_y);
// One frame; `keys` are the buttons held (BUTTON_LEFT and BUTTON_RIGHT move
// the pet).
YarnResult yarn_step(YarnState* s, u16 keys);

// The ball's speed after `bats` bats, Q8.
s32 yarn_speed(u32 bats);
// The fun a round of `bats` bats earns (pet_played's points).
u8 yarn_fun(u32 bats);

#endif // DEN_YARN_RULES_H
