# libtonc (vendored)

- **Upstream:** <https://github.com/gbadev-org/libtonc>
- **Commit:** `c5af1b2cb019dcde43216596390490bc07800b21` (2026-04-08)
- **License:** MIT, see [license.txt](license.txt)

Copied from upstream (`include/`, `src/`, `asm/`, `license.txt`) with these changes:

| Removed | Why |
| --- | --- |
| `include/tonc_libgba.h` | Carries libgba's LGPL notice; must never be compiled into games ([docs/licensing.md](../../docs/licensing.md)) |
| `src/tte/tte_iohook.c` | Requires devkitARM's `sys/iosupport.h` (stdio hooks); the engine does not link a C library |
| `src/font/*.png` | Source images of the fonts; the `.s` files built from them are kept |
| `Makefile`, docs, `base.c/.h`, `.github/` | Replaced by `third_party/CMakeLists.txt` |

No upstream file is otherwise modified, except that line endings are normalized from CRLF to LF on commit (`.gitattributes`). To update: copy the same directories from a new upstream commit, reapply the removals, update the commit above and `third_party/licenses/libtonc.txt`, and rebuild.
