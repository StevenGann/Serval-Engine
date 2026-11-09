#ifndef RACER_SCREENS_H
#define RACER_SCREENS_H

// The title's and the results' text and the logo, over the road scene that
// game.c draws behind them.

#include "track.h"

// The engine's third text style, light red (text.h): SAVE FAILED.
#define TEXT_WARNING 2

// The title: clears the text and shows the records. title_draw() each
// frame (`frames` since the title came up): the logo and PRESS START
// blinking. title_erased() confirms the records erased (or the save
// failing) for a while.
void title_show(void);
void title_draw(u32 frames);
void title_erased(void);

// The results of a race that is over: FINISH or TIME UP, the race time or
// the distance reached, the lap times with the race's best marked, which
// records fell (`fell`, RECORD_* bits) and whether saving them failed.
// results_draw() each frame blinks the way back.
void results_show(const Race* r, u32 fell);
void results_draw(u32 frames);

#endif // RACER_SCREENS_H
