// asteroids: the arcade classic. Steer a ship, shoot rocks, survive waves.
//
// Demonstrates:
//   - The engine's splash screen (serval_splash)
//   - Rotating sprites (sprite_draw_rotated / spr_angle): the ship turns, and
//     rocks spin while sharing a few rotation matrices
//   - Wrap-around physics (physics_set_wrap): the ship, rocks and shots
//     leaving one edge come back on the other (sparks have no body and just
//     fly off)
//   - Thrust and drag with sine and cosine (fx_sin, fx_cos, fx_mul)
//   - Many short-lived entities (bullets, rocks splitting in two, explosion
//     sparks and chips of rock) created and destroyed every frame, with game
//     components; their frames chosen by age (sparks cool from white to red)
//   - Effects that aren't entities: blast rings and exhaust puffs in a small
//     array of their own, drawn with sprite_draw / sprite_draw_ex (scaled)
//   - Collisions with body_overlap, using hitboxes smaller than the sprites
//     (sprite origins center the art on its hitbox)
//   - Blinking by toggling SPRITE_HIDDEN in spr_flags
//   - A space backdrop: a starfield map layer that wraps and drifts
//     (map_load, MAP_LAYER_WRAP, map_set_scroll), stars that twinkle by
//     changing three background colors every frame (tileset_set_colors,
//     color_mix), and a sky gradient with a color per scanline
//     (raster_backdrop), dithered so it shows no bands
//   - Juice: screen shake by moving the camera (camera_set), flashes and fades
//     with screen_set_brightness, a title of big sprite letters that bob and
//     glint (SPRITE_PALETTE draws a letter with a lit-up palette)
//   - Sound effects: shots, thrust rumble, explosions, a heartbeat that speeds
//     up as rocks are cleared, and jingles; sound priorities keep gunfire from
//     cutting a jingle short and the rumble from cutting an explosion short
//   - An attract mode: rocks drift behind the title screen, which alternates
//     with the high-score table
//   - Save data (save_read, save_write, save_erase): a top-10 table of scores
//     and initials, kept in one save slot as a small versioned struct, with
//     the default table for a first boot or a save that can't be used
//     (scores.c; how a game picks and raises the version is explained there)
//   - A game split into files: main.c runs the game, scores.c the table and
//     the initials entry screen, art.h the graphics
//   - Text styles (text_set_style, text_set_style_color) and the font's drop
//     shadow (text_set_shadow): gold headings, dim instructions, the new entry
//     in the table in yellow; held-button repeat for the letters
//     (button_repeat, with button_repeat_reset so UP still held from
//     thrusting doesn't run through them); counting and listing entities of a
//     kind (ecs_count, ecs_gather)
//
// What to expect when booting the ROM:
//   - First the Serval Engine splash: "made with" and the engine's logo (a
//     serval's head beside "SERVAL ENGINE" in chunky gold and cream letters)
//     fade in on black, a coin-like jingle plays, and they fade out (about 3
//     seconds; any button skips it once the logo is in).
//   - The title fades in: deep space, blue at the top, violet in the middle
//     and blue-green at the bottom, with faint purple nebulae and stars that
//     twinkle, all drifting slowly. Four large cratered brown rocks drift and
//     spin across it behind "ASTEROIDS" in big gold-to-red letters that bob
//     in a wave (a white glint runs along them every few seconds), a blinking
//     "PRESS START" and the controls in pale blue.
//   - After START (a rising chime and a white flash): "WAVE 1" in gold for a
//     moment, a yellow ring closing in on the center where a silver dart of a
//     ship (red stripes, cyan canopy) appears, blinking while it can't be hit,
//     and a wave of large rocks. The top line shows "SCORE" (six digits) and
//     "WAVE"; small ship icons at the top right count the ships left (past 5,
//     one icon and "x6").
//   - Left and Right turn the ship; Up thrusts (a flickering flame, a trail of
//     orange puffs and a low rumble) and the ship drifts on, slowing gently to
//     a stop; A fires glowing cyan shots ("pew"), up to 4 at a time, in the
//     direction the ship faces.
//   - Shot rocks burst with a crunch into sparks (white-hot, cooling to red
//     embers) and chips of rock, inside a ring of fire that grows and fades;
//     they split: large into two medium, medium into two small; small ones
//     vanish. The screen shakes a little for large and medium ones. 20, 50
//     and 100 points. The rocks come in three shapes of each size.
//   - A low two-note heartbeat plays throughout, faster as the wave is cleared.
//   - Hitting a rock destroys the ship: a white flash, a big ring of fire, a
//     burst of sparks and a hard shake (the rock breaks and scores as if
//     shot); a new ship appears in the center after a moment, blinking for two
//     seconds. A rising jingle marks an extra ship every 10,000 points.
//   - Clearing all rocks starts the next wave ("WAVE 2", ...) with one more
//     large rock (up to 14), placed away from the ship.
//   - With no ships left: "GAME OVER" in gold with a falling tune. If the
//     score doesn't make the high-score table, START plays again, and after 10
//     seconds the attract mode comes back.
//   - If it does: after 2 seconds (or START), "NEW HIGH SCORE" with a rising
//     jingle, the score, its place, and three yellow letters "A  A  A" with a
//     caret under the one being changed. UP and DOWN cycle it through A-Z, '.'
//     and space (shown as '_'), with a short blip, repeating while held (UP
//     still held from thrusting does nothing until released and pressed
//     again); A or RIGHT moves to the next letter, B or LEFT back (a lower
//     blip). A on the last letter, or START at any time, confirms with a
//     two-note chime: the table is saved and shown with the new entry in
//     yellow.
//   - The attract mode alternates every 5 seconds, fading through black,
//     between the title and the "HIGH SCORES" table: places 1 to 10, initials
//     and scores. On a first boot it holds made-up players (SRV 5000 down to
//     MAX 400). The table survives power-off: in mGBA, in the .sav file next
//     to the ROM.
//   - For demos: on the title or the table, hold L and R and press SELECT to
//     erase the saved table. A falling tone plays, and the default table
//     shows with "HIGH SCORES RESET" under it.
//   - START pauses (the screen dims, "PAUSED"; the stars still twinkle) and
//     resumes.
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers.

#include "serval/serval.h"

#include "art.h"
#include "scores.h"

// --- Sounds ------------------------------------------------------------------

enum {
    SND_START,
    SND_FIRE,
    SND_THRUST,
    SND_EXPLODE_LARGE,
    SND_EXPLODE_MEDIUM,
    SND_EXPLODE_SMALL,
    SND_SHIP_EXPLODE,
    SND_BEAT_LOW,
    SND_BEAT_HIGH,
    SND_EXTRA_SHIP,
    SND_GAME_OVER,
    SND_PAUSE,
    SND_HIGH_SCORE,
    SND_LETTER,
    SND_NEXT_LETTER,
    SND_CONFIRM,
    SND_RESET,
    SOUND_COUNT
};

static const u16 start_notes[] = {392, 523, 659, 784};
static const u16 extra_ship_notes[] = {784, 988, 1175, 1568};
static const u16 game_over_notes[] = {392, 330, 262, 196};
static const u16 high_score_notes[] = {523, 659, 784, 0, 659, 784, 1047, 1047};
static const u16 confirm_notes[] = {784, 1047};

// Shots, jingles and the pause tick on square 1, the heartbeat on square 2,
// rumble and explosions on the noise channel. A sound can't replace one of
// higher priority on its channel: shots don't cut a jingle short, and the
// thrust rumble (renewed every few frames) doesn't cut an explosion short.
// The initials entry screen's blips use square 2 (the heartbeat is silent by
// then), so typing doesn't cut the high-score jingle short either.
#define JINGLE_PRIORITY 1    // jingles and the pause tick, over shots
#define EXPLOSION_PRIORITY 1 // explosions, over the rumble
static const PsgSound sounds[SOUND_COUNT] = {
    [SND_START] = {.duty = PSG_DUTY_25,
                   .frames = 5,
                   .notes = start_notes,
                   .note_count = 4,
                   .priority = JINGLE_PRIORITY},
    [SND_FIRE] = {.channel = PSG_SQUARE1,
                  .duty = PSG_DUTY_25,
                  .frequency = 1600,
                  .frames = 7,
                  .slide = -1,
                  .slide_size = 2,
                  .fade = -1,
                  .volume = 10},
    [SND_THRUST] = {.channel = PSG_NOISE, .frequency = 2000, .frames = 9, .volume = 6},
    [SND_EXPLODE_LARGE] = {.channel = PSG_NOISE,
                           .frequency = 1500,
                           .fade = -3,
                           .priority = EXPLOSION_PRIORITY},
    [SND_EXPLODE_MEDIUM] = {.channel = PSG_NOISE,
                            .frequency = 4000,
                            .fade = -2,
                            .priority = EXPLOSION_PRIORITY},
    [SND_EXPLODE_SMALL] = {.channel = PSG_NOISE,
                           .frequency = 9000,
                           .fade = -1,
                           .priority = EXPLOSION_PRIORITY},
    [SND_SHIP_EXPLODE] = {.channel = PSG_NOISE,
                          .frequency = 800,
                          .fade = -5,
                          .priority = EXPLOSION_PRIORITY},
    [SND_BEAT_LOW] = {.channel = PSG_SQUARE2, .frequency = 98, .frames = 6, .fade = -1},
    [SND_BEAT_HIGH] = {.channel = PSG_SQUARE2, .frequency = 110, .frames = 6, .fade = -1},
    [SND_EXTRA_SHIP] = {.duty = PSG_DUTY_12,
                        .frames = 5,
                        .notes = extra_ship_notes,
                        .note_count = 4,
                        .priority = JINGLE_PRIORITY},
    [SND_GAME_OVER] = {.duty = PSG_DUTY_25,
                       .frames = 12,
                       .notes = game_over_notes,
                       .note_count = 4,
                       .priority = JINGLE_PRIORITY},
    [SND_PAUSE] = {.duty = PSG_DUTY_12, .frequency = 784, .frames = 3, .priority = JINGLE_PRIORITY},
    [SND_HIGH_SCORE] = {.duty = PSG_DUTY_12,
                        .frames = 6,
                        .notes = high_score_notes,
                        .note_count = 8,
                        .priority = JINGLE_PRIORITY},
    [SND_LETTER] = {.channel = PSG_SQUARE2, .duty = PSG_DUTY_25, .frequency = 1319, .frames = 2},
    [SND_NEXT_LETTER] = {.channel = PSG_SQUARE2,
                         .duty = PSG_DUTY_25,
                         .frequency = 880,
                         .frames = 4},
    [SND_CONFIRM] = {.duty = PSG_DUTY_25,
                     .frames = 5,
                     .notes = confirm_notes,
                     .note_count = 2,
                     .priority = JINGLE_PRIORITY},
    [SND_RESET] =
        {.frequency = 880, .frames = 24, .slide = -2, .slide_size = 3, .priority = JINGLE_PRIORITY},
};

static const PsgSound* const sound_table[SOUND_COUNT] = {
    [SND_START] = &sounds[SND_START],
    [SND_FIRE] = &sounds[SND_FIRE],
    [SND_THRUST] = &sounds[SND_THRUST],
    [SND_EXPLODE_LARGE] = &sounds[SND_EXPLODE_LARGE],
    [SND_EXPLODE_MEDIUM] = &sounds[SND_EXPLODE_MEDIUM],
    [SND_EXPLODE_SMALL] = &sounds[SND_EXPLODE_SMALL],
    [SND_SHIP_EXPLODE] = &sounds[SND_SHIP_EXPLODE],
    [SND_BEAT_LOW] = &sounds[SND_BEAT_LOW],
    [SND_BEAT_HIGH] = &sounds[SND_BEAT_HIGH],
    [SND_EXTRA_SHIP] = &sounds[SND_EXTRA_SHIP],
    [SND_GAME_OVER] = &sounds[SND_GAME_OVER],
    [SND_PAUSE] = &sounds[SND_PAUSE],
    [SND_HIGH_SCORE] = &sounds[SND_HIGH_SCORE],
    [SND_LETTER] = &sounds[SND_LETTER],
    [SND_NEXT_LETTER] = &sounds[SND_NEXT_LETTER],
    [SND_CONFIRM] = &sounds[SND_CONFIRM],
    [SND_RESET] = &sounds[SND_RESET],
};

// --- Game state --------------------------------------------------------------

// Game components: what kind of object an entity is.
#define C_ROCK C_GAME(0)
#define C_BULLET C_GAME(1)
#define C_SPARK C_GAME(2)

// Hitboxes are smaller than the art, so near misses stay misses; the sprites'
// origins (art.h) center the art on them.
#define SHIP_HITBOX 10

#define TURN_SPEED ANGLE_DEG(4) // per frame
#define THRUST (FX_ONE / 12)    // pixels per frame per frame
#define MAX_SPEED FX(3)
#define BULLET_SPEED FX(4)
#define BULLET_LIFE 40 // frames
#define MAX_BULLETS 4
#define FIRE_COOLDOWN 8   // frames between shots
#define SPARK_LIFE_MIN 10 // frames
#define SPARK_LIFE_MAX 20
#define RESPAWN_DELAY 90        // frames without a ship after a crash
#define INVULNERABLE_FRAMES 120 // blinking after (re)spawning
#define START_SHIPS 3
#define EXTRA_SHIP_EVERY 10000
#define FIRST_WAVE_ROCKS 4
#define SPAWN_CLEARANCE 48 // pixels: new rocks appear at least this far from the ship

// Each wave has one more large rock, up to MAX_WAVE_ROCKS, so that everything
// fits in the engine's 128 entities. Worst case: 14 large rocks split into
// 56 small ones; 1 ship and 4 bullets; sparks: 16 from the ship plus 6 per
// broken rock (sparks and chips together), and while sparks last (20 frames)
// at most 8 rocks break (the 4 shots in flight, 3 more fired every 8 frames,
// 1 hit by the ship), so 64. 56 + 1 + 4 + 64 = 125. (The effects below are
// not entities; drawn last, they are the ones the hardware's 128 sprites
// leave out in such a crowd.)
#define MAX_WAVE_ROCKS 14

// TITLE is the attract mode, alternating between two pages; NEW_HIGH_SCORE
// is the initials entry screen (scores.c).
typedef enum { TITLE, PLAYING, PAUSED, GAME_OVER, NEW_HIGH_SCORE } State;
typedef enum { PAGE_TITLE, PAGE_SCORES } Page;

#define PAGE_FRAMES 300      // the attract pages alternate every 5 seconds
#define NEW_ENTRY_FRAMES 600 // the table with a new entry stays 10 seconds
#define GAME_OVER_FRAMES 120 // "GAME OVER" before the initials entry screen
#define GAME_OVER_IDLE 600   // without a high score, then the attract mode

// Per-entity game data, beside the engine's component pools.
static u8 life[MAX_ENT];      // C_BULLET, C_SPARK: frames left
static u8 rock_size[MAX_ENT]; // C_ROCK: 0 large, 1 medium, 2 small
static s8 rock_spin[MAX_ENT]; // C_ROCK: +1 or -1

static const u16 rock_sprites[3] = {SPR_ROCK_LARGE, SPR_ROCK_MEDIUM, SPR_ROCK_SMALL};
static const u8 rock_hitbox[3] = {24, 12, 6};
static const u16 rock_points[3] = {20, 50, 100};
static const u16 rock_sounds[3] = {SND_EXPLODE_LARGE, SND_EXPLODE_MEDIUM, SND_EXPLODE_SMALL};

static State state;
static Entity ship = ENTITY_NONE;
static u16 ship_angle;
static int ship_timer; // PLAYING: while > 0, waiting to respawn (no ship) or blinking
static int score, ships, wave, next_extra_ship;
static int fire_cooldown, rumble_timer;
static int beat_timer, beat_phase;
static int wave_shots; // shots it took to clear the wave when it started
static u16 spin;       // shared rock rotation: every rock uses spin or -spin
static Page page;
static int page_timer;   // TITLE: frames left on this page; GAME_OVER: frames shown
static int rank;         // the last game's place in the high-score table, or -1
static int highlight;    // the table entry to mark (the newest), or -1
static int banner_timer; // PLAYING: frames left showing "WAVE n"

// --- Helpers -----------------------------------------------------------------

// Creates an entity with a sprite and a square hitbox centered at (x, y).
static u32 spawn(u32 components, u16 sprite, int hitbox, FIXED x, FIXED y, FIXED vx, FIXED vy) {
    Entity e = entity_create(C_POS | C_VEL | C_SPR | components);
    if (e == ENTITY_NONE)
        return MAX_ENT;
    u32 i = entity_index(e);
    pos_x[i] = x - FX(hitbox) / 2;
    pos_y[i] = y - FX(hitbox) / 2;
    vel_x[i] = vx;
    vel_y[i] = vy;
    spr_id[i] = sprite;
    body_w[i] = body_h[i] = (u8)hitbox;
    return i;
}

static FIXED center_x(u32 i) {
    return pos_x[i] + FX(body_w[i]) / 2;
}

static FIXED center_y(u32 i) {
    return pos_y[i] + FX(body_h[i]) / 2;
}

static void destroy_all(u32 components) {
    ECS_FOR_EACH(i, components) {
        entity_destroy(entity_at(i));
    }
}

// A burst flying out from (x, y) in random directions: `count` pieces, the
// first `chips` of them chips of rock (slower), the rest sparks. Both are
// C_SPARK entities: they only differ in their sprite.
static void spawn_sparks(FIXED x, FIXED y, int count, int chips) {
    for (int k = 0; k < count; k++) {
        bool chip = k < chips;
        u16 dir = (u16)random_u32();
        FIXED speed =
            chip ? FX_ONE / 4 + random_range(0, FX_ONE) : FX_ONE / 2 + random_range(0, FX(2));
        u32 i = spawn(C_SPARK, chip ? SPR_DEBRIS : SPR_SPARK, 2, x, y, fx_mul(fx_cos(dir), speed),
                      fx_mul(fx_sin(dir), speed));
        if (i < MAX_ENT) {
            life[i] = (u8)random_range(SPARK_LIFE_MIN, SPARK_LIFE_MAX);
            spr_frame[i] = chip ? (u8)random_range(0, 1) : 0;
        }
    }
}

// --- Effects -----------------------------------------------------------------

// Blast rings and exhaust puffs are only for show: no hitbox, no physics, and
// nothing to keep track of. So they are not entities, which stay free for the
// game (see MAX_WAVE_ROCKS), but a small array of their own, moved and drawn
// with sprite_draw() every frame.
typedef enum {
    EFFECT_NONE,
    EFFECT_RING,    // a rock's blast: SPR_RING growing, 3 frames per step
    EFFECT_RING_IN, // the ship arriving: SPR_RING shrinking
    EFFECT_BLAST,   // the ship's blast: SPR_RING at twice the size
    EFFECT_PUFF,    // exhaust: a spark cooling from yellow to red
} EffectKind;

typedef struct {
    FIXED x, y, vx, vy; // center, on the playfield, and speed
    u8 kind;            // EffectKind
    u8 age, length;     // frames shown so far, of length
} Effect;

#define MAX_EFFECTS 24
static Effect effects[MAX_EFFECTS] SERVAL_EWRAM_BSS; // in EWRAM: IWRAM is scarce

// Starts an effect, unless all MAX_EFFECTS are showing (then it is skipped:
// nobody misses one puff of smoke).
static void add_effect(EffectKind kind, FIXED x, FIXED y, FIXED vx, FIXED vy, int length) {
    for (int k = 0; k < MAX_EFFECTS; k++) {
        if (effects[k].kind == EFFECT_NONE) {
            effects[k] = (Effect){x, y, vx, vy, (u8)kind, 0, (u8)length};
            return;
        }
    }
}

static void update_effects(void) {
    for (int k = 0; k < MAX_EFFECTS; k++) {
        Effect* e = &effects[k];
        if (e->kind == EFFECT_NONE)
            continue;
        if (++e->age >= e->length) {
            e->kind = EFFECT_NONE;
            continue;
        }
        e->x += e->vx;
        e->y += e->vy;
    }
}

// Drawn after sys_render, so behind the rocks and the ship. sprite_draw()
// takes screen positions: the camera (shaking, see shake_screen) comes off.
static void draw_effects(void) {
    for (int k = 0; k < MAX_EFFECTS; k++) {
        const Effect* e = &effects[k];
        int x = fx_to_int(e->x) - camera_x(), y = fx_to_int(e->y) - camera_y();
        switch (e->kind) {
        case EFFECT_RING:
            sprite_draw(SPR_RING, e->age / 3, x, y, 0);
            break;
        case EFFECT_RING_IN:
            sprite_draw(SPR_RING, 4 - e->age / 3, x, y, 0);
            break;
        case EFFECT_BLAST:
            sprite_draw_ex(SPR_RING, e->age / 4, x, y, 0, FX(2), FX(2), 0);
            break;
        case EFFECT_PUFF: // spark frames 1-3: yellow, orange, red
            sprite_draw(SPR_SPARK, 1 + e->age * 3 / e->length, x - 1, y - 1, 0);
            break;
        }
    }
}

// Screen shake: the camera jumps around by a few pixels, less as it calms
// down. Without a map on the playfield the camera only moves what is drawn:
// the entities (sys_render draws them at their position minus the camera),
// the effects and, at half the distance, the starfield (its scroll_factor).
// The text layer stays put, so the score doesn't shake.
static int shake; // frames left

static void shake_screen(int frames) {
    shake = int_max(shake, frames);
}

static void update_camera(void) {
    int amount = (shake + 3) / 4; // pixels
    if (shake > 0)
        shake--;
    camera_set(random_range(-amount, amount), random_range(-amount, amount));
}

// Fades and flashes: the screen's brightness steps toward a target by one
// level a frame. A flash sets it high and lets it come back down to 0.
static int brightness, brightness_target;

static void flash_screen(int level) {
    brightness = level;
    brightness_target = 0;
}

static void update_brightness(void) {
    if (brightness < brightness_target)
        brightness++;
    else if (brightness > brightness_target)
        brightness--;
    screen_set_brightness(brightness);
}

// --- The sky -----------------------------------------------------------------

// Behind the stars: a backdrop color per scanline (raster_backdrop), from
// deep blue at the top through violet to a dark blue-green at the bottom. The
// table is made once at boot; the engine reads it every frame.
//
// The screen has 32 levels of red, green and blue: too few for a gradient
// this dark and gentle, which would show as bands. So the lines dither: each
// adds 0, 4, 2 or 6 (by turns) to the 8-bit color before COLOR_RGB drops its
// 3 low bits, and a color between two levels shows as lines of both, mixed in
// the right proportion.
static Color sky[SCREEN_H] SERVAL_EWRAM_BSS;

typedef struct {
    u8 r, g, b;
} Rgb;

static const Rgb sky_top = {6, 6, 26}, sky_middle = {28, 12, 50}, sky_bottom = {8, 28, 50};

static Color sky_line(const Rgb* from, const Rgb* to, int step, int steps, int dither) {
    return COLOR_RGB(from->r + (to->r - from->r) * step / steps + dither,
                     from->g + (to->g - from->g) * step / steps + dither,
                     from->b + (to->b - from->b) * step / steps + dither);
}

static void sky_begin(void) {
    static const u8 dither[4] = {0, 4, 2, 6};
    const int half = SCREEN_H / 2;
    for (int y = 0; y < SCREEN_H; y++) {
        sky[y] = y < half ? sky_line(&sky_top, &sky_middle, y, half, dither[y % 4])
                          : sky_line(&sky_middle, &sky_bottom, y - half, half, dither[y % 4]);
    }
    raster_backdrop(sky);
    tileset_load(&star_tileset);
    map_load(&star_layer);
}

// The starfield drifts slowly (map_set_scroll, counting on: the layer wraps),
// and three of its colors swell and fade out of step, so stars twinkle.
static void update_sky(void) {
    u32 t = frame_count();
    map_set_scroll(3, (int)(t / 6), (int)(t / 14));
    Color colors[3];
    for (int k = 0; k < 3; k++) {
        FIXED swell = fx_sin((u16)(t * (300 + 110 * k) + k * ANGLE_DEG(120))); // -1 to 1
        u32 amount = (u32)(160 + fx_mul(swell, 96));                           // 64 to 256
        colors[k] = color_mix(star_palette[1], star_palette[STAR_TWINKLE_COLOR + k], amount);
    }
    tileset_set_colors(STAR_TWINKLE_COLOR, colors, 3);
}

// --- Ship --------------------------------------------------------------------

static void spawn_ship(void) {
    u32 i = spawn(C_BODY, SPR_SHIP, SHIP_HITBOX, FX(SCREEN_W / 2), FX(SCREEN_H / 2), 0, 0);
    if (i == MAX_ENT)
        return; // no free entity (see MAX_WAVE_ROCKS): tried again next frame
    ship = entity_at(i);
    ship_angle = ANGLE_DEG(-90); // facing up
    ship_timer = INVULNERABLE_FRAMES;
    add_effect(EFFECT_RING_IN, FX(SCREEN_W / 2), FX(SCREEN_H / 2), 0, 0, 15);
}

static void destroy_ship(void) {
    u32 i = entity_index(ship);
    spawn_sparks(center_x(i), center_y(i), 16, 0);
    add_effect(EFFECT_BLAST, center_x(i), center_y(i), 0, 0, 20);
    flash_screen(8);
    shake_screen(20);
    entity_destroy(ship);
    ship = ENTITY_NONE;
    psg_play(SND_SHIP_EXPLODE);
    ships--;
    ship_timer = RESPAWN_DELAY;
}

static void fire(u32 i) {
    if (ecs_count(C_BULLET) >= MAX_BULLETS)
        return;
    FIXED dx = fx_cos(ship_angle), dy = fx_sin(ship_angle);
    // From the nose, at bullet speed plus the ship's own velocity.
    u32 b = spawn(C_BODY | C_BULLET, SPR_BULLET, 2, center_x(i) + dx * 7, center_y(i) + dy * 7,
                  vel_x[i] + fx_mul(dx, BULLET_SPEED), vel_y[i] + fx_mul(dy, BULLET_SPEED));
    if (b < MAX_ENT) {
        life[b] = BULLET_LIFE;
        psg_play(SND_FIRE);
    }
}

// Drag: the ship loses 1/64 of its speed each frame (at least 1/256 pixel per
// frame), the same in every direction, so it coasts to a stop.
static FIXED drag(FIXED v) {
    FIXED loss = int_max(int_abs(v) / 64, 1);
    if (int_abs(v) <= loss)
        return 0;
    return v > 0 ? v - loss : v + loss;
}

static void control_ship(void) {
    if (!entity_alive(ship))
        return;
    u32 i = entity_index(ship);
    if (button_down(BUTTON_LEFT))
        ship_angle = (u16)(ship_angle - TURN_SPEED);
    if (button_down(BUTTON_RIGHT))
        ship_angle = (u16)(ship_angle + TURN_SPEED);
    spr_angle[i] = ship_angle; // the art faces right, which is angle 0

    bool thrusting = button_down(BUTTON_UP);
    // Thrusting, the flame flickers: frames 1 and 2 by turns.
    spr_frame[i] = thrusting ? (u8)(1 + frame_count() / 2 % 2) : 0;
    if (thrusting) {
        FIXED dx = fx_cos(ship_angle), dy = fx_sin(ship_angle);
        vel_x[i] += fx_mul(dx, THRUST);
        vel_y[i] += fx_mul(dy, THRUST);
        // Exhaust: a puff every other frame from the nozzles, blown backward.
        if (frame_count() % 2 == 0) {
            FIXED spread = random_range(-FX_ONE / 4, FX_ONE / 4);
            add_effect(EFFECT_PUFF, center_x(i) - dx * 7, center_y(i) - dy * 7,
                       vel_x[i] - dx + fx_mul(dy, spread), vel_y[i] - dy - fx_mul(dx, spread), 12);
        }
        if (--rumble_timer <= 0) {
            psg_play(SND_THRUST); // a low rumble, renewed while the button is held
            rumble_timer = 8;
        }
    }
    vel_x[i] = drag(vel_x[i]);
    vel_y[i] = drag(vel_y[i]);
    vel_x[i] = int_clamp(vel_x[i], -MAX_SPEED, MAX_SPEED);
    vel_y[i] = int_clamp(vel_y[i], -MAX_SPEED, MAX_SPEED);

    if (fire_cooldown > 0)
        fire_cooldown--;
    if (button_down(BUTTON_A) && fire_cooldown == 0) {
        fire(i);
        fire_cooldown = FIRE_COOLDOWN;
    }

    // Blink while invulnerable: hidden on alternate 4-frame stretches.
    if (ship_timer > 0)
        ship_timer--;
    bool hidden = ship_timer > 0 && (ship_timer / 4) % 2 == 1;
    spr_flags[i] = hidden ? SPRITE_HIDDEN : 0;
}

// --- Rocks -------------------------------------------------------------------

// A rock of the given size (0 large to 2 small) at (x, y), drifting in a
// random direction, faster the smaller it is.
static void spawn_rock(int size, FIXED x, FIXED y) {
    u16 dir = (u16)random_u32();
    FIXED speed = FX_ONE / 2 + random_range(0, FX_ONE / 2) + size * (FX_ONE / 2);
    u32 i = spawn(C_BODY | C_ROCK, rock_sprites[size], rock_hitbox[size], x, y,
                  fx_mul(fx_cos(dir), speed), fx_mul(fx_sin(dir), speed));
    if (i < MAX_ENT) {
        spr_frame[i] = (u8)random_range(0, 2); // one of three shapes
        rock_size[i] = (u8)size;
        rock_spin[i] = random_range(0, 1) ? 1 : -1;
    }
}

// True if (x, y) is too close to the ship for a new rock.
static bool near_ship(FIXED x, FIXED y) {
    if (!entity_alive(ship))
        return false;
    u32 i = entity_index(ship);
    return int_abs(x - center_x(i)) < FX(SPAWN_CLEARANCE) &&
           int_abs(y - center_y(i)) < FX(SPAWN_CLEARANCE);
}

// Shots it takes to clear a rock: a large one splits into two medium, each
// into two small: 1 + 2 + 4.
static const u8 rock_shots[3] = {7, 3, 1};

// A new wave of up to MAX_WAVE_ROCKS large rocks along the screen edges, away
// from the center where the ship (re)appears and from the ship itself.
static void spawn_wave(int count) {
    count = int_min(count, MAX_WAVE_ROCKS);
    for (int k = 0; k < count;) {
        bool side = random_range(0, 1);
        FIXED x = side ? FX(random_range(0, 1) * (SCREEN_W - 16)) : FX(random_range(0, SCREEN_W));
        FIXED y =
            side ? FX(random_range(16, SCREEN_H)) : FX(16 + random_range(0, 1) * (SCREEN_H - 32));
        if (near_ship(x, y))
            continue; // try another spot
        spawn_rock(0, x, y);
        k++;
    }
    wave_shots = count * rock_shots[0];
}

// Shots it would take to clear every rock left.
static int shots_left(void) {
    int n = 0;
    ECS_FOR_EACH(i, C_ROCK) {
        n += rock_shots[rock_size[i]];
    }
    return n;
}

static void add_score(int points) {
    score += points;
    if (score >= next_extra_ship) {
        ships++;
        next_extra_ship += EXTRA_SHIP_EVERY;
        psg_play(SND_EXTRA_SHIP);
    }
}

// A rock's blast: a ring of 5, 3 or 2 steps (large to small), and a shake for
// the bigger ones.
static const u8 blast_length[3] = {15, 9, 6};
static const u8 blast_shake[3] = {8, 4, 0};

// Splits (or, when small, removes) the rock in slot r, with sparks, chips of
// rock, a blast and sound.
static void break_rock(u32 r) {
    int size = rock_size[r];
    FIXED x = center_x(r), y = center_y(r);
    entity_destroy(entity_at(r));
    int pieces = 6 - 2 * size;
    spawn_sparks(x, y, pieces, pieces / 2);
    add_effect(EFFECT_RING, x, y, 0, 0, blast_length[size]);
    shake_screen(blast_shake[size]);
    psg_play(rock_sounds[size]);
    add_score(rock_points[size]);
    if (size < 2) {
        spawn_rock(size + 1, x, y);
        spawn_rock(size + 1, x, y);
    }
}

// --- Systems -----------------------------------------------------------------

// Spins rocks (sharing two rotation angles), ages bullets and sparks, and
// picks their frames: a shot's glow pulses; a spark cools as its life runs
// out, from white-hot (frame 0) to a red ember (frame 3).
static void update_objects(void) {
    spin = (u16)(spin + ANGLE_DEG(2));
    ECS_FOR_EACH(i, C_ROCK) {
        spr_angle[i] = rock_spin[i] > 0 ? spin : (u16)-spin;
    }
    ECS_FOR_EACH(i, C_BULLET) {
        if (--life[i] == 0)
            entity_destroy(entity_at(i));
        else
            spr_frame[i] = life[i] / 2 % 2;
    }
    ECS_FOR_EACH(i, C_SPARK) {
        if (--life[i] == 0)
            entity_destroy(entity_at(i));
        else if (spr_id[i] == SPR_SPARK)
            spr_frame[i] = life[i] > 15 ? 0 : life[i] > 10 ? 1 : life[i] > 5 ? 2 : 3;
    }
    update_effects();
}

// Bullets and the ship against the rocks. The rocks are listed once
// (ecs_gather) instead of an ECS_FOR_EACH per bullet, which would scan the
// whole pool each time. The list is a snapshot: a rock broken here leaves
// its slot in it (empty, or taken by one of its pieces), so each entry is
// checked with ent_has() first.
static void check_collisions(void) {
    static u8 rocks[MAX_ENT];
    u32 rock_total = ecs_gather(C_ROCK, rocks);
    ECS_FOR_EACH(b, C_BULLET) {
        for (u32 k = 0; k < rock_total; k++) {
            u32 r = rocks[k];
            if (ent_has(r, C_ROCK) && body_overlap(b, r)) {
                entity_destroy(entity_at(b));
                break_rock(r);
                break; // this bullet is gone
            }
        }
    }
    if (entity_alive(ship) && ship_timer == 0) {
        u32 s = entity_index(ship);
        for (u32 k = 0; k < rock_total; k++) {
            u32 r = rocks[k];
            if (ent_has(r, C_ROCK) && body_overlap(s, r)) {
                break_rock(r);
                destroy_ship();
                break;
            }
        }
    }
}

// The heartbeat: two alternating low notes, BEAT_SLOW frames apart when a
// wave starts, closing in on BEAT_FAST as the shots needed to clear it run out.
#define BEAT_SLOW 48
#define BEAT_FAST 14

static void update_heartbeat(void) {
    if (--beat_timer > 0)
        return;
    psg_play(beat_phase ? SND_BEAT_HIGH : SND_BEAT_LOW);
    beat_phase = !beat_phase;
    beat_timer = BEAT_FAST + (BEAT_SLOW - BEAT_FAST) * shots_left() / wave_shots;
}

// --- Screens -----------------------------------------------------------------

#define BANNER_ROW 6     // "WAVE n", above the ship
#define BANNER_FRAMES 90 // how long it shows
#define HUD_SHIP_ICONS 5 // ships left drawn as icons; more show as a number

// The top line: the score and the wave in text, the ships left as small ship
// sprites at the right end.
static void draw_hud(void) {
    text_print_line(0, 0, text_format(" SCORE %06d   WAVE %d", score, wave));
    int icons = ships > HUD_SHIP_ICONS ? 1 : ships;
    for (int k = 0; k < icons; k++)
        sprite_draw(SPR_LIFE, 0, SCREEN_W - 11 - 9 * k, 1, 0);
    if (ships > HUD_SHIP_ICONS)
        text_print(25, 0, text_format("x%d", ships));
}

static void show_wave_banner(void) {
    text_set_style(TEXT_ACCENT);
    text_print_centered(BANNER_ROW, text_format("WAVE %d", wave));
    text_set_style(TEXT_NORMAL);
    banner_timer = BANNER_FRAMES;
}

static void start_game(void) {
    random_seed(random_entropy()); // every game differs
    destroy_all(C_ROCK);
    destroy_all(C_BULLET);
    destroy_all(C_SPARK);
    score = 0;
    ships = START_SHIPS;
    wave = 1;
    next_extra_ship = EXTRA_SHIP_EVERY;
    beat_timer = 30;
    state = PLAYING;
    text_clear();
    flash_screen(6);
    psg_play(SND_START);
    spawn_ship();
    spawn_wave(FIRST_WAVE_ROCKS);
    show_wave_banner();
}

static void update_playing(void) {
    control_ship();
    sys_movement();
    sys_physics();
    update_objects();
    check_collisions();
    update_heartbeat();

    if (banner_timer > 0 && --banner_timer == 0)
        text_print_line(0, BANNER_ROW, "");
    if (!entity_alive(ship) && --ship_timer <= 0) {
        if (ships > 0) {
            spawn_ship();
        } else {
            state = GAME_OVER;
            page_timer = 0;
            rank = scores_rank(score);
            psg_play(SND_GAME_OVER);
            text_print_line(0, BANNER_ROW, "");
            text_set_style(TEXT_ACCENT);
            text_print_centered(9, "GAME OVER");
            text_set_style(TEXT_NORMAL);
            if (rank < 0)
                text_print_centered(11, "PRESS START");
        }
    }
    if (ecs_count(C_ROCK) == 0) {
        wave++;
        spawn_wave(FIRST_WAVE_ROCKS - 1 + wave);
        show_wave_banner();
    }
}

// --- Attract mode, game over and high scores --------------------------------

// The title: big letters (SPR_LETTER, one frame per letter of LOGO_GLYPHS),
// each bobbing on its own beat of a sine wave, with a glint running along the
// word now and then: one letter at a time drawn in the PAL_GLINT palette
// (SPRITE_PALETTE), the same art lit up.
#define LOGO_TEXT "ASTEROIDS"
#define LOGO_SPACING 17 // pixels from letter to letter
#define LOGO_Y 18

static int glyph_frame(char c) {
    for (int k = 0; LOGO_GLYPHS[k]; k++) {
        if (LOGO_GLYPHS[k] == c)
            return k;
    }
    return 0;
}

static void draw_logo(void) {
    int count = (int)sizeof LOGO_TEXT - 1;
    int left = (SCREEN_W - count * LOGO_SPACING) / 2;
    u32 t = frame_count();
    int glint = (int)(t % 150) / 4 - 2; // the letter lit up: none for most of the time
    for (int k = 0; k < count; k++) {
        int bob = fx_to_int(2 * fx_sin((u16)(t * ANGLE_DEG(3) + k * ANGLE_DEG(40))));
        sprite_draw(SPR_LETTER, (u8)glyph_frame(LOGO_TEXT[k]), left + k * LOGO_SPACING,
                    LOGO_Y + bob, k == glint ? SPRITE_PALETTE(PAL_GLINT) : 0);
    }
}

#define PRESS_START_ROW_TITLE 9
#define PRESS_START_ROW_SCORES 18
#define PAGE_FADE 16 // frames of fading out at the end of a page, and in at the start

// Attract mode: the title or the high-score table, shown for some frames.
static void show_page(Page p, int frames) {
    state = TITLE;
    page = p;
    page_timer = frames;
    brightness_target = 0; // fading in, if the last page faded out
    text_clear();
    if (p == PAGE_TITLE) {
        text_print_centered(PRESS_START_ROW_TITLE, "PRESS START");
        text_set_style(TEXT_DIM);
        text_print_centered(13, "LEFT/RIGHT:TURN  UP:THRUST");
        text_print_centered(15, "A:FIRE  START:PAUSE");
        text_set_style(TEXT_NORMAL);
    } else {
        scores_draw(highlight);
        text_print_centered(PRESS_START_ROW_SCORES, "PRESS START");
    }
}

static void update_title(bool start) {
    if (start) {
        start_game();
        return;
    }
    // Hidden, for demos: hold L and R and press SELECT to erase the table.
    if (button_down(BUTTON_L) && button_down(BUTTON_R) && button_pressed(BUTTON_SELECT)) {
        scores_reset();
        highlight = -1;
        show_page(PAGE_SCORES, PAGE_FRAMES);
        text_print_centered(16, "HIGH SCORES RESET");
        psg_play(SND_RESET);
        return;
    }
    // "PRESS START" blinks: on for 40 frames, off for 20.
    if (page_timer % 60 == 0 || page_timer % 60 == 20) {
        int row = page == PAGE_TITLE ? PRESS_START_ROW_TITLE : PRESS_START_ROW_SCORES;
        text_print_centered(row, page_timer % 60 == 0 ? "" : "PRESS START");
    }
    // Fading out to the next page: black (16 steps down) as the page ends.
    if (page_timer == PAGE_FADE + 1)
        brightness_target = SCREEN_BRIGHTNESS_MIN;
    if (--page_timer == 0) {
        highlight = -1; // the newest entry is marked only the first time
        show_page(page == PAGE_TITLE ? PAGE_SCORES : PAGE_TITLE, PAGE_FRAMES);
    }
    if (page == PAGE_TITLE)
        draw_logo();
}

// After "GAME OVER": the initials entry screen if the score made the table
// (START gets there sooner), otherwise START plays again, and after a while
// the attract mode comes back.
static void update_game_over(bool start) {
    page_timer++;
    if (rank >= 0) {
        if (start || page_timer >= GAME_OVER_FRAMES) {
            state = NEW_HIGH_SCORE;
            entry_begin(score);
            psg_play(SND_HIGH_SCORE);
        }
    } else if (start) {
        start_game();
    } else if (page_timer >= GAME_OVER_IDLE) {
        show_page(PAGE_SCORES, PAGE_FRAMES);
    }
}

static void update_new_high_score(void) {
    switch (entry_update()) {
    case ENTRY_LETTER:
        psg_play(SND_LETTER);
        break;
    case ENTRY_MOVE:
        psg_play(SND_NEXT_LETTER);
        break;
    case ENTRY_DONE: // saved: show the table with the new entry marked
        psg_play(SND_CONFIRM);
        highlight = rank;
        show_page(PAGE_SCORES, NEW_ENTRY_FRAMES);
        break;
    case ENTRY_NONE:
        break;
    }
}

// --- Main --------------------------------------------------------------------

int main(void) {
    serval_init();
    serval_splash();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&asteroids_group);
    psg_table_set(sound_table, SOUND_COUNT);
    physics_set_wrap(true, true);
    sky_begin();

    // Text: pale blue-white with a dark shadow, readable over stars and
    // rocks; TEXT_ACCENT (gold) for headings, TEXT_DIM for instructions.
    text_set_color(COLOR_RGB(232, 238, 255), COLOR_RGB(12, 8, 32));
    text_set_shadow(true);
    text_set_style_color(TEXT_ACCENT, COLOR_RGB(255, 200, 80), COLOR_RGB(64, 20, 32));
    text_set_style_color(TEXT_DIM, COLOR_RGB(140, 160, 214), COLOR_RGB(12, 8, 32));

    scores_load();
    rank = highlight = -1;
    spawn_wave(FIRST_WAVE_ROCKS); // drifting behind the title
    show_page(PAGE_TITLE, PAGE_FRAMES);
    brightness = SCREEN_BRIGHTNESS_MIN; // fading in from the splash's black

    for (;;) {
        frame_begin();
        update_brightness();
        bool start = button_pressed(BUTTON_START);
        switch (state) {
        case TITLE:
        case GAME_OVER:
        case NEW_HIGH_SCORE:
            // The rocks keep drifting and spinning behind these screens.
            sys_movement();
            sys_physics();
            update_objects();
            if (state == TITLE)
                update_title(start);
            else if (state == GAME_OVER)
                update_game_over(start);
            else
                update_new_high_score();
            break;
        case PLAYING:
            if (start) {
                state = PAUSED;
                brightness_target = -6; // dimmed while paused
                psg_play(SND_PAUSE);
                text_print_centered(9, "PAUSED");
            } else {
                update_playing();
            }
            break;
        case PAUSED:
            if (start) {
                state = PLAYING;
                brightness_target = 0;
                psg_play(SND_PAUSE);
                text_print_line(0, 9, "");
            }
            break;
        }
        if (state == PLAYING || state == PAUSED)
            draw_hud();
        if (state != PAUSED)
            update_camera();
        update_sky();
        // Sprites drawn first are in front: the title and the HUD's ships
        // (above), the entities, then the effects behind them.
        sys_render();
        draw_effects();
        frame_end();
    }
}
