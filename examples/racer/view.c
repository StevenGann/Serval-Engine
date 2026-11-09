// Drawing the road and the sky: a frame of road from three cheap tricks on
// one still picture.
//
// 1. The bend: a raster table. The road picture (art.c) is a straight road,
//    and it never changes. raster_scroll() gives every screen line its own
//    horizontal scroll, which the hardware takes from a table during the
//    gaps between lines (HBlank DMA). road_frame() (road.c) works out the
//    160 values: each line shifted by the curves ahead summed up from the
//    car to that line, and by where the car is across the road. A layer 16
//    metatiles (256 pixels) wide that wraps lies whole in VRAM, so any line
//    can take any shift; road.c keeps each line's road clear of the screen's
//    edges so its wrapped copy never shows.
//
// 2. The stripes: palette writes. Every tile row of the road has its own
//    palette bank, and its pixels name a material (grass, rumble strip,
//    tarmac, dash) and a group of lines in the row, never a colour. So one
//    write of thirteen colours per row makes each third of the row light or
//    dark: road_frame() says which, from the stretch of track the row's
//    lines show, and as the car moves on the boundaries roll down the
//    screen toward the player. Far up, where a light and a dark stretch
//    would fall within a few lines, the rows take the mix of the two (haze)
//    rather than flicker. Twelve calls of tileset_set_colors() a frame, all
//    copied to the palette in the next VBlank.
//
// 3. The sky's swing: a whole-layer scroll. The sky (another wrapping
//    layer) slides sideways by the curve under the car times the speed,
//    so the horizon swings the other way as the road bends.
//
// The time of day is a walk through art.c's colour stages, one stage every
// few frames, from the one shown toward the one wanted: the road's colours
// come from the stage with the stripes every frame, and the sky's bank, the
// backdrop and the sprites' palettes are written only when the stage
// changes. A bump, or a crash's first frames, shakes the picture by
// scrolling both layers down a line or two (the raster table's shifts stay
// put: a line then shows a road a little narrower than its own, which keeps
// it clear of the edges).

#include "view.h"

#include "art.h"

// The sky drifts (curve * speed) >> SKY_SWING_SHIFT 256ths of a pixel a
// frame: about a pixel and a third at top speed on the sharpest curve.
#define SKY_SWING_SHIFT 10
// Frames per stage of the time of day: TIME_STEPS stages, about two seconds.
#define STAGE_FRAMES 8

// The raster table the engine reads at every frame_end(): this frame's
// offsets, copied from the road frame so the caller's may be anywhere.
static s16 offsets[SCREEN_H] SERVAL_EWRAM_BSS;
static s32 sky_x;        // the sky's scroll, 256ths of a pixel; counts on, never wraps
static int stage;        // the time of day shown, a stage of time_colors
static int stage_frames; // frames toward the next stage
static int written;      // the stage whose sky and sprite colours are in the palettes

void view_init(void) {
    tileset_load(&road_tileset);
    map_load(&road_layer);
    map_load(&sky_layer);
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    for (int y = 0; y < SCREEN_H; y++)
        offsets[y] = 0;
    raster_scroll(ROAD_BG, false, offsets);
    view_reset(TIME_STAGE(TIME_AFTERNOON));
}

void view_reset(int to) {
    sky_x = 0;
    stage = to;
    stage_frames = 0;
    written = -1;
}

void view_step(const RoadFrame* f, FIXED speed, int to) {
    sky_x += (f->curve * speed) >> SKY_SWING_SHIFT;
    if (stage == to) {
        stage_frames = 0;
    } else if (++stage_frames == STAGE_FRAMES) {
        stage_frames = 0;
        stage += stage < to ? 1 : -1;
    }
}

// Each road row's thirteen colours from its groups' shades.
static void stripe(const RoadFrame* f, const TimeColors* c) {
    Color colors[ROAD_STRIPE_COLORS + 1];
    for (int row = 0; row < ROAD_ROWS; row++) {
        for (int g = 0; g < ROAD_GROUPS; g++) {
            const Color* shade = c->road[f->shade[row][g]];
            for (int m = 0; m < MATERIALS; m++)
                colors[m * ROAD_GROUPS + g] = shade[m];
        }
        colors[ROAD_EDGE_COLOR - 1] = c->edge;
        tileset_set_colors(row * 16 + 1, colors, ROAD_STRIPE_COLORS + 1);
    }
}

void view_draw(const RoadFrame* f, int shake) {
    const TimeColors* c = &time_colors[stage];
    for (int y = HORIZON; y < SCREEN_H; y++)
        offsets[y] = f->offsets[y];
    stripe(f, c);
    if (written != stage) {
        written = stage;
        tileset_set_colors(SKY_BANK * 16 + 1, c->sky, 15);
        screen_set_backdrop(c->backdrop);
        sprite_set_colors(SPR_JEEP, 0, &c->sprites[0][0], TINTED_PALETTES * 16);
    }
    map_set_scroll(ROAD_BG, 0, -shake);
    map_set_scroll(SKY_BG, sky_x >> 8, -shake);
}

int view_shake(const Race* r) {
    // Down hard, back, down again, settling: indexed by frames left.
    static const u8 shake[CAR_BUMP_FRAMES + 1] = {0, 0, 0, 0, 1, 0, 1, 0, 1,
                                                  1, 0, 1, 2, 1, 0, 2, 2};
    int left = r->bump_timer;
    if (r->car_state == CAR_CRASHED) // a crash's first frames jolt as a bump does
        left = r->crash_timer - (CAR_CRASH_FRAMES - CAR_BUMP_FRAMES);
    return left > 0 && left <= CAR_BUMP_FRAMES ? shake[left] : 0;
}
