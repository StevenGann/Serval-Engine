// The streaming scene: streamed sprite groups, SPRITE_GROUP_STREAMED
// (docs/sprites.md#residency-modes).
//
// Plasma orbs: one 64x64 sprite of 32 frames, 2,048 tiles, twice what sprite
// VRAM holds. Its group is streamed with 8 slots (512 tiles at the top of
// sprite VRAM), so only the frames on screen are in VRAM: each frame drawn
// takes a slot, a frame still in a slot from an earlier frame costs nothing,
// and a new one is copied to its slot in VBlank by frame_end(). Six orbs, each
// at its own speed, are six frames: most of them new every frame.
//
// A puts every orb on the same frame: they share one slot, one copy a frame.
// B shows ten orbs on ten different frames, two more than the slots: the last
// two drawn are not drawn (the HUD shows sprite_stats().dropped). The stars
// behind are resident sprites of another group, below the slots in VRAM.
//
// The frames are drawn on the canvas (art.c) the first time the scene is
// entered, into EWRAM; a game's would be ROM data from its build (a streamed
// sprite's frames are copied from wherever .tiles points). The scene's
// contract (what main.c resets, what leave() must undo) is in effects.h;
// main.c's header says what the scene looks like.

#include "effects.h"

enum { SPR_ORB, SPR_STAR, SPRITE_COUNT };

#define ORB_FRAMES 32
#define ORB_SIZE 64
#define ORB_TILES (ORB_SIZE / 8 * ORB_SIZE / 8)
#define SLOTS 8
#define ORBS 6
#define CROWD 10 // B: more orbs on different frames than there are slots
#define STARS 24

static u32 orb_tiles[ORB_FRAMES * ORB_TILES * 8] SERVAL_EWRAM_BSS; // 64 KB
static bool orb_drawn;

static const u32 star_tiles[8] = {
    0x00000000, 0x00010000, 0x00010000, 0x01121100, 0x00010000, 0x00010000, 0x00000000, 0,
};

static const SpriteAsset orb = {
    .size = SPRITE_64x64, .frame_count = ORB_FRAMES, .tiles = orb_tiles};
static const SpriteAsset star = {.size = SPRITE_8x8, .tiles = star_tiles};
static const SpriteAsset* const sprite_table[SPRITE_COUNT] = {[SPR_ORB] = &orb, [SPR_STAR] = &star};

// The orbs: a ramp from deep violet through magenta and orange to white, and
// a dark rim.
#define RIM 15
static const u16 orb_palette[16] = {
    0,
    COLOR_RGB(24, 8, 56),
    COLOR_RGB(48, 12, 96),
    COLOR_RGB(80, 16, 136),
    COLOR_RGB(120, 24, 168),
    COLOR_RGB(160, 32, 176),
    COLOR_RGB(200, 48, 160),
    COLOR_RGB(232, 72, 128),
    COLOR_RGB(248, 112, 96),
    COLOR_RGB(255, 152, 72),
    COLOR_RGB(255, 192, 72),
    COLOR_RGB(255, 224, 112),
    COLOR_RGB(255, 240, 168),
    COLOR_RGB(255, 252, 224),
    COLOR_RGB(255, 255, 255),
    [RIM] = COLOR_RGB(16, 4, 32),
};
static const u16 star_palette[16] = {0, COLOR_RGB(120, 140, 200), COLOR_RGB(230, 240, 255)};

static const u16 orb_ids[] = {SPR_ORB};
static const SpriteGroup orb_group = {
    .sprite_ids = orb_ids,
    .palettes = orb_palette,
    .sprite_count = 1,
    .palette_count = 1,
    .flags = SPRITE_GROUP_STREAMED,
    .slots = SLOTS,
};
static const u16 star_ids[] = {SPR_STAR};
static const SpriteGroup star_group = {
    .sprite_ids = star_ids, .palettes = star_palette, .sprite_count = 1, .palette_count = 1};

// The orb's 32 frames: a plasma (two waves across it, one down it and one
// from the middle, each moving with the frame, a full cycle over the 32)
// inside a circle, darker toward the rim.
static void draw_orb(void) {
    static u8 radius[ORB_SIZE * ORB_SIZE] SERVAL_EWRAM_BSS; // half pixels from the middle
    for (int y = 0; y < ORB_SIZE; y++) {
        for (int x = 0; x < ORB_SIZE; x++) {
            int dx = x * 2 - (ORB_SIZE - 1), dy = y * 2 - (ORB_SIZE - 1);
            int d2 = dx * dx + dy * dy, r = 0;
            while ((r + 1) * (r + 1) <= d2)
                r++;
            radius[y * ORB_SIZE + x] = (u8)int_min(r, 255);
        }
    }
    for (u32 f = 0; f < ORB_FRAMES; f++) {
        u16 phase = (u16)(f * 65536 / ORB_FRAMES);
        FIXED across[ORB_SIZE], down[ORB_SIZE], ring[ORB_SIZE];
        for (u32 k = 0; k < ORB_SIZE; k++) {
            across[k] = fx_sin((u16)(k * 1400 + phase));
            down[k] = fx_sin((u16)(k * 1100 - 2 * phase)) + fx_sin((u16)(k * 700 + phase));
            ring[k] = fx_sin((u16)(k * 1800 - phase));
        }
        canvas_begin(ORB_SIZE, ORB_SIZE);
        for (int y = 0; y < ORB_SIZE; y++) {
            for (int x = 0; x < ORB_SIZE; x++) {
                int r = radius[y * ORB_SIZE + x];
                if (r > 62)
                    continue; // outside: transparent
                if (r > 58) {
                    canvas_plot(x, y, RIM);
                    continue;
                }
                int v = across[x] + down[y] + 2 * ring[r / 2]; // -1280 to 1280
                int level = 1 + (v + 1280) * 13 / 2561;        // 1-13
                level -= r * r / 1100;                         // darker toward the rim
                level += r < 20;                               // a brighter heart
                canvas_plot(x, y, (u32)int_clamp(level, 1, 14));
            }
        }
        canvas_pack(&orb_tiles[f * ORB_TILES * 8]);
    }
}

static bool synced; // A: every orb on the same frame
static bool crowd;  // B: ten orbs
static u32 ticks;
// What the HUD's last line shows, to print it only when it changes.
static u32 shown_orbs, shown_dropped;
static bool shown_synced;

static void enter(void) {
    if (!orb_drawn) {
        draw_orb();
        orb_drawn = true;
    }
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&star_group);
    sprite_group_load(&orb_group);
    screen_set_backdrop(COLOR_RGB(8, 6, 20));
    synced = crowd = false;
    ticks = 0;
    shown_orbs = shown_dropped = 0xFFFF;
    text_print_centered(1, "32 FRAMES, ONLY 8 IN VRAM");
    text_print_centered(18, "A: IN STEP   B: TEN ORBS");
}

static void update(void) {
    if (button_pressed(BUTTON_A))
        synced = !synced;
    if (button_pressed(BUTTON_B))
        crowd = !crowd;
    ticks++;

    // Six orbs in two rows of three, or ten in two rows of five, overlapping.
    u32 count = crowd ? CROWD : ORBS, per_row = count / 2;
    int step = crowd ? 44 : 76, left = crowd ? 8 : 12;
    for (u32 k = 0; k < count; k++) {
        // Each orb at its own speed and phase; all on one frame (A); or, ten
        // of them, three frames apart (B).
        u32 frame = synced  ? ticks / 2
                    : crowd ? ticks / 2 + k * 3
                            : ticks * (k % 3 + 1) / 3 + k * 5;
        int x = left + (int)(k % per_row) * step;
        int y = 16 + (int)(k / per_row) * 64;
        sprite_draw(SPR_ORB, (u8)(frame % ORB_FRAMES), x, y, 0);
    }
    for (u32 k = 0; k < STARS; k++) // resident, behind the orbs
        sprite_draw(SPR_STAR, 0, (int)((k * 97 + ticks / 4) % SCREEN_W), 8 + (int)(k * 53 % 136),
                    SPRITE_BEHIND_PLAYFIELD);

    // The last frame's dropped draws: orbs past the slots.
    u32 dropped = sprite_stats().dropped;
    if (count != shown_orbs || dropped != shown_dropped || synced != shown_synced) {
        shown_orbs = count;
        shown_dropped = dropped;
        shown_synced = synced;
        text_print_centered(
            19, text_format("%u ORBS%s, DROPPED: %u", count, synced ? " IN STEP" : "", dropped));
    }
}

static void leave(void) {}

const Scene stream_scene = {.name = "STREAMING", .enter = enter, .update = update, .leave = leave};
