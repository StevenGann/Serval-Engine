// The den's save: see den_save.h.

#include "den_save.h"

#include <string.h>

#include <serval/save.h>

#define DEN_SLOT 0

_Static_assert(DEN_SAVE_BYTES <= SAVE_SLOT_MAX, "the den fits a save slot");

typedef struct Cursor {
    u8* p;
    bool ok; // reading: no value was out of range
} Cursor;

// Writing and reading share one walk over the fields (`w` writes), so the
// two cannot disagree.
static u32 field(Cursor* c, bool w, u32 v, int bytes) {
    u32 r = 0;
    for (int i = 0; i < bytes; i++) {
        if (w)
            *c->p++ = (u8)(v >> (8 * i));
        else
            r |= (u32)*c->p++ << (8 * i);
    }
    return w ? v : r;
}

#define F8(x) ((x) = (u8)field(c, w, (x), 1))
#define F16(x) ((x) = (u16)field(c, w, (x), 2))
#define F32(x) ((x) = field(c, w, (x), 4))

static void limit(Cursor* c, bool w, u32 v, u32 max) {
    if (!w && v > max)
        c->ok = false;
}

static void name(Cursor* c, bool w, char* s) {
    bool ended = false; // writing: nothing after the first NUL is stored
    for (int i = 0; i < 9; i++) {
        u8 v = w && ended ? 0 : (u8)s[i];
        if (w && !v)
            ended = true;
        s[i] = (char)field(c, w, v, 1);
    }
    int n = 0;
    while (n < 9 && s[n])
        n++;
    if (!w && n == 9)
        c->ok = false; // not terminated
    for (int i = 0; !w && i < n; i++)
        if (s[i] < 'A' || s[i] > 'Z')
            c->ok = false; // names are A-Z only
    for (int i = n; !w && i < 9; i++)
        if (s[i])
            c->ok = false;
}

static void coat(Cursor* c, bool w, Coat* k) {
    F8(k->shade);
    F8(k->spots);
    F8(k->eyes);
    limit(c, w, k->shade, 3);
    limit(c, w, k->spots, 3);
    limit(c, w, k->eyes, 2);
}

static void flag(Cursor* c, bool w, bool* b) {
    u8 v = w ? *b : 0; // reading: *b may hold anything yet
    F8(v);
    limit(c, w, v, 1);
    *b = v != 0;
}

static void pet(Cursor* c, bool w, Pet* p) {
    name(c, w, p->name);
    coat(c, w, &p->coat);
    F8(p->generation);
    if (!w && p->generation == 0)
        c->ok = false; // the first kitten is generation 1
    F32(p->born);
    F32(p->last_tick);
    F32(p->last_interaction);
    F16(p->full);
    F16(p->fun);
    F16(p->energy);
    F16(p->health);
    limit(c, w, p->full, 10000);
    limit(c, w, p->fun, 10000);
    limit(c, w, p->energy, 10000);
    limit(c, w, p->health, 10000);
    F8(p->droppings);
    limit(c, w, p->droppings, 4);
    F8(p->sickness);
    limit(c, w, p->sickness, 2);
    flag(c, w, &p->asleep);
    flag(c, w, &p->lights_off);
    flag(c, w, &p->dead);
    flag(c, w, &p->litter_due);
    F8(p->treats_recent);
    F32(p->died_at);
    F32(p->awake_until);
    F16(p->dropping_clock);
    F16(p->treat_clock);
    limit(c, w, p->dropping_clock, 240);
    limit(c, w, p->treat_clock, 120);
}

static void entry(Cursor* c, bool w, AlbumEntry* e) {
    name(c, w, e->name);
    coat(c, w, &e->coat);
    F8(e->generation);
    F8(e->end);
    limit(c, w, e->end, END_TOMBSTONE);
    F32(e->age_minutes);
    for (int i = 0; i < 3; i++)
        F16(e->best[i]);
}

static void walk(Cursor* c, bool w, DenSave* s) {
    pet(c, w, &s->pet);
    for (int i = 0; i < 3; i++)
        F16(s->best[i]);
    F8(s->album_count);
    limit(c, w, s->album_count, ALBUM_MAX);
    for (int i = 0; i < ALBUM_MAX; i++)
        entry(c, w, &s->album[i]);
    F32(s->last_seen);
    F32(s->offset);
    F8(s->stage_seen);
    limit(c, w, s->stage_seen, STAGE_ADULT);
}

bool den_save_load(DenSave* out) {
    u8 raw[DEN_SAVE_BYTES];
    if (save_read(DEN_SLOT, raw, sizeof raw, DEN_SAVE_VERSION) != SAVE_OK)
        return false;
    DenSave s;
    memset(&s, 0, sizeof s);
    Cursor c = {raw, true};
    walk(&c, false, &s);
    if (!c.ok || c.p != raw + sizeof raw)
        return false;
    // Unused album slots are zero.
    for (int i = s.album_count; i < ALBUM_MAX; i++) {
        AlbumEntry zero;
        memset(&zero, 0, sizeof zero);
        if (memcmp(&s.album[i], &zero, sizeof zero) != 0)
            return false;
    }
    *out = s;
    return true;
}

bool den_save_store(const DenSave* in) {
    u8 raw[DEN_SAVE_BYTES];
    DenSave copy = *in;
    Cursor c = {raw, true};
    walk(&c, true, &copy);
    if (c.p != raw + sizeof raw)
        return false;
    return save_write(DEN_SLOT, raw, sizeof raw, DEN_SAVE_VERSION);
}
