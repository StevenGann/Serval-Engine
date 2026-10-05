// The table on screen: cards that slide in from the shoe, flip, tilt and
// wobble; the bet's chip stack and chips flying between it and the bank;
// banners whose letters pop in; rising number pops; sparks and confetti.
// game.c says what happens; this file makes it move.
//
// Motion is hand-rolled: tweens with easing for positions, and a damped
// spring for each card's tilt. The engine has no tween or easing helpers
// (each card game would want the same few: ease-out-back for a deal,
// ease-in for a discard, a spring for wobble).
//
// Rotation: a card is several hardware sprites (art.c), so tilting it means
// rotating each piece about the card's center: its offset from the center
// turns by the angle (fx_cos, fx_sin) and the piece is drawn rotated by the
// same angle, so all pieces of a card share one rotation matrix (two: the
// bottom-right index is flipped, and flips are part of a matrix). The
// hardware has 32 matrices, so angles are rounded to 2 degrees, and a
// rotated sprite costs its double-size box in the per-scanline sprite budget
// (about 4 times an unrotated one): at most MAX_TILTED cards are drawn
// tilted at once, the rest straight.

#include "game.h"

#define DEAL_FRAMES 20
#define FLIP_STEP 2 // frames per flip animation frame
#define MAX_TILTED 3
#define SHADOW_DX 3
#define SHADOW_DY 4

// What a card's base is drawn with: one 32x64 sprite, or for the narrow
// flip steps a top piece (card rows 0-31) over a bottom one (rows 32-47).
typedef struct {
    u8 sprite, bottom, frame, width; // bottom: 0 for a one-piece base
    bool face;
} Base;

// The flip, back to face: the squashed steps between them, each as wide as
// it needs.
static const Base flip_frames[] = {
    {SPR_CARD, 0, CF_BACK_24, 32, false},
    {SPR_NARROW_TOP, SPR_NARROW_BOTTOM, NF_BACK_14, 16, false},
    {SPR_EDGE_TOP, SPR_EDGE_BOTTOM, 0, 8, false},
    {SPR_NARROW_TOP, SPR_NARROW_BOTTOM, NF_FACE_14, 16, false},
    {SPR_CARD, 0, CF_FACE_24, 32, false},
};
#define FLIP_STEPS ((int)(sizeof flip_frames / sizeof flip_frames[0]))
#define FLIP_LENGTH (FLIP_STEPS * FLIP_STEP)

typedef struct {
    bool used;
    bool discarding; // flying off; freed when it gets there
    u8 card;
    s16 order; // higher is in front
    // Position: the card's top-left, in pixels (24.8), tweened from -> to.
    FIXED x, y, from_x, from_y, to_x, to_y;
    s16 t, frames, delay;
    bool ease_in;      // discards speed up; deals overshoot and settle
    bool face_up;      // the side shown once a flip is over
    bool flip_pending; // flip once the deal is under way
    s16 flip;          // frames left in a flip; 0 when not flipping
    s16 lift, lift_target;
    bool glow;
    // Tilt: a spring in 1/256 degrees toward rest_angle.
    int angle, angle_vel, rest_angle;
} TableCard;

static TableCard cards[TABLE_CARDS];
static s16 next_order;

// --- Easing ------------------------------------------------------------------

// p from 0 to 256 (24.8); results in 24.8 too.
static int ease_out_back(int p) {
    int q = p - 256;
    int q2 = (q * q) >> 8, q3 = (q2 * q) >> 8;
    return 256 + ((692 * q3 + 436 * q2) >> 8); // c3 = 2.70158, c1 = 1.70158
}

static int ease_in(int p) {
    return (p * p) >> 8;
}

static int ease_out(int p) {
    int q = 256 - p;
    return 256 - ((q * q) >> 8);
}

// --- Cards -------------------------------------------------------------------

void table_reset(void) {
    for (int i = 0; i < TABLE_CARDS; i++)
        cards[i].used = false;
    next_order = 0;
}

static int alloc_card(void) {
    for (int i = 0; i < TABLE_CARDS; i++)
        if (!cards[i].used)
            return i;
    return -1;
}

int table_deal(u8 card, int x, int y, bool face_up, int delay) {
    return table_deal_from(card, SHOE_X, SHOE_Y, x, y, face_up, delay);
}

int table_deal_from(u8 card, int from_x, int from_y, int x, int y, bool face_up, int delay) {
    int id = alloc_card();
    if (id < 0)
        return -1;
    TableCard* c = &cards[id];
    *c = (TableCard){0};
    c->used = true;
    c->card = card;
    c->order = next_order++;
    c->x = c->from_x = FX(from_x);
    c->y = c->from_y = FX(from_y);
    c->to_x = FX(x);
    c->to_y = FX(y);
    c->frames = DEAL_FRAMES;
    c->delay = (s16)delay;
    c->flip_pending = face_up; // it leaves the shoe face down
    c->lift = 6;               // in the air
    return id;
}

void table_move(int id, int x, int y, int frames) {
    if (id < 0)
        return;
    TableCard* c = &cards[id];
    c->from_x = c->x;
    c->from_y = c->y;
    c->to_x = FX(x);
    c->to_y = FX(y);
    c->t = 0;
    c->frames = (s16)frames;
    c->ease_in = false;
}

void table_flip(int id) {
    if (id < 0)
        return;
    cards[id].face_up = !cards[id].face_up;
    cards[id].flip = FLIP_LENGTH;
    psg_play(SND_FLIP);
}

void table_lift(int id, int pixels) {
    if (id >= 0)
        cards[id].lift_target = (s16)pixels;
}

void table_glow(int id, bool on) {
    if (id >= 0)
        cards[id].glow = on;
}

void table_nudge(int id, int strength) {
    if (id >= 0)
        cards[id].angle_vel += strength * 256;
}

void table_set_angle(int id, int angle) {
    if (id >= 0)
        cards[id].rest_angle = angle * 256;
}

void table_discard_all(void) {
    int n = 0;
    for (int i = 0; i < TABLE_CARDS; i++) {
        TableCard* c = &cards[i];
        if (!c->used || c->discarding)
            continue;
        table_move(i, -48, fx_to_int(c->y) - 24, 22);
        c->ease_in = true;
        c->delay = (s16)(n++ * 2);
        c->discarding = true;
        c->angle_vel -= 6 * 256; // a flick as they go
    }
}

bool table_shown(int id) {
    const TableCard* c = &cards[id];
    return id >= 0 && c->used && c->face_up && !c->flip && !c->delay && c->t >= c->frames;
}

bool table_busy(void) {
    for (int i = 0; i < TABLE_CARDS; i++) {
        const TableCard* c = &cards[i];
        if (c->used && !c->discarding &&
            (c->delay || c->t < c->frames || c->flip || c->flip_pending))
            return true;
    }
    return false;
}

static void update_card(TableCard* c) {
    if (c->delay > 0) {
        if (--c->delay == 0 && c->t == 0 && !c->discarding)
            psg_play(SND_DEAL);
        return;
    }
    FIXED old_x = c->x;
    if (c->t < c->frames) {
        c->t++;
        int p = c->t * 256 / c->frames;
        int e = c->ease_in ? ease_in(p) : c->discarding ? ease_out(p) : ease_out_back(p);
        c->x = c->from_x + (int)(((s32)(c->to_x - c->from_x) * e) >> 8);
        c->y = c->from_y + (int)(((s32)(c->to_y - c->from_y) * e) >> 8);
        // Tilted by its speed, as if dragged by its leading edge.
        int speed = (c->x - old_x) >> 4; // pixels per frame * 16
        c->angle_vel += speed * 6;
        if (c->t == c->frames) {
            if (c->discarding) {
                c->used = false;
                return;
            }
            c->lift_target = 0;
            c->angle_vel += 5 * 256; // lands with a wobble
            psg_play(SND_LAND);
        }
        // A face-up deal turns over in the air, past halfway.
        if (c->flip_pending && c->t * 2 >= c->frames) {
            c->flip_pending = false;
            table_flip((int)(c - cards));
        }
    }
    if (c->flip > 0)
        c->flip--;
    if (c->lift != c->lift_target)
        c->lift = (s16)(c->lift + (c->lift < c->lift_target ? 1 : -1));
    // The tilt spring: pulled back to rest, damped.
    c->angle_vel += (c->rest_angle - c->angle) * 40 / 256;
    c->angle_vel = c->angle_vel * 215 / 256;
    c->angle += c->angle_vel;
    c->angle = int_clamp(c->angle, -20 * 256, 20 * 256);
}

// --- Sparks and confetti -----------------------------------------------------

static u8 spark_life[MAX_ENT];
#define SPARK_RESERVE 8 // entities left free for anything else

void sparks_burst(int x, int y, int count, bool confetti) {
    for (int n = 0; n < count; n++) {
        if ((int)ecs_free_count() <= SPARK_RESERVE)
            return;
        u32 mask = C_POS | C_VEL | C_SPR | C_SPARK | (confetti ? 0 : C_ANIM);
        Entity e = entity_create(mask);
        u32 i = entity_index(e);
        // One random call per statement: argument order differs between
        // compilers, and the web build must play the same game.
        u16 heading = (u16)random_range(0, 65535);
        int speed = random_range(confetti ? 300 : 200, confetti ? 700 : 500);
        pos_x[i] = FX(x);
        pos_y[i] = FX(y);
        vel_x[i] = (fx_cos(heading) * speed) >> 8;
        vel_y[i] = ((fx_sin(heading) * speed) >> 8) - (confetti ? FX(2) : FX(1) / 2);
        spr_id[i] = confetti ? SPR_CONFETTI : SPR_SPARK;
        spr_frame[i] = confetti ? (u8)random_range(0, CONFETTI_COLORS - 1) : 0;
        spr_flags[i] = SPRITE_ABOVE_FOREGROUND;
        spark_life[i] = (u8)(confetti ? random_range(50, 80) : 19);
    }
}

static void update_sparks(void) {
    ECS_FOR_EACH(i, C_SPARK) {
        if (--spark_life[i] == 0) {
            entity_destroy(entity_at(i));
            continue;
        }
        if (spr_id[i] == SPR_CONFETTI) {
            vel_y[i] = int_min(vel_y[i] + FX(1) / 10, FX(1)); // falls, flutters
            vel_x[i] = vel_x[i] * 31 / 32;
            spr_angle[i] =
                (u16)(((frame_count() / 4 + i) % 4) * ANGLE_DEG(45)); // tumbles: 4 angles
        } else {
            vel_x[i] = vel_x[i] * 7 / 8;
            vel_y[i] = vel_y[i] * 7 / 8;
        }
    }
}

// --- Chips -------------------------------------------------------------------

static const int chip_values[CHIP_KINDS] = {10, 50, 100, 500};
#define STACK_MAX 10
#define CHIP_STEP 3 // pixels between chips in a stack

static struct {
    int amount, x, y;
    u8 kinds[STACK_MAX];
    s8 drop[STACK_MAX];  // pixels above its place, falling in
    u8 delay[STACK_MAX]; // frames before it appears (chips flying over first)
    int count;
} stack;

// Splits an amount into chips, biggest first (at most `max`).
static int make_chips(int amount, u8* kinds, int max) {
    int n = 0;
    for (int k = CHIP_KINDS - 1; k >= 0 && n < max; k--)
        while (amount >= chip_values[k] && n < max) {
            kinds[n++] = (u8)k;
            amount -= chip_values[k];
        }
    return n;
}

void chips_set(int amount, int x, int y, int delay) {
    u8 kinds[STACK_MAX];
    int n = make_chips(amount, kinds, STACK_MAX);
    // Chips the stack keeps stay put; new ones drop in from above, one
    // after the other.
    int added = 0;
    for (int i = 0; i < n; i++) {
        bool same = i < stack.count && stack.kinds[i] == kinds[i] && stack.x == x && stack.y == y;
        if (!same) {
            stack.drop[i] = 12;
            stack.delay[i] = (u8)(delay + 2 * added++);
        }
        stack.kinds[i] = kinds[i];
    }
    stack.count = n;
    stack.amount = amount;
    stack.x = x;
    stack.y = y;
}

#define FLYING_MAX 12
static struct {
    bool used;
    u8 kind;
    s16 delay, t;
    s16 from_x, from_y, to_x, to_y;
} flying[FLYING_MAX];
#define FLY_FRAMES 24

void chips_fly(int amount, int from_x, int from_y, int to_x, int to_y) {
    u8 kinds[6];
    int n = make_chips(amount, kinds, 6);
    for (int i = 0, slot = 0; i < n; i++) {
        while (slot < FLYING_MAX && flying[slot].used)
            slot++;
        if (slot == FLYING_MAX)
            return;
        flying[slot].used = true;
        flying[slot].kind = kinds[i];
        flying[slot].delay = (s16)(i * 4);
        flying[slot].t = 0;
        flying[slot].from_x = (s16)from_x;
        flying[slot].from_y = (s16)(from_y - i * CHIP_STEP);
        flying[slot].to_x = (s16)to_x;
        flying[slot].to_y = (s16)to_y;
    }
    if (n)
        psg_play(SND_CHIPS);
}

static void update_chips(void) {
    for (int i = 0; i < stack.count; i++) {
        if (stack.delay[i] > 0)
            stack.delay[i]--;
        else if (stack.drop[i] > 0)
            stack.drop[i] = (s8)int_max(0, stack.drop[i] - 3);
    }
    for (int i = 0; i < FLYING_MAX; i++) {
        if (!flying[i].used)
            continue;
        if (flying[i].delay > 0)
            flying[i].delay--;
        else if (++flying[i].t >= FLY_FRAMES)
            flying[i].used = false;
    }
}

void chips_draw(void) {
    // Flying chips first: in front of everything on the table. They arc up.
    for (int i = 0; i < FLYING_MAX; i++) {
        if (!flying[i].used || flying[i].delay)
            continue;
        int p = ease_out(flying[i].t * 256 / FLY_FRAMES);
        int x = flying[i].from_x + (((flying[i].to_x - flying[i].from_x) * p) >> 8);
        int y = flying[i].from_y + (((flying[i].to_y - flying[i].from_y) * p) >> 8);
        y -= (fx_sin((u16)(flying[i].t * 32768 / FLY_FRAMES)) * 24) >> 8;
        sprite_draw(SPR_CHIP, flying[i].kind, x, y, SPRITE_ABOVE_FOREGROUND);
    }
    // The stack, top chip first (in front).
    for (int i = stack.count - 1; i >= 0; i--)
        if (!stack.delay[i])
            sprite_draw(SPR_CHIP, stack.kinds[i], stack.x,
                        stack.y - 12 - i * CHIP_STEP - stack.drop[i], 0);
}

// --- Banners -----------------------------------------------------------------

#define BANNERS 3
#define BANNER_LETTERS 12
#define LETTER_STEP 13

static struct {
    bool used;
    u16 color; // LETTERS_*
    int x, y, length;
    s8 frames[BANNER_LETTERS];
    s16 age; // frames since shown
} banners[BANNERS];

void banner_show(const char* text, int center_x, int y, u16 color) {
    int slot = 0;
    while (slot < BANNERS - 1 && banners[slot].used)
        slot++;
    int n = 0;
    while (text[n] && n < BANNER_LETTERS) {
        banners[slot].frames[n] = (s8)letter_frame(text[n]);
        n++;
    }
    banners[slot].used = true;
    banners[slot].color = color;
    banners[slot].length = n;
    banners[slot].x = center_x - (n * LETTER_STEP + 3) / 2;
    banners[slot].y = y;
    banners[slot].age = 0;
}

void banner_clear(void) {
    for (int i = 0; i < BANNERS; i++)
        banners[i].used = false;
}

// Each letter pops in 3 frames after the one before: it drops in from above,
// overshoots, bounces and settles, wobbling (angles rounded to 4 degrees, so
// all letters share a handful of rotation matrices); then the word waves.
static void draw_banners(void) {
    for (int b = 0; b < BANNERS; b++) {
        if (!banners[b].used)
            continue;
        int age = banners[b].age;
        for (int i = 0; i < banners[b].length; i++) {
            if (banners[b].frames[i] < 0)
                continue;
            int t = age - i * 3; // this letter's age
            if (t < 0)
                continue;
            int x = banners[b].x + i * LETTER_STEP;
            int y = banners[b].y;
            u16 angle = 0;
            if (t < 30) {
                // A damped bounce: starts 14 pixels high, swings through.
                int decay = 256 - t * 256 / 30;
                y -= (fx_cos((u16)(t * 2600)) * 14 * decay) >> 16;
                int wobble = ((fx_sin((u16)(t * 3000 + i * 9000)) * 12 * decay) >> 16) / 4 * 4;
                angle = (u16)ANGLE_DEG(wobble < 0 ? 360 + wobble : wobble);
                if (wobble == 0)
                    angle = 0;
            } else {
                y += fx_sin((u16)(frame_count() * 1200 + i * 8000)) >> 7; // -2..2
            }
            sprite_draw_rotated(SPR_LETTER, (u8)banners[b].frames[i], x, y, angle,
                                SPRITE_ABOVE_HUD | banners[b].color);
        }
    }
}

// --- Number pops -------------------------------------------------------------

#define POPS 4
static struct {
    bool used;
    int value, x, y;
    s16 age;
} pops[POPS];
#define POP_FRAMES 70

void pop_number(int value, int center_x, int y) {
    int slot = 0;
    while (slot < POPS - 1 && pops[slot].used)
        slot++;
    pops[slot].used = true;
    pops[slot].value = value;
    pops[slot].x = center_x;
    pops[slot].y = y;
    pops[slot].age = 0;
}

static int draw_text_digits(const char* s, int x, int y, u16 color, int bounce, u16 flags) {
    int i = 0;
    for (; s[i]; i++) {
        char c = s[i];
        int frame = c >= '0' && c <= '9' ? c - '0'
                    : c == '+'           ? DIGIT_PLUS
                    : c == '-'           ? DIGIT_MINUS
                    : c == '/'           ? DIGIT_SLASH
                                         : DIGIT_CHIP;
        int dy = bounce ? ((i + (int)frame_count() / 3) % 2 ? -bounce : 0) : 0;
        sprite_draw(SPR_DIGIT, (u8)frame, x + i * 8, y + dy, flags | color);
    }
    return i * 8;
}

static void draw_pops(void) {
    for (int i = 0; i < POPS; i++) {
        if (!pops[i].used)
            continue;
        int age = pops[i].age;
        // Shoots up with a little overshoot, hangs, then blinks out.
        int p = ease_out_back(int_min(256, age * 256 / 24));
        int y = pops[i].y - ((p * 20) >> 8);
        if (age > POP_FRAMES - 16 && (age / 2) % 2)
            continue;
        const char* s = text_format(pops[i].value >= 0 ? "+%d" : "%d", pops[i].value);
        int len = 0;
        while (s[len])
            len++;
        draw_text_digits(s, pops[i].x - len * 4, y, pops[i].value >= 0 ? DIGITS_GOLD : DIGITS_RED,
                         age < 24 ? 2 : 0, SPRITE_ABOVE_HUD);
    }
}

void draw_number(u32 value, int x, int y, u16 color, int bounce) {
    draw_text_digits(text_format("%u", value), x, y, color, bounce, 0);
}

int number_width(u32 value) {
    int w = 8;
    while (value >= 10) {
        value /= 10;
        w += 8;
    }
    return w;
}

// --- Buttons -----------------------------------------------------------------

#define BUTTONS_X 60
#define BUTTONS_Y 143
#define BUTTON_STEP 44

void buttons_draw(const u8* buttons, int count, int selected, u16 enabled_mask, int pressed) {
    for (int i = 0; i < count; i++) {
        bool on = enabled_mask & (1u << i);
        int x = BUTTONS_X + i * BUTTON_STEP + (4 - count) * BUTTON_STEP / 2;
        int y = BUTTONS_Y;
        u16 angle = 0;
        if (i == selected) {
            if (pressed > 0) {
                y += 2; // pushed in
            } else {
                // The selected button floats and sways (3-degree steps).
                y -= 2 + (fx_sin((u16)(frame_count() * 900)) >> 7);
                int sway = (fx_sin((u16)(frame_count() * 700)) * 7) >> 8;
                sway = sway / 3 * 3;
                angle = sway ? (u16)ANGLE_DEG(sway < 0 ? 360 + sway : sway) : 0;
            }
        }
        // Greyed (not allowed now): the same button in PAL_BUTTON_OFF.
        sprite_draw_rotated(SPR_BUTTON, buttons[i], x, y, angle,
                            on ? 0 : SPRITE_PALETTE(PAL_BUTTON_OFF));
    }
}

// --- Drawing the cards -------------------------------------------------------

// Draws a piece of a card: (px, py) is its top-left on the unrotated card
// (relative to the card's top-left), (cx, cy) the card's center on screen.
static void draw_piece(u16 id, u8 frame, int px, int py, int w, int h, int cx, int cy, u16 angle,
                       u16 flags) {
    if (!angle) {
        sprite_draw(id, frame, cx - CARD_W / 2 + px, cy - CARD_H / 2 + py, flags);
        return;
    }
    // The piece's center, turned about the card's center.
    int dx = px + w / 2 - CARD_W / 2, dy = py + h / 2 - CARD_H / 2;
    FIXED c = fx_cos(angle), s = fx_sin(angle);
    int rx = (dx * c - dy * s + 128) >> 8;
    int ry = (dx * s + dy * c + 128) >> 8;
    sprite_draw_rotated(id, frame, cx + rx - w / 2, cy + ry - h / 2, angle, flags);
}

// What the card's base shows right now (mid-flip, a squashed step), and
// whether that is the face.
static Base card_base(const TableCard* c) {
    if (c->flip > 0) {
        int step = (FLIP_LENGTH - c->flip) / FLIP_STEP; // 0 .. FLIP_STEPS - 1
        if (!c->face_up)
            step = FLIP_STEPS - 1 - step; // face to back: backwards
        return flip_frames[step];
    }
    if (!c->face_up)
        return (Base){SPR_CARD, 0, CF_BACK, 32, false};
    return (Base){SPR_CARD, 0, CARD_RANK(c->card) >= RANK_JACK ? CF_COURT : CF_FACE, 32, true};
}

static void draw_card(const TableCard* c, u16 angle) {
    Base base = card_base(c);
    int cx = fx_to_int(c->x) + CARD_W / 2;
    int cy = fx_to_int(c->y) + CARD_H / 2 - c->lift;
    if (base.face) {
        int rank = CARD_RANK(c->card), suit = CARD_SUIT(c->card);
        u8 index = (u8)((rank - 1) * 4 + suit);
        draw_piece(SPR_INDEX, index, 2, 3, 8, 16, cx, cy, angle, 0);
        draw_piece(SPR_INDEX, index, CARD_W - 10, CARD_H - 19, 8, 16, cx, cy, angle,
                   SPRITE_FLIP_H | SPRITE_FLIP_V);
        if (rank == RANK_ACE) {
            draw_piece(SPR_ACE, (u8)suit, 0, 8, 32, 32, cx, cy, angle, 0);
        } else if (rank >= RANK_JACK) {
            bool black = suit == SUIT_SPADES || suit == SUIT_CLUBS;
            draw_piece(SPR_COURT, (u8)(RANK_KING - rank + (black ? 3 : 0)), 8, 16, 16, 16, cx, cy,
                       angle, 0);
        } else {
            draw_piece(SPR_PIP, (u8)suit, 8, 16, 16, 16, cx, cy, angle, 0);
        }
    }
    // A winning hand's faces get a gold outline: PAL_GLOW.
    u16 glow = c->glow && base.face ? SPRITE_PALETTE(PAL_GLOW) : 0;
    int left = (CARD_W - base.width) / 2;
    if (base.bottom) {
        draw_piece(base.sprite, base.frame, left, 0, base.width, 32, cx, cy, angle, 0);
        draw_piece(base.bottom, base.frame, left, 32, base.width, 16, cx, cy, angle, 0);
    } else {
        draw_piece(base.sprite, base.frame, left, -8, base.width, 64, cx, cy, angle, glow);
    }
}

// The shadow: full width, or 24 pixels early and late in a flip (none
// while the card is narrower).
static void draw_shadow(const TableCard* c) {
    Base base = card_base(c);
    if (base.width < 24)
        return;
    sprite_draw(SPR_CARD, base.width == 32 ? CF_FACE : CF_FACE_24,
                fx_to_int(c->x) + SHADOW_DX + c->lift / 2, fx_to_int(c->y) - 8 + SHADOW_DY,
                SPRITE_PALETTE(PAL_SHADOW));
}

static u16 card_angle(const TableCard* c) {
    int deg = c->angle >= 0 ? (c->angle + 256) / 512 * 2 : -((-c->angle + 256) / 512 * 2);
    if (deg == 0)
        return 0;
    return (u16)ANGLE_DEG(deg < 0 ? 360 + deg : deg);
}

void table_update(void) {
    for (int i = 0; i < TABLE_CARDS; i++)
        if (cards[i].used)
            update_card(&cards[i]);
    update_chips();
    update_sparks();
    sys_movement();
    sys_animate();
    for (int b = 0; b < BANNERS; b++)
        if (banners[b].used && banners[b].age < 1000)
            banners[b].age++;
    for (int i = 0; i < POPS; i++)
        if (pops[i].used && ++pops[i].age >= POP_FRAMES)
            pops[i].used = false;
}

// Front to back: banners and pops (above the text layer), sparks, flying
// chips, cards newest first (each piece before its base), the chip stack,
// then the cards' shadows behind them all.
void table_draw(void) {
    draw_banners();
    draw_pops();
    sys_render();
    chips_draw();

    // Cards by order, newest first; moving cards in front of resting ones.
    u8 sorted[TABLE_CARDS];
    int n = 0;
    for (int i = 0; i < TABLE_CARDS; i++) {
        if (!cards[i].used)
            continue;
        int key = cards[i].order + (cards[i].t < cards[i].frames ? 1000 : 0);
        int j = n++;
        while (j > 0) {
            const TableCard* o = &cards[sorted[j - 1]];
            int okey = o->order + (o->t < o->frames ? 1000 : 0);
            if (okey >= key)
                break;
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = (u8)i;
    }
    int tilted = 0;
    for (int k = 0; k < n; k++) {
        const TableCard* c = &cards[sorted[k]];
        if (c->delay && c->t == 0 && !c->discarding)
            continue; // still in the shoe
        u16 angle = card_angle(c);
        if (angle && tilted++ >= MAX_TILTED)
            angle = 0;
        draw_card(c, angle);
    }
    // Shadows: straight, further off the more a card is lifted.
    for (int k = 0; k < n; k++) {
        const TableCard* c = &cards[sorted[k]];
        if (!(c->delay && c->t == 0 && !c->discarding))
            draw_shadow(c);
    }
}
