# Third-party licenses

Verbatim license texts of third-party code compiled into every game ROM, and into every web build where noted. Games built with Serval Engine must reproduce these notices (for example in a credits screen or a notices file shipped with the game). See [docs/licensing.md](https://github.com/StevenGann/Serval-Engine/blob/main/docs/licensing.md) (release archives do not include `docs/`).

| File | Component | License | Taken from |
| --- | --- | --- | --- |
| [libtonc.txt](libtonc.txt) | libtonc | MIT | `gbadev-org/libtonc` @ `c5af1b2`, `license.txt` |
| [maxmod.txt](maxmod.txt) | Maxmod | ISC | `blocksds/maxmod` @ `8d0ea38`, `COPYING` |
| [emscripten.txt](emscripten.txt) | Emscripten runtime (web builds only) | MIT (or University of Illinois/NCSA) | Emscripten 6.0.11 (`a001454`), `LICENSE` |
| [musl.txt](musl.txt) | musl libc routines Emscripten links (web builds only) | MIT | Emscripten 6.0.11 (`a001454`), `system/lib/libc/musl/COPYRIGHT` |

The Maxmod fork is not chosen yet. Both forks use identical ISC terms; the BlocksDS copy lists additional copyright holders for its changes. If the devkitPro fork is chosen, replace this file with its `maxmod_license.txt`.

Update these files whenever a dependency is updated.
