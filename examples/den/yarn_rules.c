// Yarn Bat's rules: see yarn_rules.h.

#include "yarn_rules.h"

#define Q 256

static u32 next_random(YarnState* s) {
    s->rng = s->rng * 1664525u + 1013904223u;
    return s->rng >> 8;
}

// The integer square root (no floating point, no division).
static u32 isqrt(u32 n) {
    u32 r = 0, bit = 1u << 30;
    while (bit > n)
        bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

s32 yarn_speed(u32 bats) {
    u32 steps = bats / YARN_BATS_PER_STEP;
    u32 v = YARN_SPEED_START + steps * YARN_SPEED_STEP;
    return (s32)(v > YARN_SPEED_MAX ? YARN_SPEED_MAX : v);
}

u8 yarn_fun(u32 bats) {
    return (u8)(YARN_FUN_BASE + (bats > YARN_FUN_BATS_MAX ? YARN_FUN_BATS_MAX : bats));
}

void yarn_set_reach(YarnState* s, int w, int line_y) {
    s->paw_w = (s16)(w < 8 ? 8 : w > 48 ? 48 : w);
    s->paw_y = (s16)(line_y < YARN_CEIL_Y + 40    ? YARN_CEIL_Y + 40
                     : line_y > YARN_FLOOR_Y - 16 ? YARN_FLOOR_Y - 16
                                                  : line_y);
}

void yarn_start(YarnState* s, u32 seed) {
    *s = (YarnState){0};
    s->rng = seed ^ 0x9E3779B9u;
    s->paw_w = YARN_PADDLE_W;
    s->paw_y = YARN_PAW_Y;
    s->paw_x = (s16)((YARN_LEFT_X + YARN_WALL_X) / 2);
    s->x = (YARN_LEFT_X + 40 + (s32)(next_random(s) % 130)) * Q;
    s->y = (YARN_CEIL_Y + 12) * Q;
    s32 speed = yarn_speed(0);
    s32 vx = (s32)(next_random(s) % 96) + 32; // a slant of 0.1 to 0.5 px/frame
    if (next_random(s) & 1)
        vx = -vx;
    s->vx = vx;
    s->vy = (s32)isqrt((u32)(speed * speed - vx * vx)); // down toward the pet
    s->wait = YARN_SERVE_WAIT;
}

// The ball leaves the paw: faster by the bats so far, at an angle by where
// it met the paw.
static void bat(YarnState* s) {
    s32 speed = yarn_speed(s->bats);
    s32 off = (s->x >> 8) - s->paw_x; // -(W/2 + R) .. W/2 + R
    s32 lim = s->paw_w / 2 + 1;
    if (off > lim)
        off = lim;
    if (off < -lim)
        off = -lim;
    s32 vx = speed * off * 205 / (lim * 256); // up to 0.8 of the speed: 53 degrees
    s->vx = vx;
    s->vy = -(s32)isqrt((u32)(speed * speed - vx * vx));
}

YarnResult yarn_step(YarnState* s, u16 keys) {
    s->events = 0;
    if (keys & BUTTON_LEFT)
        s->paw_x = (s16)(s->paw_x - YARN_PADDLE_SPEED);
    if (keys & BUTTON_RIGHT)
        s->paw_x = (s16)(s->paw_x + YARN_PADDLE_SPEED);
    s16 paw_min = (s16)(YARN_LEFT_X + YARN_PET_HALF), paw_max = (s16)(YARN_WALL_X - YARN_PET_HALF);
    if (s->paw_x < paw_min)
        s->paw_x = paw_min;
    if (s->paw_x > paw_max)
        s->paw_x = paw_max;
    if (s->wait) {
        s->wait--;
        return YARN_PLAYING;
    }
    s->x += s->vx;
    s->y += s->vy;

    // The two walls and the ceiling.
    s32 left = (YARN_LEFT_X + YARN_BALL_R) * Q, right = (YARN_WALL_X - YARN_BALL_R) * Q;
    if (s->x > right) {
        s->x = 2 * right - s->x;
        s->vx = -s->vx;
        s->events |= YARN_EV_WALL;
    }
    if (s->x < left) {
        s->x = 2 * left - s->x;
        s->vx = -s->vx;
        s->events |= YARN_EV_WALL;
    }
    s32 top = (YARN_CEIL_Y + YARN_BALL_R) * Q;
    if (s->y < top) {
        s->y = 2 * top - s->y;
        s->vy = -s->vy;
        s->events |= YARN_EV_WALL;
    }

    // The paw's line.
    if (!s->missed && s->vy > 0 && s->y + YARN_BALL_R * Q >= s->paw_y * Q) {
        s32 off = (s->x >> 8) - s->paw_x;
        if (off < 0)
            off = -off;
        if (off <= s->paw_w / 2 + YARN_BALL_R / 2) {
            s->y = (s->paw_y - YARN_BALL_R) * Q;
            s->bats++;
            bat(s);
            s->events |= YARN_EV_BAT;
        } else {
            s->missed = true;
            s->events |= YARN_EV_MISS;
        }
    }
    // A missed ball lands on the floor and stays: the round is over.
    if (s->missed && s->y >= (YARN_FLOOR_Y - YARN_BALL_R) * Q) {
        s->y = (YARN_FLOOR_Y - YARN_BALL_R) * Q;
        s->vx = s->vy = 0;
        return YARN_OVER;
    }
    return YARN_PLAYING;
}
