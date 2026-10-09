# Versioning and releases

**Status:** the release workflow is in place; no release has been published yet.

Serval Engine is versioned independently of Studio Advance. Each game project selects the engine version it builds with, and the editor downloads that version on demand. **GitHub Releases on this repository are the source of truth** for which engine versions exist.

## Versioning

[Semantic versioning](https://semver.org/), tagged `vX.Y.Z`.

| Bump | When |
| --- | --- |
| Major | A breaking change to the public C API, the ROM data formats ([sprites.md](sprites.md#rom-data-format), [tilemaps.md](tilemaps.md#rom-data-format)), the bytecode format ([vm.md](vm.md)) or the [debug link](debug-link.md) |
| Minor | New features that remain compatible, including implementing [planned API](#planned-api) |
| Patch | Fixes only |

The editor does not currently check compatibility between its own version and an engine version; every release is selectable. The version number is the signal to users that an upgrade may break a project, so breaking changes must bump the major version.

**What breaks a data format.** A new field whose zero keeps today's behaviour, or a meaning for a value today's engine refuses, is a minor change. Changing what an existing field, value or default means is major. Adding a field is compatible because data is written with designated initializers (Studio Advance emits them, and [sprites.md](sprites.md#rom-data-format) and [tilemaps.md](tilemaps.md#rom-data-format) ask for them in hand-written data too), so data that predates the field leaves it zero. The rule has a consequence for loaders: they must refuse values they don't understand today (a flag bit no feature uses yet, a reserved collision type). A loader that ignored them would accept data with a stray bit, and that data would change meaning the day the bit is assigned.

**What breaks the CMake functions.** `serval_add_rom()` and `serval_add_script()` are frozen with the rest of the API in 1.0.0 ([api-freeze.md](api-freeze.md#what-10-promises)); what a change to them can break is a game's build. A new keyword, or a value they refuse today being given a meaning, is a minor change; refusing a value an earlier version accepted, or changing what an accepted one does, is major. So their checks must be as strict as they will ever be when 1.0.0 freezes them. `serval_add_rom()`'s checks of `TITLE` and `GAME_CODE` became strict before 1.0.0-rc.1, the first release ([development.md](development.md#building-a-game)): stricter than the engine commits before it, so a game built from one of those may need its arguments fixed. They now stop the configuration on an empty `TITLE` (the target name was used instead), a `GAME_CODE` shorter than 4 characters (it was padded with zero bytes, and an empty one, or one CMake reads as false such as `0` or `OFF`, became `0000`), control characters in either (they went into the header), and an empty `SAVE` (it meant `SRAM` unless policy CMP0174 was NEW); a `TITLE` over 12 characters, a `GAME_CODE` over 4 or a non-ASCII character failed the build before, and now fails the configuration. Two changes accept more: a title CMake reads as false (`0`, `OFF`, `NO`, `N`, `FALSE`, `IGNORE`, `NOTFOUND`, `*-NOTFOUND`) is kept rather than replaced by the target name, and a target name over 12 characters gives a default title cut to 12 rather than a failed build. `tools/gbafix.py` follows the same rules when called directly.

## Planned API

Planned API is declared in this version but not implemented yet, so that the API is complete and games and Studio Advance can target it now; a later minor version implements it. In the headers each planned function or constant carries `SERVAL_PLANNED` (`platform.h`), and its documentation says it is planned.

**The warning.** Every use of a planned name compiles with a warning at the use, at any optimization level, in dead code too:

```
main.c:12:5: warning: 'music_play' is deprecated: Serval: planned, not implemented in
this version: tracker music, docs/audio.md#tracker-music [-Wdeprecated-declarations]
```

"Deprecated" is the compiler's fixed wording: nothing is being removed. The warning means the call is in the API, and this engine version does nothing useful with it yet. Games built with `-Werror` stop at it.

**Silencing it on purpose**, to write code against planned API that starts working in the engine version implementing it:

| Scope | How |
| --- | --- |
| Whole game | `-DSERVAL_NO_PLANNED_WARNINGS` (CMake: `target_compile_definitions(game PRIVATE SERVAL_NO_PLANNED_WARNINGS)`), or `#define SERVAL_NO_PLANNED_WARNINGS` before the first Serval include (it has no effect after one) |
| One region | `#pragma GCC diagnostic push`, `#pragma GCC diagnostic ignored "-Wdeprecated-declarations"`, then `#pragma GCC diagnostic pop` (GCC and Clang both honour it) |
| Keep the warning, but not as an error under `-Werror` | `-Wno-error=deprecated-declarations` |

**At run time**, planned API does nothing harmful. A planned function returns 0, `false` or its type's "none" (e.g. `SFX_NONE`) and changes nothing; a loader refuses data that needs a planned feature, as it refuses any data it doesn't understand. Debug builds warn once per problem, e.g. `serval: music_play: tracker music is planned, not implemented in this engine version; nothing plays`.

**In scripts.** Scripts reach a planned feature only once a version implements it: the VM's SYS page has no calls for planned functions ([vm.md](vm.md#engine-calls)), and the script assembler refuses a planned constant by name (`MAP_CONTACT_LADDER is planned, not implemented in this engine version (ladders, ...)`), where C compiles it with the warning. The implementing version makes the name work in scripts with no change to them.

**Lifecycle.** A planned name is part of the API from the version that declares it, with the same compatibility promise as the rest. A minor version implements it: the marker goes, the signature stays, so code written against it compiles without the warning and starts working. If a planned design proves wrong, the fix is additive: a new function, with the old one documented as superseded and kept as a stub. Changing a planned name's signature or meaning after its release is a major change, like any API change.

## Manifest

The repository root contains `serval.json`, which describes the engine to the tooling. It is included in every release archive and is also how a local checkout is recognized as an engine.

```json
{
  "name": "serval-engine",
  "version": "1.4.0",
  "toolchain": { "gcc": ">=15.3" }
}
```

- `version` must equal the release tag without the `v`. A suffix such as `0.2.0-rc.1` makes a prerelease. CMake reads it as `SERVAL_VERSION_STRING` (the full string) and `SERVAL_VERSION` (`X.Y.Z` only, for `project()`); anything other than `X.Y.Z[-pre][+build]` fails configuration.
- `toolchain.gcc` is the minimum GCC version the engine needs (currently 15.3, the version CI builds and tests with). The editor bundles a single toolchain and warns when it is older than this; configuring the engine with an older `arm-none-eabi-gcc` also prints a CMake warning.

`serval.json` says `1.0.0-rc.1`: the first release is 1.0.0-rc.1, the release candidate of 1.0.0, whose API is frozen ([api-freeze.md](api-freeze.md)). 1.0.0 follows once Studio Advance has integrated against it ([handoff.md](handoff.md#from-rc1-to-100)).

## Release contents

Each GitHub release attaches a packaged archive, `serval-engine-X.Y.Z.zip`. It contains only what building a game needs (`tools/package-release.sh`, which packages only files tracked by git): `include/`, `src/` (with the startup code and linker script), `cmake/`, `third_party/` (vendored libtonc and the license texts), `CMakeLists.txt`, `CMakePresets.json`, `tools/gbafix.py`, `tools/svlua.py` and `tools/svm.py` (the Lua-subset compiler and the script assembler `serval_add_script()` runs), `serval.json` and `LICENSE`. No docs, tests or examples.

`serval-engine-X.Y.Z.zip.sha256` holds the archive's SHA-256 (`sha256sum -c` format). `serval.json` is also attached as a separate asset, so tools can read a release's manifest without downloading the archive.

A game uses the extracted archive with `add_subdirectory()` and `serval_add_rom()` from its own CMake project ([getting-started.md](getting-started.md#2-create-your-game)); CI checks this for every archive with `tools/check-consumer.sh`.

Tooling uses the attached archive rather than GitHub's auto-generated source archives, which include development-only files and are not guaranteed to be byte-stable.

- **Publishing:** the release workflow creates a *draft*; it becomes visible to Studio Advance only once published on GitHub. See [development.md](development.md#cicd).
- **Prereleases** (`vX.Y.Z-rc.N`, marked as prerelease on GitHub) are hidden by the editor unless the user opts in.
- **Withdrawing a release:** delete it on GitHub. Projects that already downloaded it keep their cached copy.
