#!/usr/bin/env python3
"""Fill in a GBA ROM header (title, game code, checksum) and pad the ROM.

The Nintendo logo area is deliberately left empty (see docs/licensing.md).
Emulators boot such ROMs; real hardware requires the logo, which is left to
users and independent tools.

Header fields: the title is 1 to 12 printable ASCII characters (space to
tilde), followed by zero bytes up to 12; the game code is exactly 4 and the
maker code exactly 2, never padded. Anything else is refused with exit
status 1, leaving the ROM as it was: a control or non-ASCII character, an
empty or longer title, a game code or maker code of another length. Without
--title, the title field is left empty (all zero), which the GBA and
emulators accept; an empty --title is refused as a likely mistake.
serval_add_rom() (cmake/Serval.cmake) always gives a title, and checks the
same rules when the game is configured.

--title-hex and --game-code-hex take the same text as hexadecimal character
codes ("48 49" or "4849" for HI), so that a build system can pass any
character without quoting it for a shell; serval_add_rom() uses them. A
value starting with "-" needs the --title=VALUE form.

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


def describe(c: str) -> str:
    if ord(c) < 0x20 or ord(c) == 0x7F:
        return "a control character"
    return f'"{c}"'


def header_text(value: str, name: str, low: int, high: int) -> bytes:
    """value as header bytes, zero-filled to high, if it is low to high
    printable ASCII characters; else ValueError saying what is wrong."""
    if value == "" and low > 0 and low != high:
        raise ValueError(f"{name} is empty: give {low} to {high} printable ASCII characters, "
                         f"or leave --{name} out for an empty {name} field")
    for c in value:
        if not " " <= c <= "~":
            raise ValueError(f"{name} can't contain {describe(c)}: use ASCII letters, digits, "
                             "spaces and punctuation")
    if low == high and len(value) != low:
        raise ValueError(f'{name} must have exactly {low} characters; "{value}" has {len(value)}')
    if len(value) > high:
        raise ValueError(f'{name} can have at most {high} characters; "{value}" has {len(value)}')
    return value.encode("ascii").ljust(high, b"\0")


def from_hex(text: str, option: str) -> str:
    try:
        return bytes.fromhex(text).decode("latin-1")
    except ValueError:
        raise ValueError(f'{option} "{text}" is not hexadecimal character codes') from None


def header_checksum(rom: bytes) -> int:
    return (-(sum(rom[TITLE_OFFSET:CHECKSUM_OFFSET]) + 0x19)) & 0xFF


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("rom", help="ROM file, modified in place")
    title = parser.add_mutually_exclusive_group()
    title.add_argument("--title", help="1 to 12 printable ASCII characters (default: none, "
                                       "the title field left empty)")
    title.add_argument("--title-hex", metavar="HEX", help="the title as hexadecimal character codes")
    game_code = parser.add_mutually_exclusive_group()
    game_code.add_argument("--game-code", help="exactly 4 printable ASCII characters "
                                               "(default 0000)")
    game_code.add_argument("--game-code-hex", metavar="HEX",
                           help="the game code as hexadecimal character codes")
    parser.add_argument("--maker-code", default="00",
                        help="exactly 2 printable ASCII characters (default 00)")
    parser.add_argument("--min-size", type=int, default=DEFAULT_MIN_SIZE,
                        help=f"pad the ROM with 0xFF to at least this many bytes "
                             f"(default {DEFAULT_MIN_SIZE}; must exceed {MULTIBOOT_MAX_SIZE})")
    args = parser.parse_args(argv)
    if args.min_size <= MULTIBOOT_MAX_SIZE:
        print(f"--min-size must exceed {MULTIBOOT_MAX_SIZE} so the ROM is never "
              "mistaken for a multiboot image", file=sys.stderr)
        return 1

    # Every field is checked before the ROM is touched.
    try:
        if args.title_hex is not None:
            title_bytes = header_text(from_hex(args.title_hex, "--title-hex"), "title", 1, TITLE_LEN)
        elif args.title is not None:
            title_bytes = header_text(args.title, "title", 1, TITLE_LEN)
        else:
            title_bytes = bytes(TITLE_LEN)
        if args.game_code_hex is not None:
            game_code_text = from_hex(args.game_code_hex, "--game-code-hex")
        elif args.game_code is not None:
            game_code_text = args.game_code
        else:
            game_code_text = "0000"
        game_code_bytes = header_text(game_code_text, "game code", GAME_CODE_LEN, GAME_CODE_LEN)
        maker_code_bytes = header_text(args.maker_code, "maker code", MAKER_CODE_LEN,
                                       MAKER_CODE_LEN)
    except ValueError as e:
        print(e, file=sys.stderr)
        return 1

    with open(args.rom, "rb") as f:
        rom = bytearray(f.read())
    if len(rom) < HEADER_END:
        print(f"{args.rom}: too small to contain a ROM header", file=sys.stderr)
        return 1

    rom[TITLE_OFFSET:TITLE_OFFSET + TITLE_LEN] = title_bytes
    rom[GAME_CODE_OFFSET:GAME_CODE_OFFSET + GAME_CODE_LEN] = game_code_bytes
    rom[MAKER_CODE_OFFSET:MAKER_CODE_OFFSET + MAKER_CODE_LEN] = maker_code_bytes
    rom[FIXED_VALUE_OFFSET] = 0x96
    rom[CHECKSUM_OFFSET] = header_checksum(rom)
    if len(rom) < args.min_size:
        rom.extend(b"\xff" * (args.min_size - len(rom)))

    with open(args.rom, "wb") as f:
        f.write(rom)
    return 0


if __name__ == "__main__":
    sys.exit(main())
