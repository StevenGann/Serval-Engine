#!/usr/bin/env python3
"""Check a ROM built by serval_add_rom() for problems the build cannot catch.

- The ROM is padded beyond the multiboot size and has a valid header
  (fixed value, checksum, title and game code): gbafix.py ran.
- newlib's libc.a is not in the link map: games stay free of its license.
- The code contains no BLX instruction: the ARM7TDMI (ARMv4T) has none, so
  one would crash on hardware.

Usage: check-rom.py --rom R.gba [--map R.map] [--elf R.elf --objdump OBJDUMP]
                    [--title T] [--game-code C]
Exits non-zero, listing every failed check, if any fails.
"""

import argparse
import re
import subprocess
import sys

MULTIBOOT_MAX_SIZE = 256 * 1024
TITLE_OFFSET, TITLE_LEN = 0xA0, 12
GAME_CODE_OFFSET, GAME_CODE_LEN = 0xAC, 4
FIXED_VALUE_OFFSET = 0xB2
CHECKSUM_OFFSET = 0xBD


def check_rom(path, title, game_code):
    errors = []
    with open(path, "rb") as f:
        rom = f.read()
    if len(rom) <= MULTIBOOT_MAX_SIZE:
        errors.append(f"{path}: {len(rom)} bytes; ROMs of 256 KiB or less can be mistaken "
                      "for multiboot images (did gbafix.py run?)")
    if len(rom) < 0xC0:
        return errors + [f"{path}: too small to contain a ROM header"]
    if rom[FIXED_VALUE_OFFSET] != 0x96:
        errors.append(f"{path}: header fixed value is 0x{rom[FIXED_VALUE_OFFSET]:02X}, "
                      "not 0x96 (did gbafix.py run?)")
    checksum = (-(sum(rom[TITLE_OFFSET:CHECKSUM_OFFSET]) + 0x19)) & 0xFF
    if rom[CHECKSUM_OFFSET] != checksum:
        errors.append(f"{path}: header checksum is 0x{rom[CHECKSUM_OFFSET]:02X}, "
                      f"expected 0x{checksum:02X}")
    for name, value, offset, length in (("title", title, TITLE_OFFSET, TITLE_LEN),
                                        ("game code", game_code, GAME_CODE_OFFSET, GAME_CODE_LEN)):
        if value is None:
            continue
        actual = rom[offset:offset + length].rstrip(b"\0").decode("ascii", "replace")
        if actual != value:
            errors.append(f"{path}: header {name} is {actual!r}, expected {value!r}")
    return errors


def check_map(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        hits = sorted({line.strip() for line in f if re.search(r"[/\\]libc(_nano)?\.a\b", line)})
    if hits:
        return [f"{path}: newlib's libc.a is linked (keep newlib out of the link):"] + \
               [f"    {line}" for line in hits[:5]]
    return []


def check_no_blx(elf, objdump):
    result = subprocess.run([objdump, "-d", elf], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        return [f"{objdump} -d {elf} failed:\n{result.stderr}"]
    hits = [line.strip() for line in result.stdout.splitlines()
            if re.search(r"\sblx\b", line)]
    if hits:
        return [f"{elf}: {len(hits)} BLX instruction(s); the ARM7TDMI has no BLX:"] + \
               [f"    {line}" for line in hits[:5]]
    return []


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rom", required=True)
    parser.add_argument("--map")
    parser.add_argument("--elf")
    parser.add_argument("--objdump")
    parser.add_argument("--title")
    parser.add_argument("--game-code")
    args = parser.parse_args()
    if bool(args.elf) != bool(args.objdump):
        parser.error("--elf and --objdump go together")

    try:
        errors = check_rom(args.rom, args.title, args.game_code)
        if args.map:
            errors += check_map(args.map)
        if args.elf:
            errors += check_no_blx(args.elf, args.objdump)
    except OSError as e:
        errors = [str(e)]
    for error in errors:
        print(error, file=sys.stderr)
    if not errors:
        print(f"{args.rom}: ok")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
