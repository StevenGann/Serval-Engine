# Platforms and portability

GBA is the only 1.0 target. The planned order afterwards is GB/GBC, then DS.

## Portability rules

These keep future targets possible without a rewrite:

- The [core API](core-api.md) is the abstraction boundary: each call means the same thing on every target.
- VM opcodes are platform-neutral: no hardware addresses, no hard dependency on 32-bit values ([vm.md](vm.md)).
- Assets are stored at source quality by the tooling and converted per target at build time.
- Each target declares a profile of its limits (sprites, palettes, VRAM, resolution).

## Targets

| Target | Runtime base | Emulator | Key challenges |
| --- | --- | --- | --- |
| GBA (1.0) | libtonc + Maxmod | mGBA fork (MPL) | Baseline |
| GB/GBC | GBDK-2020 | mGBA already supports it | The VM on the 8-bit SM83 must be very lean; study GBVM (MIT) |
| DS | BlocksDS (libnds-based) | melonDS or DeSmuME, both GPL | GPL emulator must run as a separate process over the [debug link](debug-link.md). Two screens, touch, 3D |
