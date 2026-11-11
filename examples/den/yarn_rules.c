// Yarn Bat's rules: see yarn_rules.h.

#include "yarn_rules.h"

#define PAW_MIN (YARN_CEIL_Y + YARN_PADDLE_H / 2)
#define PAW_MAX (YARN_FLOOR_Y - YARN_PADDLE_H / 2)
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

void yarn_start(YarnState* s, u32 seed) {
    *s = (YarnState){0};
    s->rng = seed ^ 0x9E3779B9u;
    s->paw_y = (s16)((YARN_CEIL_Y + YARN_FLOOR_Y) / 2);
    s->x = 120 * Q;
    s->y = (YARN_CEIL_Y + 24 + (s32)(next_random(s) % 80)) * Q;
    s32 speed = yarn_speed(0);
    s32 vy = (s32)(next_random(s) % 96) + 32; // a slant of 0.1 to 0.5 px/frame
    if (next_random(s) & 1)
        vy = -vy;
    s->vy = vy;
    s->vx = (s32)isqrt((u32)(speed * speed - vy * vy)); // toward the far wall
    s->wait = YARN_SERVE_WAIT;
}

// The ball leaves the paw: faster by the bats so far, at an angle by where
// it met the paw.
static void bat(YarnState* s) {
    s32 speed = yarn_speed(s->bats);
    s32 off = (s->y >> 8) - s->paw_y; // -(H/2 + R) .. H/2 + R
    s32 lim = YARN_PADDLE_H / 2 + 1;
    if (off > lim)
        off = lim;
    if (off < -lim)
        off = -lim;
    s32 vy = speed * off * 205 / (lim * 256); // up to 0.8 of the speed: 53 degrees
    s->vy = vy;
    s->vx = (s32)isqrt((u32)(speed * speed - vy * vy));
}

YarnResult yarn_step(YarnState* s, u16 keys) {
    s->events = 0;
    if (keys & BUTTON_UP)
        s->paw_y = (s16)(s->paw_y - YARN_PADDLE_SPEED);
    if (keys & BUTTON_DOWN)
        s->paw_y = (s16)(s->paw_y + YARN_PADDLE_SPEED);
    if (s->paw_y < PAW_MIN)
        s->paw_y = PAW_MIN;
    if (s->paw_y > PAW_MAX)
        s->paw_y = PAW_MAX;
    if (s->wait) {
        s->wait--;
        return YARN_PLAYING;
    }
    s->x += s->vx;
    s->y += s->vy;

    // The far wall, the ceiling and the floor.
    s32 right = (YARN_WALL_X - YARN_BALL_R) * Q;
    if (s->x > right) {
        s->x = 2 * right - s->x;
        s->vx = -s->vx;
        s->events |= YARN_EV_WALL;
    }
    s32 top = (YARN_CEIL_Y + YARN_BALL_R) * Q, bottom = (YARN_FLOOR_Y - YARN_BALL_R) * Q;
    if (s->y < top) {
        s->y = 2 * top - s->y;
        s->vy = -s->vy;
        s->events |= YARN_EV_WALL;
    }
    if (s->y > bottom) {
        s->y = 2 * bottom - s->y;
        s->vy = -s->vy;
        s->events |= YARN_EV_WALL;
    }

    // The paw.
    if (!s->missed && s->vx < 0 && s->x - YARN_BALL_R * Q <= YARN_PAW_X * Q) {
        s32 off = (s->y >> 8) - s->paw_y;
        if (off < 0)
            off = -off;
        if (off <= YARN_PADDLE_H / 2 + YARN_BALL_R / 2) {
            s->x = (YARN_PAW_X + YARN_BALL_R) * Q;
            s->bats++;
            bat(s);
            s->events |= YARN_EV_BAT;
        } else {
            s->missed = true;
            s->events |= YARN_EV_MISS;
        }
    }
    return s->missed && s->x < -YARN_BALL_R * Q ? YARN_OVER : YARN_PLAYING;
}
