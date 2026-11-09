// Savanna Run: a pseudo-3D road racer across the savanna at sunset.
// (Placeholder: the full description of the example comes with its last part.)

#include "game.h"

int main(void) {
    serval_init();
    serval_splash();
    game_init();
    for (;;) {
        frame_begin();
        game_frame();
        frame_end();
    }
}
