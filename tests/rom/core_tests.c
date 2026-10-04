// Tests that need the hardware (or mGBA): frame timing, OAM flushing, input.

#include "../test.h"
#include "serval/core.h"
#include "serval/gba.h"

#include <tonc.h>

static void frame_end_returns_in_vblank(void) {
    frame_begin();
    frame_end();
    CHECK(REG_VCOUNT >= 160); // lines 160-227 are VBlank
}

static void frame_end_flushes_submitted_sprites(void) {
    frame_begin();
    CHECK(gba_oam_submit(ATTR0_SQUARE | 40, ATTR1_SIZE_8 | 60, 5));
    frame_end();
    CHECK(oam_mem[0].attr0 == (ATTR0_SQUARE | 40));
    CHECK(oam_mem[0].attr1 == (ATTR1_SIZE_8 | 60));
    CHECK(oam_mem[0].attr2 == 5);
    CHECK(oam_mem[1].attr0 & ATTR0_HIDE); // not submitted this frame
}

static void sprites_disappear_when_not_drawn(void) {
    frame_begin();
    gba_oam_submit(0, 0, 0);
    frame_end();
    frame_begin();
    frame_end();
    CHECK(oam_mem[0].attr0 & ATTR0_HIDE);
}

static void oam_submit_stops_at_128(void) {
    frame_begin();
    for (unsigned i = 0; i < 128; i++)
        CHECK(gba_oam_submit(0, 0, 0));
    CHECK(!gba_oam_submit(0, 0, 0));
    frame_end();
}

static void no_keys_held_without_input(void) {
    frame_begin();
    CHECK(!key_down(KEY_ANY));
    CHECK(!key_pressed(KEY_A));
    frame_end();
}

TEST_SUITE(core_tests, "core", {"frame_end_returns_in_vblank", frame_end_returns_in_vblank},
           {"frame_end_flushes_submitted_sprites", frame_end_flushes_submitted_sprites},
           {"sprites_disappear_when_not_drawn", sprites_disappear_when_not_drawn},
           {"oam_submit_stops_at_128", oam_submit_stops_at_128},
           {"no_keys_held_without_input", no_keys_held_without_input});
