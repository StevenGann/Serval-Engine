# Core API

The lowest engine layer is a raylib-style flat C API over libtonc. It is also the abstraction boundary for future platforms: each call means the same thing on every target (see [platforms.md](platforms.md)).

**Status:** implemented (input, frame loop, sprites, map backgrounds and the camera, PSG sound effects and music, brightness fades, text, math, random, paths, save data, debug). Maxmod music and sampled sound are planned ([audio.md](audio.md)); so are tileset groups ([tilemaps.md](tilemaps.md)), palette management ([sprites.md](sprites.md#palettes)) and alpha blending ([runtime-systems.md](runtime-systems.md#special-effects)). The full function list with units, limits and misuse behaviour is in [api-reference.md](api-reference.md); this page covers the design and the hardware the engine owns.

```c
void serval_init(void);               // once at startup: wait states, interrupts, display, ECS, sound
void serval_splash(void);             // optional: the "Made with Serval Engine" splash

void frame_begin(void);               // poll buttons, empty the sprite draw list
void frame_end(void);                 // VBlank sync, copy shadow OAM, stream maps, step sound and music

u32  frame_count(void);               // frames since serval_init()
u32  frame_cpu_cycles(void);          // previous frame's work, in CPU cycles
u32  frame_cpu_permille(void);        // the same, in thousandths of the frame budget
u32  frame_budget_cycles(void);       // 280,896 per frame at 60 Hz

bool button_down(u16 buttons);        // BUTTON_A, BUTTON_LEFT, ... (OR-able: any of them)
bool button_pressed(u16 buttons);
bool button_repeat(u16 buttons);      // menus: on the press, then after 20 frames every 4 while held

int  screen_width(void);              // also SCREEN_W / SCREEN_H constants
int  screen_height(void);
void screen_set_backdrop(Color color); // Color from COLOR_RGB(r, g, b), 0-255 components

void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags);
void sprite_draw_rotated(u16 sprite_id, u8 frame, int x, int y, u16 angle, u16 flags);
void sprite_draw_ex(u16 sprite_id, u8 frame, int x, int y, u16 angle, FIXED scale_x, FIXED scale_y, u16 flags);
```

`serval/serval.h` includes every public header except `gba.h`. Sprite loading is described in [sprites.md](sprites.md#api).

Modules, all in `include/serval/` (details in [api-reference.md](api-reference.md)):

| Header | Provides |
| --- | --- |
| `core.h` | Init, splash, frame loop, frame count, CPU timing, buttons, held-button repeat (`button_repeat`, `button_repeat_set`) |
| `screen.h` | Screen size, `Color`, `COLOR_RGB`, backdrop color, brightness fades (`screen_set_brightness`) |
| `sprites.h` | Sprite assets, groups, drawing, rotation, layers, per-draw palettes, animation data ([sprites.md](sprites.md)) |
| `map.h` | Tilesets, metatile map layers on BG1-BG3, scroll offsets, the camera, runtime cell changes, map collision and map bodies (`C_MAPBODY`, `sys_map_movement`) ([tilemaps.md](tilemaps.md)) |
| `ecs.h` | Entities, engine components, `ECS_FOR_EACH`, `ecs_count`, `ecs_gather`, `sys_movement`, `sys_animate`, `sys_render`, `sys_render_by_depth` ([ecs.md](ecs.md)) |
| `path.h` | Movement patterns as `PathStep` tables: `path_start()`, `sys_path()` ([runtime-systems.md](runtime-systems.md#paths)) |
| `physics.h` | Bouncing bodies (`C_BODY`): gravity (global and per body), maximum fall speed, bounds, open edges, wrap-around, contacts, `sys_physics()`, `body_overlap()`, `body_hit_side()`. For balls, particles and debris; platformer characters are map bodies |
| `audio.h` | PSG sound effects (`psg_table_set()`, `psg_play()`, `psg_stop_all()`) and PSG music (`psg_music_play()`, pause, tempo, volume) on the tone generators ([audio.md](audio.md)) |
| `save.h` | Save slots with checksums and versions on SRAM, Flash or EEPROM (`localStorage` on the web) ([runtime-systems.md](runtime-systems.md#save-data)) |
| `text.h` | HUD/debug text on BG0 (8x8 font, 30x20 cells, up to four color styles such as a highlight, centering within columns) and `text_format()` (printf-style without a C library) |
| `fixed.h` | 24.8 fixed point: `FX(n)`, `fx_to_int()`, `FX_ONE` |
| `math.h` | `int_min`, `int_max`, `int_abs`, `int_clamp`, `fx_mul`, `fx_div` (prefixed to avoid libtonc's `clamp`/`min`/`max`); u16 angles (`ANGLE_DEG(d)`, clockwise on screen) with `fx_sin`/`fx_cos`, `angle_of` (atan2) and `fx_length` |
| `random.h` | Deterministic xorshift32: `random_seed()`, `random_u32()`, `random_range(lo, hi)`, and `random_entropy()`, a seed from the player's input timing (the same input gives the same value on every platform) |
| `debug.h` | `debug_log()` (mGBA debug log), `debug_warning_count()`, `debug_exit()` |
| `platform.h` | Integer types, `FIXED`, `NULL`, IWRAM/EWRAM placement macros |
| `gba.h` | GBA-only escape hatches (`gba_oam_submit()`); not included by `serval.h` |

## Debug builds report misuse

In Debug and RelWithDebInfo builds (`SERVAL_DEBUG`), the engine reports API misuse as warnings in the emulator's debug log (mGBA: *Tools > View Logs*), prefixed `serval:`, instead of failing silently. For example:

```
serval: sprite_draw: sprite 3 is not loaded; load a sprite group containing it
serval: sprite_group_load: needs 1025 tiles, but only 1024 of 1024 are free
serval: entity_create: all 128 entities are in use; returning ENTITY_NONE
```

Each problem is reported once rather than every frame. Release builds compile the checks out entirely, so they cost nothing; the API still fails safely (nothing is drawn, `false` or `ENTITY_NONE` is returned). Games can use `SERVAL_DEBUG` for their own debug code too. [api-reference.md](api-reference.md) marks which calls warn.

**Hardware the engine configures:** `serval_init()` sets `WAITCNT` to the standard 3/1 ROM wait states with prefetch (power-on default is 4/2 without prefetch), which speeds up all code and data in ROM, including the game's; sets display mode 0 with sprites on and 1D sprite tile mapping; enables the VBlank interrupt (libtonc's interrupt dispatcher, no handlers); and turns sound on (tone generators at full volume on both speakers).

**Hardware the engine reserves:** OAM and the 32 sprite rotation matrices (rebuilt every frame from draw calls), OBJ VRAM and OBJ palettes (sprite groups), the PSG sound channels 1, 2 and 4 (`audio.h`), timers 2 and 3 (the cycle counter behind `frame_cpu_cycles()`; `random_entropy()` doesn't use it, so games stay deterministic), BG palette entry 0 (the backdrop), BG0 with charblock 0 and screenblock 31 once text is used, and BG palette bank 15 (colors 1-8: a text and a shadow color for each of the four text styles; plus bank 14, banks 10-13 and the blend registers during `serval_splash()`, which also leaves its logo's tiles in charblock 1). Once a map layer is loaded: BG1-BG3 (control and scroll registers, DISPCNT enable bits), charblocks 1-2 (tileset) and screenblocks 28-30, and BG palette banks 0-14 (colors 1-15) when a tileset is loaded ([tilemaps.md](tilemaps.md#vram-layout)).

## Splash screen

`serval_splash()` shows "made with" (grey, the text font) over the Serval Engine logo on a black backdrop and returns about three seconds later: a 500 ms fade-in, a 500 ms hold, a coin-like jingle, a 1.5 s hold and a 500 ms fade-out (hardware fade-to-black on the text layer, which the logo shares). Any button but L and R after the fade-in skips the rest.

The logo is drawn when the splash starts, from ASCII pictures and a few rules in `src/gba/splash_art.c`, into charblock 1 (BG0 keeps charblock 0 as its base; a 10-bit tile index reaches both; tiles with nothing drawn take no VRAM) and BG palette banks 10-13, and shown on the text layer's map. The design is chosen, and so is the head: a front-on serval with big green eyes, whisker dots and enormous ears (the left upright with a notch bitten from its edge, the right swivelled out as if listening) beside "SERVAL" over "ENGINE" in chunky gold and cream letters (mark and wordmark). **Until its final variation is picked** ([open-questions.md](open-questions.md)), four are built, one per bank, and R and L show the next and previous one (wrapping) from the first frame until the fade-out begins, instantly (the map is rewritten right after `frame_end()`, inside VBlank). A switch restarts the hold: the fade-out comes a full 2 s hold after the latest switch; the jingle plays once, at its usual time, and L and R do nothing during the fade-out. Nothing is saved; style 0 shows by default. Style 0 is the head as chosen; the others change one decision each, as small patches over the same drawing: 1 the expression, a knowing look (dark lids, both eyes glancing toward the name, the right corner of the mouth lifted); 2 the markings, a serval's (the swivelled ear's back black with a white bar across it, a short outer forehead stripe, spots down the temples and more on the cheeks, a darker nose); 3 the finish, a rendering pass (the outline a deep warm brown instead of black, light on the ear rims and the top of the forehead, a touch of shade inside the ears and under the chin, the letters' bottom edge a shade darker than their bevel). The timing and the buttons are the portable `src/core/splash_logic.c`, tested natively (`tests/splash_logic_tests.c`).

It borrows the backdrop, BG0 (control register and on/off state), the text layer (drawing without the text shadow), one color in BG palette bank 14 and colors 1-15 of banks 10-13, the blend registers (the game's `screen_set_brightness` level) and PSG square 1, and puts them back, so the screen then shows the game's backdrop (black by default). Not restored: text already on the layer (cleared), charblock 1 (the logo's tiles stay, as a game loads its tilesets after the splash) and, if the game hadn't used text yet, charblock 0 tiles 0-95 (the font) and BG0's scroll. Every example except `hello` and `bunnymark` calls it.

## Dependencies stay behind the API

Games never need to include or call a third-party library (libtonc, or Maxmod once audio uses it) directly; everything they need is Serval API. They may still use libtonc, which stays on the include path:

- Public headers include no third-party headers. The host build compiles them, and the examples, with no libtonc available, so CI fails if one sneaks in.
- Public names never collide with libtonc's (hence `BUTTON_*` rather than libtonc's `KEY_*` macros). `tests/rom/compat_*.c` include both in either order and must compile warning-free.
- GBA-only escape hatches live in `serval/gba.h` with a `gba_` prefix, e.g. `gba_oam_submit()` for raw OAM entries, so the portable API stays the same on every target.

New engine features are driven by the examples: when an example needs something, it gets a Serval API rather than a direct library call.

## Sprite submission model

Sprites are retained by the hardware (the PPU reads OAM every scanline), but the API rebuilds a shadow OAM each frame from draw calls, then copies it to OAM in VBlank (`frame_end()`).

- Priority among same-priority sprites is OAM order, so depth sorting is just sorting the draw list.
- Anything not submitted disappears, so there are no stale sprites.
- Flicker multiplexing and metasprites need no slot management.
- Cost: rebuilding 1 KB of OAM in ARM-mode IWRAM code is a few thousand cycles out of about 280,000 per frame.

## Persistently managed resources

Immediate-mode submission applies to OAM only. These scarce resources are managed persistently:

- Tile VRAM: sprite groups ([sprites.md](sprites.md)) and one tileset per room ([tilemaps.md](tilemaps.md#tilesets))
- Palette banks: bump-allocated per sprite group today; sharing and a shadow palette are planned ([sprites.md](sprites.md#palettes))
- Background maps: streamed around the camera from map layers in ROM; the camera is a position the game sets each frame, which `sys_render()` and `sys_render_by_depth()` subtract from entity positions, while `sprite_draw()` takes screen coordinates ([tilemaps.md](tilemaps.md#streaming), [runtime-systems.md](runtime-systems.md#camera))

The 32 sprite rotation matrices are not persistent: like OAM, they are rebuilt each frame from draw calls, shared by sprites with the same angle, flips and scales ([sprites.md](sprites.md#api)).
