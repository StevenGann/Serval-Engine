// Tests for alpha blending on the GBA (blend.c, and SPRITE_BLEND in
// sprites.c): the blend registers screen_set_blend() sets in VBlank, how it
// shares the color effect with the brightness and the splash screen, and the
// semi-transparent mode of SPRITE_BLEND sprites in OAM. What the screen shows
// for these settings is the web renderer's tests (tests/web_ppu_tests.c),
// matched against mGBA.

#include "../test.h"
#include "serval/core.h"
#include "serval/debug.h"
#include "serval/ecs.h"
#include "serval/math.h"
#include "serval/screen.h"
#include "serval/sprites.h"
#include "serval/text.h"

#include <tonc.h>

#include "../../src/gba/internal.h"
#include "../../src/gba/screen_internal.h"

#ifdef SERVAL_DEBUG
#define WARNINGS(n) (n)
#else
#define WARNINGS(n) 0
#endif

static void show_frame(void) {
    frame_begin();
    frame_end();
}

// BLDCNT for the alpha blending mode with these first and second targets.
static u16 alpha_control(u32 top, u32 bottom) {
    return (u16)(top | BLD_STD | bottom << 8);
}

// Blending off, applied, and the brightness at 0: what every case starts and
// ends with.
static void blend_off(void) {
    screen_set_brightness(0);
    screen_set_blend(0, 0, 0, 0);
    show_frame();
}

// --- The registers ------------------------------------------------------------

// The settings reach BLDCNT and BLDALPHA at the next frame_end(), not at the
// call, and stay until changed.
static void settings_apply_in_vblank(void) {
    blend_off();
    CHECK(REG_BLDCNT == 0);
    u32 before = debug_warning_count();
    screen_set_blend(LAYER_FOREGROUND, LAYER_ALL & ~LAYER_FOREGROUND, 8, 8);
    CHECK(REG_BLDCNT == 0); // not yet
    show_frame();
    CHECK(REG_BLDCNT == alpha_control(LAYER_FOREGROUND, LAYER_ALL & ~LAYER_FOREGROUND));
    CHECK(REG_BLDCNT == (BLD_BG1 | BLD_STD |
                         (BLD_BG0 | BLD_BG2 | BLD_BG3 | BLD_OBJ | BLD_BACKDROP) << BLD_BOT_SHIFT));
    CHECK(REG_BLDALPHA == BLDA_BUILD(8, 8));
    show_frame(); // kept
    CHECK(REG_BLDCNT == alpha_control(LAYER_FOREGROUND, LAYER_ALL & ~LAYER_FOREGROUND));
    CHECK(REG_BLDALPHA == BLDA_BUILD(8, 8));
    // The last call of a frame wins.
    screen_set_blend(LAYER_PLAYFIELD, LAYER_BACKGROUND, 4, 12);
    screen_set_blend(LAYER_HUD | LAYER_SPRITES, LAYER_BACKDROP, 16, 16);
    show_frame();
    CHECK(REG_BLDCNT == alpha_control(LAYER_HUD | LAYER_SPRITES, LAYER_BACKDROP));
    CHECK(REG_BLDALPHA == BLDA_BUILD(16, 16));
    CHECK(debug_warning_count() == before);
    blend_off();
    CHECK(REG_BLDCNT == 0);
}

// The LAYER_* constants are BLDCNT's target bits (screen.h promises it).
static void layers_are_the_hardware_bits(void) {
    CHECK(LAYER_HUD == BLD_BG0 && LAYER_FOREGROUND == BLD_BG1 && LAYER_PLAYFIELD == BLD_BG2 &&
          LAYER_BACKGROUND == BLD_BG3 && LAYER_SPRITES == BLD_OBJ &&
          LAYER_BACKDROP == BLD_BACKDROP && LAYER_ALL == (BLD_ALL | BLD_BACKDROP));
}

// With no second target nothing blends: the effect is off. With no first
// target, only SPRITE_BLEND sprites blend: the mode and second targets are
// set, the first targets empty.
static void bottom_zero_is_off_top_zero_is_sprites_only(void) {
    screen_set_blend(LAYER_ALL, 0, 8, 8);
    show_frame();
    CHECK(REG_BLDCNT == 0);
    screen_set_blend(0, LAYER_PLAYFIELD | LAYER_BACKDROP, 10, 6);
    show_frame();
    CHECK(REG_BLDCNT == alpha_control(0, LAYER_PLAYFIELD | LAYER_BACKDROP));
    CHECK(REG_BLDALPHA == BLDA_BUILD(10, 6));
    blend_off();
}

// Weights past 16 are clamped to 16, bits outside LAYER_ALL dropped; each
// problem warns once.
static void clamps_and_ignores_with_one_warning_each(void) {
    u32 before = debug_warning_count();
    screen_set_blend(LAYER_FOREGROUND, LAYER_PLAYFIELD, 17, 31);
    CHECK(debug_warning_count() == before + WARNINGS(1));
    show_frame();
    CHECK(REG_BLDALPHA == BLDA_BUILD(16, 16));
    screen_set_blend(LAYER_FOREGROUND, LAYER_PLAYFIELD, 4, 99);
    show_frame();
    CHECK(REG_BLDALPHA == BLDA_BUILD(4, 16));
    CHECK(debug_warning_count() == before + WARNINGS(1)); // once, not per call
    screen_set_blend(LAYER_FOREGROUND | 0x40, LAYER_PLAYFIELD | 0xFF00, 8, 8);
    CHECK(debug_warning_count() == before + WARNINGS(2));
    show_frame();
    CHECK(REG_BLDCNT == alpha_control(LAYER_FOREGROUND, LAYER_PLAYFIELD));
    screen_set_blend(0x80, 0x100, 8, 8); // nothing left: off
    show_frame();
    CHECK(REG_BLDCNT == 0);
    CHECK(debug_warning_count() == before + WARNINGS(2));
    blend_off();
}

// --- Shared with the brightness and the splash ----------------------------------

#define FADED_BLACK (BLD_ALL | BLD_BACKDROP | BLD_BLACK)

// While the brightness is not 0, BLDCNT is the brightness's (no second
// targets, so semi-transparent sprites fade too); blending resumes when it
// returns to 0, with the settings last applied, also those given while it
// was paused.
static void brightness_pauses_blending(void) {
    const u16 see_through = alpha_control(LAYER_FOREGROUND, LAYER_ALL);
    screen_set_blend(LAYER_FOREGROUND, LAYER_ALL, 8, 8);
    show_frame();
    CHECK(REG_BLDCNT == see_through);
    screen_set_brightness(-6);
    CHECK(REG_BLDCNT == FADED_BLACK);
    show_frame(); // frame_end() doesn't take it back
    CHECK(REG_BLDCNT == FADED_BLACK);
    screen_set_brightness(0); // at once, like the brightness itself
    CHECK(REG_BLDCNT == see_through);

    // New settings while paused are applied (BLDALPHA) but not shown.
    screen_set_brightness(4);
    screen_set_blend(0, LAYER_PLAYFIELD, 16, 16);
    show_frame();
    CHECK(REG_BLDCNT == (BLD_ALL | BLD_BACKDROP | BLD_WHITE));
    CHECK(REG_BLDALPHA == BLDA_BUILD(16, 16));
    screen_set_brightness(0);
    CHECK(REG_BLDCNT == alpha_control(0, LAYER_PLAYFIELD));

    // Turned off while paused: off when the brightness returns.
    screen_set_brightness(-16);
    screen_set_blend(0, 0, 0, 0);
    show_frame();
    screen_set_brightness(0);
    CHECK(REG_BLDCNT == 0);
}

// The splash screen holds BLDCNT while it runs (frame_end() leaves it alone,
// serval_blend_borrow), and gives the game's blending back at the end, also
// settings given just before it and applied during it.
static void splash_borrows_the_effect(void) {
    screen_set_blend(LAYER_FOREGROUND, LAYER_ALL, 8, 8);
    show_frame();
    serval_blend_borrow(true);
    REG_BLDCNT = BLD_BG0 | BLD_BLACK;
    screen_set_blend(LAYER_PLAYFIELD, LAYER_BACKGROUND, 12, 4);
    show_frame();
    CHECK(REG_BLDCNT == (BLD_BG0 | BLD_BLACK)); // left alone
    serval_blend_borrow(false);
    serval_screen_apply_brightness();
    CHECK(REG_BLDCNT == alpha_control(LAYER_PLAYFIELD, LAYER_BACKGROUND));

    screen_set_blend(LAYER_FOREGROUND, LAYER_ALL, 8, 8); // applied during the splash
    serval_splash();
    CHECK(REG_BLDCNT == alpha_control(LAYER_FOREGROUND, LAYER_ALL));
    CHECK(REG_BLDALPHA == BLDA_BUILD(8, 8));
    blend_off();
}

// --- SPRITE_BLEND ---------------------------------------------------------------

#define OAM_MODE(k) (oam_mem[k].attr0 & (ATTR0_BLEND | ATTR0_WINDOW)) // the graphics mode
#define OAM_AFFINE(k) (oam_mem[k].attr0 & ATTR0_AFF)
#define OAM_HIDDEN(k) ((oam_mem[k].attr0 & 0x0300) == ATTR0_HIDE)
#define OAM_BANK(k) (oam_mem[k].attr2 >> ATTR2_PALBANK_SHIFT)

enum { SPR_DOT, SPR_SHADOW, SPR_GHOST, SPRITE_COUNT };

static const u32 dot_tiles[8] = {0x11111111, 0x11111111};
// Two pieces: the first opaque, the second blended by its own flag.
static const SpritePiece shadow_pieces[] = {
    {.sprite = SPR_DOT},
    {.x = 2, .y = 2, .sprite = SPR_DOT, .flags = SPRITE_BLEND | SPRITE_PALETTE(1)},
};
static const SpriteAsset dot = {.size = SPRITE_8x8, .tiles = dot_tiles};
static const SpriteAsset shadow = {
    .flags = SPRITE_ASSET_METASPRITE, .pieces = shadow_pieces, .piece_count = 2};
static const SpriteAsset* const table[SPRITE_COUNT] = {
    [SPR_DOT] = &dot, [SPR_SHADOW] = &shadow, [SPR_GHOST] = &dot};
static const u16 palettes[32] = {[1] = 0x1234, [17] = 0x0567};
static const SpriteGroup group = {.palettes = palettes, .sprite_count = 3, .palette_count = 2};

// One frame's draws, and the semi-transparent sprites they leave in OAM:
// bit k for OAM entry k, with `drawn` entries in all.
typedef struct {
    void (*draw)(void);
    u32 drawn, semi;
} BlendDraw;

static u32 entity; // the entity the render cases draw

static void draw_plain(void) {
    sprite_draw(SPR_DOT, 0, 20, 20, SPRITE_BLEND);
    sprite_draw(SPR_DOT, 0, 40, 20, 0);
}
static void draw_rotated(void) {
    sprite_draw_rotated(SPR_DOT, 0, 20, 20, ANGLE_DEG(90), SPRITE_BLEND);
}
static void draw_scaled(void) {
    sprite_draw_ex(SPR_DOT, 0, 20, 20, 0, FX(2), FX(2), SPRITE_BLEND | SPRITE_FLIP_H);
}
static void draw_shrunk(void) {
    sprite_draw_ex(SPR_DOT, 0, 20, 20, 0, FX(1) / 2, FX(1) / 2, SPRITE_BLEND);
}
static void draw_palette(void) {
    sprite_draw(SPR_GHOST, 0, 20, 20, SPRITE_BLEND | SPRITE_PALETTE(1));
}
static void draw_whole_metasprite(void) {
    sprite_draw(SPR_SHADOW, 0, 20, 20, SPRITE_BLEND); // both pieces
}
static void draw_blended_piece(void) {
    sprite_draw(SPR_SHADOW, 0, 20, 20, 0); // only the second piece
}
static void draw_rotated_metasprite(void) {
    sprite_draw_rotated(SPR_SHADOW, 0, 20, 20, ANGLE_DEG(45), 0); // only the second piece
}
static void render(void) {
    sys_render();
}
static void render_by_depth(void) {
    sys_render_by_depth();
}
static void render_scaled(void) {
    spr_flags[entity] = SPRITE_BLEND | SPRITE_SCALED;
    spr_scale[entity] = FX(2);
    sys_render();
    spr_flags[entity] = SPRITE_BLEND;
    spr_scale[entity] = 0;
}
static void render_plain(void) {
    spr_flags[entity] = 0;
    sys_render();
    spr_flags[entity] = SPRITE_BLEND;
}

// Every way of drawing gives a SPRITE_BLEND sprite the semi-transparent mode,
// and only those sprites, without a warning: plain and transformed draws,
// with a palette, a metasprite's pieces when the draw or the piece has the
// flag, and the render systems, which send such entities down their
// out-of-line path.
static void blended_sprites_are_semi_transparent(void) {
    static const BlendDraw draws[] = {
        {draw_plain, 2, 1 << 0},
        {draw_rotated, 1, 1 << 0},
        {draw_scaled, 1, 1 << 0},
        {draw_shrunk, 1, 1 << 0},
        {draw_palette, 1, 1 << 0},
        {draw_whole_metasprite, 2, 1 << 0 | 1 << 1},
        {draw_blended_piece, 2, 1 << 1},
        {draw_rotated_metasprite, 2, 1 << 1},
        {render, 1, 1 << 0},
        {render_by_depth, 1, 1 << 0},
        {render_scaled, 1, 1 << 0},
        {render_plain, 1, 0},
    };
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&group));
    ecs_reset();
    entity = entity_index(entity_create(C_POS | C_SPR));
    pos_x[entity] = pos_y[entity] = FX(30);
    spr_id[entity] = SPR_DOT;
    spr_flags[entity] = SPRITE_BLEND; // alone: with SPRITE_PALETTE it would take
                                      // the out-of-line path anyway
    u32 before = debug_warning_count();
    for (u32 k = 0; k < sizeof(draws) / sizeof(draws[0]); k++) {
        frame_begin();
        draws[k].draw();
        frame_end();
        bool ok = OAM_HIDDEN(draws[k].drawn);
        for (u32 s = 0; s < draws[k].drawn; s++) {
            bool semi = (draws[k].semi >> s) & 1;
            ok &= !OAM_HIDDEN(s) && OAM_MODE(s) == (semi ? ATTR0_BLEND : 0);
        }
        CHECK(ok);
        if (!ok)
            debug_log(text_format("blended_sprites_are_semi_transparent: draw %u", k));
    }
    CHECK(debug_warning_count() == before);
    ecs_reset();
}

// The flag leaves the rest of the sprite as it is: position, affine mode,
// palette.
static void blending_keeps_the_rest_of_the_sprite(void) {
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&group));
    frame_begin();
    sprite_draw(SPR_GHOST, 0, 20, 30, SPRITE_BLEND | SPRITE_PALETTE(1));
    sprite_draw(SPR_GHOST, 0, 20, 30, SPRITE_PALETTE(1));
    sprite_draw_ex(SPR_DOT, 0, 50, 30, ANGLE_DEG(30), FX(1), FX(1), SPRITE_BLEND);
    sprite_draw_ex(SPR_DOT, 0, 50, 30, ANGLE_DEG(30), FX(1), FX(1), 0);
    frame_end();
    CHECK(oam_mem[0].attr0 == (oam_mem[1].attr0 | ATTR0_BLEND));
    CHECK(oam_mem[0].attr1 == oam_mem[1].attr1 && oam_mem[0].attr2 == oam_mem[1].attr2);
    CHECK(OAM_BANK(0) == 1);
    CHECK(OAM_AFFINE(2) && oam_mem[2].attr0 == (oam_mem[3].attr0 | ATTR0_BLEND));
    CHECK(oam_mem[2].attr1 == oam_mem[3].attr1 && oam_mem[2].attr2 == oam_mem[3].attr2);
    sprite_table_set(NULL, 0);
}

// CPU cycles, from the cascaded timers serval_init() starts.
static u32 cycles(void) {
    u32 hi, lo;
    do {
        hi = REG_TM3D;
        lo = REG_TM2D;
    } while (hi != REG_TM3D);
    return hi << 16 | lo;
}

// Cycle budgets only hold for optimized code: Debug builds (-O0) check
// correctness but not timing.
#ifdef __OPTIMIZE__
#define CHECK_TIMING(cond) CHECK(cond)
#else
#define CHECK_TIMING(cond) ((void)(cond))
#endif

// sprites.h SPRITE_BLEND: the render systems draw blended entities on their
// out-of-line path, as they do SPRITE_PALETTE ones. What that costs per
// sprite, against plain ones, logged: sys_render() over 64 entities.
static void blended_entities_cost(void) {
    enum { COUNT = 64 };
    static const u16 variants[3] = {0, SPRITE_BLEND, SPRITE_PALETTE(1)};
    sprite_table_set(table, SPRITE_COUNT);
    CHECK(sprite_group_load(&group));
    ecs_reset();
    for (u32 k = 0; k < COUNT; k++) {
        u32 i = entity_index(entity_create(C_POS | C_SPR));
        pos_x[i] = FX((s32)(k * 37 % 230));
        pos_y[i] = FX((s32)(k * 23 % 150));
        spr_id[i] = SPR_DOT;
    }
    u32 cost[3];
    for (u32 v = 0; v < 3; v++) {
        for (u32 i = 0; i < COUNT; i++)
            spr_flags[i] = variants[v];
        frame_begin();
        u32 t0 = cycles();
        sys_render();
        cost[v] = cycles() - t0;
        CHECK(serval_oam_used == COUNT);
        frame_end();
        CHECK(OAM_MODE(COUNT - 1) == (v == 1 ? ATTR0_BLEND : 0));
    }
    u32 blend = (cost[1] - cost[0]) / COUNT, palette = (cost[2] - cost[0]) / COUNT;
    debug_log(text_format("sys_render, %u sprites: plain %u cycles, SPRITE_BLEND %u (+%u a "
                          "sprite), SPRITE_PALETTE %u (+%u)",
                          COUNT, cost[0], cost[1], blend, cost[2], palette));
    // About +106 a sprite when this was written (gba-release, mGBA; +120 for
    // SPRITE_PALETTE, which looks its palette up too): the out-of-line path's
    // calls, on top of a plain sprite's 200.
    CHECK_TIMING(cost[1] > cost[0] && blend < 160);
    ecs_reset();
    sprite_table_set(NULL, 0);
}

TEST_SUITE(gba_blend_tests, "gba blend", {"settings_apply_in_vblank", settings_apply_in_vblank},
           {"layers_are_the_hardware_bits", layers_are_the_hardware_bits},
           {"bottom_zero_is_off_top_zero_is_sprites_only",
            bottom_zero_is_off_top_zero_is_sprites_only},
           {"clamps_and_ignores_with_one_warning_each", clamps_and_ignores_with_one_warning_each},
           {"brightness_pauses_blending", brightness_pauses_blending},
           {"splash_borrows_the_effect", splash_borrows_the_effect},
           {"blended_sprites_are_semi_transparent", blended_sprites_are_semi_transparent},
           {"blending_keeps_the_rest_of_the_sprite", blending_keeps_the_rest_of_the_sprite},
           {"blended_entities_cost", blended_entities_cost});
