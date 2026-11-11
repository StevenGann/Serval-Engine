#ifndef DEN_POUNCE_RULES_H
#define DEN_POUNCE_RULES_H

// High Pounce's rules, portable C (no hardware; tested on the host). A
// 60-second round in tall grass: the serval hunts by ear. Rustles appear at
// random places in the grass; most hide a mouse, some a bird. The serval
// creeps, listens and leaps. The score is the points: +1 a mouse, +3 a bird.
//
// Rulings where the rules left a choice:
//  - The world is one screen wide, x in pixels 0 to 239 (the serval's feet,
//    POUNCE_X_MIN to POUNCE_X_MAX), positions in 1/256 pixel (Q8).
//  - The leap is fixed: A starts it, it lasts POUNCE_LEAP_FRAMES (40) frames,
//    covers POUNCE_REACH (48) pixels forward in the direction the serval
//    faces and peaks POUNCE_PEAK (40) pixels up half way (pounce_leap_x(),
//    pounce_leap_h(): the arc the game plays). So aiming is by position and
//    facing, not by target: the serval faces the way it last walked. Near the
//    screen's edge the landing is held inside it. Steering in the air does
//    nothing. After landing the serval recovers for POUNCE_RECOVER frames.
//  - A landing within POUNCE_CATCH_R (12) pixels of a mouse's rustle catches
//    it (+1; the nearest if several). If it caught nothing, swatted nothing
//    and flushed no bird, it is a miss: every rustle within POUNCE_SCARE_R
//    (48) of the landing goes quiet for POUNCE_SCARE_FRAMES (2 s) and then
//    returns where it was.
//  - A bird's rustle looks and sounds like a mouse's. Un-crouched (anywhere
//    but crouched on the ground: walking, standing, or in the air), the
//    serval within POUNCE_FLUSH_R (24) pixels of it flushes the bird, and
//    so does a landing within POUNCE_CATCH_R of it. The bird flies up and
//    away (POUNCE_BIRD_SPEED, rising to POUNCE_BIRD_H) and is gone off the
//    screen. Crouching (B held, on the ground) is slower (POUNCE_CROUCH) and
//    does not flush: that is how to creep up on a bird that you want to flush
//    on purpose, or not at all.
//  - Pressing A while a flying bird is within POUNCE_REACH pixels of the
//    serval (either side) and no higher than POUNCE_SWAT_H swats it at once
//    (+3): the leap, turned toward the bird, goes on, and counts as a hit
//    (no scare). The bird is the swat's only target: it is checked when A is
//    pressed, not along the arc.
//  - Rustles: up to POUNCE_ACTIVE at a time, each lasting POUNCE_LIFE_MIN to
//    POUNCE_LIFE_MAX frames, at least POUNCE_SPAWN_GAP from the serval and
//    from each other when they appear. A rustle makes a burst of noise and
//    shaking grass every POUNCE_PULSE_MIN to POUNCE_PULSE_MAX frames
//    (POUNCE_BURST_FRAMES long); mice also wander slowly. Silent between
//    bursts: there is nothing to see or hear then.
//  - A press of A during a leap or the recovery is kept, and leaps as soon as
//    the serval can if it was pressed at most POUNCE_BUFFER (8) frames before.
//  - The round ends when frame POUNCE_ROUND_FRAMES has been stepped. A leap
//    that lands on that frame counts (a catch, a miss); one still in the air
//    does not (no catch, no scare). A swat is scored when A is pressed, so it
//    counts even if the leap's landing never comes.
//  - Crouching helps the ears: while crouched on the ground the rustles sound
//    one step louder (pounce_hear(), a fifth level above pounce_loudness()'s
//    four), so a serval that creeps hears better as well as startling no
//    birds. There are no footsteps: crouched is not "quieter" in any other way.
//  - The serval's ears turn to a burst it hears (within POUNCE_HEAR_R, the
//    nearest one if several): pounce_heard() gives the direction for
//    POUNCE_HEARD_FRAMES frames, following the serval as it moves.
//  - Fun: POUNCE_FUN_BASE + points, at most POUNCE_FUN_POINTS_MAX of them.

#include <serval/core.h>

#define POUNCE_ROUND_FRAMES 3600
#define POUNCE_X_MIN 16
#define POUNCE_X_MAX 224
#define POUNCE_WALK 192  // 0.75 px/frame
#define POUNCE_CROUCH 96 // 0.375 px/frame
#define POUNCE_LEAP_FRAMES 40
#define POUNCE_REACH 48
#define POUNCE_PEAK 40
#define POUNCE_RECOVER 10
#define POUNCE_BUFFER 8 // an A pressed this many frames before the serval can leap, leaps then
#define POUNCE_CATCH_R 12
#define POUNCE_FLUSH_R 24
#define POUNCE_SCARE_R 48
#define POUNCE_SCARE_FRAMES 120
#define POUNCE_BIRD_SPEED 192 // 0.75 px/frame, away
#define POUNCE_BIRD_RISE 384  // 1.5 px/frame, up
#define POUNCE_BIRD_H 30
#define POUNCE_SWAT_H 48
#define POUNCE_ACTIVE 3
#define POUNCE_SLOTS 6 // the active ones and the birds in the air
#define POUNCE_LIFE_MIN 420
#define POUNCE_LIFE_MAX 720
#define POUNCE_PULSE_MIN 70
#define POUNCE_PULSE_MAX 130
#define POUNCE_BURST_FRAMES 14
#define POUNCE_SPAWN_GAP 40
#define POUNCE_HEAR_R 144
#define POUNCE_HEARD_FRAMES 45
#define POUNCE_FUN_BASE 10
#define POUNCE_FUN_POINTS_MAX 30
#define POUNCE_MOUSE_SPEED 64 // 0.25 px/frame

typedef enum {
    PR_FREE,
    PR_ACTIVE, // a rustle: kind says what is under it
    PR_SCARED, // quiet until `timer` runs out
    PR_FLY,    // a bird in the air
} PounceRustleState;

enum { PK_MOUSE, PK_BIRD };

typedef struct {
    u8 state, kind;
    s32 x;     // Q8
    s32 h;     // PR_FLY: the height, Q8
    s16 vx;    // Q8 per frame
    u16 life;  // frames until the rustle ends
    u16 pulse; // frames until the next burst
    u16 turn;  // frames until a mouse changes its mind
    u16 timer; // PR_SCARED: frames to go
    u8 burst;  // frames of the current burst left
} PounceRustle;

// What happened in a step (PounceState.events), for sounds and effects.
enum {
    POUNCE_EV_LEAP = 1,
    POUNCE_EV_LAND = 2,
    POUNCE_EV_CATCH = 4,  // a mouse: ev_x
    POUNCE_EV_SWAT = 8,   // a bird: ev_x, ev_h
    POUNCE_EV_MISS = 16,  // a landing that scared the grass: ev_x
    POUNCE_EV_FLUSH = 32, // a bird took off: ev_x
};

typedef enum { POUNCE_PLAYING, POUNCE_OVER } PounceResult;

typedef struct {
    u32 rng;
    u32 frame;
    s32 x;       // the serval's feet, Q8
    s8 face;     // +1 right, -1 left
    bool crouch; // crouched on the ground
    u8 leap;     // frames into the leap (0: on the ground)
    s8 leap_dir;
    s32 leap_x0; // Q8
    u8 recover;
    bool hit; // the leap swatted a bird
    u16 prev_keys;
    u32 a_at; // the frame an A press was kept at (0: none), see pounce_rules.h
    u16 points, mice, birds;
    u8 events;
    s16 ev_x, ev_h; // pixels
    u8 bursts;      // bit i: rustle i began a burst this frame
    u16 spawn_wait;
    s16 heard_x; // where the last burst heard was (pixels)
    u16 heard_t; // frames left of hearing it
    bool quiet;  // no new rustles appear (for tests)
    PounceRustle r[POUNCE_SLOTS];
} PounceState;

// The leap's arc: after `t` frames (0 to POUNCE_LEAP_FRAMES) the distance
// travelled forward (Q8) and the height (pixels).
s32 pounce_leap_dx(int t);
int pounce_leap_h(int t);

// Starts a round with the serval in the middle and two rustles.
void pounce_start(PounceState* s, u32 seed);

// One frame; `keys` are the buttons held (LEFT, RIGHT, B to crouch, A to
// leap: a press, not a hold).
PounceResult pounce_step(PounceState* s, u16 keys);

// Puts a rustle in a free slot (for the start, and for tests). Returns its
// index, or -1 if none is free.
int pounce_put(PounceState* s, int kind, int x_pixels);

// The serval's x in pixels.
int pounce_x(const PounceState* s);

// Its height above the ground in pixels (the leap).
int pounce_height(const PounceState* s);

// A rustle's loudness at a distance in pixels: 0 (not heard) to 4.
int pounce_loudness(int distance);

// What the serval hears of a rustle at `distance`: pounce_loudness(), one
// step louder (up to 5) while it is crouched on the ground, if heard at all.
int pounce_hear(const PounceState* s, int distance);

// The last burst heard, if any is still in mind: the frames left (0: none)
// and the signed pixels from the serval to it (negative: to the left).
int pounce_heard(const PounceState* s, int* dx);

// The seconds left in the round, rounded up.
int pounce_seconds_left(const PounceState* s);

// The fun a round that scored `points` earns (pet_played's points).
u8 pounce_fun(u32 points);

#endif // DEN_POUNCE_RULES_H
