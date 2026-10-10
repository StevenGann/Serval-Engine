# Third-party licenses

Verbatim license texts of third-party code compiled into game ROMs, and into web builds where noted. `libtonc.txt` covers every ROM and every web build; `emscripten.txt` and `musl.txt` every web build; `maxmod.txt` only ROMs that link Maxmod: those that call tracker music or sampled sound effects (`audio_bank_set()`, `music_*()`, `sfx_*()`; web builds don't link it). Games built with Serval Engine must reproduce the notices of what they contain (for example in a credits screen or a notices file shipped with the game). See [docs/licensing.md](https://github.com/StevenGann/Serval-Engine/blob/main/docs/licensing.md) (release archives do not include `docs/`).

| File | Component | License | Taken from |
| --- | --- | --- | --- |
| [libtonc.txt](libtonc.txt) | libtonc | MIT | `gbadev-org/libtonc` @ `c5af1b2`, `license.txt` |
| [maxmod.txt](maxmod.txt) | Maxmod | ISC | `blocksds/maxmod` @ `a797317` (`v1.24.0-blocks`), `COPYING` (also in `third_party/maxmod/`) |
| [emscripten.txt](emscripten.txt) | Emscripten runtime (web builds only) | MIT (or University of Illinois/NCSA) | Emscripten 6.0.11 (`a001454`), `LICENSE` |
| [musl.txt](musl.txt) | musl libc routines Emscripten links (web builds only) | MIT | Emscripten 6.0.11 (`a001454`), `system/lib/libc/musl/COPYRIGHT` |

Maxmod comes from BlocksDS ([docs/audio.md](https://github.com/StevenGann/Serval-Engine/blob/main/docs/audio.md#maxmod-blocksds)), vendored in `third_party/maxmod/` (its `VENDORED.md`). A ROM links it only if the game calls tracker music or sampled sound effects, so only such a game owes its notice.

Update these files whenever a dependency is updated.
