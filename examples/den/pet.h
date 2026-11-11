#ifndef DEN_PET_H
#define DEN_PET_H

// The life of Serval Den's pet: needs, sleep, droppings, health, neglect and
// growing up. Portable C: no hardware and no rtc_* calls. Time arrives as
// `now`, the clock's seconds (u32, since 2000-01-01), and the hour of day as
// a function the caller passes (the game builds one on rtc_time_from_seconds).
//
// The life is stepped in whole minutes from `last_tick`, so a pet advanced
// once over a long absence is byte for byte the pet advanced minute by
// minute. Needs are in hundredths of a point (0-10000, shown as 0-100).
//
// Rulings where the rules left a choice:
//  - Fields added to Pet: `dropping_clock` (awake minutes since the last
//    dropping) and `treat_clock` (minutes since treats_recent last fell),
//    because the rules need a counter and the struct had none.
//  - A dead pet changes nothing but `last_tick`, which still follows the
//    clock (it keeps a pet advanced at once equal to one advanced live).
//  - A clock gone backwards by d seconds is no time passed: last_tick = now
//    and born, last_interaction, awake_until and died_at (when nonzero) all
//    move back by d (saturating at 0), so age, neglect and the wake window
//    stay as they were. A reset to 2000 can't be followed by a pet older than
//    the clock reading, so the game should keep a monotonic den time (the
//    clock plus a saved offset) and never hand such a jump to the core.
//  - Catch-up is exact: minutes are stepped one by one while the pet lives,
//    and the rest is added to last_tick at once once it is dead. pet_care()
//    and pet_played() treat now < last_tick as last_tick, so
//    last_interaction <= last_tick always, and a living pet is dead within
//    2160 minutes of its last interaction: at most 2160 minutes are stepped.
//  - Cost: about 1000 cycles a stepped minute with hour_of =
//    (s % 86400) / 3600 (three divisions by 60 and the clamps), about 3-4k
//    with an hour_of built on rtc_time_from_seconds; 2160 minutes is 8 to 30
//    frames. Catch up at boot or behind a loading screen, not inside a
//    60 fps frame. The game's hour_of should be (s % 86400u) / 3600u.
//  - Order within a minute: sleep and wake, needs, droppings, health,
//    sickness, neglect, growing up.
//  - CARE_REFUSED_ASLEEP is never returned by pet_care(): a sleeping pet is
//    woken (the action succeeds), except that a refused action wakes no one.
//    It is kept for a screen that wants to refuse by itself.
//  - A pet born in the night falls asleep at its first minute.
//  - MEDICINE also lifts health to at least 2000, or a pet at health 0 would
//    be made sick again the next minute and never recover.
//  - pet_played() does nothing to a dead or sleeping pet.
//  - Mood depends only on sickness and the neglect clock, not on the needs.

#include <serval/core.h>

typedef enum { STAGE_KITTEN, STAGE_ADOLESCENT, STAGE_ADULT } PetStage;
typedef enum { MOOD_HAPPY, MOOD_SAD, MOOD_SICK, MOOD_SICKER, MOOD_DEAD } PetMood;

typedef struct Coat {
    u8 shade, spots, eyes; // shade 0-3, spots 0-3, eyes 0-2
} Coat;

typedef struct Pet {
    char name[9]; // up to 8 letters, NUL-terminated
    Coat coat;
    u8 generation;                 // 1 for the first kitten
    u32 born;                      // clock seconds at birth
    u32 last_tick;                 // clock seconds the life has been stepped to
    u32 last_interaction;          // clock seconds
    u16 full, fun, energy, health; // 0-10000
    u8 droppings;                  // 0-4
    u8 sickness;                   // 0 well, 1 sick, 2 sicker
    bool asleep, lights_off, dead, litter_due;
    u8 treats_recent;   // treats not yet worn off (one wears off per 2 h)
    u32 died_at;        // clock seconds (0 while alive)
    u32 awake_until;    // clock seconds: a pet woken at night stays awake until then (0: none)
    u16 dropping_clock; // awake minutes since the last dropping
    u16 treat_clock;    // minutes since treats_recent last fell
} Pet;

// A new pet: all needs 8000, health 10000, awake, a kitten born at `now`.
void pet_new(Pet* p, const char* name, Coat coat, u8 generation, u32 now);

// Steps whole minutes from last_tick to now. `hour_of` maps clock seconds to
// the hour of day (0-23).
void pet_advance(Pet* p, u32 now, int (*hour_of)(u32 seconds));

PetStage pet_stage(const Pet* p, u32 now);
PetMood pet_mood(const Pet* p, u32 now);
u32 pet_age_minutes(const Pet* p, u32 now);
u32 pet_minutes_to_next_stage(const Pet* p, u32 now); // 0 for adults
u32 pet_neglect_minutes(const Pet* p, u32 now);

typedef enum { CARE_MEAL, CARE_TREAT, CARE_PET, CARE_CLEAN, CARE_MEDICINE, CARE_LIGHTS } CareAction;
typedef enum {
    CARE_DONE,
    CARE_REFUSED_FULL,
    CARE_REFUSED_NOT_SICK,
    CARE_REFUSED_CLEAN,
    CARE_REFUSED_ASLEEP,
    CARE_REFUSED_DEAD
} CareResult;

CareResult pet_care(Pet* p, CareAction a, u32 now);
bool pet_can_play(const Pet* p, u32 now);        // energy >= 2000, awake, alive
void pet_played(Pet* p, u32 now, u8 fun_points); // fun += fun_points*100, energy -= 1500

#endif // DEN_PET_H
