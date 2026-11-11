// Acacia Climb's rules: see climb_rules.h.

#include "climb_rules.h"

#define Q 256
#define UNIT (CLIMB_ROW_H * Q)

static u32 hash3(u32 seed, u32 row, u32 k) {
    u32 h = seed * 0x9E3779B1u ^ (row + 0x7F4A7C15u) * 0x85EBCA77u ^ (k + 1u) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

int climb_ramp(int row) {
    if (row <= CLIMB_WARMUP_ROWS)
        return 0;
    if (row >= CLIMB_HARD_ROW)
        return 256;
    return (row - CLIMB_WARMUP_ROWS) * 256 / (CLIMB_HARD_ROW - CLIMB_WARMUP_ROWS);
}

// `a` at the warm-up's end, `b` at the hardest, by the ramp.
static u32 lerp(u32 a, u32 b, int ramp) {
    return a + (b - a) * (u32)ramp / 256u;
}

static void make_branch(ClimbBranch* b, int row, bool left, int io, int len, u32 r, bool main) {
    *b = (ClimbBranch){0};
    b->inner_right = left; // a bough on the left has the trunk at its right end
    // What the bough does, in percent of the rolls: rare at the warm-up's
    // end, three times as likely (a little less for the other boughs, which
    // are the safety net) from CLIMB_HARD_ROW on. A main bough has a nest only
    // on every third row (never on two rows running: the way up has to stay
    // within the jump's reach).
    int ramp = climb_ramp(row);
    u32 nest = main ? (row % 3 == 2 ? lerp(12, 36, ramp) : 0) : lerp(8, 22, ramp);
    u32 snap = nest + (main ? lerp(15, 45, ramp) : lerp(17, 45, ramp));
    u32 sway = snap + (main ? lerp(8, 24, ramp) : lerp(10, 30, ramp));
    if (row >= 8 && r < nest)
        b->flags = CLIMB_NESTS;
    else if (row >= 5 && r < snap)
        b->flags = CLIMB_SNAPS;
    else if (row >= 3 && r >= snap && r < sway)
        b->flags = CLIMB_SWAYS;
    // A nest's bough sits nearer the trunk: the nest-free part has to stay
    // within reach.
    if ((b->flags & CLIMB_NESTS) && io > CLIMB_NEST_IO_MAX)
        io = CLIMB_NEST_IO_MAX;
    if (left) {
        b->x1 = (s16)(CLIMB_TRUNK_X - io);
        b->x0 = (s16)(b->x1 - len);
    } else {
        b->x0 = (s16)(CLIMB_TRUNK_X + io);
        b->x1 = (s16)(b->x0 + len);
    }
    if (b->flags & CLIMB_NESTS)
        b->nest_x = (s16)(left ? b->x1 - 4 : b->x0 + 4);
}

void climb_row(u32 seed, int row, ClimbRow* out) {
    *out = (ClimbRow){0};
    out->count = 1;
    if (row <= 0) {
        out->b[0] = (ClimbBranch){.x0 = 48, .x1 = 192};
        return;
    }
    u32 h = hash3(seed, (u32)row, 0);
    bool left = (row & 1) == 0;
    int ramp = climb_ramp(row);
    int io_max = 10 + (row / 4 < 6 ? row / 4 : 6) + ramp * (CLIMB_IO_HARD - 16) / 256;
    int io = 6 + (int)((h & 0xFF) * (u32)(io_max - 5) >> 8);
    // Shorter boughs more often, higher up.
    int len = 32 + 16 * (int)((((h >> 8) & 0xFF) * (u32)(256 - ramp / 2) >> 8) * 3u >> 8);
    make_branch(&out->b[0], row, left, io, len, (h >> 16) % 100u, true);
    if (row >= 2 && (hash3(seed, (u32)row, 1) & 1)) {
        u32 g = hash3(seed, (u32)row, 2);
        int io2 = 22 + (int)((g & 0xFF) * 16u >> 8);
        out->count = 2;
        make_branch(&out->b[1], row, !left, io2, 32, (g >> 8) % 100u, false);
    }
}

int climb_sway_period(int row) {
    return CLIMB_SWAY_PERIOD - (CLIMB_SWAY_PERIOD - CLIMB_SWAY_PERIOD_HARD) * climb_ramp(row) / 256;
}

int climb_sway(int row, int idx, u32 frame) {
    u32 period = (u32)climb_sway_period(row);
    u32 phase = (frame + (u32)row * 29u + (u32)idx * 53u) % period;
    int t = (int)(phase < period / 2 ? phase : period - phase);
    int q = (int)(period / 4);
    return (t - q) * CLIMB_SWAY / q;
}

void climb_branch_free(const ClimbBranch* b, bool swing, int* lo, int* hi) {
    *lo = b->x0;
    *hi = b->x1;
    if (b->flags & CLIMB_NESTS) {
        if (b->inner_right)
            *hi = b->nest_x - CLIMB_NEST_SAFE;
        else
            *lo = b->nest_x + CLIMB_NEST_SAFE;
    }
    if (swing && (b->flags & CLIMB_SWAYS)) {
        *lo -= CLIMB_SWAY;
        *hi += CLIMB_SWAY;
    }
}

// --- The jump ---------------------------------------------------------------------------------

// One frame of flight: gravity, the cut of a jump that A is no longer held
// for, the fall's top speed; moves y. The arc climb_jump_reach() measures and
// climb_step() plays.
static void air_step(s32* y, s32* vy, bool a_held) {
    if (*vy < -CLIMB_CUT_V && !a_held)
        *vy = -CLIMB_CUT_V;
    *vy += CLIMB_GRAV;
    if (*vy > CLIMB_FALL_V)
        *vy = CLIMB_FALL_V;
    *y += *vy;
}

int climb_jump_reach(void) {
    s32 y = 0, vy = -CLIMB_JUMP_V, x = 0;
    for (;;) {
        s32 prev = y;
        x += CLIMB_AIR;
        air_step(&y, &vy, true);
        if (vy > 0 && prev <= -UNIT && y >= -UNIT)
            return (int)(x / Q);
        if (vy > 0 && y > 0)
            return 0; // never reaches a row up
    }
}

// --- State -------------------------------------------------------------------------------------

static int slot(ClimbState* s, int row) {
    int k = row & (CLIMB_RING - 1);
    if (s->tag[k] != row) {
        s->tag[k] = (s16)row;
        s->snap[k][0] = s->snap[k][1] = 0;
        s->broken[k] = 0;
    }
    return k;
}

bool climb_broken(const ClimbState* s, int row, int idx) {
    int k = row & (CLIMB_RING - 1);
    return s->tag[k] == row && (s->broken[k] >> idx & 1);
}

int climb_snap_left(const ClimbState* s, int row, int idx) {
    int k = row & (CLIMB_RING - 1);
    return s->tag[k] == row ? s->snap[k][idx] : 0;
}

void climb_stand_on(ClimbState* s, int row, int idx, int x) {
    s->standing = true;
    s->row = (s16)row;
    s->idx = (u8)idx;
    s->x = x * Q;
    s->y = -row * UNIT;
    s->vx = s->vy = s->push = 0;
    if (row > s->rows)
        s->rows = (u16)row;
}

void climb_start(ClimbState* s, u32 seed) {
    *s = (ClimbState){0};
    s->seed = seed;
    for (int k = 0; k < CLIMB_RING; k++)
        s->tag[k] = (s16)-1;
    climb_stand_on(s, 0, 0, CLIMB_TRUNK_X);
    s->cam = -CLIMB_START_FEET;
}

u8 climb_fun(u32 rows) {
    u32 extra = rows / 2;
    return (u8)(CLIMB_FUN_BASE + (extra > CLIMB_FUN_ROWS_MAX ? CLIMB_FUN_ROWS_MAX : extra));
}

// --- A frame -----------------------------------------------------------------------------------

static int sway_of(int row, int idx, const ClimbBranch* b, u32 frame) {
    return b->flags & CLIMB_SWAYS ? climb_sway(row, idx, frame) : 0;
}

// Falling through a row's height: lands on its bough, if there is one under
// the feet.
static bool try_land(ClimbState* s, s32 prev_y) {
    s32 hp = -prev_y, hq = -s->y; // heights, hq <= hp
    if (hp < 0)
        return false;
    int r_hi = (int)(hp / UNIT);
    if (r_hi > CLIMB_ROWS_MAX)
        r_hi = CLIMB_ROWS_MAX; // no boughs above the top
    int r_lo = hq <= 0 ? 0 : (int)((hq + UNIT - 1) / UNIT);
    int px = (int)(s->x / Q);
    for (int r = r_hi; r >= r_lo; r--) {
        ClimbRow row;
        climb_row(s->seed, r, &row);
        int k = slot(s, r);
        for (int i = 0; i < row.count; i++) {
            const ClimbBranch* b = &row.b[i];
            if (s->broken[k] >> i & 1)
                continue;
            int off = sway_of(r, i, b, s->frame);
            if (px < b->x0 + off || px > b->x1 + off)
                continue;
            s->y = -r * UNIT;
            s->vy = 0;
            s->push = 0;
            s->standing = true;
            s->row = (s16)r;
            s->idx = (u8)i;
            if (r > s->rows)
                s->rows = (u16)r;
            s->events |= CLIMB_EV_LAND;
            if (r == CLIMB_ROWS_MAX)
                s->events |= CLIMB_EV_TOP;
            if ((b->flags & CLIMB_SNAPS) && s->snap[k][i] == 0) {
                s->snap[k][i] = CLIMB_SNAP_FRAMES;
                s->events |= CLIMB_EV_CRACK;
                s->ev_row = (s16)r;
                s->ev_idx = (u8)i;
            }
            return true;
        }
    }
    return false;
}

// A nest hanging under a bough, at the serval's body: knocks it down.
static void check_nests(ClimbState* s) {
    int fy = (int)(s->y / Q), px = (int)(s->x / Q);
    int h = -fy; // the feet's height
    // The body spans heights h+2 .. h+18; a nest at row r spans 40r-18 .. 40r-8
    // (it hangs from the bough's underside, 8 to 18 pixels below its top).
    int lo = h + 10, hi = h + 36;
    if (hi < CLIMB_ROW_H)
        return;
    int r_lo = lo <= 0 ? 1 : (lo + CLIMB_ROW_H - 1) / CLIMB_ROW_H;
    int r_hi = hi / CLIMB_ROW_H;
    for (int r = r_lo; r <= r_hi; r++) {
        ClimbRow row;
        climb_row(s->seed, r, &row);
        int k = slot(s, r);
        for (int i = 0; i < row.count; i++) {
            const ClimbBranch* b = &row.b[i];
            if (!(b->flags & CLIMB_NESTS) || (s->broken[k] >> i & 1))
                continue;
            if (px + 4 < b->nest_x - 5 || px - 4 > b->nest_x + 5)
                continue;
            if (s->vy < CLIMB_KNOCK_V)
                s->vy = CLIMB_KNOCK_V; // down at least this fast
            s->push = px < b->nest_x ? -CLIMB_KNOCK_PUSH : CLIMB_KNOCK_PUSH;
            s->knock = 40;
            s->events |= CLIMB_EV_NEST;
            s->ev_row = (s16)r;
            s->ev_idx = (u8)i;
            return;
        }
    }
}

ClimbResult climb_step(ClimbState* s, u16 keys) {
    s->events = 0;
    s->frame++;
    bool a_now = (keys & BUTTON_A) != 0;
    if (a_now && !(s->prev_keys & BUTTON_A))
        s->buffer = CLIMB_BUFFER;
    else if (s->buffer)
        s->buffer--;
    s->prev_keys = keys;
    if (s->knock)
        s->knock--;
    int dir = (keys & BUTTON_RIGHT) ? 1 : (keys & BUTTON_LEFT) ? -1 : 0;

    // Dry boughs count down to their break.
    for (int k = 0; k < CLIMB_RING; k++)
        for (int i = 0; i < 2; i++)
            if (s->snap[k][i] && !(s->broken[k] >> i & 1) && --s->snap[k][i] == 0) {
                s->broken[k] |= (u8)(1u << i);
                s->events |= CLIMB_EV_SNAP;
                s->ev_row = s->tag[k];
                s->ev_idx = (u8)i;
                if (s->standing && s->row == s->tag[k] && s->idx == i) {
                    s->standing = false;
                    s->vy = 0;
                }
            }

    if (s->standing) {
        ClimbRow row;
        climb_row(s->seed, s->row, &row);
        const ClimbBranch* b = &row.b[s->idx];
        int off = sway_of(s->row, s->idx, b, s->frame);
        int was = sway_of(s->row, s->idx, b, s->frame - 1);
        s->x += (off - was) * Q + dir * CLIMB_WALK;
        if (s->x < CLIMB_X_MIN * Q)
            s->x = CLIMB_X_MIN * Q;
        if (s->x > CLIMB_X_MAX * Q)
            s->x = CLIMB_X_MAX * Q;
        int px = (int)(s->x / Q);
        if (px < b->x0 + off || px > b->x1 + off) {
            s->standing = false; // walked off its end
            s->vy = 0;
        } else if (s->buffer) {
            s->buffer = 0;
            s->standing = false;
            s->vy = -CLIMB_JUMP_V;
            s->events |= CLIMB_EV_JUMP;
        } else {
            return CLIMB_PLAYING;
        }
    }

    // In the air.
    s->x += dir * CLIMB_AIR + s->push;
    s->push -= s->push / 4;
    if (s->x < CLIMB_X_MIN * Q)
        s->x = CLIMB_X_MIN * Q;
    if (s->x > CLIMB_X_MAX * Q)
        s->x = CLIMB_X_MAX * Q;
    s32 prev = s->y;
    air_step(&s->y, &s->vy, a_now);
    if (s->vy > 0)
        try_land(s, prev);
    if (!s->standing && !s->knock)
        check_nests(s);

    if (s->events & CLIMB_EV_TOP)
        return CLIMB_TOP;
    // The camera follows upward only, easing.
    s32 target = s->y / Q - CLIMB_CAM_FEET;
    if (target < s->cam)
        s->cam -= (s->cam - target + 3) / 4;
    if (s->y / Q - s->cam > CLIMB_FALL_LINE) {
        s->events |= CLIMB_EV_OVER;
        return CLIMB_OVER;
    }
    return CLIMB_PLAYING;
}
