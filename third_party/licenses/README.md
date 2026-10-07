# Third-party licenses

Verbatim license texts of third-party code compiled into every game ROM, and into every web build where noted. Games built with Serval Engine must reproduce these notices (for example in a credits screen or a notices file shipped with the game). See [docs/licensing.md](https://github.com/StevenGann/Serval-Engine/blob/main/docs/licensing.md) (release archives do not include `docs/`).

| File | Component | License | Taken from |
| --- | --- | --- | --- |
| [libtonc.txt](libtonc.txt) | libtonc | MIT | `gbadev-org/libtonc` @ `c5af1b2`, `license.txt` |
| [maxmod.txt](maxmod.txt) | Maxmod | ISC | `blocksds/maxmod` @ `8d0ea38`, `COPYING` |
| [emscripten.txt](emscripten.txt) | Emscripten runtime (web builds only) | MIT (or University of Illinois/NCSA) | Emscripten 6.0.11 (`a001454`), `LICENSE` |
| [musl.txt](musl.txt) | musl libc routines Emscripten links (web builds only) | MIT | Emscripten 6.0.11 (`a001454`), `system/lib/libc/musl/COPYRIGHT` |

Maxmod comes from BlocksDS (decided for 1.0; [docs/audio.md](https://github.com/StevenGann/Serval-Engine/blob/main/docs/audio.md#maxmod-blocksds)). Tracker music and sampled sound are planned API, so no ROM links Maxmod yet; the notice is here for the version that implements them.

Update these files whenever a dependency is updated.
