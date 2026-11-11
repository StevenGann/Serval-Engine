// den: Serval Den's pet (examples/den/pet.c), natively. Time is the clock's
// seconds; every test starts at T0, 08:00 on a day whose midnight is
// 762480000 (= 8825 * 86400, the origin 762525296 rounded down to midnight),
// with hour_of = (s / 3600) % 24. A pet born at 08:00 is awake for 13 h, then
// asleep from 21:00 to 06:59; tests that pass 21:00 say so.

#include "test.h"

#include <string.h>

#include <serval/random.h>
#include <serval/save.h>

#include "../examples/den/album.h"
#include "../examples/den/climb_rules.h"
#include "../examples/den/clock.h"
#include "../examples/den/den_save.h"
#include "../examples/den/events.h"
#include "../examples/den/litter.h"
#include "../examples/den/pet.h"
#include "../examples/den/pounce_rules.h"
#include "../examples/den/yarn_rules.h"

#define MIDNIGHT 762480000u
#define T0 (MIDNIGHT + 8u * 3600u)
#define MIN 60u

static int hour_of(u32 s) {
    return (int)((s / 3600u) % 24u);
}

static Pet fresh(void) {
    Pet p;
    Coat c = {1, 2, 0};
    pet_new(&p, "MIMI", c, 1, T0);
    return p;
}

static void test_new_defaults(void) {
    Pet p = fresh();
    CHECK(p.full == 8000 && p.fun == 8000 && p.energy == 8000);
    CHECK(p.health == 10000);
    CHECK(pet_stage(&p, T0) == STAGE_KITTEN);
    CHECK(pet_mood(&p, T0) == MOOD_HAPPY);
    CHECK(strcmp(p.name, "MIMI") == 0);
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
        if (m >= 720 && m < 2160)
            CHECK(p.fun <= 3000);
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

// The litter test's care, as a function of the minute since birth: every 6 h
// (14:00, 20:00, 02:00 and 08:00, so the night is woken), and the lights off
// at 20:00 each day.
static void care_at(Pet* p, u32 m) {
    u32 now = T0 + m * MIN;
    if (m % 360)
        return;
    pet_care(p, CARE_MEAL, now);
    for (int i = 0; i < 3; i++)
        pet_care(p, CARE_PET, now);
    pet_care(p, CARE_TREAT, now);
    pet_care(p, CARE_CLEAN, now);
    pet_care(p, CARE_MEDICINE, now);
    if (m % 1440 == 720)
        pet_care(p, CARE_LIGHTS, now);
}

static void test_catch_up_equals_live(void) {
    // A dead pet: once, every minute, and at random points.
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

    // A living pet over six days, cared for, woken at night, lights toggled:
    // live against random splits that stop at each care.
    Pet live = fresh();
    for (u32 k = 1; k <= 8640; k++) {
        pet_advance(&live, T0 + k * MIN, hour_of);
        care_at(&live, k);
    }
    CHECK(!live.dead);
    for (u32 trial = 0; trial < 4; trial++) {
        Pet s = fresh();
        u32 at = 0;
        while (at < 8640) {
            seed = seed * 1103515245u + 12345u;
            u32 to = at + 1 + (seed >> 16) % 900;
            u32 next_care = (at / 360 + 1) * 360;
            if (to > next_care)
                to = next_care;
            pet_advance(&s, T0 + to * MIN, hour_of);
            care_at(&s, to);
            at = to;
        }
        CHECK(memcmp(&s, &live, sizeof s) == 0);
    }

    // Care, the clock back a day, then 3000 minutes at once or in halves.
    Pet x = fresh(), y = fresh();
    pet_advance(&x, T0 + 600 * MIN, hour_of);
    pet_care(&x, CARE_PET, T0 + 600 * MIN);
    y = x;
    pet_advance(&x, T0 + 600 * MIN - 86400, hour_of);
    pet_advance(&y, T0 + 600 * MIN - 86400, hour_of);
    pet_advance(&x, T0 + 600 * MIN - 86400 + 3000 * MIN, hour_of);
    pet_advance(&y, T0 + 600 * MIN - 86400 + 1500 * MIN, hour_of);
    pet_advance(&y, T0 + 600 * MIN - 86400 + 3000 * MIN, hour_of);
    CHECK(memcmp(&x, &y, sizeof x) == 0);
    CHECK(x.dead && x.died_at == T0 + 600 * MIN - 86400 + 2160 * MIN);

    // Care ahead of the stepped life counts as at last_tick.
    Pet z = fresh();
    pet_advance(&z, T0 + 100 * MIN, hour_of);
    pet_care(&z, CARE_PET, T0 + 50 * MIN);
    CHECK(z.last_interaction == T0 + 100 * MIN);
}

static void test_backwards_is_no_time(void) {
    Pet p = fresh();
    pet_advance(&p, T0 + 600 * MIN, hour_of);
    pet_care(&p, CARE_PET, T0 + 600 * MIN);
    pet_advance(&p, T0 + 700 * MIN, hour_of);
    Pet before = p;
    u32 d = 3600, old = T0 + 700 * MIN, now = old - d;
    pet_advance(&p, now, hour_of);
    CHECK(p.last_tick == now);
    CHECK(p.full == before.full && p.fun == before.fun && p.energy == before.energy &&
          p.health == before.health);
    CHECK(pet_age_minutes(&p, now) == pet_age_minutes(&before, old));
    CHECK(pet_neglect_minutes(&p, now) == pet_neglect_minutes(&before, old));
    CHECK(pet_mood(&p, now) == pet_mood(&before, old));
    CHECK(p.born == before.born - d && p.last_interaction == before.last_interaction - d);

    // Stepping on from there is the same life as before the jump.
    Pet q = before;
    pet_advance(&p, now + 60 * MIN, hour_of);
    pet_advance(&q, old + 60 * MIN, hour_of);
    CHECK(p.full == q.full && p.fun == q.fun && p.energy == q.energy && p.health == q.health);
    CHECK(p.asleep == q.asleep && p.droppings == q.droppings);

    // The wake window moves too: woken at 22:00, back a day, asleep at 23:00.
    Pet w = fresh();
    pet_advance(&w, MIDNIGHT + 22 * 3600, hour_of);
    pet_care(&w, CARE_PET, MIDNIGHT + 22 * 3600);
    CHECK(!w.asleep);
    pet_advance(&w, MIDNIGHT + 22 * 3600 - 86400, hour_of);
    CHECK(w.awake_until == MIDNIGHT + 23 * 3600 - 86400);
    pet_advance(&w, MIDNIGHT + 23 * 3600 - 86400, hour_of);
    CHECK(w.asleep);

    // A reset to near the origin saturates at 0 and doesn't crash.
    Pet r = fresh();
    pet_advance(&r, T0 + 100 * MIN, hour_of);
    pet_advance(&r, 1000, hour_of);
    CHECK(r.last_tick == 1000 && r.born == 0 && r.last_interaction == 0);
}

// Rates are checked against exact numbers: a clean awake pet at 08:00.
static void test_needs_rates(void) {
    Pet k = fresh(); // kitten
    pet_advance(&k, T0 + 60 * MIN, hour_of);
    CHECK(k.full == 8000 - 60 * 13 && k.fun == 8000 - 60 * 8 && k.energy == 8000 - 60 * 10);

    Pet a = fresh(); // adolescent
    a.born = T0 - 2000 * MIN;
    pet_advance(&a, T0 + 60 * MIN, hour_of);
    CHECK(a.full == 8000 - 60 * 10 && a.fun == 8000 - 60 * 8 && a.energy == 8000 - 60 * 10);

    Pet s = fresh(); // sick: x3/2, truncated
    s.sickness = 1;
    pet_advance(&s, T0 + 60 * MIN, hour_of);
    CHECK(s.full == 8000 - 60 * 19 && s.fun == 8000 - 60 * 12 && s.energy == 8000 - 60 * 10);
    Pet sa = fresh();
    sa.born = T0 - 2000 * MIN;
    sa.sickness = 1;
    pet_advance(&sa, T0 + 60 * MIN, hour_of);
    CHECK(sa.full == 8000 - 60 * 15 && sa.fun == 8000 - 60 * 12);

    // At neglect 720 fun is capped at 3000.
    Pet n = fresh();
    n.last_interaction = T0 - 1000 * MIN;
    pet_advance(&n, T0 + MIN, hour_of);
    CHECK(n.fun == 3000);
    Pet n2 = fresh();
    n2.last_interaction = T0 - 600 * MIN;
    pet_advance(&n2, T0 + MIN, hour_of);
    CHECK(n2.fun == 8000 - 8);

    // Needs clamp at 0.
    Pet z = fresh();
    z.full = 5;
    pet_advance(&z, T0 + MIN, hour_of);
    CHECK(z.full == 0);
}

static void test_droppings(void) {
    Pet k = fresh(); // a kitten: one every 180 awake minutes
    pet_advance(&k, T0 + 179 * MIN, hour_of);
    CHECK(k.droppings == 0);
    pet_advance(&k, T0 + 180 * MIN, hour_of);
    CHECK(k.droppings == 1);

    Pet a = fresh(); // adolescent: every 240
    a.born = T0 - 2000 * MIN;
    pet_advance(&a, T0 + 239 * MIN, hour_of);
    CHECK(a.droppings == 0);
    pet_advance(&a, T0 + 240 * MIN, hour_of);
    CHECK(a.droppings == 1);
    // Asleep minutes don't count: 21:00 is minute 780 (3 so far, 60 on the clock).
    pet_advance(&a, MIDNIGHT + 24 * 3600 + 6 * 3600 + 59 * 60, hour_of);
    CHECK(a.asleep && a.droppings == 3);
    // Awake again at 07:00: the 4th comes 180 minutes later, not before.
    pet_advance(&a, MIDNIGHT + 24 * 3600 + 7 * 3600 + 179 * MIN, hour_of);
    CHECK(a.droppings == 3);
    pet_advance(&a, MIDNIGHT + 24 * 3600 + 7 * 3600 + 180 * MIN, hour_of);
    CHECK(a.droppings == 4);

    // At most 4.
    Pet c = fresh();
    c.born = T0 - 2000 * MIN;
    c.droppings = 4;
    pet_advance(&c, T0 + 240 * MIN, hour_of);
    CHECK(c.droppings == 4);
}

static void test_health(void) {
    Pet p = fresh();
    p.full = p.fun = p.energy = 0;
    p.health = 5000; // three needs at 0: -6
    pet_advance(&p, T0 + MIN, hour_of);
    CHECK(p.health == 5000 - 6);

    Pet f = fresh();
    f.full = 0;
    pet_advance(&f, T0 + MIN, hour_of);
    CHECK(f.health == 10000 - 2);

    Pet d = fresh(); // dropping penalty: -2 for each beyond 2
    d.health = 5000;
    d.droppings = 2;
    pet_advance(&d, T0 + MIN, hour_of);
    CHECK(d.health == 5000 - 0 && d.sickness == 0);
    d.droppings = 4;
    pet_advance(&d, T0 + 2 * MIN, hour_of);
    CHECK(d.health == 5000 - 4);

    Pet w = fresh(); // all well: +1; not with a dropping, or when sick
    w.health = 5000;
    pet_advance(&w, T0 + MIN, hour_of);
    CHECK(w.health == 5001);
    Pet w2 = fresh();
    w2.health = 5000;
    w2.droppings = 1;
    pet_advance(&w2, T0 + MIN, hour_of);
    CHECK(w2.health == 5000);
    Pet w3 = fresh();
    w3.health = 5000;
    w3.sickness = 1;
    pet_advance(&w3, T0 + MIN, hour_of);
    CHECK(w3.health == 5000);

    Pet z = fresh(); // health 0 makes it sick
    z.full = 0;
    z.health = 2;
    pet_advance(&z, T0 + MIN, hour_of);
    CHECK(z.health == 0 && z.sickness == 1);
}

static void test_meal_and_litter_gate(void) {
    Pet p = fresh();
    p.full = 5000;
    CHECK(pet_care(&p, CARE_MEAL, T0) == CARE_DONE && p.full == 9000);
    p.full = 100;
    pet_care(&p, CARE_TREAT, T0);
    CHECK(p.full == 1600 && p.fun == 9500);

    // Sick at age 8640: no litter until medicine.
    Pet s = fresh();
    s.born = T0 - 8639 * MIN;
    s.sickness = 1;
    pet_advance(&s, T0 + MIN, hour_of);
    CHECK(pet_age_minutes(&s, T0 + MIN) == 8640 && !s.litter_due);
    CHECK(pet_care(&s, CARE_MEDICINE, T0 + MIN) == CARE_DONE);
    pet_advance(&s, T0 + 2 * MIN, hour_of);
    CHECK(s.litter_due);
}

static void test_played_needs_awake_alive(void) {
    Pet d = fresh();
    d.dead = true;
    pet_played(&d, T0 + MIN, 20);
    CHECK(d.fun == 8000 && d.energy == 8000 && d.last_interaction == T0);
    Pet s = fresh();
    s.asleep = true;
    pet_played(&s, T0 + MIN, 20);
    CHECK(s.fun == 8000 && s.energy == 8000 && s.last_interaction == T0);
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

// --- Litter, album and save ---------------------------------------------------

static void test_litter_size_and_names(void) {
    random_seed(777);
    int sizes[5] = {0};
    for (int i = 0; i < 1000; i++) {
        Coat m = {1, 1, 1}, f = {2, 3, 0};
        u8 avoid = (u8)(i % 32);
        Litter l;
        litter_make(&l, m, f, avoid);
        CHECK(l.count >= 2 && l.count <= 4);
        sizes[l.count]++;
        for (int a = 0; a < l.count; a++) {
            CHECK(l.names[a] < 32 && l.names[a] != avoid);
            for (int b = 0; b < a; b++)
                CHECK(l.names[a] != l.names[b]);
        }
    }
    CHECK(sizes[2] > 0 && sizes[3] > 0 && sizes[4] > 0);
    // 255 avoids nothing.
    Litter l;
    litter_make(&l, (Coat){0, 0, 0}, (Coat){0, 0, 0}, 255);
    CHECK(l.count >= 2);
    // The names: 32, A-Z, at most 8 letters, all different.
    for (int i = 0; i < 32; i++) {
        size_t n = strlen(litter_names[i]);
        CHECK(n >= 1 && n <= 8);
        for (size_t k = 0; k < n; k++)
            CHECK(litter_names[i][k] >= 'A' && litter_names[i][k] <= 'Z');
        for (int j = 0; j < i; j++)
            CHECK(strcmp(litter_names[i], litter_names[j]) != 0);
    }
    for (int i = 0; i < 200; i++) {
        Coat c = litter_visitor_coat();
        CHECK(c.shade <= 3 && c.spots <= 3 && c.eyes <= 2);
    }
}

static void test_litter_inheritance(void) {
    random_seed(4242);
    Coat same = {2, 1, 2};
    int kept = 0, traits = 0;
    for (int i = 0; i < 500; i++) {
        Litter l;
        litter_make(&l, same, same, 255);
        for (int k = 0; k < l.count; k++) {
            Coat c = l.coats[k];
            CHECK(c.shade <= 3 && c.spots <= 3 && c.eyes <= 2);
            kept += (c.shade == 2) + (c.spots == 1) + (c.eyes == 2);
            traits += 3;
        }
    }
    CHECK(kept * 100 >= traits * 80);
    CHECK(kept < traits); // mutations do happen
    // Different parents: kittens show both parents' traits.
    int from_m = 0, from_f = 0;
    for (int i = 0; i < 200; i++) {
        Litter l;
        litter_make(&l, (Coat){0, 0, 0}, (Coat){3, 3, 2}, 255);
        for (int k = 0; k < l.count; k++) {
            from_m += l.coats[k].shade == 0;
            from_f += l.coats[k].shade == 3;
        }
    }
    CHECK(from_m > 100 && from_f > 100);
}

static AlbumEntry entry_n(int n) {
    AlbumEntry e;
    memset(&e, 0, sizeof e);
    e.name[0] = (char)('A' + n % 26);
    e.generation = (u8)n;
    e.age_minutes = 1000u * (u32)n;
    e.best[1] = (u16)n;
    return e;
}

static void test_album_ring(void) {
    album_set(0, 0);
    CHECK(album_count() == 0 && album_get(0) == 0);
    for (int i = 1; i <= 17; i++) {
        AlbumEntry e = entry_n(i);
        album_add(&e);
        CHECK(album_count() == (i < 16 ? i : 16));
    }
    CHECK(album_count() == 16);
    CHECK(album_get(0)->generation == 17); // newest first
    CHECK(album_get(15)->generation == 2); // 1 was dropped
    CHECK(album_get(16) == 0);
    AlbumEntry out[ALBUM_MAX];
    CHECK(album_export(out) == 16 && out[3].generation == 14);
    album_set(0, 0);
}

static DenSave sample_save(void) {
    DenSave s;
    memset(&s, 0, sizeof s);
    s.pet = fresh();
    s.pet.sickness = 1;
    s.pet.asleep = true;
    s.pet.droppings = 3;
    s.pet.treats_recent = 2;
    s.pet.dropping_clock = 77;
    s.pet.treat_clock = 33;
    s.pet.died_at = 0;
    s.pet.awake_until = T0 + 99;
    s.pet.energy = 1234;
    s.best[0] = 11;
    s.best[1] = 22;
    s.best[2] = 65535;
    s.album_count = 3;
    for (int i = 0; i < 3; i++)
        s.album[i] = entry_n(i + 1);
    s.last_seen = T0 + 12345;
    s.offset = 0xFFFF0000u;
    s.stage_seen = 2;
    return s;
}

static void test_save_round_trip(void) {
    save_erase(0);
    DenSave out;
    CHECK(!den_save_load(&out)); // none
    DenSave s = sample_save();
    CHECK(den_save_store(&s));
    memset(&out, 0xAA, sizeof out);
    CHECK(den_save_load(&out));
    CHECK(strcmp(out.pet.name, "MIMI") == 0 && out.pet.coat.shade == 1 && out.pet.coat.spots == 2);
    CHECK(out.pet.dropping_clock == 77 && out.pet.treat_clock == 33);
    CHECK(out.pet.awake_until == T0 + 99 && out.pet.energy == 1234 && out.pet.asleep);
    CHECK(out.pet.born == s.pet.born && out.pet.last_tick == s.pet.last_tick);
    CHECK(out.best[2] == 65535 && out.album_count == 3 && out.last_seen == s.last_seen);
    CHECK(out.offset == 0xFFFF0000u && out.stage_seen == 2);
    CHECK(out.album[2].generation == 3 && out.album[1].age_minutes == 2000);
    CHECK(save_slot_size(0) == DEN_SAVE_BYTES);
    // A second round trip is byte-stable.
    CHECK(den_save_store(&out));
    DenSave again;
    CHECK(den_save_load(&again));
    CHECK(memcmp(&again.pet, &out.pet, sizeof out.pet) == 0 && again.last_seen == out.last_seen);
    save_erase(0);
}

static void test_save_refuses_other_version(void) {
    save_erase(0);
    DenSave s = sample_save();
    CHECK(den_save_store(&s));
    u8 raw[DEN_SAVE_BYTES];
    CHECK(save_read(0, raw, sizeof raw, DEN_SAVE_VERSION) == SAVE_OK);
    CHECK(save_write(0, raw, sizeof raw, 2));
    DenSave out;
    CHECK(!den_save_load(&out));
    CHECK(save_write(0, raw, sizeof raw - 1, DEN_SAVE_VERSION)); // another size
    CHECK(!den_save_load(&out));
    CHECK(save_write(0, raw, sizeof raw, DEN_SAVE_VERSION));
    CHECK(den_save_load(&out));
    save_erase(0);
}

static void test_save_refuses_garbage(void) {
    random_seed(99);
    u8 raw[DEN_SAVE_BYTES];
    DenSave out;
    int accepted = 0;
    for (int t = 0; t < 200; t++) {
        for (u32 i = 0; i < sizeof raw; i++)
            raw[i] = (u8)random_range(0, 255);
        CHECK(save_write(0, raw, sizeof raw, 2)); // wrong version
        CHECK(!den_save_load(&out));
        CHECK(save_write(0, raw, sizeof raw - 5, DEN_SAVE_VERSION)); // wrong size
        CHECK(!den_save_load(&out));
        CHECK(save_write(0, raw, sizeof raw, DEN_SAVE_VERSION)); // right ones, random values
        accepted += den_save_load(&out);
    }
    CHECK(accepted == 0);

    // A good save with one value out of range is refused.
    DenSave s = sample_save();
    CHECK(den_save_store(&s));
    CHECK(save_read(0, raw, sizeof raw, DEN_SAVE_VERSION) == SAVE_OK);
    u8 good[DEN_SAVE_BYTES];
    memcpy(good, raw, sizeof raw);
    raw[9] = 9; // shade
    CHECK(save_write(0, raw, sizeof raw, DEN_SAVE_VERSION));
    CHECK(!den_save_load(&out));
    memcpy(raw, good, sizeof raw);
    raw[0] = 'm'; // names are A-Z only
    CHECK(save_write(0, raw, sizeof raw, DEN_SAVE_VERSION));
    CHECK(!den_save_load(&out));
    memcpy(raw, good, sizeof raw);
    raw[12] = 0; // generation 0
    CHECK(save_write(0, raw, sizeof raw, DEN_SAVE_VERSION));
    CHECK(!den_save_load(&out));
    memcpy(raw, good, sizeof raw);
    raw[25] = 0xFF; // full
    raw[26] = 0xFF;
    CHECK(save_write(0, raw, sizeof raw, DEN_SAVE_VERSION));
    CHECK(!den_save_load(&out));
    memcpy(raw, good, sizeof raw);
    raw[DEN_SAVE_BYTES - 1] = 3; // stage_seen past the adult
    CHECK(save_write(0, raw, sizeof raw, DEN_SAVE_VERSION));
    CHECK(!den_save_load(&out));
    memcpy(raw, good, sizeof raw);
    CHECK(save_write(0, raw, sizeof raw, DEN_SAVE_VERSION));
    CHECK(den_save_load(&out));
    save_erase(0);
}

static void test_pet_rebase(void) {
    Pet p = fresh();
    p.awake_until = T0 + 600;
    p.died_at = 0;
    p.last_interaction = T0 - 120;
    // Forward an hour: every set time moves with it; died_at 0 stays unset.
    pet_rebase(&p, T0 + 3600);
    CHECK(p.last_tick == T0 + 3600 && p.born == T0 + 3600);
    CHECK(p.last_interaction == T0 + 3600 - 120 && p.awake_until == T0 + 4200 && p.died_at == 0);
    CHECK(pet_age_minutes(&p, T0 + 3600) == 0 && pet_neglect_minutes(&p, T0 + 3600) == 2);
    // Back two hours, past its start: the same, and nothing below 0.
    pet_rebase(&p, T0 - 3600);
    CHECK(p.last_tick == T0 - 3600 && p.born == T0 - 3600 && p.awake_until == T0 - 3000);
    CHECK(p.last_interaction == T0 - 3600 - 120 && p.died_at == 0);
    // A dead pet's died_at moves; a time earlier than the step stops at 0.
    p.dead = true;
    p.died_at = T0 - 3600;
    p.awake_until = 0;
    p.born = 100;
    pet_rebase(&p, 600);
    CHECK(p.last_tick == 600 && p.died_at == 600 && p.born == 0 && p.awake_until == 0);
    // pet_advance's backward path is the same rebase.
    Pet a = fresh(), b;
    pet_advance(&a, T0 + 90 * MIN, hour_of);
    b = a;
    pet_advance(&a, T0 + 30 * MIN, hour_of);
    pet_rebase(&b, T0 + 30 * MIN);
    CHECK(memcmp(&a, &b, sizeof a) == 0);
}

static void test_clock_resolve(void) {
    u32 now, off;
    const u32 seen = T0 + 5000, age = 3 * 86400;
    // No clock.
    CHECK(clock_resolve(false, false, T0, seen, age, 7, &now, &off) == CLOCK_MISSING);
    CHECK(now == 0 && off == 0);
    // An hour on: the offset is kept and added.
    CHECK(clock_resolve(true, false, seen + 3600 - 100, seen, age, 100, &now, &off) == CLOCK_OK);
    CHECK(now == seen + 3600 && off == 100);
    // Exactly 30 days is still OK; a second more is a jump.
    CHECK(clock_resolve(true, false, seen + CLOCK_JUMP_LIMIT, seen, age, 0, &now, &off) ==
          CLOCK_OK);
    CHECK(clock_resolve(true, false, seen + CLOCK_JUMP_LIMIT + 1, seen, age, 0, &now, &off) ==
          CLOCK_JUMPED);
    CHECK(now == seen + CLOCK_JUMP_LIMIT + 1 && off == 0);
    // Back an hour (daylight saving): the offset is left alone, the hour stays real.
    CHECK(clock_resolve(true, false, seen - 3600, seen, age, 0, &now, &off) == CLOCK_WENT_BACK);
    CHECK(now == seen - 3600 && off == 0);
    // Reset by the clock's flag: game time resumes at last seen.
    CHECK(clock_resolve(true, true, 42, seen, age, 100, &now, &off) == CLOCK_WAS_RESET);
    CHECK(now == seen && off == 100 + (seen - 142) && 42 + off == seen);
    // Reset seen only from the reading: earlier than 2000-01-01 plus the pet's age.
    CHECK(clock_resolve(true, false, age - 1, seen, age, 0, &now, &off) == CLOCK_WAS_RESET);
    CHECK(now == seen && (u32)(age - 1 + off) == seen);
    // A reading at the pet's age is a clock gone back, not a reset.
    CHECK(clock_resolve(true, false, age, seen, age, 0, &now, &off) == CLOCK_WENT_BACK);
    // The offset wraps as unsigned arithmetic: a game clock behind the cartridge's.
    CHECK(clock_resolve(true, true, seen + 500, seen, age, 0, &now, &off) == CLOCK_WAS_RESET);
    CHECK(now == seen && off == (u32)-500 && seen + 500 + off == seen);
    // A second boot after the reset with the kept offset: OK, an hour on.
    CHECK(clock_resolve(true, false, seen + 500 + 3600, seen, age, (u32)-500, &now, &off) ==
          CLOCK_OK);
    CHECK(now == seen + 3600 && off == (u32)-500);
}

// --- Life events ------------------------------------------------------------------------------

static void test_events_next(void) {
    Pet p = fresh();
    CHECK(events_next(&p, STAGE_KITTEN) == EVENT_NONE);
    // Grown to an adolescent: shown once, then not again.
    p.born = T0 - 1440 * MIN;
    CHECK(events_next(&p, STAGE_KITTEN) == EVENT_GROW_UP);
    CHECK(events_next(&p, STAGE_ADOLESCENT) == EVENT_NONE);
    // Kitten to adult while away: one scene, from the stage last shown.
    p.born = T0 - 4320 * MIN;
    CHECK(events_next(&p, STAGE_KITTEN) == EVENT_GROW_UP);
    CHECK(events_next(&p, STAGE_ADULT) == EVENT_NONE);
    // The litter after growing up; growing up first.
    p.litter_due = true;
    CHECK(events_next(&p, STAGE_ADOLESCENT) == EVENT_GROW_UP);
    CHECK(events_next(&p, STAGE_ADULT) == EVENT_LITTER);
    // Death before anything.
    p.dead = true;
    CHECK(events_next(&p, STAGE_KITTEN) == EVENT_TOMBSTONE);
    CHECK(events_next(&p, STAGE_ADULT) == EVENT_TOMBSTONE);
}

static void test_events_live_equals_advance(void) {
    // 30 h away from 08:00 (a night's sleep inside), 40 h (dead), and the
    // litter's last hours: each the pet pet_advance() makes.
    const u32 spans[3] = {30 * 60, 40 * 60, 6 * 60};
    for (int i = 0; i < 3; i++) {
        Pet a = fresh(), b;
        if (i == 2) {
            a.born = T0 - (8640 - 120) * MIN; // an adult two hours from its litter
            a.full = a.fun = a.energy = 9000;
        }
        b = a;
        u32 now = T0 + spans[i] * MIN + 29; // and 29 seconds
        Away w;
        events_live(&a, now, hour_of, &w);
        pet_advance(&b, now, hour_of);
        CHECK(memcmp(&a, &b, sizeof a) == 0);
        CHECK(w.minutes == spans[i]);
        if (i == 0) {
            CHECK(!w.died && w.stage_before == STAGE_KITTEN && w.stage_after == STAGE_ADOLESCENT);
            CHECK(w.slept_minutes == 10 * 60); // 21:00 to 07:00
        }
        if (i == 1)
            CHECK(w.died && a.dead && w.slept_minutes < 40 * 60);
        if (i == 2)
            CHECK(w.litter && !w.died && w.stage_before == STAGE_ADULT);
    }
    // A clock gone back: nothing lived.
    Pet c = fresh();
    pet_advance(&c, T0 + 90 * MIN, hour_of);
    Away w;
    events_live(&c, T0 + 30 * MIN, hour_of, &w);
    CHECK(w.minutes == 0 && !w.died && c.last_tick == T0 + 30 * MIN);
}

static void test_events_summary(void) {
    char lines[EVENT_LINES][EVENT_LINE_MAX];
    char d[16];
    CHECK(events_duration(45, d) == 3 && strcmp(d, "45M") == 0);
    CHECK(events_duration(480, d) == 2 && strcmp(d, "8H") == 0);
    CHECK(events_duration(510, d) == 6 && strcmp(d, "8H 30M") == 0);
    CHECK(events_duration(3 * 1440 + 4 * 60 + 5, d) == 5 && strcmp(d, "3D 4H") == 0);
    CHECK(events_duration(2 * 1440, d) == 2 && strcmp(d, "2D") == 0);
    CHECK(events_duration(0xFFFFFFFFu, d) <= 12);

    // Half an hour away, nothing happened: no card.
    Pet p = fresh();
    Away w;
    events_live(&p, T0 + 30 * MIN, hour_of, &w);
    CHECK(events_summary(&p, &w, lines) == 0);

    // Overnight: slept, grew up, then the mood.
    Pet q = fresh();
    q.born = T0 - 20 * 60 * MIN; // 20 h old at 08:00
    events_live(&q, T0 + 24 * 60 * MIN, hour_of, &w);
    int n = events_summary(&q, &w, lines);
    CHECK(n == 4);
    CHECK(strcmp(lines[0], "AWAY 1D") == 0);
    CHECK(strcmp(lines[1], "MIMI GREW UP!") == 0);
    CHECK(strcmp(lines[2], "MIMI SLEPT 10H") == 0);
    CHECK(strcmp(lines[3], "MIMI IS VERY SICK") == 0); // 24 h without care

    // Gone: two lines, the stone says the rest.
    Pet r = fresh();
    events_live(&r, T0 + 40 * 60 * MIN, hour_of, &w);
    CHECK(events_summary(&r, &w, lines) == 2 && strcmp(lines[1], "MIMI DIED") == 0);
    CHECK(strcmp(lines[0], "AWAY 1D 16H") == 0);

    // Under an hour but the litter came due: a card all the same.
    Pet s = fresh();
    s.born = T0 - (8640 - 10) * MIN;
    s.full = s.fun = s.energy = 9000;
    events_live(&s, T0 + 20 * MIN, hour_of, &w);
    n = events_summary(&s, &w, lines);
    CHECK(n == 2 && strcmp(lines[1], "A VISITOR IS AT THE DEN") == 0);

    // Every line fits the screen with the longest name.
    Pet t = fresh();
    strcpy(t.name, "ABCDEFGH");
    t.born = T0 - 20 * 60 * MIN;
    events_live(&t, T0 + 30u * 1440 * MIN, hour_of, &w);
    n = events_summary(&t, &w, lines);
    for (int i = 0; i < n; i++)
        CHECK(strlen(lines[i]) <= 30);
}

// --- Yarn Bat -----------------------------------------------------------------------------------

static YarnState yarn_ready(s32 x, s32 y, s32 vx, s32 vy) {
    YarnState s;
    yarn_start(&s, 7);
    s.wait = 0;
    s.x = x * 256;
    s.y = y * 256;
    s.vx = vx;
    s.vy = vy;
    return s;
}

static void test_yarn_ball_bounces(void) {
    // The far wall reflects the ball and says so.
    YarnState s = yarn_ready(YARN_WALL_X - YARN_BALL_R - 1, 80, 512, 0);
    CHECK(yarn_step(&s, 0) == YARN_PLAYING);
    CHECK(s.vx == -512 && (s.events & YARN_EV_WALL));
    CHECK(s.x < (YARN_WALL_X - YARN_BALL_R) * 256); // it is back inside
    // The floor and the ceiling reflect the vertical speed.
    s = yarn_ready(120, YARN_FLOOR_Y - YARN_BALL_R - 1, 0, 600);
    yarn_step(&s, 0);
    CHECK(s.vy == -600 && (s.events & YARN_EV_WALL));
    s = yarn_ready(120, YARN_CEIL_Y + YARN_BALL_R + 1, 0, -600);
    yarn_step(&s, 0);
    CHECK(s.vy == 600 && s.y > (YARN_CEIL_Y + YARN_BALL_R) * 256);
    // It waits at the start.
    yarn_start(&s, 3);
    s32 x0 = s.x;
    yarn_step(&s, 0);
    CHECK(s.x == x0 && s.wait == YARN_SERVE_WAIT - 1);
}

// A ball that reaches the paw at y offset `off` from its middle.
static YarnState yarn_bat_at(int off) {
    YarnState s = yarn_ready(YARN_PAW_X + YARN_BALL_R + 1, 80 + off, -400, 0);
    s.paw_y = 80;
    yarn_step(&s, 0);
    return s;
}

static void test_yarn_paddle_hit_angle(void) {
    YarnState mid = yarn_bat_at(0), top = yarn_bat_at(-10), bottom = yarn_bat_at(10);
    CHECK(mid.events & YARN_EV_BAT && mid.bats == 1);
    CHECK(top.events & YARN_EV_BAT && bottom.events & YARN_EV_BAT);
    CHECK(mid.vx > 0 && mid.vy == 0);         // straight back
    CHECK(top.vx > 0 && top.vy < 0);          // the paw's top sends it up
    CHECK(bottom.vx > 0 && bottom.vy > 0);    // its bottom, down
    CHECK(top.vy == -bottom.vy);              // symmetric
    CHECK(-top.vy > 100 && -top.vy > mid.vy); // a real slant
    // The speed is kept (to a rounding step) whatever the angle.
    for (int off = -10; off <= 10; off += 5) {
        YarnState s = yarn_bat_at(off);
        s32 sp2 = s.vx * s.vx + s.vy * s.vy, want = yarn_speed(1) * yarn_speed(1);
        CHECK(sp2 <= want && sp2 > want - 2 * yarn_speed(1) * 2);
    }
}

static void test_yarn_paw_reach(void) {
    // The same ball, 12 pixels off the paw's middle: a small paw misses it, a
    // big one bats it; and the paw stays on the court whatever its reach.
    YarnState small = yarn_ready(YARN_PAW_X + YARN_BALL_R + 1, 92, -400, 0);
    YarnState big = small;
    small.paw_y = big.paw_y = 80;
    yarn_set_reach(&small, 16);
    yarn_set_reach(&big, 32);
    yarn_step(&small, 0);
    yarn_step(&big, 0);
    CHECK(small.events & YARN_EV_MISS && small.bats == 0);
    CHECK(big.events & YARN_EV_BAT && big.bats == 1);
    for (int i = 0; i < 100; i++)
        yarn_step(&big, BUTTON_DOWN);
    CHECK(big.paw_y == YARN_FLOOR_Y - 16);
}

static void test_yarn_speed_cap(void) {
    CHECK(yarn_speed(0) == 384 && yarn_speed(3) == 384);
    CHECK(yarn_speed(4) == 400 && yarn_speed(8) == 416);
    CHECK(yarn_speed(159) == 1008 && yarn_speed(160) == 1024);
    CHECK(yarn_speed(161) == 1024 && yarn_speed(5000) == 1024);
    CHECK(yarn_speed(4000000) == 1024);
    // Fun: 10 + the bats, at most 30 of them.
    CHECK(yarn_fun(0) == 10 && yarn_fun(7) == 17 && yarn_fun(30) == 40 && yarn_fun(900) == 40);
    // A bat at the cap keeps the cap.
    YarnState s = yarn_ready(YARN_PAW_X + YARN_BALL_R + 1, 80, -1024, 0);
    s.paw_y = 80;
    s.bats = 999;
    yarn_step(&s, 0);
    CHECK(s.vx == 1024 && s.bats == 1000);
}

static void test_yarn_miss_ends(void) {
    YarnState s = yarn_ready(YARN_PAW_X + YARN_BALL_R + 1, 130, -400, 0);
    s.paw_y = 40; // far from the ball
    int steps = 0;
    bool missed = false;
    while (yarn_step(&s, 0) == YARN_PLAYING && steps < 500) {
        steps++;
        missed |= (s.events & YARN_EV_MISS) != 0;
    }
    missed |= (s.events & YARN_EV_MISS) != 0;
    CHECK(missed && s.missed && s.bats == 0 && steps < 500);
    CHECK(s.x < 0);
    // A whole round with no input ends: the kitten stands still and the ball
    // returns to a paw that happens to be in the way, or not.
    yarn_start(&s, 99);
    int n = 0;
    while (yarn_step(&s, 0) == YARN_PLAYING && n < 5000)
        n++;
    CHECK(n < 5000);
    // Moving the paw: held up and down it stays on the court.
    for (int i = 0; i < 100; i++)
        yarn_step(&s, BUTTON_UP);
    CHECK(s.paw_y == YARN_CEIL_Y + YARN_PADDLE_H / 2);
    yarn_start(&s, 1);
    for (int i = 0; i < 100; i++)
        yarn_step(&s, BUTTON_DOWN);
    CHECK(s.paw_y == YARN_FLOOR_Y - YARN_PADDLE_H / 2);
}

// --- Acacia Climb -------------------------------------------------------------------------------

#define UNIT (CLIMB_ROW_H * 256)

static int span_gap(int alo, int ahi, int blo, int bhi) {
    if (ahi < blo)
        return blo - ahi;
    if (bhi < alo)
        return alo - bhi;
    return 0;
}

// The smallest gap between a bough of the row below and one of this row (the
// part of it a nest leaves, a swaying one at its nearest swing).
static int row_gap(const ClimbRow* below, const ClimbRow* row) {
    int best = 1000;
    for (int i = 0; i < below->count; i++)
        for (int j = 0; j < row->count; j++) {
            int alo, ahi, blo, bhi;
            climb_branch_free(&below->b[i], true, &alo, &ahi);
            climb_branch_free(&row->b[j], true, &blo, &bhi);
            int g = span_gap(alo, ahi, blo, bhi);
            if (g < best)
                best = g;
        }
    return best;
}

static void test_climb_rows_reachable(void) {
    static const u32 seeds[] = {1, 7, 12345, 0xDEADBEEFu, 99, 0x1234567u};
    int reach = climb_jump_reach();
    CHECK(reach >= 56 && reach <= 64);
    int bad = 0, rows = 0, worst = 0, sways = 0, snaps = 0, nests = 0, early = 0, alternate = 0;
    for (unsigned k = 0; k < sizeof seeds / sizeof seeds[0]; k++)
        for (int r = 1; r <= 1700; r++) {
            ClimbRow below, row;
            climb_row(seeds[k], r - 1, &below);
            climb_row(seeds[k], r, &row);
            rows++;
            int g = row_gap(&below, &row);
            if (g > reach)
                bad++;
            if (g > worst)
                worst = g;
            for (int i = 0; i < row.count; i++) {
                sways += (row.b[i].flags & CLIMB_SWAYS) != 0;
                snaps += (row.b[i].flags & CLIMB_SNAPS) != 0;
                nests += (row.b[i].flags & CLIMB_NESTS) != 0;
                if (r < 3 && row.b[i].flags & CLIMB_SWAYS)
                    early++;
                if (r < 5 && row.b[i].flags & CLIMB_SNAPS)
                    early++;
                if (r < 8 && row.b[i].flags & CLIMB_NESTS)
                    early++;
            }
            // The main bough alternates sides; a second one is on the other.
            bool left = row.b[0].x1 <= CLIMB_TRUNK_X;
            alternate += left == ((r & 1) == 0);
            if (row.count == 2)
                alternate += (row.b[1].x1 <= CLIMB_TRUNK_X) != left;
            else
                alternate++;
        }
    CHECK(rows == 10200);
    CHECK(bad == 0);
    CHECK(worst <= reach - 6); // room for a human
    CHECK(sways > 500 && snaps > 500 && nests > 300);
    CHECK(early == 0);
    CHECK(alternate == 2 * rows);
    // The top of the tree, and the rows just under it, are as reachable.
    for (unsigned k = 0; k < sizeof seeds / sizeof seeds[0]; k++)
        for (int r = CLIMB_ROWS_MAX - 200; r <= CLIMB_ROWS_MAX; r++) {
            ClimbRow below, row;
            climb_row(seeds[k], r - 1, &below);
            climb_row(seeds[k], r, &row);
            CHECK(row_gap(&below, &row) <= reach - 6);
        }
    // The tree is a function of the seed.
    ClimbRow a, b;
    climb_row(5, 40, &a);
    climb_row(5, 40, &b);
    CHECK(a.count == b.count && a.b[0].x0 == b.b[0].x0 && a.b[0].flags == b.b[0].flags);
    // The ledge at the foot.
    climb_row(5, 0, &a);
    CHECK(a.count == 1 && a.b[0].x0 == 48 && a.b[0].x1 == 192 && a.b[0].flags == 0);
}

// The arc: how high a held and a tapped jump go, straight up from the ledge.
static int jump_peak(int hold_frames) {
    ClimbState s;
    climb_start(&s, 3);
    s.cam = -100000; // far above: the round does not end
    s.x = 100 * 256;
    int top = 0;
    for (int i = 0; i < 80; i++) {
        climb_step(&s, i < hold_frames ? BUTTON_A : 0);
        int h = (int)(-s.y / 256);
        if (h > top)
            top = h;
    }
    return top;
}

static void test_climb_jump_arc(void) {
    int held = jump_peak(80), tapped = jump_peak(1);
    CHECK(held >= 45 && held <= 51); // a row (40) and a bit
    CHECK(tapped >= 8 && tapped <= 20);
    // Held to a point, the rise is in between.
    int mid = jump_peak(8);
    CHECK(mid > tapped && mid < held);
}

// Lands on row 1 from the ledge, rising through it first (one-way).
static void test_climb_one_way_and_land(void) {
    ClimbState s;
    climb_start(&s, 11);
    s.cam = -100000;
    ClimbRow r1;
    climb_row(11, 1, &r1);
    s.x = (r1.b[0].x0 + 4) * 256;
    bool through = false, landed = false;
    int rows_before = 0;
    for (int i = 0; i < 90 && !landed; i++) {
        climb_step(&s, i < 40 ? BUTTON_A : 0);
        if (!landed && s.y < -UNIT && s.vy < 0)
            through = true; // above row 1 and still rising
        if (s.events & CLIMB_EV_LAND) {
            landed = true;
            rows_before = s.rows;
        }
    }
    CHECK(through && landed);
    CHECK(s.standing && s.row == 1 && s.rows == 1 && rows_before == 1);
    CHECK(s.y == -UNIT);
}

static void test_climb_snap_timer(void) {
    // A dry bough (row 5 or higher) that does not sway.
    int row = 5, idx = 0;
    ClimbRow r;
    for (;; row++) {
        climb_row(21, row, &r);
        if (r.b[0].flags & CLIMB_SNAPS)
            break;
    }
    ClimbState s;
    climb_start(&s, 21);
    s.cam = -row * CLIMB_ROW_H - 100;
    s.standing = false;
    s.x = ((r.b[idx].x0 + r.b[idx].x1) / 2) * 256;
    s.y = -row * UNIT - 3 * 256;
    s.vy = 256;
    int n = 0;
    while (!(s.events & CLIMB_EV_LAND) && n++ < 20)
        climb_step(&s, 0);
    CHECK(s.events & CLIMB_EV_LAND);
    CHECK(s.events & CLIMB_EV_CRACK);
    CHECK(s.standing && s.row == row);
    CHECK(climb_snap_left(&s, row, idx) == CLIMB_SNAP_FRAMES);
    for (int i = 1; i < CLIMB_SNAP_FRAMES; i++) {
        climb_step(&s, 0);
        CHECK(!(s.events & CLIMB_EV_SNAP) && s.standing);
    }
    CHECK(!climb_broken(&s, row, idx));
    climb_step(&s, 0); // the 30th frame after landing
    CHECK(s.events & CLIMB_EV_SNAP);
    CHECK(s.ev_row == row && s.ev_idx == idx);
    CHECK(climb_broken(&s, row, idx) && !s.standing);
    // It is gone: falling onto it lands on nothing.
    s.y = -row * UNIT - 3 * 256;
    s.vy = 256;
    for (int i = 0; i < 6; i++)
        climb_step(&s, 0);
    CHECK(!s.standing || s.row != row);
    // Jumping off before it breaks does not save it.
    climb_start(&s, 21);
    s.cam = -row * CLIMB_ROW_H - 100;
    s.standing = false;
    s.x = ((r.b[idx].x0 + r.b[idx].x1) / 2) * 256;
    s.y = -row * UNIT - 3 * 256;
    s.vy = 256;
    while (!(s.events & CLIMB_EV_LAND))
        climb_step(&s, 0);
    climb_step(&s, BUTTON_A);
    CHECK(s.events & CLIMB_EV_JUMP && !s.standing);
    bool snapped = false;
    for (int i = 0; i < CLIMB_SNAP_FRAMES; i++) {
        climb_step(&s, 0);
        snapped |= (s.events & CLIMB_EV_SNAP) != 0;
        s.cam = -100000;
    }
    CHECK(snapped && climb_broken(&s, row, idx));
}

static void test_climb_fall_ends(void) {
    ClimbState s;
    climb_start(&s, 4);
    // Walk off the ledge's left end.
    int n = 0;
    ClimbResult r = CLIMB_PLAYING;
    s32 cam = s.cam;
    while (r == CLIMB_PLAYING && n < 400) {
        r = climb_step(&s, BUTTON_LEFT);
        CHECK(s.cam >= cam); // the camera never goes down
        cam = s.cam;
        n++;
    }
    CHECK(r == CLIMB_OVER && (s.events & CLIMB_EV_OVER));
    CHECK(n > 40 && n < 100);
    CHECK(s.rows == 0 && !s.standing);
    CHECK(s.y / 256 - s.cam > CLIMB_FALL_LINE);
    // Standing still, nothing happens.
    climb_start(&s, 4);
    for (int i = 0; i < 1000; i++)
        CHECK(climb_step(&s, 0) == CLIMB_PLAYING);
}

static void test_climb_nest_knocks(void) {
    int row = 8, idx = 0;
    ClimbRow r;
    for (;; row++) {
        climb_row(33, row, &r);
        for (idx = 0; idx < r.count; idx++)
            if (r.b[idx].flags & CLIMB_NESTS)
                break;
        if (idx < r.count)
            break;
    }
    ClimbState s;
    climb_start(&s, 33);
    s.standing = false;
    s.x = (r.b[idx].nest_x - 3) * 256;
    s.y = -(row * CLIMB_ROW_H - 26) * 256; // under the bough, rising into the nest
    s.vy = -768;
    s.cam = s.y / 256 - 100;
    climb_step(&s, 0);
    CHECK(s.events & CLIMB_EV_NEST);
    CHECK(s.vy > 0 && s.push < 0); // down, away from the nest (it was to its left)
    CHECK(s.ev_row == row && s.ev_idx == idx);
    // The next frames do not knock again, and it falls.
    climb_step(&s, 0);
    CHECK(!(s.events & CLIMB_EV_NEST) && s.vy > 0);
    // Standing on the bough, the serval is not touched.
    climb_start(&s, 33);
    climb_stand_on(&s, row, idx, r.b[idx].nest_x + 2 * (r.b[idx].inner_right ? -1 : 1) * 6);
    s.cam = s.y / 256 - 100;
    for (int i = 0; i < 20; i++) {
        climb_step(&s, 0);
        CHECK(!(s.events & CLIMB_EV_NEST));
    }
}

static void test_climb_score_and_fun(void) {
    CHECK(climb_fun(0) == 10 && climb_fun(1) == 10 && climb_fun(2) == 11);
    CHECK(climb_fun(40) == 30 && climb_fun(60) == 40 && climb_fun(61) == 40);
    CHECK(climb_fun(100000) == 40);
    ClimbState s;
    climb_start(&s, 8);
    s.cam = -100000;
    climb_stand_on(&s, 9, 0, 100);
    CHECK(s.rows == 9);
    climb_stand_on(&s, 4, 0, 100);
    CHECK(s.rows == 9); // the score is the highest
}

static void test_climb_early_jump_is_buffered(void) {
    ClimbState s;
    climb_start(&s, 6);
    s.cam = -100000;
    // Press A two frames before touching down: it jumps on landing.
    s.standing = false;
    s.x = 120 * 256;
    s.y = -3 * 256;
    s.vy = 256;
    climb_step(&s, BUTTON_A);
    CHECK(!s.standing);
    int n = 0;
    while (!(s.events & CLIMB_EV_LAND) && n++ < 10)
        climb_step(&s, BUTTON_A);
    CHECK(s.events & CLIMB_EV_LAND);
    climb_step(&s, BUTTON_A);
    CHECK(s.events & CLIMB_EV_JUMP);
}

// The ramp: the warm-up is gentle, the hardest from row 60, and the tree up
// there has more hazards, wider gaps and a quicker sway than the warm-up's.
static int inner_of(const ClimbBranch* b) {
    return b->inner_right ? CLIMB_TRUNK_X - b->x1 : b->x0 - CLIMB_TRUNK_X;
}

static void test_climb_ramp(void) {
    CHECK(climb_ramp(0) == 0 && climb_ramp(CLIMB_WARMUP_ROWS) == 0);
    CHECK(climb_ramp(34) > 100 && climb_ramp(34) < 156);
    CHECK(climb_ramp(CLIMB_HARD_ROW) == 256 && climb_ramp(5000) == 256);
    CHECK(climb_sway_period(5) == CLIMB_SWAY_PERIOD);
    CHECK(climb_sway_period(CLIMB_HARD_ROW) == CLIMB_SWAY_PERIOD_HARD);
    int warm_hazards = 0, warm_rows = 0, hard_hazards = 0, hard_rows = 0;
    int warm_gap = 0, hard_gap = 0;
    for (u32 seed = 1; seed <= 6; seed++) {
        for (int r = 9; r <= 16; r++) {
            ClimbRow row;
            climb_row(seed, r, &row);
            warm_rows++;
            warm_hazards += row.b[0].flags != 0;
            if (inner_of(&row.b[0]) > warm_gap)
                warm_gap = inner_of(&row.b[0]);
        }
        for (int r = 60; r <= 400; r++) {
            ClimbRow row;
            climb_row(seed, r, &row);
            hard_rows++;
            hard_hazards += row.b[0].flags != 0;
            if (inner_of(&row.b[0]) > hard_gap)
                hard_gap = inner_of(&row.b[0]);
        }
    }
    // Hazard shares in percent: at least twice as many up there.
    CHECK(hard_hazards * 100 / hard_rows >= 2 * (warm_hazards * 100 / warm_rows));
    CHECK(warm_gap <= 16 && hard_gap >= 20 && hard_gap <= CLIMB_IO_HARD);
    // A swaying bough up there is back where it was after the hard period,
    // not the easy one.
    ClimbRow row;
    int found = -1;
    for (int r = 100; found < 0; r++) {
        climb_row(3, r, &row);
        if (row.b[0].flags & CLIMB_SWAYS)
            found = r;
    }
    CHECK(climb_sway(found, 0, 7) == climb_sway(found, 0, 7 + CLIMB_SWAY_PERIOD_HARD));
    CHECK(climb_sway(found, 0, 7) != climb_sway(found, 0, 7 + CLIMB_SWAY_PERIOD));
}

// The top: landing on row CLIMB_ROWS_MAX ends the round, and nothing is
// above it.
static void test_climb_top_of_the_tree(void) {
    ClimbRow r;
    int seed = 9;
    // A seed whose top bough is still.
    for (;; seed++) {
        climb_row((u32)seed, CLIMB_ROWS_MAX, &r);
        if (!(r.b[0].flags & CLIMB_SWAYS))
            break;
    }
    ClimbState s;
    climb_start(&s, (u32)seed);
    s.standing = false;
    s.rows = CLIMB_ROWS_MAX - 1;
    s.x = ((r.b[0].x0 + r.b[0].x1) / 2) * 256;
    s.y = -CLIMB_ROWS_MAX * UNIT - 3 * 256;
    s.cam = s.y / 256 - 100;
    s.vy = 256;
    ClimbResult res = CLIMB_PLAYING;
    for (int i = 0; i < 20 && res == CLIMB_PLAYING; i++)
        res = climb_step(&s, 0);
    CHECK(res == CLIMB_TOP && (s.events & CLIMB_EV_TOP));
    CHECK(s.rows == CLIMB_ROWS_MAX && s.row == CLIMB_ROWS_MAX && s.standing);
    // Past the top's height there is nothing to land on.
    climb_start(&s, (u32)seed);
    s.standing = false;
    s.x = (r.b[0].x0 + 4) * 256;
    s.y = -(CLIMB_ROWS_MAX + 1) * UNIT - 3 * 256;
    s.cam = s.y / 256 - 100;
    s.vy = 256;
    for (int i = 0; i < 40 && s.y < -CLIMB_ROWS_MAX * UNIT - 2 * 256; i++)
        climb_step(&s, 0);
    CHECK(s.y > -CLIMB_ROWS_MAX * UNIT - 2 * 256 || !s.standing);
    CHECK(s.rows == 0 || s.rows == CLIMB_ROWS_MAX);
}

// A simple bot plays the real physics: it jumps toward the next row's nearest
// bough (counted where that bough will be when it lands) and steers under it.
// It must keep climbing (the tree can't be unwinnable), and at no time may
// every bough of the next row be out of reach of the bough it stands on for
// longer than a sway can take to bring one in.
typedef struct {
    int rows, blocked_run, blocked_max;
    bool over;
} BotRun;

static const ClimbBranch* bot_cur(const ClimbRow* cur, int idx) {
    return &cur->b[idx < cur->count ? idx : 0];
}

static BotRun bot_climb(u32 seed, int target_rows, int max_frames) {
    ClimbState s;
    climb_start(&s, seed);
    BotRun out = {0};
    int reach = climb_jump_reach();
    for (int f = 0; f < max_frames && s.rows < target_rows; f++) {
        int tr = s.row + 1; // the next row above the bough it last stood on
        ClimbRow nxt, cur;
        climb_row(seed, tr, &nxt);
        climb_row(seed, s.row, &cur);
        int px = (int)(s.x / 256);
        const ClimbBranch* cb = bot_cur(&cur, s.idx);
        int cur_lo, cur_hi;
        climb_branch_free(cb, false, &cur_lo, &cur_hi);
        int cur_off = (cb->flags & CLIMB_SWAYS) ? climb_sway(s.row, s.idx, s.frame) : 0;
        int best_gap = 1000, best_lo = 0, best_hi = 0, best_off = 0;
        bool any_reach = false;
        for (int j = 0; j < nxt.count; j++) {
            int lo, hi;
            climb_branch_free(&nxt.b[j], false, &lo, &hi);
            bool sways = (nxt.b[j].flags & CLIMB_SWAYS) != 0;
            int off = sways ? climb_sway(tr, j, s.frame + 28) : 0; // where it will be on landing
            int now = sways ? climb_sway(tr, j, s.frame) : 0;
            if (span_gap(cur_lo + cur_off, cur_hi + cur_off, lo + now, hi + now) <= reach)
                any_reach = true;
            int g = px < lo + off ? lo + off - px : px > hi + off ? px - hi - off : 0;
            if (g < best_gap) {
                best_gap = g;
                best_lo = lo;
                best_hi = hi;
                best_off = off;
            }
        }
        if (any_reach)
            out.blocked_run = 0;
        else if (++out.blocked_run > out.blocked_max)
            out.blocked_max = out.blocked_run;
        int cx = px < best_lo + best_off + 6   ? best_lo + best_off + 6
                 : px > best_hi + best_off - 6 ? best_hi + best_off - 6
                                               : px;
        u16 keys = 0;
        if (s.standing) {
            if (best_gap > 36)
                keys = px < cx ? BUTTON_RIGHT : BUTTON_LEFT;
            else
                keys = BUTTON_A | (px < cx - 3 ? BUTTON_RIGHT : px > cx + 3 ? BUTTON_LEFT : 0);
        } else {
            int h = (int)(-s.y / 256);
            if (s.vy < 0 && h < tr * CLIMB_ROW_H + 6)
                keys |= BUTTON_A;
            keys |= px < cx - 2 ? BUTTON_RIGHT : px > cx + 2 ? BUTTON_LEFT : 0;
        }
        if (climb_step(&s, keys) == CLIMB_OVER) {
            out.over = true;
            break;
        }
    }
    out.rows = s.rows;
    return out;
}

static void test_climb_bot_climbs(void) {
    static const u32 seeds[] = {1234, 12, 20, 77, 4242};
    for (unsigned k = 0; k < sizeof seeds / sizeof seeds[0]; k++) {
        BotRun r = bot_climb(seeds[k], 40, 20000);
        CHECK(r.rows >= 30);
        CHECK(r.blocked_max <= CLIMB_SWAY_PERIOD); // never stuck longer than a swing
    }
}

// --- High Pounce -------------------------------------------------------------------------------

static PounceState pounce_ready(int x) {
    PounceState s;
    pounce_start(&s, 5);
    s.quiet = true;
    memset(s.r, 0, sizeof s.r);
    s.x = x * 256;
    return s;
}

// A rustle that stays where it is.
static int pounce_still(PounceState* s, int kind, int x) {
    int i = pounce_put(s, kind, x);
    s->r[i].turn = 60000;
    s->r[i].life = 60000;
    s->r[i].pulse = 60000;
    return i;
}

// Presses A, then lets go until the serval has landed.
static void pounce_leap(PounceState* s) {
    pounce_step(s, BUTTON_A);
    for (int i = 1; i < POUNCE_LEAP_FRAMES; i++)
        pounce_step(s, 0);
}

static void test_pounce_leap_lands_at_reach(void) {
    PounceState s = pounce_ready(100);
    pounce_step(&s, BUTTON_A);
    CHECK(s.events & POUNCE_EV_LEAP);
    CHECK(s.leap == 1);
    int top = 0;
    for (int i = 1; i < POUNCE_LEAP_FRAMES; i++) {
        CHECK(!(s.events & POUNCE_EV_LAND));
        pounce_step(&s, 0);
        if (pounce_height(&s) > top)
            top = pounce_height(&s);
    }
    CHECK(s.events & POUNCE_EV_LAND);
    CHECK(top == POUNCE_PEAK && pounce_height(&s) == 0);
    CHECK(pounce_x(&s) == 100 + POUNCE_REACH);
    // To the left, from the same place.
    s = pounce_ready(100);
    pounce_step(&s, BUTTON_LEFT);
    pounce_step(&s, 0);
    int x0 = pounce_x(&s);
    pounce_leap(&s);
    CHECK(s.face == -1 && pounce_x(&s) == x0 - POUNCE_REACH);
    // Steering in the air does nothing; at the edge the landing is held inside.
    s = pounce_ready(100);
    pounce_step(&s, BUTTON_A);
    for (int i = 1; i < POUNCE_LEAP_FRAMES; i++)
        pounce_step(&s, BUTTON_LEFT);
    CHECK(pounce_x(&s) == 100 + POUNCE_REACH);
    s = pounce_ready(POUNCE_X_MAX - 10);
    pounce_leap(&s);
    CHECK(pounce_x(&s) == POUNCE_X_MAX);
    // A held down is one leap, not many.
    s = pounce_ready(60);
    for (int i = 0; i < POUNCE_LEAP_FRAMES + POUNCE_RECOVER + 5; i++)
        pounce_step(&s, BUTTON_A);
    CHECK(pounce_x(&s) == 60 + POUNCE_REACH && s.leap == 0);
}

static void test_pounce_catch_radius(void) {
    int land = 100 + POUNCE_REACH;
    // Within the radius: caught, +1, no scare.
    for (int off = -POUNCE_CATCH_R; off <= POUNCE_CATCH_R; off += POUNCE_CATCH_R) {
        PounceState s = pounce_ready(100);
        int m = pounce_still(&s, PK_MOUSE, land + off);
        int other = pounce_still(&s, PK_MOUSE, land + 30);
        pounce_leap(&s);
        CHECK(s.events & POUNCE_EV_CATCH);
        CHECK(s.points == 1 && s.mice == 1 && s.r[m].state == PR_FREE);
        CHECK(s.r[other].state == PR_ACTIVE && !(s.events & POUNCE_EV_MISS));
    }
    // Just outside: a miss, and a mouse 13 px away is scared, not caught.
    for (int off = -POUNCE_CATCH_R - 1; off <= POUNCE_CATCH_R + 1; off += 2 * POUNCE_CATCH_R + 2) {
        PounceState s = pounce_ready(100);
        int m = pounce_still(&s, PK_MOUSE, land + off);
        pounce_leap(&s);
        CHECK(!(s.events & POUNCE_EV_CATCH) && (s.events & POUNCE_EV_MISS));
        CHECK(s.points == 0 && s.r[m].state == PR_SCARED);
    }
    // Only a mouse is caught by landing on it; the nearest of two.
    PounceState s = pounce_ready(100);
    int far = pounce_still(&s, PK_MOUSE, land - 10);
    int near = pounce_still(&s, PK_MOUSE, land + 3);
    pounce_leap(&s);
    CHECK(s.r[near].state == PR_FREE && s.r[far].state == PR_ACTIVE && s.points == 1);
}

static void test_pounce_miss_scares(void) {
    int land = 100 + POUNCE_REACH;
    PounceState s = pounce_ready(100);
    int in = pounce_still(&s, PK_MOUSE, land - POUNCE_SCARE_R);
    int out = pounce_still(&s, PK_MOUSE, land + POUNCE_SCARE_R + 1);
    pounce_leap(&s);
    CHECK(s.r[in].state == PR_SCARED && s.r[out].state == PR_ACTIVE);
    // Quiet for two seconds, then back where it was.
    for (int i = 0; i < POUNCE_SCARE_FRAMES - 2; i++)
        pounce_step(&s, 0);
    CHECK(s.r[in].state == PR_SCARED);
    pounce_step(&s, 0);
    CHECK(s.r[in].state == PR_ACTIVE && (int)(s.r[in].x / 256) == land - POUNCE_SCARE_R);
    // A scared rustle makes no burst.
    s = pounce_ready(100);
    in = pounce_put(&s, PK_MOUSE, land);
    s.r[in].turn = 60000;
    s.r[in].pulse = POUNCE_SCARE_FRAMES / 2;
    s.r[in].state = PR_SCARED;
    s.r[in].timer = POUNCE_SCARE_FRAMES;
    for (int i = 0; i < POUNCE_SCARE_FRAMES - 1; i++) {
        pounce_step(&s, 0);
        CHECK(!s.bursts);
    }
}

static void test_pounce_bird_flush_distance(void) {
    // Walking: within 24 px flushes, 25 does not.
    PounceState s = pounce_ready(100);
    int b = pounce_still(&s, PK_BIRD, 100 + POUNCE_FLUSH_R + 1);
    pounce_step(&s, 0);
    CHECK(s.r[b].state == PR_ACTIVE);
    s = pounce_ready(100);
    b = pounce_still(&s, PK_BIRD, 100 + POUNCE_FLUSH_R);
    pounce_step(&s, 0);
    CHECK(s.r[b].state == PR_FLY && (s.events & POUNCE_EV_FLUSH));
    // Crouched, it stays.
    s = pounce_ready(100);
    b = pounce_still(&s, PK_BIRD, 100 + 5);
    for (int i = 0; i < 30; i++)
        pounce_step(&s, BUTTON_B);
    CHECK(s.crouch && s.r[b].state == PR_ACTIVE);
    // Creeping up on it crouched does not flush it either; standing up does.
    s = pounce_ready(60);
    b = pounce_still(&s, PK_BIRD, 100);
    while (pounce_x(&s) < 95)
        pounce_step(&s, BUTTON_B | BUTTON_RIGHT);
    CHECK(s.r[b].state == PR_ACTIVE);
    pounce_step(&s, 0);
    CHECK(s.r[b].state == PR_FLY);
    // It flies up and away from the serval, and off the screen.
    CHECK(s.r[b].vx > 0);
    int top = 0;
    for (int i = 0; i < 400 && s.r[b].state == PR_FLY; i++) {
        pounce_step(&s, 0);
        if ((int)(s.r[b].h / 256) > top)
            top = (int)(s.r[b].h / 256);
    }
    CHECK(top == POUNCE_BIRD_H && s.r[b].state == PR_FREE);
    // A mouse is never flushed.
    s = pounce_ready(100);
    int m = pounce_still(&s, PK_MOUSE, 100);
    pounce_step(&s, 0);
    CHECK(s.r[m].state == PR_ACTIVE);
}

static void test_pounce_swat(void) {
    // A bird in the air within reach is swatted by A: +3, a hit.
    PounceState s = pounce_ready(100);
    int b = pounce_still(&s, PK_BIRD, 100 + POUNCE_FLUSH_R);
    pounce_step(&s, 0);
    CHECK(s.r[b].state == PR_FLY);
    int other = pounce_still(&s, PK_MOUSE, 100 + POUNCE_REACH + 30);
    for (int i = 0; i < 4; i++)
        pounce_step(&s, 0);
    pounce_step(&s, BUTTON_A);
    CHECK((s.events & POUNCE_EV_SWAT) && s.points == 3 && s.birds == 1);
    CHECK(s.r[b].state == PR_FREE);
    for (int i = 1; i < POUNCE_LEAP_FRAMES; i++)
        pounce_step(&s, 0);
    CHECK(!(s.events & POUNCE_EV_MISS) && s.r[other].state == PR_ACTIVE);
    // Out of reach: no swat, and the leap misses.
    s = pounce_ready(100);
    b = pounce_still(&s, PK_BIRD, 100 + POUNCE_FLUSH_R);
    pounce_step(&s, 0);
    s.r[b].x = (100 + POUNCE_REACH + 1) * 256;
    pounce_step(&s, BUTTON_A);
    CHECK(!(s.events & POUNCE_EV_SWAT) && s.r[b].state == PR_FLY && s.points == 0);
    // It is swatted from the other side too, the leap turning to it.
    s = pounce_ready(100);
    b = pounce_still(&s, PK_BIRD, 100 - POUNCE_FLUSH_R);
    pounce_step(&s, 0);
    pounce_step(&s, BUTTON_A);
    CHECK((s.events & POUNCE_EV_SWAT) && s.face == -1);
    // A bird the landing is on takes off (a hit, +0).
    s = pounce_ready(100);
    b = pounce_still(&s, PK_BIRD, 100 + POUNCE_REACH);
    pounce_leap(&s);
    CHECK(s.r[b].state == PR_FLY && s.points == 0 && !(s.events & POUNCE_EV_MISS));
}

static void test_pounce_round_timer(void) {
    PounceState s;
    pounce_start(&s, 1);
    CHECK(pounce_seconds_left(&s) == 60);
    for (int i = 1; i < POUNCE_ROUND_FRAMES; i++) {
        CHECK(pounce_step(&s, 0) == POUNCE_PLAYING);
        if (i == 1)
            CHECK(pounce_seconds_left(&s) == 60);
        if (i == 60)
            CHECK(pounce_seconds_left(&s) == 59);
    }
    CHECK(pounce_seconds_left(&s) == 1);
    CHECK(pounce_step(&s, 0) == POUNCE_OVER);
    CHECK(pounce_seconds_left(&s) == 0);
    // Rustles keep appearing, never more than POUNCE_ACTIVE at a time.
    pounce_start(&s, 2);
    int seen = 0;
    for (int i = 0; i < 1200; i++) {
        pounce_step(&s, 0);
        int n = 0;
        for (int k = 0; k < POUNCE_SLOTS; k++)
            n += s.r[k].state == PR_ACTIVE || s.r[k].state == PR_SCARED;
        CHECK(n <= POUNCE_ACTIVE);
        seen += s.bursts != 0;
    }
    CHECK(seen > 5);
}

static void test_pounce_hearing_and_fun(void) {
    CHECK(pounce_loudness(0) == 4 && pounce_loudness(27) == 4 && pounce_loudness(28) == 3);
    CHECK(pounce_loudness(-55) == 3 && pounce_loudness(56) == 2 && pounce_loudness(96) == 1);
    CHECK(pounce_loudness(POUNCE_HEAR_R - 1) == 1 && pounce_loudness(POUNCE_HEAR_R) == 0);
    CHECK(pounce_fun(0) == 10 && pounce_fun(12) == 22 && pounce_fun(30) == 40);
    CHECK(pounce_fun(31) == 40 && pounce_fun(5000) == 40);
    // A burst heard points the ears at it, and follows the serval.
    PounceState s = pounce_ready(100);
    int m = pounce_still(&s, PK_MOUSE, 40);
    s.r[m].pulse = 1;
    pounce_step(&s, 0);
    CHECK(s.bursts == 1 << m);
    int dx = 0;
    CHECK(pounce_heard(&s, &dx) > 0 && dx == -60);
    pounce_step(&s, BUTTON_LEFT);
    CHECK(pounce_heard(&s, &dx) > 0 && dx > -60 && dx < 0);
    for (int i = 0; i < POUNCE_HEARD_FRAMES; i++)
        pounce_step(&s, 0);
    CHECK(pounce_heard(&s, &dx) == 0);
    // Too far to hear: no ears.
    s = pounce_ready(230);
    m = pounce_still(&s, PK_MOUSE, 40);
    s.r[m].pulse = 1;
    pounce_step(&s, 0);
    CHECK(s.bursts == 1 << m && pounce_heard(&s, &dx) == 0);
    // Crouching is slower.
    s = pounce_ready(100);
    pounce_step(&s, BUTTON_RIGHT);
    int walked = (int)(s.x - 100 * 256);
    s = pounce_ready(100);
    pounce_step(&s, BUTTON_RIGHT | BUTTON_B);
    CHECK((int)(s.x - 100 * 256) * 2 == walked);
}

static void test_pounce_buffered_press(void) {
    // A pressed in the last frames of the recovery leaps as soon as it can.
    PounceState s = pounce_ready(60);
    pounce_leap(&s);
    CHECK(s.leap == 0 && s.recover > 0);
    int left = s.recover;
    for (int i = 0; i < left - POUNCE_BUFFER + 1 && s.recover > POUNCE_BUFFER - 1; i++)
        pounce_step(&s, 0);
    pounce_step(&s, BUTTON_A); // pressed while recovering
    CHECK(s.leap == 0);
    int steps = 0;
    while (s.leap == 0 && steps < 20) {
        pounce_step(&s, 0);
        steps++;
    }
    CHECK(s.leap == 1 && steps <= POUNCE_BUFFER);
    // Pressed too early (more than the buffer before it can act): dropped.
    s = pounce_ready(60);
    pounce_step(&s, BUTTON_A);
    pounce_step(&s, 0);
    pounce_step(&s, BUTTON_A); // in the air, 38 frames before landing
    for (int i = 0; i < POUNCE_LEAP_FRAMES + POUNCE_RECOVER + 5; i++)
        pounce_step(&s, 0);
    CHECK(s.leap == 0 && pounce_x(&s) == 60 + POUNCE_REACH);
}

static void test_pounce_crouch_hears_better(void) {
    PounceState s = pounce_ready(100);
    pounce_step(&s, 0);
    CHECK(pounce_hear(&s, 20) == 4 && pounce_hear(&s, 100) == 1);
    pounce_step(&s, BUTTON_B);
    CHECK(s.crouch && pounce_hear(&s, 20) == 5 && pounce_hear(&s, 100) == 2);
    CHECK(pounce_hear(&s, POUNCE_HEAR_R) == 0); // too far stays too far
}

TEST_SUITE(
    den_tests, "den", {"pet_new_defaults", test_new_defaults}, {"pet_stages", test_stages},
    {"pet_neglect_thresholds", test_neglect_thresholds},
    {"pet_care_resets_neglect", test_care_resets_neglect}, {"pet_sleep_cycle", test_sleep_cycle},
    {"pet_catch_up_equals_live", test_catch_up_equals_live},
    {"pet_backwards_is_no_time", test_backwards_is_no_time},
    {"pet_long_absence", test_long_absence}, {"pet_needs_rates", test_needs_rates},
    {"pet_droppings", test_droppings}, {"pet_health", test_health},
    {"pet_meal_and_litter_gate", test_meal_and_litter_gate},
    {"pet_played_needs_awake_alive", test_played_needs_awake_alive},
    {"litter_size_and_names", test_litter_size_and_names},
    {"litter_inheritance", test_litter_inheritance}, {"album_ring", test_album_ring},
    {"den_save_round_trip", test_save_round_trip}, {"den_clock_resolve", test_clock_resolve},
    {"den_pet_rebase", test_pet_rebase},
    {"den_save_refuses_other_version", test_save_refuses_other_version},
    {"den_save_refuses_garbage", test_save_refuses_garbage}, {"pet_litter_due", test_litter_due},
    {"pet_care_refusals", test_care_refusals}, {"pet_play", test_play},
    {"pet_treats_hurt", test_treats_hurt}, {"yarn_ball_bounces", test_yarn_ball_bounces},
    {"yarn_paddle_hit_angle", test_yarn_paddle_hit_angle}, {"yarn_paw_reach", test_yarn_paw_reach},
    {"yarn_speed_cap", test_yarn_speed_cap}, {"yarn_miss_ends", test_yarn_miss_ends},
    {"climb_rows_reachable", test_climb_rows_reachable}, {"climb_jump_arc", test_climb_jump_arc},
    {"climb_one_way_and_land", test_climb_one_way_and_land},
    {"climb_snap_timer", test_climb_snap_timer}, {"climb_fall_ends", test_climb_fall_ends},
    {"climb_nest_knocks", test_climb_nest_knocks},
    {"climb_score_and_fun", test_climb_score_and_fun},
    {"climb_early_jump_is_buffered", test_climb_early_jump_is_buffered},
    {"climb_ramp", test_climb_ramp}, {"climb_top_of_the_tree", test_climb_top_of_the_tree},
    {"climb_bot_climbs", test_climb_bot_climbs},
    {"pounce_leap_lands_at_reach", test_pounce_leap_lands_at_reach},
    {"pounce_catch_radius", test_pounce_catch_radius},
    {"pounce_miss_scares", test_pounce_miss_scares},
    {"pounce_bird_flush_distance", test_pounce_bird_flush_distance},
    {"pounce_swat", test_pounce_swat}, {"pounce_round_timer", test_pounce_round_timer},
    {"pounce_hearing_and_fun", test_pounce_hearing_and_fun},
    {"pounce_buffered_press", test_pounce_buffered_press},
    {"pounce_crouch_hears_better", test_pounce_crouch_hears_better},
    {"events_next", test_events_next},
    {"events_live_equals_advance", test_events_live_equals_advance},
    {"events_summary", test_events_summary});
