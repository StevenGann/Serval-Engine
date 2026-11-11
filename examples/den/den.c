// The den on screen: see den.h.
//
// The serval's behaviour is chosen from the pet each frame: dead, a stone;
// asleep, it walks to its nest and sleeps there (Zzz); sick, it lies where it
// is and shivers, tinted blue; sad, it sits with its ears down; content, it
// wanders the floor, stops, looks about, blinks and flicks its tail. A care
// action plays over that for a moment: a meal at the bowl, hearts and a purr,
// a sparkle.

#include "den.h"

#include "art.h"
#include "clock.h"
#include "sound.h"

#include <serval/serval.h>

#define CATCH_UP_SECONDS (15 * 60) // the most life one frame steps (about 125,000 cycles)

enum { CUE_NONE, CUE_EAT, CUE_PETTED, CUE_MEDICINE, CUE_CLEAN };

static struct {
    int x, y;   // the paws' middle, Q4 (sixteenths of a pixel)
    int tx, ty; // where it is walking to
    bool walking, left, placed;
    int wait; // frames before it walks again
    int cue, cue_timer;
    int last_mood; // -1: not yet seen
    bool was_asleep;
} cat;

// What the palettes show now, to write them only when it changes.
static int shown_tod = -1, shown_sick = -1, shown_lights = -1;
static u32 shown_minute = 0xFFFFFFFF;

// Where droppings lie (screen pixels, the dropping's middle bottom).
static const u8 drop_spot[4][2] = {{150, 140}, {196, 130}, {108, 146}, {178, 148}};

#define BOWL_FRAMES_LONG 150

void den_enter(void) {
    art_load();
    shown_tod = shown_sick = shown_lights = -1;
    shown_minute = 0xFFFFFFFF;
    cat.placed = false;
    cat.cue = CUE_NONE;
    text_set_shadow(true);
    text_set_color(COLOR_RGB(248, 236, 208), COLOR_RGB(30, 20, 16));
    text_clear();
}

static int speed(int stage) {
    return stage == STAGE_KITTEN ? 9 : stage == STAGE_ADOLESCENT ? 12 : 14; // Q4 per frame
}

// Walks toward (tx, ty); true once there.
static bool walk_to(int stage) {
    int dx = cat.tx - cat.x, dy = cat.ty - cat.y;
    int s = speed(stage);
    if (int_abs(dx) <= s && int_abs(dy) <= s) {
        cat.x = cat.tx;
        cat.y = cat.ty;
        cat.walking = false;
        return true;
    }
    cat.walking = true;
    cat.left = dx < 0 || (dx == 0 && cat.left);
    // move along the larger axis at full speed, the other in proportion
    if (int_abs(dx) >= int_abs(dy)) {
        cat.x += dx > 0 ? s : -s;
        cat.y += dy * s / int_abs(dx);
    } else {
        cat.y += dy > 0 ? s : -s;
        cat.x += dx * s / int_abs(dy);
    }
    return false;
}

void den_update(Pet* p, u32 now) {
    u32 target = now;
    if (now > p->last_tick && now - p->last_tick > CATCH_UP_SECONDS)
        target = p->last_tick + CATCH_UP_SECONDS;
    pet_advance(p, target, clock_hour);

    u32 t = p->last_tick;
    int stage = (int)pet_stage(p, t);
    PetMood mood = pet_mood(p, t);
    if (!cat.placed) {
        cat.placed = true;
        cat.x = cat.tx = (p->asleep ? NEST_X : 150) * 16;
        cat.y = cat.ty = (p->asleep ? NEST_Y + 2 : 134) * 16;
        cat.wait = 60;
        cat.last_mood = -1;
        cat.was_asleep = p->asleep;
    }
    if (cat.cue_timer > 0 && --cat.cue_timer == 0)
        cat.cue = CUE_NONE;
    // Its voice: a falling mew when its mood worsens, a chirp when it wakes.
    if (cat.last_mood >= 0 && (int)mood > cat.last_mood && mood != MOOD_DEAD)
        sound_sad();
    else if (cat.was_asleep && !p->asleep && !p->dead)
        sound_mew();
    cat.last_mood = (int)mood;
    cat.was_asleep = p->asleep;

    if (p->dead || mood >= MOOD_SICK) {
        cat.walking = false;
        return;
    }
    if (p->asleep) {
        cat.cue = CUE_NONE;
        cat.tx = NEST_X * 16;
        cat.ty = (NEST_Y + 2) * 16;
        walk_to(stage);
        return;
    }
    if (cat.cue == CUE_EAT || mood == MOOD_SAD) {
        cat.walking = false;
        return;
    }
    // Content: wander, stop, look about.
    if (cat.walking) {
        if (walk_to(stage))
            cat.wait = random_range(90, 300);
    } else if (cat.cue == CUE_NONE && --cat.wait <= 0) {
        cat.tx = random_range(30, 210) * 16;
        cat.ty = random_range(FLOOR_TOP + 6, FLOOR_BOTTOM) * 16;
        // out of the nest when awake
        int ndx = cat.tx / 16 - NEST_X, ndy = (cat.ty / 16 - NEST_Y) * 3;
        if (ndx * ndx + ndy * ndy < 34 * 34)
            cat.tx = (NEST_X + 40) * 16;
        walk_to(stage);
    }
}

CareResult den_care(Pet* p, CareAction a, u32 now) {
    bool was_asleep = p->asleep;
    CareResult r = pet_care(p, a, now);
    if (r != CARE_DONE) {
        sound_no();
        return r;
    }
    if (was_asleep && a != CARE_LIGHTS) {
        cat.x = cat.tx = (NEST_X + 30) * 16; // woken, it steps out of the nest
        cat.walking = false;
    }
    cat.cue_timer = 0;
    cat.cue = CUE_NONE;
    switch (a) {
    case CARE_MEAL:
    case CARE_TREAT:
        cat.cue = CUE_EAT;
        cat.cue_timer = BOWL_FRAMES_LONG;
        cat.walking = false;
        sound_eat();
        break;
    case CARE_PET:
        cat.cue = CUE_PETTED;
        cat.cue_timer = 100;
        cat.walking = false;
        sound_purr();
        break;
    case CARE_MEDICINE:
        cat.cue = CUE_MEDICINE;
        cat.cue_timer = 60;
        sound_medicine();
        break;
    case CARE_CLEAN:
        cat.cue = CUE_CLEAN;
        cat.cue_timer = 40;
        sound_clean();
        break;
    case CARE_LIGHTS:
        sound_lights(p->lights_off);
        break;
    }
    return r;
}

// --- Drawing
// --------------------------------------------------------------------------------------

// The pet's frame for this moment.
static int pet_frame(const Pet* p, PetMood mood, u32 f) {
    if (p->asleep)
        return cat.walking ? PF_WALK0 + (int)(f / 8 % 4) : ((f / 40) & 1 ? PF_SLEEP1 : PF_SLEEP0);
    if (mood >= MOOD_SICK)
        return (f / 90) % 3 == 2 ? PF_LIE1 : PF_LIE;
    if (cat.cue == CUE_EAT)
        return (f / 10) & 1 ? PF_EAT1 : PF_EAT0;
    if (cat.cue == CUE_PETTED || cat.cue == CUE_MEDICINE)
        return PF_HAPPY;
    if (mood == MOOD_SAD)
        return (f / 50) % 4 == 3 ? PF_SIT1 : PF_SIT;
    if (cat.walking)
        return PF_WALK0 + (int)(f / 8 % 4);
    u32 k = f % 240;
    if (k < 8)
        return PF_BLINK;
    if (k >= 120 && k < 136)
        return PF_STAND_TAIL;
    return PF_STAND;
}

// The sun's or the moon's place: along an arc across the mouth over `span`
// seconds from `rise`; false while it's down.
static bool sky_arc(u32 day_seconds, u32 rise, u32 span, int* x, int* y) {
    u32 s = (day_seconds + 86400 - rise) % 86400;
    if (s >= span)
        return false;
    *x = MOUTH_X0 - 4 + (int)(s * (MOUTH_X1 - MOUTH_X0 - 8) / span);
    u16 a = (u16)(s * 32768u / span); // 0 to half a turn
    *y = 86 - fx_to_int(fx_sin(a) * 58);
    return true;
}

static void draw_hud(const Pet* p, u32 now) {
    u32 minute = now / 60;
    if (minute == shown_minute)
        return;
    shown_minute = minute;
    RtcTime t;
    rtc_time_from_seconds(now, &t);
    text_print_line(1, 0, p->name);
    text_print(24, 0, text_format("%02u:%02u", (u32)t.hour, (u32)t.minute));
}

void den_draw(const Pet* p, u32 now) {
    u32 f = frame_count();
    u32 t = p->last_tick;
    int stage = (int)pet_stage(p, t);
    PetMood mood = pet_mood(p, t);
    int tod = art_time_of_day(clock_hour(now));
    int sick = p->dead ? 0 : p->sickness;
    if (tod != shown_tod || (int)p->lights_off != shown_lights) {
        art_set_scene(tod, p->lights_off);
        shown_sick = -1;
    }
    if (tod != shown_tod || (int)p->lights_off != shown_lights || sick != shown_sick) {
        art_set_pet(p->coat, sick, tod, p->lights_off);
        shown_tod = tod;
        shown_lights = p->lights_off;
        shown_sick = sick;
    }
    draw_hud(p, now);
    map_set_scroll(SKY_BG, (int)(f / 12), 0); // the clouds drift

    // Front to back: effects, the serval and the things on the floor by
    // depth, the lamp; the sun and the moon behind the den.
    int px = cat.x / 16, py = cat.y / 16;
    int size = stage == STAGE_KITTEN ? 16 : 32;
    int top = py - size + 2;

    if (p->asleep && !cat.walking && !p->dead) {
        int k = (int)(f % 120);
        sprite_draw(SPR_ZZZ, 1, px + 6 + k / 20, top + 6 - k / 6, 0);
        if (k >= 40)
            sprite_draw(SPR_ZZZ, 0, px + 10 + (k - 40) / 16, top + 2 - (k - 40) / 6, 0);
    }
    if (cat.cue == CUE_PETTED) {
        int k = 100 - cat.cue_timer;
        for (int i = 0; i < 3; i++) {
            int age = k - i * 20;
            if (age >= 0 && age < 60)
                sprite_draw(SPR_HEART, age > 40, px - 6 + i * 6 + ((age / 8) & 1),
                            top + 6 - age / 3, 0);
        }
    }
    if (cat.cue == CUE_MEDICINE) {
        int k = 60 - cat.cue_timer;
        sprite_draw(SPR_SPARK, k / 20, px - 4, top - 2, 0);
        sprite_draw(SPR_SPARK, (k / 20 + 1) % 3, px + 6, top + 6, 0);
    }
    if (cat.cue == CUE_CLEAN) {
        int k = 40 - cat.cue_timer;
        for (int i = 0; i < 4; i++)
            sprite_draw(SPR_SPARK, k * 3 / 40, drop_spot[i][0] - 4, drop_spot[i][1] - 8, 0);
    }

    // Depth: things lower on the screen are in front.
    typedef struct {
        int y;
        u16 id;
        u8 frame;
        int x, top;
        u16 flags;
    } Draw;
    Draw list[8];
    int n = 0;
    if (p->dead) {
        list[n++] = (Draw){py, SPR_TOMB, 0, px - 8, py - 15, 0};
    } else {
        int shiver = 0;
        if (mood >= MOOD_SICK && (mood == MOOD_SICKER || (f / 30) % 3 == 0))
            shiver = (f / 3) & 1;
        list[n++] = (Draw){
            py,  (u16)(SPR_KITTEN + stage),   (u8)pet_frame(p, mood, f), px - size / 2 + shiver,
            top, cat.left ? SPRITE_FLIP_H : 0};
    }
    if (cat.cue == CUE_EAT) {
        int k = cat.cue_timer * BOWL_FRAMES / BOWL_FRAMES_LONG;
        int bx = cat.left ? px - size / 2 - 6 : px + size / 2 - 10;
        list[n++] = (Draw){py + 1, SPR_BOWL, (u8)(BOWL_FRAMES - 1 - k), bx, py - 6, 0};
    }
    for (int i = 0; i < p->droppings && i < 4; i++)
        list[n++] = (Draw){drop_spot[i][1],     SPR_DROP, (u8)(i & 1), drop_spot[i][0] - 4,
                           drop_spot[i][1] - 7, 0};
    // sort by y, the lowest first (in front)
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && list[j].y > list[j - 1].y; j--) {
            Draw d = list[j];
            list[j] = list[j - 1];
            list[j - 1] = d;
        }
    for (int i = 0; i < n; i++)
        sprite_draw(list[i].id, list[i].frame, list[i].x, list[i].top, list[i].flags);
    // a whiff over droppings left a while
    for (int i = 0; i < p->droppings && i < 4; i++)
        if ((f / 20 + (u32)i * 7) % 6 == 0)
            sprite_draw(SPR_DROP, 2, drop_spot[i][0] - 3, drop_spot[i][1] - 16, 0);

    sprite_draw(SPR_LAMP, p->lights_off ? 2 : (u8)((f / 14) & 1), LAMP_X, LAMP_Y, 0);

    u32 day = now % 86400;
    int sx, sy;
    if (sky_arc(day, 5 * 3600, 16 * 3600, &sx, &sy))
        sprite_draw(SPR_SUN, 0, sx, sy, SPRITE_BEHIND_PLAYFIELD);
    if (sky_arc(day, 20 * 3600, 10 * 3600, &sx, &sy))
        sprite_draw(SPR_MOON, 0, sx, sy, SPRITE_BEHIND_PLAYFIELD);
}
