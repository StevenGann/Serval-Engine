#ifndef DEN_DEN_H
#define DEN_DEN_H

// The den on screen: the scene (the sky by the hour, the sun or the moon,
// the lights), the serval living in it, animated by its size and mood, its
// droppings, and what the care actions show.

#include "pet.h"

// Shows the den again after a screen that hid it (art_hide(), a game): the
// pictures, palettes and text layer anew, the serval where it was. The
// screen must be black.
void den_resume(void);

// Loads the den's pictures and shows the scene (the screen black; fade in
// after). The next den_update() places the serval: in its nest if asleep.
void den_enter(void);

// One frame: steps the pet's life toward `now` (at most 15 minutes a frame,
// so a jump of the clock is caught up over a few frames, each in budget) and
// moves the serval.
void den_update(Pet* p, u32 now);

// Draws the frame: sprites, the time on the HUD, palettes when the hour, the
// lights or the serval's health change.
void den_draw(const Pet* p, u32 now);

// The den without the serval, its droppings and the HUD, for a scene that
// draws its own: the palettes for the hour (the den's light with the lights
// as `lights_off` says; the serval's coat in PAL_PET), the drifting sky, the
// lamp, the sun and the moon. Call after the scene's own sprites.
void den_draw_scene(const Pet* p, u32 now, bool lights_off);

// Where the serval stands (the paws' middle, screen pixels), and whether it
// faces left.
void den_pet_spot(int* x, int* y);
bool den_pet_left(void);

// A care action, as the player sees it: pet_care(), then what it shows and
// sounds (a meal at the bowl, hearts and a purr, a sparkle, the "no").
CareResult den_care(Pet* p, CareAction a, u32 now);

#endif // DEN_DEN_H
