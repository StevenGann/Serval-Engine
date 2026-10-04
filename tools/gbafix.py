#!/usr/bin/env python3
"""Fill in a GBA ROM header: title, game code and header checksum.

The Nintendo logo area is deliberately left empty (see docs/licensing.md).
Emulators boot such ROMs; real hardware requires the logo, which is left to
users and independent tools.
"""

import argparse
import sys

TITLE_OFFSET, TITLE_LEN = 0xA0, 12
GAME_CODE_OFFSET, GAME_CODE_LEN = 0xAC, 4
MAKER_CODE_OFFSET, MAKER_CODE_LEN = 0xB0, 2
FIXED_VALUE_OFFSET = 0xB2
CHECKSUM_OFFSET = 0xBD
HEADER_END = 0xC0


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
    args = parser.parse_args()

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

    with open(args.rom, "wb") as f:
        f.write(rom)
    return 0


if __name__ == "__main__":
    sys.exit(main())
