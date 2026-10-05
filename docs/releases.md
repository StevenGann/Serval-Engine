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
  "toolchain": { "gcc": ">=15.3" }
}
```

- `version` must equal the release tag without the `v`. A suffix such as `0.2.0-rc.1` makes a prerelease.
- `toolchain.gcc` is the minimum GCC version the engine needs (currently 15.3, the version CI builds and tests with). The editor bundles a single toolchain and warns when it is older than this.

No release has been published yet; `serval.json` says `0.1.0`.

## Release contents

Each GitHub release attaches a packaged archive, `serval-engine-X.Y.Z.zip`. It contains only what building a game needs: headers, sources, linker scripts, `serval.json`, `LICENSE` and `third_party/licenses/`.

`serval.json` is also attached as a separate asset, so tools can read a release's manifest without downloading the archive.

Tooling uses the attached archive rather than GitHub's auto-generated source archives, which include development-only files and are not guaranteed to be byte-stable.

- **Publishing:** the release workflow creates a *draft*; it becomes visible to Studio Advance only once published on GitHub. See [development.md](development.md#cicd).
- **Prereleases** (`vX.Y.Z-rc.N`, marked as prerelease on GitHub) are hidden by the editor unless the user opts in.
- **Withdrawing a release:** delete it on GitHub. Projects that already downloaded it keep their cached copy.
