# Pixel surface

**Status:** implemented (`include/serval/surface.h`, `src/gba/surface.c`; `surface_set_colors()` with the other palette writes in `src/gba/palette.c`), on the GBA and the web. Added after the 1.0 freeze as new API: seven functions under the `surface_` prefix, nothing existing changed ([api-freeze.md](api-freeze.md#after-the-freeze)). The [`raycaster`](../examples/raycaster/main.c) example is built on it.

The pixel surface is the engine's first step into 3D, and it is deliberately a small one. Tiles and sprites can't show a first-person view, filled polygons or a plasma: those need pixels the game sets one by one. The engine gives exactly that, **a picture of 240 x 160 pixels, one byte each, with two pages and a flip, and the hardware's stretch**; what is drawn into it, and how, is the game's own code. A raycaster casts its rays and textures its columns itself; a rail shooter would draw its ground plane and its ships; none of that is in the engine, so none of it constrains the next kind of game.

## What the engine provides

| Function | What it does |
| --- | --- |
| `surface_begin()` | Turns the surface on: the screen shows it instead of tiled backgrounds. Returns `false` (and changes nothing, *warns*) while a map layer is loaded |
| `surface_end()` | Turns it off: back to tiled backgrounds |
| `surface_pixels()` | The page to draw into: `SURFACE_W` x `SURFACE_H` (240 x 160) bytes, row after row; pixel (x, y) is byte `y * SURFACE_W + x`. `NULL` (*warns*) while the surface is off |
| `surface_flip()` | Shows the page being drawn at the next `frame_end()`, in VBlank, and makes the other the page to draw |
| `surface_clear(color)` | Fills the page being drawn with one color number |
| `surface_set_colors(index, colors, count)` | Sets some of the surface's 256 colors, at the next `frame_end()` |
| `surface_set_transform(x0, y0, xx, xy, yx, yy)` | How the screen shows the page: screen pixel (sx, sy) shows surface pixel (x0 + xx·sx + xy·sy, y0 + yx·sx + yy·sy). Scroll, zoom, rotate, or turn the picture on its side, at no cost in CPU time |

A pixel is a color number: 0 is transparent (the backdrop shows through) and 1-255 are the colors set with `surface_set_colors()`. The game draws into the page that is not on screen, over as many frames as the picture takes, then flips: a picture is never seen half drawn.

```c
if (surface_begin()) {
    surface_set_colors(0, palette, 256);
    // every frame, or every few:
    u32* page = (u32*)surface_pixels();
    ... // draw
    surface_flip();
}
```

## What it costs: text, maps and half the sprite tiles

While the surface is on it is the screen's only background.

- **No text layer.** `text.h`'s calls do nothing (*warns*, once). `surface_begin()` turns the layer off; the next text call after `surface_end()` sets it up again, fonts and colors included. Show text with sprites (the raycaster composes its menus' lines from its own fonts into sprite frames, a hardware sprite per 32 pixels, and its HUD's numbers from digit sprites), or draw it into the surface.
- **No map layers and no tileset.** `surface_begin()` refuses while a map layer is loaded (`map_unload()` them first), and `map_load()`, `tileset_load()` and `tileset_set_tiles()` refuse while the surface is on (*warns*). After `surface_end()`, load the tileset again before a map layer: its video memory held pixels.
- **Sprites keep 512 of their 1,024 tiles**, and `surface_begin()` and `surface_end()` unload every sprite group, as `sprite_groups_reset()` does: load the groups to show over the surface after `surface_begin()`. Everything else about sprites is unchanged (drawing, rotation, blending, the 128 hardware sprites).
- **No splash:** `serval_splash()` returns at once (*warns*) while the surface is on. It runs at boot, before any of this.
- **Unchanged:** the backdrop (color 0), brightness fades, alpha blending (the surface is `LAYER_PLAYFIELD`, so sprites can blend over it and it over the backdrop), `raster_backdrop()`, sound, save data, the VM.

A game can switch between the two worlds behind a fade (menus as text, then the surface to play), or keep the surface on throughout and draw its menus with sprites, as the raycaster does: its title and menus stand over its own view, running dimmed behind them. Both calls take effect at once, not at the next `frame_end()`.

**Why.** The surface is the hardware's display mode 4, whose two pages are the first 38,400 bytes of VRAM at `0x06000000` and at `0x0600A000`. That is all the VRAM the tiled backgrounds use (the text layer's font in charblock 0, the tileset in charblocks 1-2, the screenblocks at the top of charblock 3) and, for the second page, sprite tiles 0-431; and in bitmap modes the hardware shows only sprite tiles 512 and up, whatever is stored below. The engine doesn't save and restore any of it: turning the surface on is a decision about the whole screen.

## Writing pixels

**Write the page 2 or 4 pixels at a time** (`u16` or `u32` stores at even addresses), never single bytes. The GBA's video memory stores 16 bits at a time, and a one-byte store there sets the neighbouring byte to the same value. The web build's memory stores the single byte, so such a bug shows only on the GBA. Two ways to draw single pixels:

- read the pair, change one byte, write the pair back; or
- draw in a buffer of the game's own in RAM and copy it to the page in words. The raycaster does this a column at a time: 160 bytes in IWRAM, where byte stores are also faster than anywhere else, then 40 word stores.

Reading the page is fine at any size. The page pointer changes with each flip (at the `frame_end()` after `surface_flip()`, not at the call), so ask `surface_pixels()` every frame rather than keeping it. After a flip, the page to draw holds the picture from two flips ago.

`surface_clear()` costs about 25,400 CPU cycles (9% of a frame), so a game that draws every pixel anyway shouldn't call it. `surface_begin()` clears both pages and takes about 66,400 (a quarter of a frame): call it behind a fade.

## The transform

The surface is drawn by background 2, which in mode 4 is an affine background: the hardware walks the page along any straight line per scanline. `surface_set_transform()` sets that line. All six values are 24.8 fixed point (`FX()`); the default is the identity, `(0, 0, FX(1), 0, 0, FX(1))`. Screen pixels whose surface position falls outside the page show the backdrop.

```c
// The top-left 120 x 80 pixels, doubled to fill the screen: a quarter of the pixels to draw.
surface_set_transform(0, 0, FX(1) / 2, 0, 0, FX(1) / 2);

// Screen columns as surface rows, at half width: 120 columns of 160 pixels.
surface_set_transform(0, 0, 0, FX(1), FX(1) / 2, 0);

// Rotated by `a` about the screen's center, zoomed by `zoom`.
FIXED c = fx_mul(fx_cos(a), zoom), s = fx_mul(fx_sin(a), zoom);
surface_set_transform(FX(120) - 120 * c + 80 * s, FX(80) - 120 * s - 80 * c, c, -s, s, c);
```

The second is the raycaster's. A raycaster draws columns, and a column of a row-major picture is 160 bytes each 240 apart; turned on its side, a column of the view is 160 consecutive bytes, written as 40 words. Halving the width (each column shown two screen pixels wide) halves the work again. The hardware does both as it draws the screen.

The values are applied at the next `frame_end()`, in VBlank, together with a flip made in the same frame. The matrix terms must lie within -128 to just under 128 and `x0`, `y0` within -524,288 to just under 524,288, the registers' ranges; others are clamped (*warns*).

## Colors

The surface's 256 colors are the background palette (the same memory as a tileset's 16 banks). `surface_set_colors()` writes through the engine's shadow palette like `sprite_set_colors()` and `tileset_set_colors()`: the colors change at the next `frame_end()`, in VBlank, about 140 cycles per 16 colors written. Color 0 is the backdrop, as `screen_set_backdrop()` sets it. `surface_begin()` leaves in the palette whatever the tileset and the text layer had put there, so set the colors after it; the text layer writes its own (241-248) again when it is next set up.

With 256 colors and no blending inside the picture, shading is done in the palette. The raycaster spends them as 31 base colors at 7 brightness levels and one row of other colors (its violet wisps'), color number = level × 32 + base, so darkening a texel with distance is one OR; the levels fade toward a dark blue rather than black, and glowing colors (fire, gold, eyes) darken less, all decided when the table is made. Other tables of the same colors are swapped in whole: mixed toward red (`color_mix()`) for a moment when the player is hurt, and toward night blue behind the menus.

## Timing: pictures over several frames

`surface_flip()` only notes that a flip is wanted; `frame_end()` makes it in VBlank, in the step that writes the background registers ([frame-loop.md](frame-loop.md#vblank-flush)), with the transform. Without a flip `frame_end()` keeps showing the same page. So a picture may take any number of frames: draw part, `frame_end()`, draw the rest, flip.

The raycaster draws each picture over two frames (52 of its 120 columns, then 68 and the flip) and steps its world once per picture: 30 pictures a second, each frame within its time, sound and input still served 60 times a second. A picture that needs one frame and a half would otherwise show at 30 a second with the frame loop stalled half the time.

For scale, on the GBA (release build, measured with `frame_cpu_permille()`): the raycaster's two frames of a picture each take 55% to 80% of a frame's CPU time for 120 x 160 textured pixels with a few things in view, its drawing loops in IWRAM as ARM code. (An unoptimized Debug build of the same loops takes over 200%: it plays correctly, at about a third of the speed.) A full-screen, full-resolution picture drawn pixel by pixel in C is beyond one frame; the transform (fewer pixels, stretched) and several frames per picture are how a game fits.

The [`life`](../examples/life/main.c) example (Conway's Game of Life) uses the surface the other way round: its picture is its state. Its world of 120 x 80 cells is the page's top-left corner, a byte per cell whose value is at once the cell's state and its color number (empty, a fading trail, alive at an age), so the step reads the old generation and writes the new one into RAM and into the page in the same pass, in words, and the palette does the rest. The transform stretches the corner 2x to fill the screen, or 4x about the cursor with an eased zoom, at no CPU cost; its text is sprites. A generation costs it 160,000 cycles for a sparse world and 330,000 for a fresh random soup (its step in IWRAM as ARM code), so, like the raycaster, it splits a generation over two frames unless the last one says the next will fit in one.

## Hardware (GBA)

- **`DISPCNT`:** mode 4, background 2 on, backgrounds 0, 1 and 3 off, and bit 4 selecting the page; written at `surface_begin()` and at every `frame_end()` while the surface is on (so the map module's last write for a layer unloaded just before can't hide the surface). The other bits (sprites, their mapping, forced blank, windows, "H-Blank interval free") are kept. `surface_end()` sets mode 0 with every background off.
- **`BG2CNT`:** priority 2, the playfield's ([tilemaps.md](tilemaps.md)). **`BG2PA`-`BG2PD`, `BG2X`, `BG2Y`:** the transform, written in VBlank when it changed (they are write-only; the engine keeps what it wrote).
- **VRAM:** `0x06000000`-`0x060095FF` and `0x0600A000`-`0x060135FF`.
- **Palette RAM:** the background palette, through the shadow palette.

The module is linked only into games that call it: `frame_end()` reaches the flip through a hook, and the modules that must refuse their calls test one flag (`serval_surface_on`, in `core.c`). A game without a surface pays for those tests only: bunnymark, which prints three text lines a frame, went from 73,562 to 73,604 cycles a frame ([development.md](development.md#benchmark)).

## Web

Nothing is ported: `src/gba/surface.c` compiles unchanged, and the web renderer (`src/web/ppu.c`) already drew mode 4 with background 2's transform and page select, from the same registers and VRAM ([platforms.md](platforms.md#web)). The pictures match the GBA's pixel for pixel. One difference a game can notice: single-byte stores to the page work on the web and smear on the GBA ([above](#writing-pixels)).

## What is not in the engine

Each of these is the game's, on purpose; each could be added later, as new functions, without changing the seven above.

- **Drawing.** No lines, rectangles, triangles, blits or text on the surface. The right inner loop depends entirely on the game (the raycaster's is a few dozen lines and nothing like a polygon filler's), and the surface is too slow to draw through a general one.
- **Other bitmap modes.** Mode 3 (240 x 160, 15-bit color, one page) and mode 5 (160 x 128, 15-bit color, two pages) are not offered. Mode 4 has two pages at full size and a palette, which is what shading and flashes want; the transform gives the lower resolutions.
- **Keeping the text layer.** In bitmap modes the hardware has no tiled background to put it on.
- **Frame pacing.** Nothing schedules a picture over several frames or tells the game how long its last one took, beyond `frame_cpu_cycles()`; the raycaster splits its columns by hand.
- **A fast division.** The ARM7TDMI has none and a 3D game wants one per column or per polygon edge; the raycaster brings its own table of reciprocals. A good candidate for `math.h` once a second 3D example shows what shape it should have ([examples-roadmap.md](examples-roadmap.md#what-raycaster-exposed)).

## Tests

- `tests/rom/surface_tests.c` (the test ROM, last, since it overwrites the other suites' VRAM): the mode switch and the display bits it keeps, the flip and the transform landing in VBlank and not before, clamping, colors through the shadow palette and their refusals, sprite groups loading from tile 512 and from 0 again afterwards, the text layer off and restored, maps and the surface refusing each other, the splash skipped, every call ignored while off; and the costs above.
- `tests/web_ppu_tests.c` (host): the web renderer's mode 4 under the transforms the header documents.
- The `raycaster` and `life` examples, checked on the GBA and the web with the capture tools ([development.md](development.md)).
