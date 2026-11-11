#ifndef DEN_SOUND_H
#define DEN_SOUND_H

// Serval Den's sounds, PSG only (no Maxmod: its IWRAM is better left to the
// mini-games): the care sounds, the serval's voice and the outside heard
// through the den's mouth.

#include <stdbool.h>

void sound_init(void);
void sound_purr(void);     // petted
void sound_eat(void);      // crunching
void sound_mew(void);      // a happy chirp-mew (servals chirp)
void sound_sad(void);      // a falling mew
void sound_clean(void);    // a sweep
void sound_medicine(void); // a soft chime
void sound_lights(bool off);
void sound_no(void); // refused
// Yarn Bat: the paw's thump, a tap on the wall, a missed ball.
void sound_bat(void);
void sound_tap(void);
void sound_miss(void);
// Acacia Climb: a jump, a bough snapping, a knock from a nest. (The landing
// is sound_tap().)
void sound_jump(void);
void sound_snap(void);
void sound_knock(void);
// High Pounce: the grass rustling at a loudness from 1 (far) to 5 (right
// there, crouched; pounce_hear()), and a bird taking off. (The leap is sound_jump(),
// a catch sound_mew(), a swat sound_bat(), a miss sound_miss().)
void sound_rustle(int loudness);
void sound_flutter(void);
// Now and then, through the mouth: birds by day, crickets at night. Call once
// a frame with the time of day (art.h's TOD_*).
void sound_ambient(int tod);

#endif // DEN_SOUND_H
