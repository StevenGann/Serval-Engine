# Licensing

**Status:** current. Licenses verified against upstream on 2026-10-04; Maxmod is listed but not linked yet.

## Engine

The runtime engine and bytecode VM are released under the **[MIT License](../LICENSE)**. It is linked into every game ROM, so the license must be permissive, attribution-light and impose no copyleft on games.

MIT was chosen over zlib for its familiarity and because it matches libtonc's license. zlib would require no attribution in games, but games owe notices for libtonc and Maxmod regardless, so it would only remove one entry from the notices file. That advantage would return only if the engine stopped using both libraries; relicensing later needs every contributor's consent.

Consequently, every dependency linked into the ROM must be compatible with that goal, and the obligations they place on games are listed below.

## What ends up in a game ROM

Verified against upstream on 2026-10-04. Verbatim license texts for the libraries compiled into ROMs are in [`third_party/licenses/`](../third_party/licenses/).

| Component | Source | License | Obligation for a shipped game |
| --- | --- | --- | --- |
| Serval Engine | this repo | MIT | Include copyright and permission notice |
| libtonc | [gbadev-org/libtonc](https://github.com/gbadev-org/libtonc) | MIT (© 2005-2009 J Vijn) | Include copyright and permission notice |
| Maxmod (planned: not linked yet) | [blocksds/maxmod](https://github.com/blocksds/maxmod) or [devkitPro/maxmod](https://github.com/devkitPro/maxmod) | ISC | Include copyright and permission notice |
| libgcc (compiler runtime, e.g. division helpers) | GCC | GPL v3 + GCC Runtime Library Exception | None: the exception covers code compiled by GCC |
| newlib (C library) | toolchain | Mix of BSD-style licenses per file | Mostly "reproduce notice in documentation"; avoid linking it (see below) |
| C runtime startup + linker script | Serval Engine (`src/gba/crt0.s`, `src/gba/gba.ld`) | MIT | Same as Serval Engine |

The text layer's 8x8 font (`sys8`) comes from libtonc and is covered by its notice.

**Web builds** ([platforms.md](platforms.md#web)) contain the same engine and libtonc code, compiled by Emscripten, plus code Emscripten adds to every page:

| Component | Source | License | Obligation for a shipped game |
| --- | --- | --- | --- |
| Emscripten runtime (JavaScript glue, `emmalloc`) | [emscripten-core/emscripten](https://github.com/emscripten-core/emscripten) | MIT (or University of Illinois/NCSA) | Include copyright and permission notice |
| musl (C library routines such as `memcpy`) | bundled with Emscripten | MIT | Include copyright and permission notice |
| compiler-rt (compiler runtime) | bundled with Emscripten | Apache 2.0 with LLVM Exception | None: the exception covers compiled code |

**Bottom line:** every game must include the notices of Serval Engine and libtonc (plus Emscripten and musl for web builds), and of Maxmod once the engine links it (planned music and sampled SFX, [audio.md](audio.md)). The tooling should therefore generate a third-party notices file for every exported game, from [`LICENSE`](../LICENSE) and [`third_party/licenses/`](../third_party/licenses/).

### Rules that keep it this way

- **Never include `tonc_libgba.h`.** It is removed from the vendored copy ([VENDORED.md](../third_party/libtonc/VENDORED.md)). libtonc's libgba compatibility header carries libgba's LGPL v2+ notice and is standalone (nothing else in libtonc includes it). LGPL code statically linked into a ROM would oblige games to allow relinking.
- **Write our own crt0 and linker script.** devkitARM's GBA startup code (`gba_crt0.s`) is MPL 2.0, which would require every game to tell recipients where to get that file's source. Its linker script (`gba_cart.ld`, by Jeff Frohwein) carries no license at all. The engine also needs its own ROM header anyway, without the Nintendo logo.
- **Keep newlib out of the link.** GCC can emit `memcpy`/`memset` calls even in code that never calls libc. The engine provides its own `memcpy`, `memset`, `memmove`, `memcmp` and `strlen` (`src/gba/libc.c`, backed by libtonc's `tonccpy`/`toncset` and linked into every ROM as an object), so newlib and its many notices are not linked. Check the link map when adding code; every ROM's `*_rom_checks` test fails if `libc.a` appears in it ([development.md](development.md#tests)).

### Provenance note for legal review

Two libtonc headers say parts came from libgba: `tonc_bios.h` ("pretty much copied verbatim from Pern and dkARM's libgba", itself from the CowBite spec and GBATEK) and `tonc_memdef.h` ("comms items taken from libgba"). Both contain hardware facts (register addresses, bit definitions, BIOS call numbers and prototypes), which are generally not copyrightable, and libtonc as a whole is MIT. Worth confirming in the planned legal review.

## Host tools

These run on the developer's PC and are not linked into games. Their obligations fall on whoever redistributes them (e.g. the editor package), not on games.

| Tool | License | Notes |
| --- | --- | --- |
| `mmutil` (soundbank builder) | BSD-3-Clause (© 2008 Mukunda Johnson) | Binary redistribution must reproduce the notice in documentation |
| GCC toolchain (devkitARM or ARM `arm-none-eabi-gcc`) | GPL v3 | Compiled games are not GPL. Check devkitARM's redistribution terms before bundling |
| mGBA fork (emulator / debugger) | MPL 2.0 | File-level copyleft: publish changes to mGBA files |

## Nintendo material

The engine ships nothing Nintendo-owned: no BIOS (use mGBA's built-in replacement for emulation) and no logo bitmap in generated ROM headers. Emulators do not require it.
