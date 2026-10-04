# Core API

The lowest engine layer is a raylib-style flat C API over libtonc. It is also the abstraction boundary for future platforms: each call means the same thing on every target (see [platforms.md](platforms.md)).

```c
void serval_init(void);               // once at startup: interrupts, display, OAM, ECS

void frame_begin(void);
void frame_end(void);                 // VBlank sync + flush shadow OAM/palettes/queues

u32  frame_cpu_cycles(void);           // previous frame's work, in CPU cycles
u32  frame_budget_cycles(void);        // 280,896 per frame at 60 Hz

bool button_down(u16 buttons);        // BUTTON_A, BUTTON_LEFT, ... (OR-able)
bool button_pressed(u16 buttons);

int  screen_width(void);
int  screen_height(void);
void screen_set_backdrop(Color color); // Color from COLOR_RGB(r, g, b), 0-255 components

void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags);
```

Headers: `serval/serval.h` includes everything. All of the above is implemented; sprite loading is described in [sprites.md](sprites.md#api).

Other modules, all in `include/serval/`:

| Header | Provides |
| --- | --- |
| `fixed.h` | 24.8 fixed point: `FX(n)`, `fx_to_int()`, `FX_ONE` |
| `random.h` | Deterministic xorshift32: `random_seed()`, `random_u32()`, `random_range(lo, hi)` |
| `text.h` | HUD/debug text on BG0 (8x8 font, 30x20 cells): `text_print()`, `text_clear()`, `text_format()` (printf-style without a C library; `%d %u %x` take any 32-bit integer) |
| `debug.h` | `debug_log()` (mGBA debug log) and `debug_exit()` (ends a headless `mgba-rom-test` run with an exit code) |

**Hardware the engine reserves:** timers 2 and 3 (the cycle counter behind `frame_cpu_cycles()`), BG0 with charblock 0 and screenblock 31 once text is used, and BG palette bank 15.

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
- The 32 affine matrices, deduplicated per frame
- Background data ([tilemaps.md](tilemaps.md))
