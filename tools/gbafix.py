#!/usr/bin/env python3
"""Fill in a GBA ROM header (title, game code, checksum) and pad the ROM.

The Nintendo logo area is deliberately left empty (see docs/licensing.md).
Emulators boot such ROMs; real hardware requires the logo, which is left to
users and independent tools.

Padding: emulators guess whether a small file is a cartridge ROM or a
multiboot image (which runs from EWRAM and can be at most 256 KiB). Older
mGBA releases (0.8.x and earlier) misdetect small ROMs whose startup code
references EWRAM, as Serval's crt0 does, and run them from the wrong address:
a white screen. A ROM larger than 256 KiB is never treated as multiboot, so
ROMs are padded to --min-size (default 512 KiB) with 0xFF, the value of
erased flash.
"""

import argparse
import sys

TITLE_OFFSET, TITLE_LEN = 0xA0, 12
GAME_CODE_OFFSET, GAME_CODE_LEN = 0xAC, 4
MAKER_CODE_OFFSET, MAKER_CODE_LEN = 0xB0, 2
FIXED_VALUE_OFFSET = 0xB2
CHECKSUM_OFFSET = 0xBD
HEADER_END = 0xC0
MULTIBOOT_MAX_SIZE = 256 * 1024
DEFAULT_MIN_SIZE = 512 * 1024


def ascii_field(value: str, length: int, name: str) -> bytes:
    data = value.encode("ascii")
    if len(data) > length:
        raise ValueError(f"{name} must be at most {length} ASCII characters: {value!r}")
    return data.ljust(length, b"\0")


def header_checksum(rom: bytes) -> int:
    return (-(sum(rom[TITLE_OFFSET:CHECKSUM_OFFSET]) + 0x19)) & 0xFF


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("rom", help="ROM file, modified in place")
    parser.add_argument("--title", default="", help="up to 12 ASCII characters")
    parser.add_argument("--game-code", default="0000", help="4 ASCII characters")
    parser.add_argument("--maker-code", default="00", help="2 ASCII characters")
    parser.add_argument("--min-size", type=int, default=DEFAULT_MIN_SIZE,
                        help=f"pad the ROM with 0xFF to at least this many bytes "
                             f"(default {DEFAULT_MIN_SIZE}; must exceed {MULTIBOOT_MAX_SIZE})")
    args = parser.parse_args()
    if args.min_size <= MULTIBOOT_MAX_SIZE:
        print(f"--min-size must exceed {MULTIBOOT_MAX_SIZE} so the ROM is never "
              "mistaken for a multiboot image", file=sys.stderr)
        return 1

    with open(args.rom, "rb") as f:
        rom = bytearray(f.read())
    if len(rom) < HEADER_END:
        print(f"{args.rom}: too small to contain a ROM header", file=sys.stderr)
        return 1

    try:
        rom[TITLE_OFFSET:TITLE_OFFSET + TITLE_LEN] = ascii_field(args.title, TITLE_LEN, "title")
        rom[GAME_CODE_OFFSET:GAME_CODE_OFFSET + GAME_CODE_LEN] = ascii_field(
            args.game_code, GAME_CODE_LEN, "game code")
        rom[MAKER_CODE_OFFSET:MAKER_CODE_OFFSET + MAKER_CODE_LEN] = ascii_field(
            args.maker_code, MAKER_CODE_LEN, "maker code")
    except ValueError as e:
        print(e, file=sys.stderr)
        return 1
    rom[FIXED_VALUE_OFFSET] = 0x96
    rom[CHECKSUM_OFFSET] = header_checksum(rom)
    if len(rom) < args.min_size:
        rom.extend(b"\xff" * (args.min_size - len(rom)))

    with open(args.rom, "wb") as f:
        f.write(rom)
    return 0


if __name__ == "__main__":
    sys.exit(main())
