#!/usr/bin/env python3
"""Check a ROM built by serval_add_rom() for problems the build cannot catch.

- The ROM is padded beyond the multiboot size and has a valid header
  (fixed value, checksum, title and game code): gbafix.py ran.
- newlib's libc.a is not in the link map: games stay free of its license.
- The code contains no BLX instruction: the ARM7TDMI (ARMv4T) has none, so
  one would crash on hardware.
- If the game links the save code (save.h), the ROM contains exactly one
  save type ID string on a 4-byte boundary, the one of its save type
  (serval_add_rom's SAVE: "SRAM_V", "FLASH512_V", "FLASH1M_V" or "EEPROM_V"),
  which emulators and flash carts look for to give the game its save memory;
  if it doesn't, the ROM contains none.

Usage: check-rom.py --rom R.gba [--map R.map] [--elf R.elf --objdump OBJDUMP]
                    [--title T] [--game-code C] [--save-type TYPE]
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


# Save type ID strings scanners look for (on 4-byte boundaries), and the one
# each serval_add_rom() SAVE type puts in the ROM.
SAVE_IDS = (b"SRAM_V", b"SRAM_F_V", b"FLASH_V", b"FLASH512_V", b"FLASH1M_V", b"EEPROM_V")
SAVE_TYPE_IDS = {
    "SRAM": b"SRAM_V113",
    "FLASH64K": b"FLASH512_V131",
    "FLASH128K": b"FLASH1M_V103",
    "EEPROM8K": b"EEPROM_V124",
    "EEPROM512": b"EEPROM_V124",
}


def find_save_ids(rom):
    """(offset, ID string) of every save type ID string on a 4-byte boundary."""
    found = []
    for prefix in SAVE_IDS:
        at = rom.find(prefix)
        while at >= 0:
            if at % 4 == 0:
                end = at + len(prefix)
                while end < len(rom) and end - at < 16 and 0x21 <= rom[end] < 0x7F:
                    end += 1
                found.append((at, rom[at:end]))
            at = rom.find(prefix, at + 1)
    return sorted(found)


def check_save_id(rom_path, map_path, save_type):
    """The save type's ID string is in the ROM, alone, if the save code was linked."""
    with open(map_path, encoding="utf-8", errors="replace") as f:
        linked = f.read().split("Linker script and memory map", 1)[-1]
    saves = re.search(r"^\s*\.rodata\.serval_save_device\b", linked, re.MULTILINE)
    with open(rom_path, "rb") as f:
        rom = f.read()
    found = find_save_ids(rom)
    listed = ", ".join(f"{s.decode('ascii', 'replace')!r} at 0x{at:X}" for at, s in found)
    if not saves:
        if found:
            return [f"{rom_path}: doesn't link the save code but contains save type ID "
                    f"strings ({listed}): emulators and flash carts would give it save memory"]
        return []
    if save_type is None:
        return []
    expected = SAVE_TYPE_IDS.get(save_type)
    if expected is None:
        return [f"{rom_path}: unknown save type {save_type!r}"]
    if not found:
        return [f"{rom_path}: links the save code but has no {expected.decode()!r} ID string on "
                "a 4-byte boundary: emulators and flash carts won't give it save memory"]
    if len(found) > 1 or found[0][1] != expected:
        return [f"{rom_path}: save type {save_type} needs exactly one save type ID string, "
                f"{expected.decode()!r}, but the ROM has {listed}"]
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
    parser.add_argument("--save-type", choices=sorted(SAVE_TYPE_IDS))
    args = parser.parse_args()
    if bool(args.elf) != bool(args.objdump):
        parser.error("--elf and --objdump go together")

    try:
        errors = check_rom(args.rom, args.title, args.game_code)
        if args.map:
            errors += check_map(args.map)
            errors += check_save_id(args.rom, args.map, args.save_type)
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
