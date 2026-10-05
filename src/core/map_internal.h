#ifndef SERVAL_CORE_MAP_INTERNAL_H
#define SERVAL_CORE_MAP_INTERNAL_H

// Engine-internal: map state shared by the portable map module (src/core/map.c,
// src/ecs/map_movement.c) and the platform code that draws the layers
// (src/gba/map.c). Not part of the public API.

#include "serval/map.h"

// The camera, read by sys_render and sys_render_by_depth every frame.
extern int serval_camera_x, serval_camera_y;

// The layer shown on each background (index 1-3; 0 unused), or NULL.
extern const MapLayer* serval_map_layers[4];
// map_set_scroll()'s offsets of backgrounds 1-3 (index 0 unused).
extern int serval_map_offset_x[4], serval_map_offset_y[4];

// The layer pixel shown at the screen's top-left for the layer on background
// bg: the camera times its scroll factor (none for MAP_LAYER_FIXED), rounded
// down, plus map_set_scroll()'s offset.
int serval_map_layer_x(const MapLayer* layer);
int serval_map_layer_y(const MapLayer* layer);

// Backgrounds whose layer was loaded or unloaded since the platform last drew
// them (bit n = background n): they are redrawn from scratch.
extern u8 serval_map_reload;

// The playfield's runtime changes (map_set_cell): cell (mx, my) shows metatile
// `cell` instead of what the layer's data says.
typedef struct {
    u16 mx, my, cell;
} MapChange;
extern MapChange serval_map_changes[MAP_MAX_CHANGES];
extern u32 serval_map_change_count;

// Playfield cells changed since the platform last drew them. If more cells
// changed than fit, serval_map_redraw_all is set instead.
extern MapChange serval_map_redraw[MAP_MAX_CHANGES];
extern u32 serval_map_redraw_count;
extern bool serval_map_redraw_all;

// Called by map_load(): the platform starts drawing map layers (GBA: hooks
// the VBlank flush into frame_end()). Host builds do nothing.
void serval_map_attach(void);

// The metatile index of cell (mx, my) of the playfield, which must be inside
// it, including runtime changes.
u32 serval_map_cell_in(const MapLayer* layer, u32 mx, u32 my);

#endif // SERVAL_CORE_MAP_INTERNAL_H
