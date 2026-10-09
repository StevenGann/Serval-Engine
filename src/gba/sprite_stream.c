#include "serval/sprites.h"

#include <tonc.h>

#include "../core/warn.h"
#include "internal.h"

// Streamed sprite groups (docs/sprites.md#residency-modes): a frame cache.
// Each loaded streamed group owns `slots` slots of sprite VRAM, taken from the
// top down when it loads (sprites.c's loader allocates resident groups from
// tile 0 up, below serval_stream_floor()), each the size of the group's
// largest frame. A draw of one of its sprites asks for the slot holding that
// (sprite, frame) this frame: the slot it is in already (a hit costs nothing
// more), or the least recently drawn slot not drawn from this frame, with the
// frame's tiles queued to be copied from ROM in VBlank (frame_end(), step 2 of
// docs/frame-loop.md's flush, right after the OAM that shows it). sprites.c
// keeps the draw records; this file keeps the slots, in EWRAM, and runs in
// ROM: nothing here is on the resident sprites' path.

#define OBJ_TILE_COUNT 1024

// OBJ VRAM as one array of tiles (it spans two of libtonc's 512-tile charblocks).
static TILE* const obj_tiles = (TILE*)MEM_VRAM_OBJ;

// Fixed pools. A group with slots holds an ordinary sprite, which needs a
// palette of its group, so no more than 16 such groups (one per palette bank)
// can be loaded at once. The slots in all are limited to 256 (sprites.h,
// sprite_group_load()): a frame needs a hardware sprite to be on screen, so
// at most 128 slots are drawn from in a frame; the rest keep frames cached
// for later frames.
#define STREAM_MAX_GROUPS 16
#define STREAM_MAX_SLOTS 256
// One copy per slot that takes a new frame. A slot is taken only by a draw
// that got a hardware sprite, so between frame_begin() and frame_end() there
// are at most 128.
#define STREAM_MAX_COPIES 128

#define EMPTY_KEY 0xFFFFFFFFu // no sprite ID is 0xFFFF

typedef struct {
    u16 first_slot; // its slots in slot_key and slot_drawn
    u16 slot_count;
    u16 first_tile; // VRAM tile of its slot 0; slot s at first_tile + s * slot_tiles
    u8 slot_tiles;  // tiles per slot: its largest frame's
    u8 segment;     // the mark segment it loaded in (sprites.c)
    u16 recent;     // the slot of its last hit or new frame, tried first
    u16 pad;
} StreamGroup;

typedef struct {
    const u32* from; // the frame's tiles in ROM
    u16 tile;        // its slot's first tile
    u16 words;       // 8 per tile
} StreamCopy;

static EWRAM_BSS StreamGroup groups[STREAM_MAX_GROUPS];
// Per slot: the frame it holds (sprite ID | frame << 16, or EMPTY_KEY) and the
// frame it was last drawn in (serial below; 0 for never).
static EWRAM_BSS u32 slot_key[STREAM_MAX_SLOTS];
static EWRAM_BSS u32 slot_drawn[STREAM_MAX_SLOTS];
static EWRAM_BSS StreamCopy copies[STREAM_MAX_COPIES];

// None of it is on a hot path, so all of it is in EWRAM, IWRAM being the
// game's.
static EWRAM_BSS u32 group_count;
static EWRAM_BSS u32 slot_count; // slots taken by the loaded groups
static EWRAM_BSS u32 copy_count;
static SERVAL_EWRAM_DATA u32 floor_tile = OBJ_TILE_COUNT; // the lowest tile the slots use
// The frame being drawn: advanced by every frame_end(), so a slot whose
// slot_drawn equals it holds a frame drawn this frame and can't be taken.
static SERVAL_EWRAM_DATA u32 serial = 1;

#ifdef SERVAL_DEBUG
static EWRAM_BSS bool warned_full;
#endif

u32 serval_stream_floor(void) {
    return floor_tile;
}

int serval_stream_add(u32 slots, u32 slot_tiles, u32 segment) {
    if (group_count == STREAM_MAX_GROUPS)
        return -1; // can't happen: the palette banks run out first
    if (slot_count + slots > STREAM_MAX_SLOTS) {
        SERVAL_WARN("sprite_group_load: %u slots, but the loaded streamed groups have %u of the %u "
                    "there can be; not loaded",
                    slots, slot_count, STREAM_MAX_SLOTS);
        return -1;
    }
    // sprite_group_load() checked that the slots fit above the resident groups.
    floor_tile -= slots * slot_tiles;
    StreamGroup* g = &groups[group_count];
    *g = (StreamGroup){.first_slot = (u16)slot_count,
                       .slot_count = (u16)slots,
                       .first_tile = (u16)floor_tile,
                       .slot_tiles = (u8)slot_tiles,
                       .segment = (u8)segment};
    for (u32 s = slot_count; s < slot_count + slots; s++) {
        slot_key[s] = EMPTY_KEY;
        slot_drawn[s] = 0;
    }
    slot_count += slots;
    return (int)group_count++;
}

#ifdef SERVAL_DEBUG
static __attribute__((noinline, cold)) void warn_full(u32 id, u32 slots) {
    if (warned_full)
        return;
    warned_full = true;
    SERVAL_WARN("sprite %u: more frames of its streamed group drawn in one frame than its %u "
                "slots; the extra ones are not shown (sprite_stats().dropped)",
                id, slots);
}
#define SLOTS_FULL(id, slots) warn_full(id, slots)
#else
#define SLOTS_FULL(id, slots) ((void)(id), (void)(slots))
#endif

int serval_stream_slot(u32 group, u32 key, const u32* from, u32 tiles) {
    StreamGroup* g = &groups[group];
    u32* keys = &slot_key[g->first_slot];
    u32* drawn = &slot_drawn[g->first_slot];
    // Draws of one frame often come together (a crowd on the same frame, a
    // character drawn each frame): its last slot first.
    u32 s = g->recent;
    if (keys[s] != key) {
        // Otherwise one pass: the slot holding it, or the one drawn longest
        // ago, not this frame (never-drawn slots have 0, the oldest).
        u32 oldest = serial, victim = g->slot_count;
        for (s = 0; s < g->slot_count; s++) {
            if (keys[s] == key)
                break;
            if (drawn[s] < oldest) {
                oldest = drawn[s];
                victim = s;
            }
        }
        if (s == g->slot_count) {
            // A new frame. Every slot holds a frame drawn this frame (or, for
            // draws outside frame_begin() and frame_end(), the copies ran
            // out): not drawn.
            if (victim == g->slot_count || copy_count == STREAM_MAX_COPIES) {
                serval_sprites_dropped++;
                SLOTS_FULL(key & 0xFFFF, g->slot_count);
                return -1;
            }
            s = victim;
            keys[s] = key;
            copies[copy_count++] = (StreamCopy){from, (u16)(g->first_tile + s * g->slot_tiles),
                                                (u16)(tiles * (sizeof(TILE) / 4))};
        }
        g->recent = (u16)s;
    }
    drawn[s] = serial;
    return (int)(g->first_tile + s * g->slot_tiles);
}

// The groups loaded in segment `segment` and later are the last ones loaded:
// the top of the stack, and the lowest slots in VRAM.
void serval_stream_release(u32 segment) {
    while (group_count && groups[group_count - 1].segment >= segment)
        group_count--;
    if (group_count) {
        const StreamGroup* top = &groups[group_count - 1];
        slot_count = top->first_slot + top->slot_count;
        floor_tile = top->first_tile;
    } else {
        slot_count = 0;
        floor_tile = OBJ_TILE_COUNT;
    }
    // Copies queued for slots that are gone would overwrite whatever loads
    // there next.
    u32 kept = 0;
    for (u32 k = 0; k < copy_count; k++) {
        if (copies[k].tile >= floor_tile)
            copies[kept++] = copies[k];
    }
    copy_count = kept;
}

void serval_stream_reset(void) {
    group_count = 0;
    slot_count = 0;
    copy_count = 0;
    floor_tile = OBJ_TILE_COUNT;
#ifdef SERVAL_DEBUG
    warned_full = false;
#endif
}

void serval_stream_commit(void) {
    for (u32 k = 0; k < copy_count; k++)
        memcpy32(obj_tiles + copies[k].tile, copies[k].from, copies[k].words);
    copy_count = 0;
    serial++;
}
