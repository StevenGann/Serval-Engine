# Licensing

## Engine

The runtime engine and bytecode VM will be released under **MIT or zlib** (not yet chosen). It is linked into every game ROM, so the license must be permissive and attribution-light and must impose no terms on games built with it.

Consequently, every dependency linked into the ROM must be compatible with that goal.

## Third-party dependencies

| Dependency | Use | License | Notes |
| --- | --- | --- | --- |
| libtonc | Hardware layer | Verify | Use the maintained [gbadev-org fork](https://github.com/gbadev-org/libtonc) |
| Maxmod | Audio runtime | Permissive (verify) | Linked into ROMs |
| `mmutil` | Soundbank builder (host tool) | Verify | Not linked into ROMs |
| GCC (devkitARM or ARM `arm-none-eabi-gcc`) | Toolchain | GPL | Compiled games are not GPL. Check devkitARM's redistribution terms before bundling |
| mGBA (fork) | Emulator / debugger | MPL 2.0 | File-level copyleft: publish changes to mGBA files only |

## Nintendo material

The engine ships nothing Nintendo-owned: no BIOS (use mGBA's built-in replacement for emulation) and no logo bitmap in generated ROM headers. Emulators do not require it.
