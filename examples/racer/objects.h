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

// The jeep cruising on its own (the title): leaning into the curves.
void jeep_draw_cruise(const RoadFrame* f);

// Everything on and beside track t in front of the camera at `distance`
// (where the car is), on the road frame f: its scenery and checkpoints, and
// `count` vehicles of traffic (none on the title), nearest first, shaken
// down by `shake` lines.
void objects_draw(const Track* t, const RoadFrame* f, FIXED distance, const Vehicle* traffic,
                  int count, int shake);

#endif // RACER_OBJECTS_H
