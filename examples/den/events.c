// Serval Den's life events, without the screen: see events.h.

#include "events.h"

LifeEvent events_next(const Pet* p, u8 stage_seen) {
    if (p->dead)
        return EVENT_TOMBSTONE;
    if ((u8)pet_stage(p, p->last_tick) > stage_seen)
        return EVENT_GROW_UP;
    if (p->litter_due)
        return EVENT_LITTER;
    return EVENT_NONE;
}

void events_live(Pet* p, u32 now, int (*hour_of)(u32 seconds), Away* out) {
    u32 from = p->last_tick;
    bool was_dead = p->dead, was_due = p->litter_due;
    out->stage_before = (u8)pet_stage(p, from);
    out->slept_minutes = 0;
    while (!p->dead && now >= p->last_tick && now - p->last_tick >= 60) {
        pet_advance(p, p->last_tick + 60, hour_of);
        if (p->asleep)
            out->slept_minutes++;
    }
    pet_advance(p, now, hour_of); // a dead pet's rest, or a clock gone back
    out->minutes = p->last_tick > from ? (p->last_tick - from) / 60 : 0;
    out->stage_after = (u8)pet_stage(p, p->last_tick);
    out->died = p->dead && !was_dead;
    out->litter = p->litter_due && !was_due;
}

// --- Words -----------------------------------------------------------------------------------

static int put(char* out, int at, const char* s) {
    while (*s && at < EVENT_LINE_MAX - 1)
        out[at++] = *s++;
    out[at] = 0;
    return at;
}

static int put_u(char* out, int at, u32 v) {
    char digits[10];
    int n = 0;
    do {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 10);
    while (n > 0 && at < EVENT_LINE_MAX - 1)
        out[at++] = digits[--n];
    out[at] = 0;
    return at;
}

int events_duration(u32 m, char* out) {
    int n = 0;
    if (m < 60) {
        n = put_u(out, 0, m);
        return put(out, n, "M");
    }
    if (m < 24 * 60) {
        n = put_u(out, 0, m / 60);
        n = put(out, n, "H");
        if (m % 60) {
            n = put(out, n, " ");
            n = put_u(out, n, m % 60);
            n = put(out, n, "M");
        }
        return n;
    }
    // Days past 99,999 are no use to anyone; clamp to keep within 12.
    u32 days = m / (24 * 60);
    n = put_u(out, 0, days > 99999 ? 99999 : days);
    n = put(out, n, "D");
    if (m / 60 % 24) {
        n = put(out, n, " ");
        n = put_u(out, n, m / 60 % 24);
        n = put(out, n, "H");
    }
    return n;
}

static int name_says(char* line, const char* name, const char* s) {
    int n = put(line, 0, name);
    return put(line, n, s);
}

int events_summary(const Pet* p, const Away* a, char lines[EVENT_LINES][EVENT_LINE_MAX]) {
    bool grew = !p->dead && a->stage_after > a->stage_before;
    if (a->minutes < 60 && !a->died && !grew && !a->litter)
        return 0;
    int n = 0;
    char d[16];
    events_duration(a->minutes, d);
    put(lines[n], put(lines[n], 0, "AWAY "), d);
    n++;
    if (p->dead) {
        // Died while away, or before (saved on the stone): the stone says
        // the rest.
        name_says(lines[n++], p->name, " DIED");
        return n;
    }
    if (grew)
        name_says(lines[n++], p->name, " GREW UP!");
    if (a->slept_minutes >= 60) {
        events_duration(a->slept_minutes / 60 * 60, d); // whole hours
        put(lines[n], name_says(lines[n], p->name, " SLEPT "), d);
        n++;
    }
    if (a->litter) {
        put(lines[n++], 0, "A VISITOR IS AT THE DEN");
        return n;
    }
    PetMood mood = pet_mood(p, p->last_tick);
    const char* how = mood == MOOD_SICKER ? " IS VERY SICK"
                      : mood == MOOD_SICK ? " IS SICK"
                      : mood == MOOD_SAD  ? " IS SAD"
                      : p->full < 3000    ? " IS HUNGRY"
                      : p->droppings >= 2 ? " NEEDS A CLEAN"
                      : p->asleep         ? " IS ASLEEP"
                                          : " IS FINE";
    name_says(lines[n++], p->name, how);
    return n;
}
