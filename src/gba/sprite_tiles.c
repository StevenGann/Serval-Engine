// Runtime sprite tiles (docs/sprites.md#runtime-tiles): sprite_set_tiles()
// queues new pixels for one frame of a loaded sprite, and frame_end() copies
// them to OBJ VRAM in VBlank, step 3 of its flush (docs/frame-loop.md): the
// sprites' counterpart of tileset_set_tiles(), built the same way (map.c).
//
// A queued copy holds where the frame is in VRAM when the call is made
// (serval_sprite_frames(), sprites.c). That stays right until the copy: a
// load never moves a loaded sprite, and sprite_groups_release() and
// sprite_groups_reset() drop the copies queued for the sprites they unload.
// Releasing to a mark frees every tile from the mark's first on, and only
// those, so the copies it drops are the ones aimed there.
//
// frame_end() calls serval_sprite_tiles_commit() in every game, whether or not
// it uses runtime tiles; with nothing queued, that is a test of a counter.
// The state (about 100 bytes) is in EWRAM, so it takes no IWRAM.

#include "serval/sprites.h"

#include <tonc.h>

#include "../core/sprite_internal.h"
#include "../core/warn.h"
#include "internal.h"

typedef struct {
    const u32* tiles; // the new pixels: count tiles, 8 words each
    u16 first;        // the frame's first tile in OBJ VRAM
    u16 sprite_id;    // with frame: what a later call replaces
    u8 frame;
    u8 count; // the sprite's tiles_per_frame
} SpriteTileCopy;

static EWRAM_BSS SpriteTileCopy queue[SPRITE_MAX_TILE_UPDATES];
static EWRAM_BSS u32 queued;

// OBJ VRAM as one array of 1024 tiles (charblocks 4-5).
static TILE* const obj_tiles = (TILE*)MEM_VRAM_OBJ;

#ifdef SERVAL_DEBUG
// Each problem is reported once, until sprite_groups_reset() (as sprites.c's
// warnings are).
enum {
    W_TABLE,
    W_NOT_LOADED,
    W_META,
    W_STREAMED,
    W_FRAME,
    W_DATA,
    W_FULL,
};
static EWRAM_BSS u32 warned;
#define WARN_ONCE(kind, ...)                                                                       \
    do {                                                                                           \
        if (!(warned & (1u << (kind)))) {                                                          \
            warned |= 1u << (kind);                                                                \
            SERVAL_WARN(__VA_ARGS__);                                                              \
        }                                                                                          \
    } while (0)
#else
#define WARN_ONCE(kind, ...) ((void)0)
#endif

void sprite_set_tiles(u16 sprite_id, u8 frame, const u32* tiles) {
    ServalSpriteFrames f = serval_sprite_frames(sprite_id);
    if (f.kind != SERVAL_SPRITE_ORDINARY) {
        if (sprite_id >= serval_sprite_count)
            WARN_ONCE(W_TABLE,
                      "sprite_set_tiles: sprite ID %u is not in the sprite table (%u sprites); "
                      "ignored",
                      sprite_id, serval_sprite_count);
        else if (f.kind == SERVAL_SPRITE_META)
            WARN_ONCE(W_META,
                      "sprite_set_tiles: sprite %u is a metasprite, which has no tiles of its "
                      "own; set its pieces' sprites' tiles instead. Ignored",
                      sprite_id);
        else if (f.kind == SERVAL_SPRITE_STREAMED)
            WARN_ONCE(W_STREAMED,
                      "sprite_set_tiles: sprite %u is in a streamed group, whose frames are "
                      "copied from ROM as they are drawn; ignored (load it in a resident group)",
                      sprite_id);
        else
            WARN_ONCE(W_NOT_LOADED,
                      "sprite_set_tiles: sprite %u is not loaded; load a sprite group containing "
                      "it first. Ignored",
                      sprite_id);
        return;
    }
    if (frame >= f.frame_count) {
        WARN_ONCE(W_FRAME, "sprite_set_tiles: sprite %u has no frame %u (it has %u); ignored",
                  sprite_id, frame, f.frame_count);
        return;
    }
    if (!serval_plausible_pointer(tiles)) {
        WARN_ONCE(W_DATA, "sprite_set_tiles: the tiles pointer is NULL or not valid; ignored");
        return;
    }
    u32 k = 0;
    while (k < queued && (queue[k].sprite_id != sprite_id || queue[k].frame != frame))
        k++;
    if (k == SPRITE_MAX_TILE_UPDATES) {
        WARN_ONCE(W_FULL,
                  "sprite_set_tiles: more than %u frames queued in one frame; the extra ones "
                  "are ignored",
                  SPRITE_MAX_TILE_UPDATES);
        return;
    }
    if (k == queued)
        queued++;
    queue[k] = (SpriteTileCopy){.tiles = tiles,
                                .first = (u16)(f.first_tile + frame * f.tiles_per_frame),
                                .sprite_id = sprite_id,
                                .frame = frame,
                                .count = f.tiles_per_frame};
}

// Out of line, so that with nothing queued the call is little more than a
// load and a test.
static __attribute__((noinline)) void copy_queued(void) {
    for (u32 k = 0; k < queued; k++)
        memcpy32(obj_tiles + queue[k].first, queue[k].tiles, queue[k].count * 8u);
    queued = 0;
}

// In VBlank, after the OAM (frame_end()).
void serval_sprite_tiles_commit(void) {
    if (queued)
        copy_queued();
}

void serval_sprite_tiles_reset(void) {
    queued = 0;
#ifdef SERVAL_DEBUG
    warned = 0;
#endif
}

void serval_sprite_tiles_release(u32 first_tile) {
    u32 kept = 0;
    // Rare (room changes): GCC would unroll it for all 8 entries, 300 bytes.
#pragma GCC unroll 1
    for (u32 k = 0; k < queued; k++) {
        if (queue[k].first < first_tile)
            queue[kept++] = queue[k];
    }
    queued = kept;
}
