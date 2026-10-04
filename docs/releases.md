# Versioning and releases

Serval Engine is versioned independently of Studio Advance. Each game project selects the engine version it builds with, and the editor downloads that version on demand. **GitHub Releases on this repository are the source of truth** for which engine versions exist.

## Versioning

[Semantic versioning](https://semver.org/), tagged `vX.Y.Z`.

| Bump | When |
| --- | --- |
| Major | A breaking change to the public C API, the ROM data formats ([sprites.md](sprites.md#rom-data-format), [tilemaps.md](tilemaps.md#rom-data-format)), the bytecode format ([vm.md](vm.md)) or the [debug link](debug-link.md) |
| Minor | New features that remain compatible |
| Patch | Fixes only |

The editor does not currently check compatibility between its own version and an engine version; every release is selectable. The version number is the signal to users that an upgrade may break a project, so breaking changes must bump the major version.

## Manifest

The repository root contains `serval.json`, which describes the engine to the tooling. It is included in every release archive and is also how a local checkout is recognized as an engine.

```json
{
  "name": "serval-engine",
  "version": "1.4.0",
  "toolchain": { "gcc": ">=14.2" }
}
```

- `version` must match the release tag (without the `v`).
- `toolchain.gcc` is the minimum GCC version the engine needs. The editor bundles a single toolchain and warns when it is older than this.

## Release contents

Each GitHub release attaches a packaged archive, `serval-engine-X.Y.Z.zip`. It contains only what building a game needs: headers, sources, linker scripts, `serval.json`, `LICENSE` and `third_party/licenses/`.

Tooling uses this attached archive rather than GitHub's auto-generated source archives, which include development-only files and are not guaranteed to be byte-stable.

- **Prereleases** (`vX.Y.Z-rc.N`, marked as prerelease on GitHub) are hidden by the editor unless the user opts in.
- **Withdrawing a release:** delete it on GitHub. Projects that already downloaded it keep their cached copy.
