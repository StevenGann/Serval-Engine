# Core API

The lowest engine layer is a raylib-style flat C API over libtonc. It is also the abstraction boundary for future platforms: each call means the same thing on every target (see [platforms.md](platforms.md)).

```c
void serval_init(void);               // once at startup: interrupts, OAM, ECS

void frame_begin(void);
void frame_end(void);                 // VBlank sync + flush shadow OAM/palettes/queues

bool key_down(u16 key);
bool key_pressed(u16 key);

void sprite_draw(u16 sprite_id, u8 frame, int x, int y, u16 flags);
```

**Implemented:** `serval_init`, `frame_begin`, `frame_end`, `key_down`, `key_pressed` (`include/serval/core.h`). `sprite_draw` waits for the sprite asset format ([sprites.md](sprites.md)); until then, C code can submit raw OAM entries with the GBA-only escape hatch `gba_oam_submit()` (`include/serval/gba.h`). GBA-only API lives in `gba.h` with a `gba_` prefix, so the portable API stays the same on every target.

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
