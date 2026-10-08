// The ROMs that test serval_add_rom()'s TITLE and GAME_CODE with every
// printable ASCII character (tests/CMakeLists.txt): only their headers are
// checked, so the game shows a blank screen and does nothing.

#include "serval/serval.h"

int main(void) {
    serval_init();
    for (;;) {
        frame_begin();
        frame_end();
    }
}
