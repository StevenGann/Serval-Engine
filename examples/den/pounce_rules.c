// High Pounce's rules: see pounce_rules.h.

#include "pounce_rules.h"

#define Q 256

static u32 next_random(PounceState* s) {
    s->rng = s->rng * 1664525u + 1013904223u;
    return s->rng >> 8;
}

// 0 to n-1.
static int rnd(PounceState* s, int n) {
    return (int)((next_random(s) & 0xFFFF) * (u32)n >> 16);
}

static int range(PounceState* s, int lo, int hi) {
    return lo + rnd(s, hi - lo + 1);
}

static int iabs(int v) {
    return v < 0 ? -v : v;
}

s32 pounce_leap_dx(int t) {
    return POUNCE_REACH * Q * t / POUNCE_LEAP_FRAMES;
}

int pounce_leap_h(int t) {
    return POUNCE_PEAK * 4 * t * (POUNCE_LEAP_FRAMES - t) /
           (POUNCE_LEAP_FRAMES * POUNCE_LEAP_FRAMES);
}

u8 pounce_fun(u32 points) {
    return (u8)(POUNCE_FUN_BASE +
                (points > POUNCE_FUN_POINTS_MAX ? POUNCE_FUN_POINTS_MAX : points));
}

int pounce_x(const PounceState* s) {
    return (int)(s->x / Q);
}

int pounce_height(const PounceState* s) {
    return s->leap ? pounce_leap_h(s->leap) : 0;
}

int pounce_loudness(int d) {
    d = iabs(d);
    return d >= POUNCE_HEAR_R ? 0 : d >= 96 ? 1 : d >= 56 ? 2 : d >= 28 ? 3 : 4;
}

int pounce_hear(const PounceState* s, int distance) {
    int l = pounce_loudness(distance);
    return l && s->crouch && !s->leap ? l + 1 : l;
}

int pounce_heard(const PounceState* s, int* dx) {
    if (!s->heard_t)
        return 0;
    if (dx)
        *dx = s->heard_x - pounce_x(s);
    return s->heard_t;
}

int pounce_seconds_left(const PounceState* s) {
    int left = POUNCE_ROUND_FRAMES - (int)s->frame;
    return left <= 0 ? 0 : (left + 59) / 60;
}

static void arm(PounceState* s, PounceRustle* r) {
    r->pulse = (u16)range(s, POUNCE_PULSE_MIN, POUNCE_PULSE_MAX);
    r->burst = 0;
}

int pounce_put(PounceState* s, int kind, int x_pixels) {
    for (int i = 0; i < POUNCE_SLOTS; i++)
        if (s->r[i].state == PR_FREE) {
            PounceRustle* r = &s->r[i];
            *r = (PounceRustle){0};
            r->state = PR_ACTIVE;
            r->kind = (u8)kind;
            r->x = x_pixels * Q;
            r->life = (u16)range(s, POUNCE_LIFE_MIN, POUNCE_LIFE_MAX);
            r->turn = (u16)range(s, 40, 100);
            arm(s, r);
            return i;
        }
    return -1;
}

static void spawn(PounceState* s) {
    for (int tries = 0; tries < 8; tries++) {
        int x = range(s, 16, 224);
        bool ok = iabs(x - pounce_x(s)) >= POUNCE_SPAWN_GAP;
        for (int i = 0; ok && i < POUNCE_SLOTS; i++)
            if (s->r[i].state != PR_FREE && s->r[i].state != PR_FLY &&
                iabs(x - (int)(s->r[i].x / Q)) < 28)
                ok = false;
        if (ok) {
            pounce_put(s, rnd(s, 4) == 0 ? PK_BIRD : PK_MOUSE, x);
            return;
        }
    }
}

void pounce_start(PounceState* s, u32 seed) {
    *s = (PounceState){0};
    s->rng = seed ^ 0x9E3779B9u;
    s->x = 120 * Q;
    s->face = 1;
    for (int i = 0; i < 2; i++)
        spawn(s);
    s->spawn_wait = 60;
}

static void flush_bird(PounceState* s, PounceRustle* r) {
    int serval = pounce_x(s), at = (int)(r->x / Q);
    r->state = PR_FLY;
    r->h = 0;
    r->vx = (s16)(at >= serval ? POUNCE_BIRD_SPEED : -POUNCE_BIRD_SPEED);
    r->timer = 0;
    s->events |= POUNCE_EV_FLUSH;
    if (s->leap)
        s->hit = true; // startled in the air: the leap was not for nothing
    s->ev_x = (s16)at;
}

static void land(PounceState* s) {
    s->leap = 0;
    s->recover = POUNCE_RECOVER;
    s->events |= POUNCE_EV_LAND;
    s->x = s->leap_x0 + s->leap_dir * pounce_leap_dx(POUNCE_LEAP_FRAMES);
    if (s->x < POUNCE_X_MIN * Q)
        s->x = POUNCE_X_MIN * Q;
    if (s->x > POUNCE_X_MAX * Q)
        s->x = POUNCE_X_MAX * Q;
    int at = pounce_x(s);
    s->ev_x = (s16)at;
    // The nearest mouse within the catch radius.
    int best = -1, best_d = POUNCE_CATCH_R + 1;
    for (int i = 0; i < POUNCE_SLOTS; i++) {
        PounceRustle* r = &s->r[i];
        int d = iabs((int)(r->x / Q) - at);
        if (r->state == PR_ACTIVE && r->kind == PK_MOUSE && d < best_d) {
            best = i;
            best_d = d;
        }
    }
    bool hit = s->hit;
    if (best >= 0) {
        s->r[best].state = PR_FREE;
        s->points++;
        s->mice++;
        s->events |= POUNCE_EV_CATCH;
        hit = true;
    }
    // A bird under the landing takes off (it will be swatted by the next A).
    for (int i = 0; i < POUNCE_SLOTS; i++) {
        PounceRustle* r = &s->r[i];
        if (r->state == PR_ACTIVE && r->kind == PK_BIRD &&
            iabs((int)(r->x / Q) - at) <= POUNCE_CATCH_R) {
            flush_bird(s, r);
            hit = true;
        }
    }
    s->hit = false;
    if (hit)
        return;
    for (int i = 0; i < POUNCE_SLOTS; i++) {
        PounceRustle* r = &s->r[i];
        if (r->state == PR_ACTIVE && iabs((int)(r->x / Q) - at) <= POUNCE_SCARE_R) {
            r->state = PR_SCARED;
            r->timer = POUNCE_SCARE_FRAMES;
            r->burst = 0;
        }
    }
    s->events |= POUNCE_EV_MISS;
}

// A leap, from A.
static void start_leap(PounceState* s) {
    int at = pounce_x(s);
    // A bird in the air within reach is swatted at once, and the leap turns to it.
    int best = -1, best_d = POUNCE_REACH + 1;
    for (int i = 0; i < POUNCE_SLOTS; i++) {
        PounceRustle* r = &s->r[i];
        int d = iabs((int)(r->x / Q) - at);
        if (r->state == PR_FLY && d < best_d && r->h / Q <= POUNCE_SWAT_H) {
            best = i;
            best_d = d;
        }
    }
    if (best >= 0) {
        PounceRustle* r = &s->r[best];
        s->face = (s8)((int)(r->x / Q) >= at ? 1 : -1);
        s->ev_x = (s16)(r->x / Q);
        s->ev_h = (s16)(r->h / Q);
        r->state = PR_FREE;
        s->points += 3;
        s->birds++;
        s->hit = true;
        s->events |= POUNCE_EV_SWAT;
    }
    s->crouch = false;
    s->leap = 1;
    s->leap_dir = s->face;
    s->leap_x0 = s->x;
    s->events |= POUNCE_EV_LEAP;
}

static void rustles(PounceState* s) {
    int at = pounce_x(s);
    bool hidden = s->crouch && s->leap == 0;
    int heard_d = POUNCE_HEAR_R + 1;
    int active = 0;
    for (int i = 0; i < POUNCE_SLOTS; i++) {
        PounceRustle* r = &s->r[i];
        int rx = (int)(r->x / Q);
        switch (r->state) {
        case PR_ACTIVE:
            if (r->kind == PK_BIRD && !hidden && iabs(rx - at) <= POUNCE_FLUSH_R) {
                flush_bird(s, r);
                break;
            }
            if (r->life && --r->life == 0) {
                r->state = PR_FREE;
                break;
            }
            active++;
            if (r->kind == PK_MOUSE) {
                if (r->turn && --r->turn == 0) {
                    r->turn = (u16)range(s, 40, 100);
                    r->vx = (s16)((rnd(s, 3) - 1) * POUNCE_MOUSE_SPEED);
                }
                r->x += r->vx;
                if (r->x < 12 * Q || r->x > 228 * Q) {
                    r->x = r->x < 12 * Q ? 12 * Q : 228 * Q;
                    r->vx = (s16)-r->vx;
                }
            }
            if (r->burst)
                r->burst--;
            if (r->pulse && --r->pulse == 0) {
                s->bursts |= (u8)(1u << i);
                r->burst = POUNCE_BURST_FRAMES;
                r->pulse =
                    (u16)(POUNCE_BURST_FRAMES + range(s, POUNCE_PULSE_MIN, POUNCE_PULSE_MAX));
                int d = iabs(rx - at);
                if (d <= POUNCE_HEAR_R && d < heard_d) {
                    heard_d = d;
                    s->heard_x = (s16)rx;
                    s->heard_t = POUNCE_HEARD_FRAMES;
                }
            }
            break;
        case PR_SCARED:
            active++;
            if (r->life && --r->life == 0)
                r->state = PR_FREE;
            else if (--r->timer == 0) {
                r->state = PR_ACTIVE;
                arm(s, r);
            }
            break;
        case PR_FLY:
            r->x += r->vx;
            if (r->h < POUNCE_BIRD_H * Q) {
                r->h += POUNCE_BIRD_RISE;
                if (r->h > POUNCE_BIRD_H * Q)
                    r->h = POUNCE_BIRD_H * Q;
            }
            r->timer++;
            if (r->x < -16 * Q || r->x > 256 * Q)
                r->state = PR_FREE;
            break;
        default:
            break;
        }
    }
    if (!s->quiet) {
        if (s->spawn_wait)
            s->spawn_wait--;
        else if (active < POUNCE_ACTIVE) {
            spawn(s);
            s->spawn_wait = (u16)range(s, 30, 90);
        }
    }
}

PounceResult pounce_step(PounceState* s, u16 keys) {
    s->events = 0;
    s->bursts = 0;
    s->frame++;
    u16 pressed = (u16)(keys & ~s->prev_keys);
    s->prev_keys = keys;
    if (pressed & BUTTON_A)
        s->a_at = s->frame;
    if (s->leap) {
        s->leap++;
        s->x = s->leap_x0 + s->leap_dir * pounce_leap_dx(s->leap);
        if (s->x < POUNCE_X_MIN * Q)
            s->x = POUNCE_X_MIN * Q;
        if (s->x > POUNCE_X_MAX * Q)
            s->x = POUNCE_X_MAX * Q;
        if (s->leap >= POUNCE_LEAP_FRAMES)
            land(s);
    } else if (s->recover) {
        s->recover--;
        s->crouch = false;
    } else {
        s->crouch = (keys & BUTTON_B) != 0;
        int speed = s->crouch ? POUNCE_CROUCH : POUNCE_WALK;
        if (keys & BUTTON_LEFT) {
            s->face = -1;
            s->x -= speed;
        } else if (keys & BUTTON_RIGHT) {
            s->face = 1;
            s->x += speed;
        }
        if (s->x < POUNCE_X_MIN * Q)
            s->x = POUNCE_X_MIN * Q;
        if (s->x > POUNCE_X_MAX * Q)
            s->x = POUNCE_X_MAX * Q;
        if (s->a_at && s->frame - s->a_at <= POUNCE_BUFFER) {
            s->a_at = 0;
            start_leap(s);
        }
    }
    rustles(s);
    if (s->heard_t)
        s->heard_t--;
    return s->frame >= POUNCE_ROUND_FRAMES ? POUNCE_OVER : POUNCE_PLAYING;
}
