# Core API

The lowest engine layer is a raylib-style flat C API over libtonc. It is also the abstraction boundary for future platforms: each call means the same thing on every target (see [platforms.md](platforms.md)).

```c
void serval_init(void);               // once at startup: interrupts, display, OAM, ECS
void serval_splash(void);             // optional: the "Made with Serval Engine" splash

void frame_begin(void);
void frame_end(void);                 // VBlank sync + flush shadow OAM/palettes/queues

u32  frame_cpu_cycles(void);           // previous frame's work, in CPU cycles
u32  frame_cpu_permille(void);         // the same, in thousandths of the frame budget
u32  frame_budget_cycles(void);        // 280,896 per frame at 60 Hz

bool button_down(u16 buttons);        // BUTTON_A, BUTTON_LEFT, ... (OR-able)
bool button_pressed(u16 buttons);

int  screen_width(void);              // also SCREEN_W / SCREEN_H constants
int  screen_height(void);
void screen_set_backdrop(Color color); // Color from COLOR_RGB(r, g, b), 0-255 components

void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags);
void sprite_draw_rotated(u16 sprite_id, u8 frame, int x, int y, u16 angle, u16 flags);
```

Headers: `serval/serval.h` includes everything. All of the above is implemented; sprite loading is described in [sprites.md](sprites.md#api).

Other modules, all in `include/serval/`:

| Header | Provides |
| --- | --- |
| `fixed.h` | 24.8 fixed point: `FX(n)`, `fx_to_int()`, `FX_ONE` |
| `audio.h` | PSG sound effects on the tone generators: `psg_table_set()`, `psg_play()`, `psg_stop_all()`; `PsgSound` (frequency, frames, duty, volume, fade, pitch slide, optional melody) |
| `physics.h` | Bouncing bodies (`C_BODY`): `physics_set_gravity()`, `physics_set_bounds()`, `physics_set_open_edges()`, `physics_set_wrap()`, `sys_physics()`; per-entity size, bounce and friction. For balls, particles and debris, not platformer characters. `body_overlap(a, b)` tests two bodies' rectangles; a body without `C_VEL` is a static collider |
| `math.h` | `int_min`, `int_max`, `int_abs`, `int_clamp`, `fx_mul`, `fx_div` (prefixed to avoid libtonc's `clamp`/`min`/`max`); angles as u16 turns (`ANGLE_DEG(d)`, clockwise on screen) with `fx_sin`/`fx_cos` from a 1024-step table |
| `random.h` | Deterministic xorshift32: `random_seed()`, `random_u32()`, `random_range(lo, hi)`; `random_entropy()` for a seed that varies between runs (e.g. taken when the player presses START) |
| `text.h` | HUD/debug text on BG0 (8x8 font, 30x20 cells): `text_print()`, `text_print_line()` (also blanks the rest of the row), `text_clear()`, `text_format()` (printf-style without a C library; `%d %u %x` take any 32-bit integer; not compiler-checked, but a NULL `%s` prints `(null)` and debug builds catch a `%s` that isn't a pointer) |
| `debug.h` | `debug_log()` (mGBA debug log), `debug_warning_count()`, and `debug_exit()` (ends a headless `mgba-rom-test` run with an exit code) |

## Debug builds report misuse

In Debug and RelWithDebInfo builds (`SERVAL_DEBUG`), the engine reports API misuse as warnings in the emulator's debug log (mGBA: *Tools > View Logs*), prefixed `serval:`, instead of failing silently. For example:

```
serval: sprite_draw: sprite 3 is not loaded; load a sprite group containing it
serval: sprite_group_load: needs 1025 tiles, but only 1024 of 1024 are free
serval: entity_create: all 128 entities are in use; returning ENTITY_NONE
```

Each problem is reported once rather than every frame. Release builds compile the checks out entirely, so they cost nothing; the API still fails safely (nothing is drawn, `false` or `ENTITY_NONE` is returned). Games can use `SERVAL_DEBUG` for their own debug code too.

**Hardware the engine configures:** `serval_init()` sets `WAITCNT` to the standard 3/1 ROM wait states with prefetch (power-on default is 4/2 without prefetch), which speeds up all code and data in ROM, including the game's; sets display mode 0 with sprites on and 1D sprite tile mapping; enables the VBlank interrupt; and turns sound on (tone generators at full volume on both speakers).

**Hardware the engine reserves:** OAM and the 32 sprite rotation matrices (rebuilt every frame from draw calls), OBJ VRAM and OBJ palettes (sprite groups), the PSG sound channels 1, 2 and 4 (`audio.h`), timers 2 and 3 (the cycle counter behind `frame_cpu_cycles()`), BG0 with charblock 0 and screenblock 31 once text is used, and BG palette bank 15 (plus bank 14 and the blend registers during `serval_splash()`).

## Splash screen

`serval_splash()` shows "made with" (grey) and "Serval Engine" (white) on black and returns about three seconds later: a 500 ms fade-in, a 500 ms hold, a coin-like jingle, a 1.5 s hold and a 500 ms fade-out (hardware fade-to-black on the text layer). Any button after the fade-in skips the rest. It borrows the backdrop, the text layer, BG palette banks 14-15 and PSG square 1, and puts them back; the screen is left black. A logo is planned. Pong and Asteroids call it.

## Dependencies stay behind the API

Games never need to include or call a third-party library (libtonc, Maxmod) directly; everything they need is Serval API. They may still use libtonc, which stays on the include path:

- Public headers include no third-party headers. The host build compiles them, and the examples, with no libtonc available, so CI fails if one sneaks in.
- Public names never collide with libtonc's (hence `BUTTON_*` rather than libtonc's `KEY_*` macros). `tests/rom/compat_*.c` include both in either order and must compile warning-free.
- GBA-only escape hatches live in `serval/gba.h` with a `gba_` prefix, e.g. `gba_oam_submit()` for raw OAM entries, so the portable API stays the same on every target.

New engine features are driven by the examples: when an example needs something, it gets a Serval API rather than a direct library call.

Audio calls are listed in [audio.md](audio.md).

## Sprite submission model

Sprites are retained by the hardware (the PPU reads OAM every scanline), but the API rebuilds a shadow OAM each frame from draw calls, then DMAs it to OAM in VBlank.

- Priority among same-priority sprites is OAM order, so depth sorting is just sorting the draw list.
- Anything not submitted disappears, so there are no stale sprites.
- Flicker multiplexing and metasprites need no slot management.
- Cost: rebuilding 1 KB of OAM in ARM-mode IWRAM code is a few thousand cycles out of about 280,000 per frame.

## Persistently managed resources

Immediate-mode submission applies to OAM only. These scarce resources are managed persistently:

- Tile VRAM ([sprites.md](sprites.md), [tilemaps.md](tilemaps.md))
- Palette banks ([sprites.md](sprites.md#palettes))
- Background data ([tilemaps.md](tilemaps.md))

The 32 sprite rotation matrices are not persistent: like OAM, they are rebuilt each frame from draw calls, shared by sprites with the same angle and flips ([sprites.md](sprites.md#api)).
