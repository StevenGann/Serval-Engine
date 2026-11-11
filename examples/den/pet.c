// The life of Serval Den's pet: see pet.h.

#include "pet.h"

#include <string.h>

#define NEED_MAX 10000
#define MAX_STEPPED 2880 // minutes stepped one by one (48 h); the rest only ages the pet
#define KITTEN_MINUTES 1440
#define ADOLESCENT_MINUTES 4320
#define LITTER_AGE 8640
#define SAD_AT 720
#define SICK_AT 1080
#define SICKER_AT 1440
#define DEAD_AT 2160

static u16 add(u16 v, int d) {
    int r = (int)v + d;
    return (u16)(r < 0 ? 0 : r > NEED_MAX ? NEED_MAX : r);
}

static bool is_night(int hour) {
    return hour >= 21 || hour < 7;
}

void pet_new(Pet* p, const char* name, Coat coat, u8 generation, u32 now) {
    memset(p, 0, sizeof *p);
    for (int i = 0; i < 8 && name[i]; i++)
        p->name[i] = name[i];
    p->coat = coat;
    p->generation = generation;
    p->born = p->last_tick = p->last_interaction = now;
    p->full = p->fun = p->energy = 8000;
    p->health = NEED_MAX;
}

u32 pet_age_minutes(const Pet* p, u32 now) {
    return now > p->born ? (now - p->born) / 60 : 0;
}

static PetStage stage_of(u32 age) {
    return age < KITTEN_MINUTES       ? STAGE_KITTEN
           : age < ADOLESCENT_MINUTES ? STAGE_ADOLESCENT
                                      : STAGE_ADULT;
}

PetStage pet_stage(const Pet* p, u32 now) {
    return stage_of(pet_age_minutes(p, now));
}

u32 pet_minutes_to_next_stage(const Pet* p, u32 now) {
    u32 age = pet_age_minutes(p, now);
    return age < KITTEN_MINUTES       ? KITTEN_MINUTES - age
           : age < ADOLESCENT_MINUTES ? ADOLESCENT_MINUTES - age
                                      : 0;
}

u32 pet_neglect_minutes(const Pet* p, u32 now) {
    return now > p->last_interaction ? (now - p->last_interaction) / 60 : 0;
}

PetMood pet_mood(const Pet* p, u32 now) {
    if (p->dead)
        return MOOD_DEAD;
    if (p->sickness >= 2)
        return MOOD_SICKER;
    if (p->sickness == 1)
        return MOOD_SICK;
    return pet_neglect_minutes(p, now) >= SAD_AT ? MOOD_SAD : MOOD_HAPPY;
}

// One minute, ending at clock time `t`; `prev_night` is whether the minute
// before was night.
static void step(Pet* p, u32 t, bool prev_night, bool night) {
    // Sleep and wake.
    if (!night && (p->asleep || prev_night)) {
        p->asleep = false;
        p->lights_off = false;
        p->awake_until = 0;
    } else if (night && !p->asleep && t >= p->awake_until) {
        p->asleep = true;
        p->awake_until = 0;
    }

    PetStage stage = stage_of(pet_age_minutes(p, t));
    bool sick = p->sickness > 0;
    int full_loss, fun_loss = 0;
    if (p->asleep) {
        full_loss = 4;
        if (!p->lights_off)
            fun_loss = 3;
        p->energy = add(p->energy, p->lights_off ? 25 : 12);
    } else {
        full_loss = stage == STAGE_KITTEN ? 13 : 10;
        fun_loss = 8;
        p->energy = add(p->energy, -10);
    }
    if (sick) {
        full_loss = full_loss * 3 / 2;
        fun_loss = fun_loss * 3 / 2;
    }
    p->full = add(p->full, -full_loss);
    p->fun = add(p->fun, -fun_loss);

    // Droppings, one per 240 awake minutes (kitten 180).
    if (!p->asleep && ++p->dropping_clock >= (stage == STAGE_KITTEN ? 180 : 240)) {
        p->dropping_clock = 0;
        if (p->droppings < 4)
            p->droppings++;
    }

    // Treats wear off one per 2 h.
    if (p->treats_recent > 0 && ++p->treat_clock >= 120) {
        p->treat_clock = 0;
        p->treats_recent--;
    }

    // Health.
    int dh = 0;
    if (p->full == 0)
        dh -= 2;
    if (p->fun == 0)
        dh -= 2;
    if (p->energy == 0)
        dh -= 2;
    if (p->droppings > 2)
        dh -= 2 * (p->droppings - 2);
    if (p->full >= 5000 && p->fun >= 5000 && p->energy >= 5000 && p->droppings == 0 &&
        p->sickness == 0)
        dh += 1;
    p->health = add(p->health, dh);
    if (p->health == 0 && p->sickness < 1)
        p->sickness = 1;

    // Neglect.
    u32 neglect = pet_neglect_minutes(p, t);
    if (neglect >= SAD_AT && p->fun > 3000)
        p->fun = 3000;
    if (neglect >= SICK_AT && p->sickness < 1)
        p->sickness = 1;
    if (neglect >= SICKER_AT)
        p->sickness = 2;
    if (neglect >= DEAD_AT) {
        p->dead = true;
        p->died_at = t;
        return;
    }

    // Growing up.
    if (pet_age_minutes(p, t) >= LITTER_AGE && p->sickness == 0)
        p->litter_due = true;
}

// t moved by now - from, either way, never below 0.
static u32 moved(u32 t, u32 from, u32 now) {
    if (now >= from)
        return t + (now - from);
    u32 d = from - now;
    return t > d ? t - d : 0;
}

void pet_rebase(Pet* p, u32 now) {
    u32 from = p->last_tick;
    p->born = moved(p->born, from, now);
    p->last_interaction = moved(p->last_interaction, from, now);
    if (p->awake_until)
        p->awake_until = moved(p->awake_until, from, now);
    if (p->died_at)
        p->died_at = moved(p->died_at, from, now);
    p->last_tick = now;
}

void pet_advance(Pet* p, u32 now, int (*hour_of)(u32 seconds)) {
    if (now < p->last_tick) {
        pet_rebase(p, now); // the clock went back: no time passes
        return;
    }
    u32 minutes = (now - p->last_tick) / 60;
    bool prev_night = !p->dead && minutes > 0 && is_night(hour_of(p->last_tick));
    while (minutes > 0 && !p->dead) {
        u32 t = p->last_tick + 60;
        bool night = is_night(hour_of(t));
        p->last_tick = t;
        step(p, t, prev_night, night);
        prev_night = night;
        minutes--;
    }
    // A dead pet doesn't change: the rest only moves the clock.
    p->last_tick += minutes * 60;
}

CareResult pet_care(Pet* p, CareAction a, u32 now) {
    if (now < p->last_tick)
        now = p->last_tick; // never ahead of the stepped life
    if (p->dead)
        return CARE_REFUSED_DEAD;
    if (a == CARE_LIGHTS) {
        p->lights_off = !p->lights_off;
        p->last_interaction = now;
        return CARE_DONE;
    }
    if (a == CARE_MEAL && p->full >= 9000)
        return CARE_REFUSED_FULL;
    if (a == CARE_CLEAN && p->droppings == 0)
        return CARE_REFUSED_CLEAN;
    if (a == CARE_MEDICINE && p->sickness == 0)
        return CARE_REFUSED_NOT_SICK;

    if (p->asleep) {
        p->asleep = false;
        p->fun = add(p->fun, -1000);
        p->awake_until = now + 3600;
    }
    switch (a) {
    case CARE_MEAL:
        p->full = add(p->full, 4000);
        break;
    case CARE_TREAT:
        p->full = add(p->full, 1500);
        p->fun = add(p->fun, 1500);
        if (p->treats_recent == 0)
            p->treat_clock = 0;
        if (p->treats_recent < 255)
            p->treats_recent++;
        if (p->treats_recent > 3)
            p->health = add(p->health, -1000);
        break;
    case CARE_PET:
        p->fun = add(p->fun, 1000);
        break;
    case CARE_CLEAN:
        p->droppings = 0;
        break;
    case CARE_MEDICINE:
        p->sickness--;
        if (p->health < 2000)
            p->health = 2000;
        break;
    case CARE_LIGHTS:
        break;
    }
    p->last_interaction = now;
    return CARE_DONE;
}

bool pet_can_play(const Pet* p, u32 now) {
    (void)now;
    return p->energy >= 2000 && !p->asleep && !p->dead;
}

void pet_played(Pet* p, u32 now, u8 fun_points) {
    if (p->dead || p->asleep)
        return;
    if (now < p->last_tick)
        now = p->last_tick;
    p->fun = add(p->fun, fun_points * 100);
    p->energy = add(p->energy, -1500);
    p->last_interaction = now;
}
