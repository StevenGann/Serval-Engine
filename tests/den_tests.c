// den: Serval Den's pet (examples/den/pet.c), natively. Time is the clock's
// seconds; every test starts at T0, 08:00 on a day whose midnight is
// 762480000 (= 8825 * 86400, the origin 762525296 rounded down to midnight),
// with hour_of = (s / 3600) % 24. A pet born at 08:00 is awake for 13 h, then
// asleep from 21:00 to 06:59; tests that pass 21:00 say so.

#include "test.h"

#include <string.h>

#include "../examples/den/pet.h"

#define MIDNIGHT 762480000u
#define T0 (MIDNIGHT + 8u * 3600u)
#define MIN 60u

static int hour_of(u32 s) {
    return (int)((s / 3600u) % 24u);
}

static Pet fresh(void) {
    Pet p;
    Coat c = {1, 2, 0};
    pet_new(&p, "Mimi", c, 1, T0);
    return p;
}

static void test_new_defaults(void) {
    Pet p = fresh();
    CHECK(p.full == 8000 && p.fun == 8000 && p.energy == 8000);
    CHECK(p.health == 10000);
    CHECK(pet_stage(&p, T0) == STAGE_KITTEN);
    CHECK(pet_mood(&p, T0) == MOOD_HAPPY);
    CHECK(strcmp(p.name, "Mimi") == 0);
    CHECK(p.generation == 1 && p.born == T0 && p.last_tick == T0 && p.last_interaction == T0);
    CHECK(!p.asleep && !p.dead && !p.litter_due && p.droppings == 0 && p.sickness == 0);
}

static void test_stages(void) {
    Pet p = fresh();
    CHECK(pet_minutes_to_next_stage(&p, T0) == 1440);
    CHECK(pet_stage(&p, T0 + 1439 * MIN) == STAGE_KITTEN);
    CHECK(pet_stage(&p, T0 + 1440 * MIN) == STAGE_ADOLESCENT);
    CHECK(pet_stage(&p, T0 + 4319 * MIN) == STAGE_ADOLESCENT);
    CHECK(pet_stage(&p, T0 + 4320 * MIN) == STAGE_ADULT);
    CHECK(pet_minutes_to_next_stage(&p, T0 + 1000 * MIN) == 440);
    CHECK(pet_minutes_to_next_stage(&p, T0 + 1440 * MIN) == 2880);
    CHECK(pet_minutes_to_next_stage(&p, T0 + 4320 * MIN) == 0);
    CHECK(pet_age_minutes(&p, T0 + 90) == 1);
}

static void test_neglect_thresholds(void) {
    Pet p = fresh();
    for (u32 m = 1; m <= 2160; m++) {
        pet_advance(&p, T0 + m * MIN, hour_of);
        PetMood mood = pet_mood(&p, T0 + m * MIN);
        if (m == 719)
            CHECK(mood == MOOD_HAPPY);
        if (m == 720)
            CHECK(mood == MOOD_SAD);
        if (m == 1079)
            CHECK(mood == MOOD_SAD && p.sickness == 0);
        if (m == 1080)
            CHECK(mood == MOOD_SICK && p.sickness == 1);
        if (m == 1439)
            CHECK(mood == MOOD_SICK);
        if (m == 1440)
            CHECK(mood == MOOD_SICKER && p.sickness == 2);
        if (m == 2159)
            CHECK(!p.dead && mood == MOOD_SICKER);
        if (m == 2160)
            CHECK(p.dead && mood == MOOD_DEAD && p.died_at == T0 + 2160 * MIN);
    }
    CHECK(p.fun <= 3000 || p.dead);
}

static void test_care_resets_neglect(void) {
    Pet p = fresh();
    pet_advance(&p, T0 + 1000 * MIN, hour_of);
    CHECK(pet_mood(&p, T0 + 1000 * MIN) == MOOD_SAD);
    CHECK(pet_care(&p, CARE_PET, T0 + 1000 * MIN) == CARE_DONE);
    pet_advance(&p, T0 + 1001 * MIN, hour_of);
    CHECK(pet_mood(&p, T0 + 1001 * MIN) == MOOD_HAPPY);
    CHECK(pet_neglect_minutes(&p, T0 + 1001 * MIN) == 1);

    // Sickness stays after care until medicine.
    Pet q = fresh();
    pet_advance(&q, T0 + 1100 * MIN, hour_of);
    CHECK(q.sickness == 1);
    CHECK(pet_care(&q, CARE_PET, T0 + 1100 * MIN) == CARE_DONE);
    pet_advance(&q, T0 + 1101 * MIN, hour_of);
    CHECK(pet_mood(&q, T0 + 1101 * MIN) == MOOD_SICK);
    CHECK(pet_care(&q, CARE_MEDICINE, T0 + 1101 * MIN) == CARE_DONE);
    CHECK(q.sickness == 0 && pet_mood(&q, T0 + 1101 * MIN) == MOOD_HAPPY);
}

static void test_sleep_cycle(void) {
    Pet a = fresh(), b = fresh();
    pet_advance(&a, MIDNIGHT + 20 * 3600, hour_of);
    pet_advance(&b, MIDNIGHT + 20 * 3600, hour_of);
    CHECK(pet_care(&a, CARE_LIGHTS, MIDNIGHT + 20 * 3600) == CARE_DONE);
    CHECK(a.lights_off);
    pet_advance(&a, MIDNIGHT + 21 * 3600 - MIN, hour_of);
    pet_advance(&b, MIDNIGHT + 21 * 3600 - MIN, hour_of);
    CHECK(!a.asleep && !b.asleep);
    pet_advance(&a, MIDNIGHT + 21 * 3600, hour_of);
    pet_advance(&b, MIDNIGHT + 21 * 3600, hour_of);
    CHECK(a.asleep && b.asleep);

    u16 ea = a.energy, eb = b.energy, fa = a.fun, fb = b.fun;
    pet_advance(&a, MIDNIGHT + 22 * 3600, hour_of);
    pet_advance(&b, MIDNIGHT + 22 * 3600, hour_of);
    CHECK(a.energy > ea); // energy rises while asleep (lights off: +25 a minute)
    CHECK(a.energy - ea == 60 * 25 || a.energy == 10000);
    CHECK(b.energy - eb == 60 * 12); // lights on: +12
    CHECK(a.fun == fa);              // lights off: no fun lost asleep
    CHECK(fb - b.fun == 60 * 3);     // lights on: -3 a minute

    pet_advance(&a, MIDNIGHT + 24 * 3600 + 6 * 3600 + 59 * 60, hour_of); // 06:59
    CHECK(a.asleep);
    pet_advance(&a, MIDNIGHT + 24 * 3600 + 7 * 3600, hour_of); // 07:00
    CHECK(!a.asleep && !a.lights_off);
}

static void test_catch_up_equals_live(void) {
    Pet a = fresh(), b = fresh(), c = fresh();
    pet_advance(&a, T0 + 5000 * MIN, hour_of);
    for (u32 m = 1; m <= 5000; m++)
        pet_advance(&b, T0 + m * MIN, hour_of);
    CHECK(memcmp(&a, &b, sizeof a) == 0);
    CHECK(a.dead);

    u32 seed = 12345, m = 0;
    while (m < 5000) {
        seed = seed * 1103515245u + 12345u;
        u32 step = 1 + (seed >> 16) % 700;
        m = m + step > 5000 ? 5000 : m + step;
        pet_advance(&c, T0 + m * MIN, hour_of);
    }
    CHECK(memcmp(&c, &b, sizeof c) == 0);

    // Alive too: cared for early on, then left, split and not.
    Pet d = fresh(), e = fresh();
    pet_advance(&d, T0 + 600 * MIN, hour_of);
    pet_advance(&e, T0 + 600 * MIN, hour_of);
    pet_care(&d, CARE_MEAL, T0 + 600 * MIN);
    pet_care(&e, CARE_MEAL, T0 + 600 * MIN);
    pet_advance(&d, T0 + 1500 * MIN, hour_of);
    for (u32 k = 601; k <= 1500; k++)
        pet_advance(&e, T0 + k * MIN, hour_of);
    CHECK(memcmp(&d, &e, sizeof d) == 0 && !d.dead);
}

static void test_backwards_is_no_time(void) {
    Pet p = fresh();
    pet_advance(&p, T0 + 600 * MIN, hour_of);
    Pet before = p;
    pet_advance(&p, T0 + 600 * MIN - 3600, hour_of);
    CHECK(p.last_tick == T0 + 600 * MIN - 3600);
    before.last_tick = p.last_tick;
    CHECK(memcmp(&p, &before, sizeof p) == 0);
    pet_advance(&p, T0 + 600 * MIN, hour_of);
    CHECK(p.last_tick == T0 + 600 * MIN);
    CHECK(p.full < before.full); // an hour's worth of minutes was stepped
    Pet q = before;
    q.last_tick = T0 + 600 * MIN - 3600;
    for (u32 k = 1; k <= 60; k++)
        pet_advance(&q, q.last_tick + MIN, hour_of);
    CHECK(memcmp(&p, &q, sizeof p) == 0);
}

static void test_long_absence(void) {
    Pet p = fresh();
    pet_advance(&p, T0 + 10u * 1440 * MIN, hour_of);
    CHECK(p.dead && p.died_at == T0 + 2160 * MIN);
    CHECK(pet_mood(&p, T0 + 10u * 1440 * MIN) == MOOD_DEAD);
    CHECK(p.last_tick == T0 + 10u * 1440 * MIN);

    // After a meal at +100 min, the death is 36 h after that.
    Pet q = fresh();
    pet_advance(&q, T0 + 100 * MIN, hour_of);
    pet_care(&q, CARE_PET, T0 + 100 * MIN);
    pet_advance(&q, T0 + 10u * 1440 * MIN, hour_of);
    CHECK(q.dead && q.died_at == T0 + (100 + 2160) * MIN);
}

static void test_litter_due(void) {
    Pet p = fresh();
    for (u32 m = 1; m <= 8640; m++) {
        u32 now = T0 + m * MIN;
        pet_advance(&p, now, hour_of);
        if (m % 360 == 0) { // every 6 h
            pet_care(&p, CARE_MEAL, now);
            for (int i = 0; i < 3; i++)
                pet_care(&p, CARE_PET, now);
            pet_care(&p, CARE_TREAT, now);
            pet_care(&p, CARE_CLEAN, now);
            pet_care(&p, CARE_MEDICINE, now);
        }
        CHECK(!p.dead);
        if (m < 8640 && p.litter_due) {
            CHECK(0 && "litter due early");
            break;
        }
    }
    CHECK(p.litter_due && p.sickness == 0);
    CHECK(pet_stage(&p, T0 + 8640 * MIN) == STAGE_ADULT);
}

static void test_care_refusals(void) {
    Pet p = fresh();
    p.full = 9000;
    Pet before = p;
    CHECK(pet_care(&p, CARE_MEAL, T0 + 5 * MIN) == CARE_REFUSED_FULL);
    CHECK(pet_care(&p, CARE_MEDICINE, T0 + 5 * MIN) == CARE_REFUSED_NOT_SICK);
    CHECK(pet_care(&p, CARE_CLEAN, T0 + 5 * MIN) == CARE_REFUSED_CLEAN);
    CHECK(memcmp(&p, &before, sizeof p) == 0); // nothing changed, no interaction
    CHECK(p.last_interaction == T0);

    p.full = 8999;
    CHECK(pet_care(&p, CARE_MEAL, T0 + 5 * MIN) == CARE_DONE);
    CHECK(p.full == 10000 && p.last_interaction == T0 + 5 * MIN);

    // A refused action on a sleeping pet doesn't wake it.
    Pet s = fresh();
    pet_advance(&s, MIDNIGHT + 22 * 3600, hour_of);
    CHECK(s.asleep);
    s.full = 9500;
    CHECK(pet_care(&s, CARE_MEAL, MIDNIGHT + 22 * 3600) == CARE_REFUSED_FULL);
    CHECK(s.asleep);
    // A done one wakes it: fun -1000, awake for an hour, then asleep again.
    u16 fun = s.fun;
    CHECK(pet_care(&s, CARE_PET, MIDNIGHT + 22 * 3600) == CARE_DONE);
    CHECK(!s.asleep && s.awake_until == MIDNIGHT + 23 * 3600);
    CHECK(s.fun == fun - 1000 + 1000);
    pet_advance(&s, MIDNIGHT + 22 * 3600 + 3540, hour_of);
    CHECK(!s.asleep);
    pet_advance(&s, MIDNIGHT + 23 * 3600, hour_of);
    CHECK(s.asleep);
    // Lights toggle even when asleep.
    CHECK(pet_care(&s, CARE_LIGHTS, MIDNIGHT + 23 * 3600) == CARE_DONE && s.lights_off && s.asleep);

    // Medicine on a sick pet; dead pets refuse everything.
    Pet k = fresh();
    k.sickness = 2;
    k.health = 0;
    CHECK(pet_care(&k, CARE_MEDICINE, T0) == CARE_DONE && k.sickness == 1 && k.health >= 2000);
    k.dead = true;
    for (int a = CARE_MEAL; a <= CARE_LIGHTS; a++)
        CHECK(pet_care(&k, (CareAction)a, T0) == CARE_REFUSED_DEAD);
    CHECK(!pet_can_play(&k, T0));
}

static void test_play(void) {
    Pet p = fresh();
    CHECK(pet_can_play(&p, T0));
    p.fun = 5000;
    pet_played(&p, T0 + MIN, 20);
    CHECK(p.fun == 7000 && p.energy == 6500 && p.last_interaction == T0 + MIN);
    p.energy = 1999;
    CHECK(!pet_can_play(&p, T0));
}

static void test_treats_hurt(void) {
    Pet p = fresh();
    for (int i = 0; i < 3; i++)
        CHECK(pet_care(&p, CARE_TREAT, T0 + i * MIN) == CARE_DONE);
    CHECK(p.health == 10000 && p.treats_recent == 3);
    CHECK(pet_care(&p, CARE_TREAT, T0 + 3 * MIN) == CARE_DONE);
    CHECK(p.health == 9000 && p.treats_recent == 4);
    // They decay one per 2 h.
    pet_advance(&p, T0 + 3 * MIN + 120 * MIN, hour_of);
    CHECK(p.treats_recent == 3);
}

TEST_SUITE(den_tests, "den", {"pet_new_defaults", test_new_defaults}, {"pet_stages", test_stages},
           {"pet_neglect_thresholds", test_neglect_thresholds},
           {"pet_care_resets_neglect", test_care_resets_neglect},
           {"pet_sleep_cycle", test_sleep_cycle},
           {"pet_catch_up_equals_live", test_catch_up_equals_live},
           {"pet_backwards_is_no_time", test_backwards_is_no_time},
           {"pet_long_absence", test_long_absence}, {"pet_litter_due", test_litter_due},
           {"pet_care_refusals", test_care_refusals}, {"pet_play", test_play},
           {"pet_treats_hurt", test_treats_hurt});
