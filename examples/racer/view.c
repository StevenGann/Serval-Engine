// Drawing the race: a frame of road from three cheap tricks on one still
// picture, and the jeep on top.
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
//    write of twelve colours per row makes each third of the row light or
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

#include "view.h"

#include "art.h"

// The jeep's wheels stand on this line, a few above the screen's bottom.
#define JEEP_FOOT (SCREEN_H - 3)
// The sky drifts (curve * speed) >> SKY_SWING_SHIFT 256ths of a pixel a
// frame: about a pixel and a third at top speed on the sharpest curve.
#define SKY_SWING_SHIFT 10

// The raster table the engine reads at every frame_end(): this frame's
// offsets, copied from the road frame so the caller's may be anywhere.
static s16 offsets[SCREEN_H] SERVAL_EWRAM_BSS;
static s32 sky_x; // the sky's scroll, 256ths of a pixel; counts on, never wraps
static u8 time_of_day;

void view_init(void) {
    tileset_load(&road_tileset);
    map_load(&road_layer);
    map_load(&sky_layer);
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    // The sky's lowest colour behind everything (nothing should show it).
    screen_set_backdrop(COLOR_RGB(252, 208, 124));
    for (int y = 0; y < SCREEN_H; y++)
        offsets[y] = 0;
    raster_scroll(ROAD_BG, false, offsets);
    view_reset();
}

void view_reset(void) {
    sky_x = 0;
    time_of_day = TIME_AFTERNOON;
    map_set_scroll(SKY_BG, 0, 0);
}

// Each road row's twelve colours from its groups' shades.
static void stripe(const RoadFrame* f) {
    const RoadColors* c = &road_colors[time_of_day];
    Color colors[ROAD_STRIPE_COLORS];
    for (int row = 0; row < ROAD_ROWS; row++) {
        for (int g = 0; g < ROAD_GROUPS; g++) {
            const Color* shade = c->shade[f->shade[row][g]];
            for (int m = 0; m < MATERIALS; m++)
                colors[m * ROAD_GROUPS + g] = shade[m];
        }
        tileset_set_colors(row * 16 + 1, colors, ROAD_STRIPE_COLORS);
    }
}

// The jeep: its frame by the steering, lit tail lights while braking, a
// jolt off the road, a wobble while it spins out after a crash.
static void draw_jeep(const Race* r, const RoadFrame* f) {
    static const s8 jolt[4] = {0, -1, -2, -1};
    int frame = JEEP_STRAIGHT;
    int y = JEEP_FOOT;
    if (r->car_state == CAR_CRASHED)
        frame = (r->crash_timer >> 3) & 1 ? JEEP_LEFT : JEEP_RIGHT;
    else if (r->speed > 0 && r->steer)
        frame = r->steer < 0 ? JEEP_LEFT : JEEP_RIGHT;
    if (r->car_state == CAR_OFFROAD && r->speed > 0)
        y += jolt[(r->distance >> 10) & 3];
    u16 flags = r->braking ? SPRITE_PALETTE(PAL_JEEP_BRAKING) : 0;
    sprite_draw(SPR_JEEP, (u8)frame, f->car_x, y, flags);
}

void view_frame(const Race* r, const RoadFrame* f) {
    for (int y = HORIZON; y < SCREEN_H; y++)
        offsets[y] = f->offsets[y];
    stripe(f);
    sky_x += (f->curve * r->speed) >> SKY_SWING_SHIFT;
    map_set_scroll(SKY_BG, sky_x >> 8, 0);
    draw_jeep(r, f);
}
