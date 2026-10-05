// Tests that need the hardware (or mGBA): frame timing, OAM flushing, input.

#include "../test.h"
#include "serval/core.h"
#include "serval/ecs.h"
#include "serval/gba.h"
#include "serval/random.h"
#include "serval/screen.h"
#include "serval/sprites.h"

#include <tonc.h>

#include "../../src/core/input_internal.h"

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
    CHECK(!button_repeat(BUTTON_ANY));
    frame_end();
}

// frame_begin() feeds button_repeat() the polled buttons: with none held
// (mgba-rom-test presses nothing), a repeat left over from earlier input ends.
static void frame_begin_updates_button_repeat(void) {
    serval_repeat_frame(BUTTON_A); // as if A went down
    CHECK(button_repeat(BUTTON_A));
    frame_begin();
    CHECK(!button_repeat(BUTTON_A));
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

static void entropy_changes_over_time(void) {
    u32 a = random_entropy();
    frame_begin();
    frame_end();
    frame_begin();
    CHECK(random_entropy() != a);
    frame_end();
}

// random_entropy() and frame_count() depend on the input history and the
// number of frames, not on CPU timing: the same frames give the same value
// however long each took, and within a frame it doesn't change.
static u32 play_frames(u32 count, u32 work) {
    serval_init();
    for (u32 f = 0; f < count; f++) {
        frame_begin();
        u32 before = random_entropy();
        for (volatile u32 i = 0; i < work * (f + 1); i++) {
        }
        CHECK(random_entropy() == before);
        frame_end();
    }
    CHECK(frame_count() == count);
    frame_begin();
    u32 value = random_entropy();
    frame_end();
    return value;
}

static void entropy_is_deterministic(void) {
    serval_init();
    CHECK(frame_count() == 0);
    u32 a = play_frames(5, 100);
    u32 b = play_frames(5, 3000);
    CHECK(a == b);
    CHECK(play_frames(6, 100) != a);
    CHECK(frame_count() == 7);
    serval_init(); // as the other tests expect
}

static void screen_constants_match_functions(void) {
    static const u8 row[SCREEN_W] = {0}; // usable as an array size
    CHECK(sizeof(row) == 240 && SCREEN_H == 160);
    CHECK(screen_width() == SCREEN_W && screen_height() == SCREEN_H);
}

static void cpu_permille_matches_cycles(void) {
    frame_begin();
    for (volatile u32 i = 0; i < 5000; i++) {
    }
    frame_end();
    CHECK(frame_cpu_permille() == frame_cpu_cycles() * 1000 / frame_budget_cycles());
    CHECK(frame_cpu_permille() > 0);
}

// The engine's CPU cycle counter (timers 2 and 3, cascaded), re-read if the
// low half wrapped in between.
static u32 cycles_now(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

static void cpu_permille_survives_long_frames(void) {
    // Over 4.3 million cycles (~15 frames), cycles * 1000 no longer fits in 32 bits.
    frame_begin();
    u32 start = cycles_now();
    while (cycles_now() - start < 5000000u) {
    }
    frame_end();
    u32 cycles = frame_cpu_cycles();
    CHECK(cycles >= 5000000u);
    u32 expected = cycles / 280896 * 1000 + cycles % 280896 * 1000 / 280896;
    CHECK(frame_cpu_permille() == expected); // ~17800, not a wrapped value
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

// Loads one 8x8 sprite (ID 0) for the render tests.
static void load_render_sprite(void) {
    static const u32 tiles[8] = {0};
    static const SpriteAsset sprite = {.size = SPRITE_8x8, .tiles = tiles};
    static const SpriteAsset* const table[] = {&sprite};
    static const u16 palette[16] = {0};
    static const SpriteGroup group = {.palettes = palette, .sprite_count = 1, .palette_count = 1};
    sprite_table_set(table, 1);
    sprite_group_load(&group);
}

// Creates a drawable entity at x (used to identify it in OAM) with a depth.
static void make_drawn(int x, s16 depth) {
    u32 i = entity_index(entity_create(C_POS | C_SPR));
    pos_x[i] = FX(x);
    pos_y[i] = FX(50);
    spr_depth[i] = depth;
}

static int oam_x(u32 k) {
    return oam_mem[k].attr1 & ATTR1_X_MASK;
}

static void render_by_depth_puts_higher_depths_in_front(void) {
    load_render_sprite();
    ecs_reset();
    make_drawn(10, 5);
    make_drawn(20, 20);
    make_drawn(30, -3);
    make_drawn(40, 5);    // ties with x=10: lower index first
    entity_create(C_POS); // no sprite: skipped
    frame_begin();
    sys_render_by_depth();
    frame_end();
    CHECK(oam_x(0) == 20 && oam_x(1) == 10 && oam_x(2) == 40 && oam_x(3) == 30);
    CHECK(oam_mem[4].attr0 & ATTR0_HIDE);
    ecs_reset();
}

static void render_by_depth_handles_wide_depth_ranges(void) {
    load_render_sprite(); // keys differ in their high byte: two sort passes
    ecs_reset();
    make_drawn(10, -1000);
    make_drawn(20, 500);
    make_drawn(30, 0);
    make_drawn(40, 32767);
    make_drawn(50, -32768);
    frame_begin();
    sys_render_by_depth();
    frame_end();
    CHECK(oam_x(0) == 40 && oam_x(1) == 20 && oam_x(2) == 30 && oam_x(3) == 10 && oam_x(4) == 50);
    ecs_reset();
}

TEST_SUITE(core_tests, "core", {"frame_end_returns_in_vblank", frame_end_returns_in_vblank},
           {"frame_end_flushes_submitted_sprites", frame_end_flushes_submitted_sprites},
           {"sprites_disappear_when_not_drawn", sprites_disappear_when_not_drawn},
           {"oam_submit_stops_at_128", oam_submit_stops_at_128},
           {"no_buttons_held_without_input", no_buttons_held_without_input},
           {"frame_begin_updates_button_repeat", frame_begin_updates_button_repeat},
           {"init_enables_sprites", init_enables_sprites},
           {"backdrop_sets_bg_color_0", backdrop_sets_bg_color_0},
           {"frame_cpu_cycles_measures_work", frame_cpu_cycles_measures_work},
           {"cpu_permille_matches_cycles", cpu_permille_matches_cycles},
           {"cpu_permille_survives_long_frames", cpu_permille_survives_long_frames},
           {"render_by_depth_puts_higher_depths_in_front",
            render_by_depth_puts_higher_depths_in_front},
           {"render_by_depth_handles_wide_depth_ranges", render_by_depth_handles_wide_depth_ranges},
           {"entropy_changes_over_time", entropy_changes_over_time},
           {"entropy_is_deterministic", entropy_is_deterministic},
           {"screen_constants_match_functions", screen_constants_match_functions},
           {"sys_render_draws_positioned_sprites", sys_render_draws_positioned_sprites});
