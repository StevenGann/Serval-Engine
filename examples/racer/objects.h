#ifndef RACER_OBJECTS_H
#define RACER_OBJECTS_H

// The sprites on the road: the jeep, the traffic, the scenery and the
// checkpoints' banners, each where road_project() puts it and at the size
// it gives, the nearest in front.

#include "road.h"
#include "track.h"

// The player's jeep in a race, on the road frame f: its frame by the
// steering, lit tail lights while braking, a jolt off the road, a spin after
// a crash. `shake` moves it down with the picture. Draw it before the
// objects: the first drawn is in front.
void jeep_draw_race(const Race* r, const RoadFrame* f, int shake);

// The dust behind the jeep's wheels: puffs thrown out while it drives off
// the road or skids, which grow, rise and drift outward as they thin.
// dust_reset() clears them for a new race; dust_step() is a frame of
// racing (`ev`: race_step()'s events, for RACE_EV_OFFROAD and RACE_EV_SKID);
// dust_draw() draws them as they are, beside the jeep drawn by
// jeep_draw_race() on f (call it just after: the jeep is in front of them,
// they are in front of everything else).
void dust_reset(void);
void dust_step(const Race* r, u32 ev);
void dust_draw(const RoadFrame* f, int shake);

// The jeep cruising on its own (the title): leaning into the curves.
void jeep_draw_cruise(const RoadFrame* f);

// Everything on and beside track t in front of the camera at `distance`
// (where the car is), on the road frame f: its scenery and checkpoints, and
// `count` vehicles of traffic (none on the title), nearest first, shaken
// down by `shake` lines.
void objects_draw(const Track* t, const RoadFrame* f, FIXED distance, const Vehicle* traffic,
                  int count, int shake);

#endif // RACER_OBJECTS_H
