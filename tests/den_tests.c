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
#include "../examples/den/den_save.h"
#include "../examples/den/litter.h"
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
    CHECK(strcmp(out.pet.name, "Mimi") == 0 && out.pet.coat.shade == 1 && out.pet.coat.spots == 2);
    CHECK(out.pet.dropping_clock == 77 && out.pet.treat_clock == 33);
    CHECK(out.pet.awake_until == T0 + 99 && out.pet.energy == 1234 && out.pet.asleep);
    CHECK(out.pet.born == s.pet.born && out.pet.last_tick == s.pet.last_tick);
    CHECK(out.best[2] == 65535 && out.album_count == 3 && out.last_seen == s.last_seen);
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
    raw[25] = 0xFF; // full
    raw[26] = 0xFF;
    CHECK(save_write(0, raw, sizeof raw, DEN_SAVE_VERSION));
    CHECK(!den_save_load(&out));
    memcpy(raw, good, sizeof raw);
    CHECK(save_write(0, raw, sizeof raw, DEN_SAVE_VERSION));
    CHECK(den_save_load(&out));
    save_erase(0);
}

TEST_SUITE(den_tests, "den", {"pet_new_defaults", test_new_defaults}, {"pet_stages", test_stages},
           {"pet_neglect_thresholds", test_neglect_thresholds},
           {"pet_care_resets_neglect", test_care_resets_neglect},
           {"pet_sleep_cycle", test_sleep_cycle},
           {"pet_catch_up_equals_live", test_catch_up_equals_live},
           {"pet_backwards_is_no_time", test_backwards_is_no_time},
           {"pet_long_absence", test_long_absence}, {"pet_needs_rates", test_needs_rates},
           {"pet_droppings", test_droppings}, {"pet_health", test_health},
           {"pet_meal_and_litter_gate", test_meal_and_litter_gate},
           {"pet_played_needs_awake_alive", test_played_needs_awake_alive},
           {"litter_size_and_names", test_litter_size_and_names},
           {"litter_inheritance", test_litter_inheritance}, {"album_ring", test_album_ring},
           {"den_save_round_trip", test_save_round_trip},
           {"den_save_refuses_other_version", test_save_refuses_other_version},
           {"den_save_refuses_garbage", test_save_refuses_garbage},
           {"pet_litter_due", test_litter_due}, {"pet_care_refusals", test_care_refusals},
           {"pet_play", test_play}, {"pet_treats_hurt", test_treats_hurt});
