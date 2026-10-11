#!/usr/bin/env python3
"""LZ77 compression in the GBA BIOS's format, VRAM-safe, for SPRITE_ASSET_LZ77
sprites and TILESET_LZ77 tilesets (docs/sprites.md#lz77-compression,
docs/tilemaps.md#tilesets).

The format, what the BIOS's LZ77UnCompReadNormalWrite8bit and
LZ77UnCompReadNormalWrite16bit (SWI 0x11 and 0x12, libtonc's LZ77UnCompWram
and LZ77UnCompVram) read:

- a header word, little-endian: 0x10 in its low byte (type 1, LZ77), the
  unpacked size in bytes in bits 8-31;
- then blocks of a flag byte and up to eight items, one per flag bit from the
  highest: 0 is a literal byte, copied out; 1 is a match, two bytes, the
  length less 3 in the first's high four bits and the distance less 1 in its
  low four bits and the second byte: copy `length` (3-18) bytes from
  `distance` (1-4,096) bytes back, one at a time, so a match may overlap what
  it writes (distance 2 repeats two bytes);
- unpacking stops at the size. Unused flag bits of the last block are 0, and
  zeros pad the data to a whole word.

**VRAM-safe**: no match has a distance of 1. VRAM takes no 8-bit writes, so
the 16-bit decoder (SWI 0x12, the one that can unpack into VRAM) keeps a byte
at an even address until the next byte completes its halfword. A match
copying from the byte just before it, while that byte waits, reads VRAM's old
byte instead. Every byte two or more back has been written.

**The encoder** finds, at each position, the longest match (3 to 18 bytes)
at a distance of 2 to 4,096: among the earlier positions that start with the
same three bytes, the TRIES nearest, nearest first, the first longest kept.
Then it picks the parse that takes the fewest bits: a literal costs 9 bits (8
and its flag), a match 17 whatever its length or distance, and a shorter
match at the same distance is a prefix of the longest, so choosing, from the
end back, between a literal and every length each position allows gives the
fewest bits those matches can make (ties go to the longest match: fewer items
unpack faster). Simple enough to port: Studio Advance's or any tool's own
encoder needs only the format and the distance rule above; its output may
differ byte for byte and unpack the same.

Usage:
  lz77.py compress IN OUT            raw bytes in, BIOS-format data out
  lz77.py decompress IN OUT          the reverse; refuses data that isn't
                                     VRAM-safe or doesn't stop at its size
  lz77.py c --name NAME --out-dir DIR FILE...
                                     what serval_add_lz77() (cmake/Serval.cmake)
                                     runs: NAME.c and NAME.h, one const u32
                                     array per FILE, named <file name>_lz77

In the c command each FILE is raw bytes (.bin) or, any other name (.inc),
32-bit words written as C integer literals (hex or decimal, an optional u or
U, separated by commas; C comments allowed): what a const u32 array's
initializer holds, so a C file can #include the same file for the unpacked
tiles. Needs only Python 3's standard library.
"""

import argparse
import os
import pathlib
import re
import sys

HEADER_TYPE = 0x10
MIN_MATCH, MAX_MATCH = 3, 18
MIN_DISTANCE, MAX_DISTANCE = 2, 4096  # 2: VRAM-safe (1 is the format's own minimum)
MAX_SIZE = 0xFFFFFF  # bits 8-31 of the header
TRIES = 256  # earlier positions tried per position, nearest first
LITERAL_BITS, MATCH_BITS = 9, 17


class Lz77Error(Exception):
    pass


def _longest_matches(data):
    """[(length, distance)] for each position: its longest VRAM-safe match, or
    (0, 0) if it has none of MIN_MATCH bytes or more."""
    n = len(data)
    chains = {}  # first three bytes -> the positions starting with them, ascending
    found = [(0, 0)] * n
    for i in range(n - MIN_MATCH + 1):
        key = data[i:i + MIN_MATCH]
        chain = chains.setdefault(key, [])
        limit = min(MAX_MATCH, n - i)
        best_length = best_distance = tries = 0
        for k in range(len(chain) - 1, -1, -1):
            j = chain[k]
            distance = i - j
            if distance > MAX_DISTANCE:
                break
            if distance < MIN_DISTANCE:
                continue
            length = MIN_MATCH
            while length < limit and data[j + length] == data[i + length]:
                length += 1
            if length > best_length:
                best_length, best_distance = length, distance
                if length == limit:
                    break
            tries += 1
            if tries == TRIES:
                break
        found[i] = (best_length, best_distance)
        chain.append(i)
    return found


def _parse(data):
    """The items, (byte, None) or (length, distance), with the fewest bits."""
    n = len(data)
    matches = _longest_matches(data)
    bits = [0] * (n + 1)
    take = [0] * n  # 0: a literal; else a match of that length
    for i in range(n - 1, -1, -1):
        length, _ = matches[i]
        best, pick = None, 0
        for k in range(length, MIN_MATCH - 1, -1):
            cost = MATCH_BITS + bits[i + k]
            if best is None or cost < best:
                best, pick = cost, k
        cost = LITERAL_BITS + bits[i + 1]
        if best is None or cost < best:
            best, pick = cost, 0
        bits[i], take[i] = best, pick
    items, i = [], 0
    while i < n:
        if take[i]:
            items.append((take[i], matches[i][1]))
            i += take[i]
        else:
            items.append((data[i], None))
            i += 1
    return items


def compress(data):
    """data (bytes) in the BIOS's LZ77 format, VRAM-safe, padded to a whole
    word."""
    data = bytes(data)
    if len(data) > MAX_SIZE:
        raise Lz77Error(f"{len(data)} bytes; the format holds at most {MAX_SIZE}")
    out = bytearray((HEADER_TYPE | len(data) << 8).to_bytes(4, "little"))
    items = _parse(data)
    for block in range(0, len(items), 8):
        flags_at = len(out)
        out.append(0)
        for bit, (value, distance) in enumerate(items[block:block + 8]):
            if distance is None:
                out.append(value)
            else:
                out[flags_at] |= 0x80 >> bit
                d = distance - 1
                out += bytes(((value - MIN_MATCH) << 4 | d >> 8, d & 0xFF))
    out += bytes(-len(out) % 4)
    result = bytes(out)
    if decompress(result) != data:  # a safety net: never write what doesn't unpack
        raise Lz77Error("internal error: the data does not unpack to its input")
    return result


def items(packed):
    """The header's size and the items of BIOS-format data, as (offset in the
    unpacked data, length, distance): a literal has distance None (length 1).
    Raises Lz77Error on a header that isn't type 0x10 or data that ends
    early. Doesn't check the distances (decompress() does)."""
    packed = bytes(packed)
    if len(packed) < 4:
        raise Lz77Error("shorter than its header word")
    header = int.from_bytes(packed[:4], "little")
    if header & 0xFF != HEADER_TYPE:
        raise Lz77Error(f"header 0x{header:08x}: not LZ77 (0x10 in the low byte)")
    size, at, out, found = header >> 8, 4, 0, []
    while out < size:
        if at >= len(packed):
            raise Lz77Error(f"the data ends after {out} of {size} bytes")
        flags = packed[at]
        at += 1
        for bit in range(8):
            if out >= size:
                break
            if flags & 0x80 >> bit:
                if at + 2 > len(packed):
                    raise Lz77Error(f"the data ends after {out} of {size} bytes")
                length = (packed[at] >> 4) + MIN_MATCH
                distance = ((packed[at] & 0x0F) << 8 | packed[at + 1]) + 1
                at += 2
                found.append((out, length, distance))
                out += length
            else:
                if at >= len(packed):
                    raise Lz77Error(f"the data ends after {out} of {size} bytes")
                found.append((out, 1, None))
                at += 1
                out += 1
    return size, found


def decompress(packed):
    """The bytes BIOS-format data unpacks to. Raises Lz77Error unless every
    match is VRAM-safe (a distance of 2 or more), copies from what is already
    unpacked, and ends by the header's size: what the engine's loaders accept
    (src/core/lz77.c)."""
    packed = bytes(packed)
    size, _ = items(packed)  # the header, and that the data doesn't end early
    out, at = bytearray(), 4
    while len(out) < size:
        flags = packed[at]
        at += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & 0x80 >> bit:
                length = (packed[at] >> 4) + MIN_MATCH
                distance = ((packed[at] & 0x0F) << 8 | packed[at + 1]) + 1
                at += 2
                where = len(out)
                if distance < MIN_DISTANCE:
                    raise Lz77Error(f"byte {where}: a match at distance 1, which isn't VRAM-safe")
                if distance > where:
                    raise Lz77Error(f"byte {where}: a match {distance} bytes back, before the "
                                    "start")
                if where + length > size:
                    raise Lz77Error(f"byte {where}: a match of {length} bytes runs past the "
                                    f"size, {size}")
                for _ in range(length):
                    out.append(out[-distance])
            else:
                out.append(packed[at])
                at += 1
    return bytes(out)


def unpack_vram(packed, memory, dst=0):
    """What the BIOS's 16-bit decoder (SWI 0x12) leaves in `memory` (a
    bytearray, changed in place) unpacking `packed` at offset dst (even), bad
    data included: bytes go out a halfword at a time, a byte at an even
    offset waiting for the next, and matches read memory, so a match from the
    waiting byte reads the old one; a match runs to its end even past the
    size; a last byte at an even offset is never written. As mGBA's BIOS
    (src/gba/bios.c, _unLz77) and src/web/lz77.c do. A reference for tests."""
    packed = bytes(packed)
    header = int.from_bytes(packed[:4], "little")
    remaining, at, dest, pending = header >> 8, 4, dst, 0

    def put(byte):
        nonlocal dest, pending
        if dest & 1:
            memory[dest - 1] = pending
            memory[dest] = byte
        else:
            pending = byte
        dest += 1

    while remaining > 0:
        flags = packed[at]
        at += 1
        for bit in range(8):
            if remaining <= 0:
                break
            if flags & 0x80 >> bit:
                length = (packed[at] >> 4) + MIN_MATCH
                source = dest - (((packed[at] & 0x0F) << 8 | packed[at + 1]) + 1)
                at += 2
                for _ in range(length):
                    remaining -= 1 if remaining else 0
                    put(memory[source])
                    source += 1
            else:
                put(packed[at])
                at += 1
                remaining -= 1
    return memory


_WORD = re.compile(r"(0[xX][0-9A-Fa-f]+|0|[1-9][0-9]*)[uU]?")  # no octal


def parse_words(text, name="<input>"):
    """The bytes of 32-bit words written as C integer literals separated by
    commas (comments allowed), little-endian."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", " ", text)
    tokens = [t.strip() for t in text.split(",")]
    if tokens and not tokens[-1]:
        tokens.pop()  # a trailing comma
    out = bytearray()
    for n, token in enumerate(tokens):
        m = _WORD.fullmatch(token)
        if not m:
            raise Lz77Error(f"{name}: word {n + 1}: {token!r} is not a hex or decimal integer"
                            if token else f"{name}: word {n + 1} is missing")
        value = int(m[1], 0)
        if value > 0xFFFFFFFF:
            raise Lz77Error(f"{name}: word {n + 1}: {token} doesn't fit 32 bits")
        out += value.to_bytes(4, "little")
    return bytes(out)


def read_input(path):
    """A file's bytes: raw (.bin) or words written as C literals."""
    if pathlib.PurePath(path).suffix.lower() == ".bin":
        with open(path, "rb") as f:
            return f.read()
    with open(path, encoding="utf-8") as f:
        return parse_words(f.read(), str(path))


def array_name(path):
    """<file name up to its first dot>_lz77, which must be a C identifier."""
    stem = pathlib.PurePath(path).name.split(".")[0]
    name = f"{stem}_lz77"
    if not re.fullmatch(r"[A-Za-z_]\w*", name):
        raise Lz77Error(f"{path}: {name} is not a C identifier; rename the file")
    return name


def write_c(name, out_dir, paths):
    """NAME.c and NAME.h in out_dir, one array per input (serval_add_lz77)."""
    if not re.fullmatch(r"[A-Za-z_]\w*", name):
        raise Lz77Error(f"the name {name!r} is not a C identifier")
    arrays, seen = [], {}
    for path in paths:
        array = array_name(path)
        if array in seen:
            raise Lz77Error(f"{path} and {seen[array]} both give the array {array}")
        seen[array] = path
        data = read_input(path)
        packed = compress(data)
        arrays.append((array, pathlib.PurePath(path).name, len(data), packed))
    guard = f"{name.upper()}_H"
    header = [f"// Generated by tools/lz77.py (serval_add_lz77()): LZ77 data in the GBA BIOS's",
              "// format, VRAM-safe, for SPRITE_ASSET_LZ77 and TILESET_LZ77",
              "// (docs/sprites.md#lz77-compression). Don't edit.",
              f"#ifndef {guard}", f"#define {guard}", "", '#include "serval/platform.h"', ""]
    source = header[:3] + ["", f'#include "{name}.h"', ""]
    for array, file, size, packed in arrays:
        words = [int.from_bytes(packed[k:k + 4], "little") for k in range(0, len(packed), 4)]
        note = f"// {file}: {size} bytes unpacked ({size / 32:g} tiles), {len(packed)} packed."
        header += [note, f"extern const u32 {array}[{len(words)}];", ""]
        source += [note, f"const u32 {array}[{len(words)}] = {{"]
        for k in range(0, len(words), 8):
            source.append("    " + " ".join(f"0x{w:08X}," for w in words[k:k + 8]))
        source += ["};", ""]
    header += [f"#endif // {guard}", ""]
    os.makedirs(out_dir, exist_ok=True)
    for ext, lines in ((".h", header), (".c", source)):
        target = os.path.join(out_dir, name + ext)
        text = "\n".join(lines).rstrip("\n") + "\n"
        try:
            with open(target, encoding="utf-8") as f:
                if f.read() == text:
                    continue  # unchanged: keep its time, so nothing rebuilds
        except OSError:
            pass
        with open(target, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)


def main(argv=None):
    parser = argparse.ArgumentParser(description="LZ77 in the GBA BIOS's format, VRAM-safe.")
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("compress", "decompress"):
        p = sub.add_parser(command)
        p.add_argument("input")
        p.add_argument("output")
    p = sub.add_parser("c", help="NAME.c and NAME.h for serval_add_lz77()")
    p.add_argument("--name", required=True)
    p.add_argument("--out-dir", required=True)
    p.add_argument("files", nargs="+")
    args = parser.parse_args(argv)
    try:
        if args.command == "c":
            write_c(args.name, args.out_dir, args.files)
            return 0
        with open(args.input, "rb") as f:
            data = f.read()
        result = compress(data) if args.command == "compress" else decompress(data)
        with open(args.output, "wb") as f:
            f.write(result)
    except (Lz77Error, OSError) as e:
        print(f"lz77: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
