// The status page: see status.h.

#include "status.h"

#include "art.h"

#include <serval/serval.h>

#define BAR_CELLS 12
#define BAR_COL 11
#define BAR_ROW0 3

static u32 shown_minute = 0xFFFFFFFF;

void status_reset(void) {
    shown_minute = 0xFFFFFFFF;
}

const char* status_duration(u32 m) {
    if (m < 60)
        return text_format("%uM", m);
    if (m < 24 * 60)
        return text_format("%uH %uM", m / 60, m % 60);
    return text_format("%uD %uH", m / (24 * 60), m / 60 % 24);
}

static const char* mood_word(const Pet* p, PetMood mood) {
    if (p->dead)
        return "GONE";
    if (p->asleep && mood == MOOD_HAPPY)
        return "ASLEEP";
    switch (mood) {
    case MOOD_HAPPY:
        return "HAPPY";
    case MOOD_SAD:
        return "SAD";
    case MOOD_SICK:
        return "SICK";
    default:
        return "VERY SICK";
    }
}

static void print_text(const Pet* p, u32 now) {
    u32 t = p->last_tick;
    PetStage stage = pet_stage(p, t);
    PetMood mood = pet_mood(p, t);
    static const char* const stages[3] = {"KITTEN", "ADOLESCENT", "ADULT"};
    text_clear();
    text_set_style(TEXT_HIGHLIGHT);
    text_print(2, 1, p->name);
    text_set_style(TEXT_NORMAL);
    // The stage and the generation on two rows, so that a long name never
    // touches them.
    const char* head = stages[stage];
    int n = 0;
    while (head[n])
        n++;
    text_print(28 - n, 1, head);
    const char* gen = text_format("GEN %u", (u32)p->generation);
    n = 0;
    while (gen[n])
        n++;
    text_print(28 - n, 2, gen);

    static const char* const labels[4] = {"FULL", "FUN", "ENERGY", "HEALTH"};
    const u16 values[4] = {p->full, p->fun, p->energy, p->health};
    for (int i = 0; i < 4; i++) {
        int row = BAR_ROW0 + i * 2;
        text_print(2, row, labels[i]);
        text_print(BAR_COL + BAR_CELLS + 1, row, text_format("%3u", (u32)(values[i] / 100)));
    }

    u32 age = pet_age_minutes(p, t);
    text_print(2, 11, text_format("AGE %s", status_duration(age)));
    if (p->dead) {
        text_print(2, 13, "AT REST");
    } else if (stage != STAGE_ADULT) {
        text_print(2, 13,
                   text_format("GROWS UP IN %s", status_duration(pet_minutes_to_next_stage(p, t))));
    } else if (p->litter_due || age >= PET_LITTER_MINUTES) {
        text_print(2, 13, "LITTER IS DUE");
    } else {
        text_print(2, 13, text_format("LITTER IN %s", status_duration(PET_LITTER_MINUTES - age)));
    }
    u32 neglect = pet_neglect_minutes(p, t);
    text_print(2, 15,
               neglect == 0 ? "LAST CARE JUST NOW"
                            : text_format("LAST CARE %s AGO", status_duration(neglect)));
    text_print(15, 11, text_format("MOOD %s", mood_word(p, mood)));
    if (!p->dead && mood >= MOOD_SAD) {
        text_set_style(2);
        text_print_centered(
            17, mood == MOOD_SAD ? "SAD - CARE SOON"
                                 : (mood == MOOD_SICK ? "SICK: MEDICINE" : "VERY SICK: MEDICINE"));
        text_set_style(TEXT_NORMAL);
    }
    text_print_centered(19, "B BACK");
    (void)now;
}

void status_draw(const Pet* p, u32 now) {
    u32 minute = now / 60;
    if (minute != shown_minute) {
        shown_minute = minute;
        print_text(p, now);
    }
    const u16 values[4] = {p->full, p->fun, p->energy, p->health};
    for (int i = 0; i < 4; i++) {
        int filled = values[i] * (BAR_CELLS * 8) / 10000;
        for (int c = 0; c < BAR_CELLS; c++) {
            int k = filled - c * 8;
            k = k < 0 ? 0 : k > 8 ? 8 : k;
            sprite_draw(SPR_BAR, (u8)(i * 9 + k), (BAR_COL + c) * 8, (BAR_ROW0 + i * 2) * 8, 0);
        }
    }
}
