// Tests that need the hardware (or mGBA): frame timing, OAM flushing, input.

#include "../test.h"
#include "serval/core.h"
#include "serval/ecs.h"
#include "serval/gba.h"
#include "serval/screen.h"
#include "serval/sprites.h"

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

static void no_buttons_held_without_input(void) {
    frame_begin();
    CHECK(!button_down(BUTTON_ANY));
    CHECK(!button_pressed(BUTTON_A));
    frame_end();
}

static void init_enables_sprites(void) {
    CHECK((REG_DISPCNT & (DCNT_OBJ | DCNT_OBJ_1D)) == (DCNT_OBJ | DCNT_OBJ_1D));
    CHECK((REG_DISPCNT & 7) == DCNT_MODE0);
}

static void backdrop_sets_bg_color_0(void) {
    screen_set_backdrop(COLOR_RGB(255, 0, 0));
    CHECK(pal_bg_mem[0] == RGB15(31, 0, 0));
    CHECK(COLOR_RGB(8, 16, 255) == RGB15(1, 2, 31));
}

static void cpu_permille_matches_cycles(void) {
    frame_begin();
    for (volatile u32 i = 0; i < 5000; i++) {
    }
    frame_end();
    CHECK(frame_cpu_permille() == frame_cpu_cycles() * 1000 / frame_budget_cycles());
    CHECK(frame_cpu_permille() > 0);
}

static void frame_cpu_cycles_measures_work(void) {
    frame_begin();
    frame_end();
    u32 idle = frame_cpu_cycles();
    frame_begin();
    for (volatile u32 i = 0; i < 2000; i++) {
    }
    frame_end();
    u32 busy = frame_cpu_cycles();
    CHECK(idle < busy);
    CHECK(busy < frame_budget_cycles()); // well under one frame
    CHECK(frame_budget_cycles() == 280896);
}

static void sys_render_draws_positioned_sprites(void) {
    static const u32 tiles[8] = {0};
    static const SpriteAsset sprite = {.size = SPRITE_8x8, .tiles = tiles};
    static const SpriteAsset* const table[] = {&sprite};
    static const u16 ids[] = {0};
    static const u16 palette[16] = {0};
    static const SpriteGroup group = {
        .sprite_ids = ids, .palettes = palette, .sprite_count = 1, .palette_count = 1};
    sprite_table_set(table, 1);
    CHECK(sprite_group_load(&group));
    ecs_reset();
    Entity drawn = entity_create(C_POS | C_SPR);
    entity_create(C_POS); // no sprite: not drawn
    pos_x[entity_index(drawn)] = FX(30) + FX(1) / 2;
    pos_y[entity_index(drawn)] = FX(40);
    spr_flags[entity_index(drawn)] = SPRITE_FLIP_H | SPRITE_ABOVE_HUD;

    frame_begin();
    sys_render();
    frame_end();
    CHECK((oam_mem[0].attr1 & ATTR1_X_MASK) == 30);
    CHECK((oam_mem[0].attr0 & ATTR0_Y_MASK) == 40);
    CHECK(oam_mem[0].attr1 & ATTR1_HFLIP); // spr_flags
    CHECK((oam_mem[0].attr2 & ATTR2_PRIO_MASK) == ATTR2_PRIO(0));
    CHECK(oam_mem[1].attr0 & ATTR0_HIDE);
    ecs_reset();
}

TEST_SUITE(core_tests, "core", {"frame_end_returns_in_vblank", frame_end_returns_in_vblank},
           {"frame_end_flushes_submitted_sprites", frame_end_flushes_submitted_sprites},
           {"sprites_disappear_when_not_drawn", sprites_disappear_when_not_drawn},
           {"oam_submit_stops_at_128", oam_submit_stops_at_128},
           {"no_buttons_held_without_input", no_buttons_held_without_input},
           {"init_enables_sprites", init_enables_sprites},
           {"backdrop_sets_bg_color_0", backdrop_sets_bg_color_0},
           {"frame_cpu_cycles_measures_work", frame_cpu_cycles_measures_work},
           {"cpu_permille_matches_cycles", cpu_permille_matches_cycles},
           {"sys_render_draws_positioned_sprites", sys_render_draws_positioned_sprites});
