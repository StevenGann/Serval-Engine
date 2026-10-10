# Maxmod (vendored)

- **Upstream:** <https://github.com/blocksds/maxmod> (BlocksDS's fork; a mirror of <https://codeberg.org/blocksds/maxmod>)
- **Tag:** `v1.24.0-blocks`, commit `a797317e5bb4eceebe5f41b85b620d68ef534b79` (2026-09-13)
- **License:** ISC, see [COPYING](COPYING) (also `third_party/licenses/maxmod.txt`)
- **Sound banks:** built by BlocksDS's mmutil of the same tag, `v1.24.0-blocks` (<https://github.com/blocksds/mmutil>, commit `f8abd4f40bd42023c2e4bfcc127841fd7ceecf1e`, ISC): serval.json's `toolchain.mmutil`, which `tools/build-mmutil.sh` builds and `serval_add_soundbank()` checks. Its MAS format version, `0x18` (mmutil's `source/version.h`), is what `audio_bank_set()` accepts (`src/gba/maxmod.c`).

Copied from upstream, unmodified: `COPYING`; `include/maxmod.h`, `include/mm_types.h`, `include/mm_mas.h`, `include/mm_msl.h`; `source/core/` and `source/gba/`, every file. That is the GBA library as upstream's `Makefile.plat` builds it (`SYSTEM=GBA`: `source/core` and `source/gba`). Left out:

| Left out | Why |
| --- | --- |
| `source/ds/`, `include/maxmod7.h`, `include/maxmod9.h` | The DS build (ARM7 and ARM9 halves); the engine targets the GBA |
| `Makefile`, `Makefile.plat` | Replaced by `third_party/CMakeLists.txt`, which compiles the same files with the same flags: Thumb, `-O2` (debug builds too, as upstream's debug library), C23 with GNU extensions, `__GBA__`, and `NDEBUG` outside debug builds; the hot routines put themselves in IWRAM as ARM code with `section(".iwram")` attributes and the assembly mixer's `.iwram` section |
| `documentation/`, `doxygen/`, `Doxyfile`, `readme.md` | Documentation; the engine's is `docs/audio.md`, which quotes the hardware and cost figures it relies on |

How the engine uses it (`src/gba/maxmod.c`, `docs/audio.md`): `mmInit()` with static buffers, never `mmInitDefault()` (it calls `calloc`) or `mmEnd()` (it calls `free`); to stop, `mmStop()`, `mmEffectCancelAll()` and the mixer's own `mmMixerEnd()` (`source/gba/mixer.c`, `mmEnd()` without the `free`). It also reads and sets `mp_writepos` and `mp_mix_seg` (the mixer's write position and half of the double buffer, `source/gba/mixer.c`), to mix from the VBlank handler a half a frame overran. Updating Maxmod means checking those internals and the hardware Maxmod claims (`source/gba/mixer.c`) against `docs/audio.md` and `docs/core-api.md`, then bumping `serval.json`'s `toolchain.mmutil` and `tools/build-mmutil.sh` and `tools/setup-dev.sh` to the matching mmutil.

To update: copy the same files from a new upstream tag, update the tag and commit above, `third_party/licenses/maxmod.txt` and its row in `third_party/licenses/README.md`, and rebuild and run the sampled audio tests (`rom_tests_sampled_audio`).
