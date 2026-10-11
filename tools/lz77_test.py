#!/usr/bin/env python3
"""Tests for tools/lz77.py, run by CTest in the host build (lz77_tool).

Round trips (random data of every kind, tile-like data, the examples' and
tests' tile files), VRAM safety (no match at distance 1, and what the BIOS's
16-bit decoder leaves in memory that held garbage is the input), the header,
known BIOS-format vectors, worst cases (incompressible data, runs that invite
distance 1, the longest matches and distances), empty and tiny inputs, the
fewest bits (never more than a greedy parse), the C files serval_add_lz77()
gets, and what the tool refuses. Run directly: python3 tools/lz77_test.py
"""

import glob
import io
import os
import random
import re
import sys
import tempfile
import unittest
from contextlib import redirect_stderr

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lz77  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def header(size):
    return (0x10 | size << 8).to_bytes(4, "little")


def tile_like(rng, tiles):
    """4bpp 8x8 tiles as art tends to be: transparent margins, a few colors,
    rows repeated."""
    out = bytearray()
    for _ in range(tiles):
        colors = [0] + [rng.randrange(1, 16) for _ in range(3)]
        row = None
        for _ in range(8):
            if row is None or rng.random() < 0.6:
                row = bytes(rng.choice(colors) | rng.choice(colors) << 4 for _ in range(4))
            out += row
    return bytes(out)


def greedy_size(data):
    """The size a greedy parse of the same matches gives (each position's
    longest match taken if it has one)."""
    matches = lz77._longest_matches(data)
    items, i = 0, 0
    extra = 0
    while i < len(data):
        length, _ = matches[i]
        if length:
            extra += 2
            i += length
        else:
            extra += 1
            i += 1
        items += 1
    size = 4 + extra + (items + 7) // 8
    return size + -size % 4


class RoundTrips(unittest.TestCase):
    def check(self, data):
        packed = lz77.compress(data)
        self.assertEqual(lz77.decompress(packed), data)
        self.assertEqual(len(packed) % 4, 0)
        self.assertEqual(packed[:4], header(len(data)))
        # As the BIOS's 16-bit decoder writes it, over memory full of garbage
        # (the bytes a VRAM-unsafe match would read instead).
        memory = bytearray(b"\xEE" * (len(data) + 64))
        lz77.unpack_vram(packed, memory, 0)
        self.assertEqual(bytes(memory[:len(data) - len(data) % 2]),
                         data[:len(data) - len(data) % 2])
        _, found = lz77.items(packed)
        self.assertTrue(all(d is None or d >= 2 for _, _, d in found), "a match at distance 1")
        self.assertEqual(lz77.compress(data), packed)  # deterministic
        return packed

    def test_random_data(self):
        rng = random.Random(1)
        for alphabet in (1, 2, 3, 4, 16, 256):
            for size in (5, 31, 64, 255, 1000, 4500):
                with self.subTest(alphabet=alphabet, size=size):
                    self.check(bytes(rng.randrange(alphabet) for _ in range(size)))

    def test_tile_like_data(self):
        rng = random.Random(2)
        for tiles in (1, 4, 37, 300):
            with self.subTest(tiles=tiles):
                data = tile_like(rng, tiles)
                packed = self.check(data)
                if tiles > 4:
                    self.assertLess(len(packed), len(data))

    def test_the_repository_tile_files(self):
        files = glob.glob(os.path.join(ROOT, "examples", "*", "*.inc"))
        files += glob.glob(os.path.join(ROOT, "tests", "lz77", "*.inc"))
        self.assertTrue(files)
        for path in files:
            with self.subTest(path=os.path.relpath(path, ROOT)):
                data = lz77.read_input(path)
                self.assertEqual(len(data) % 32, 0, "whole tiles")
                packed = self.check(data)
                if len(data) > 1024:  # real art: big savings
                    self.assertLess(len(packed), len(data) * 0.5)

    def test_empty_and_tiny_inputs(self):
        self.assertEqual(lz77.compress(b""), header(0))
        self.assertEqual(lz77.decompress(header(0)), b"")
        self.assertEqual(lz77.compress(b"A"), header(1) + b"\x00A\x00\x00")
        self.assertEqual(lz77.compress(b"AB"), header(2) + b"\x00AB\x00")
        self.assertEqual(lz77.compress(b"ABC"), header(3) + b"\x00ABC")
        # Four of a byte: two literals, then nothing at distance 2 long enough.
        self.assertEqual(lz77.compress(b"AAAA"), header(4) + b"\x00AAAA\x00\x00\x00")
        for n in range(0, 40):
            with self.subTest(n=n):
                self.check(bytes((k * 7) % 3 for k in range(n)))

    def test_a_run_never_uses_distance_1(self):
        """A run is where a plain encoder takes distance 1; here every match
        is two back: a literal pair, then 18 bytes at a time."""
        data = b"\xAA" * 1000
        packed = self.check(data)
        _, found = lz77.items(packed)
        self.assertEqual(found[:3], [(0, 1, None), (1, 1, None), (2, 18, 2)])
        self.assertTrue(all(d == 2 for _, _, d in found[2:]))
        self.assertLessEqual(len(packed), 4 + 1 + 2 + 2 * 56 + 7 + 3)

    def test_worst_case_incompressible(self):
        rng = random.Random(3)
        for size in (1, 7, 8, 9, 4096, 20000):
            data = bytes(rng.randrange(256) for _ in range(size))
            packed = self.check(data)
            # A flag byte per 8 literals, the header, the padding: no more.
            bound = 4 + size + (size + 7) // 8
            self.assertLessEqual(len(packed), bound + -bound % 4)

    def test_longest_match_and_farthest_distance(self):
        rng = random.Random(4)
        block = bytes(rng.randrange(256) for _ in range(18))
        filler = bytes(rng.randrange(256) for _ in range(4096 - 18))
        data = block + filler + block  # the second copy is exactly 4,096 back
        packed = self.check(data)
        _, found = lz77.items(packed)
        self.assertEqual(found[-1], (4096, 18, 4096))
        # One byte further and it is out of reach.
        data = block + filler + b"\x00" + block
        _, found = lz77.items(self.check(data))
        self.assertNotIn(4097, [d for _, _, d in found])

    def test_fewest_bits(self):
        rng = random.Random(5)
        for k in range(20):
            data = tile_like(rng, 20) if k % 2 else bytes(rng.randrange(3) for _ in range(800))
            with self.subTest(k=k):
                self.assertLessEqual(len(lz77.compress(data)), greedy_size(data))
        # Where greedy loses: a short match that hides a longer one just after.
        data = b"xyzABCDEFGHIJ" + b"..xyzA" + b"BCDEFGHIJ"
        self.assertLessEqual(len(lz77.compress(data)), greedy_size(data))

    def test_size_limit(self):
        self.assertEqual(lz77.MAX_SIZE, 0xFFFFFF)  # bits 8-31 of the header
        saved, lz77.MAX_SIZE = lz77.MAX_SIZE, 10
        try:
            self.check(b"0123456789")
            with self.assertRaisesRegex(lz77.Lz77Error, "11 bytes; the format holds at most 10"):
                lz77.compress(b"0123456789A")
        finally:
            lz77.MAX_SIZE = saved


class KnownVectors(unittest.TestCase):
    """BIOS-format data written by hand, from the format's description."""

    def test_distance_1_reads_the_old_byte(self):
        """A literal 'a', then a match of 3 from 1 back: "aaaa" to a plain
        decoder; not VRAM-safe, so refused. The 16-bit decoder, unpacking
        over "0123", reads memory: byte 0 waits for byte 1, so byte 1 copies
        the old '0'; byte 2 copies byte 1, written with it; byte 3 copies
        byte 2, waiting, so the old '2'."""
        data = header(4) + bytes([0b01000000, ord("a"), 0x00, 0x00]) + b"\x00" * 4
        with self.assertRaisesRegex(lz77.Lz77Error, "byte 1: a match at distance 1"):
            lz77.decompress(data)
        self.assertEqual(bytes(lz77.unpack_vram(data, bytearray(b"0123"))), b"a002")

    def test_distance_2_repeats_a_pair(self):
        data = header(8) + bytes([0b00100000]) + b"ab" + bytes([(6 - 3) << 4, 0x01]) + b"\x00"
        self.assertEqual(lz77.decompress(data), b"abababab")
        memory = bytearray(8)
        self.assertEqual(bytes(lz77.unpack_vram(data, memory)), b"abababab")

    def test_two_blocks(self):
        # Nine literals: the second block's flag byte after the eighth.
        data = header(9) + b"\x00" + b"12345678" + b"\x00" + b"9" + b"\x00\x00"
        self.assertEqual(lz77.decompress(data), b"123456789")

    def test_a_match_past_the_size(self):
        data = header(4) + bytes([0b00100000]) + b"ab" + bytes([0x00, 0x01]) + b"\x00\x00"
        with self.assertRaisesRegex(lz77.Lz77Error, "byte 2: a match of 3 bytes runs past"):
            lz77.decompress(data)
        # The BIOS finishes the match: a fifth byte (waiting, never written).
        memory = bytearray(b"......")
        self.assertEqual(bytes(lz77.unpack_vram(data, memory)), b"abab..")

    def test_a_match_before_the_start(self):
        data = header(6) + bytes([0b10000000, 0x00, 0x01]) + b"\x00" * 3
        with self.assertRaisesRegex(lz77.Lz77Error, "byte 0: a match 2 bytes back, before"):
            lz77.decompress(data)

    def test_bad_headers(self):
        with self.assertRaisesRegex(lz77.Lz77Error, "not LZ77"):
            lz77.decompress(b"\x11\x04\x00\x00\x00abcd")
        with self.assertRaisesRegex(lz77.Lz77Error, "shorter than its header"):
            lz77.decompress(b"\x10\x00")
        with self.assertRaisesRegex(lz77.Lz77Error, "ends after 2 of 8 bytes"):
            lz77.decompress(header(8) + b"\x00ab")


class Files(unittest.TestCase):
    def test_words(self):
        self.assertEqual(lz77.parse_words("0x04030201, 5u, // c\n /* x, y */ 0XFFFFFFFFU,\n"),
                         b"\x01\x02\x03\x04\x05\x00\x00\x00\xff\xff\xff\xff")
        self.assertEqual(lz77.parse_words(""), b"")
        for text, message in (("0x100000000", "doesn't fit 32 bits"),
                              ("1, two", "'two' is not a hex or decimal integer"),
                              ("010", "'010' is not"), ("1,,2", "word 2 is missing"),
                              ("-1", "'-1' is not")):
            with self.subTest(text=text):
                with self.assertRaisesRegex(lz77.Lz77Error, message):
                    lz77.parse_words(text, "t.inc")

    def test_c_files(self):
        rng = random.Random(6)
        tiles = tile_like(rng, 9)
        with tempfile.TemporaryDirectory() as d:
            inc = os.path.join(d, "forest_tiles.inc")
            with open(inc, "w") as f:
                f.write("// a comment\n" + ",\n".join(
                    f"0x{int.from_bytes(tiles[k:k + 4], 'little'):08X}"
                    for k in range(0, len(tiles), 4)) + ",\n")
            raw = os.path.join(d, "boss.frames.bin")
            with open(raw, "wb") as f:
                f.write(tiles[:64])
            out = os.path.join(d, "out")
            self.assertEqual(lz77.main(["c", "--name", "level_lz77", "--out-dir", out, inc, raw]),
                             0)
            with open(os.path.join(out, "level_lz77.c")) as f:
                source = f.read()
            with open(os.path.join(out, "level_lz77.h")) as f:
                decls = f.read()
            self.assertIn('#include "level_lz77.h"', source)
            self.assertIn('#include "serval/platform.h"', decls)
            for name, data in (("forest_tiles_lz77", tiles), ("boss_lz77", tiles[:64])):
                m = re.search(rf"const u32 {name}\[(\d+)\] = \{{(.*?)\}};", source, re.DOTALL)
                self.assertIsNotNone(m, name)
                packed = lz77.parse_words(m[2])
                self.assertEqual(len(packed), int(m[1]) * 4)
                self.assertEqual(lz77.decompress(packed), data)
                self.assertIn(f"extern const u32 {name}[{m[1]}];", decls)
            # Unchanged output keeps its time stamp.
            before = os.stat(os.path.join(out, "level_lz77.c")).st_mtime_ns
            lz77.main(["c", "--name", "level_lz77", "--out-dir", out, inc, raw])
            self.assertEqual(os.stat(os.path.join(out, "level_lz77.c")).st_mtime_ns, before)

    def test_c_refusals(self):
        with tempfile.TemporaryDirectory() as d:
            a = os.path.join(d, "a.inc")
            b = os.path.join(d, "a.bin")
            bad = os.path.join(d, "9lives.inc")
            for path in (a, b, bad):
                with open(path, "w") as f:
                    f.write("1")
            for args, message in ((["--name", "x-y", a], "not a C identifier"),
                                  (["--name", "x", a, b], "both give the array a_lz77"),
                                  (["--name", "x", bad], "9lives_lz77 is not a C identifier"),
                                  (["--name", "x", os.path.join(d, "none.inc")], "none.inc")):
                with self.subTest(args=args):
                    err = io.StringIO()
                    with redirect_stderr(err):
                        code = lz77.main(["c", "--out-dir", d] + args)
                    self.assertEqual(code, 1)
                    self.assertIn(message, err.getvalue())

    def test_compress_and_decompress_files(self):
        with tempfile.TemporaryDirectory() as d:
            src, packed, back = (os.path.join(d, n) for n in ("in", "packed", "back"))
            data = tile_like(random.Random(7), 5)
            with open(src, "wb") as f:
                f.write(data)
            self.assertEqual(lz77.main(["compress", src, packed]), 0)
            self.assertEqual(lz77.main(["decompress", packed, back]), 0)
            with open(back, "rb") as f:
                self.assertEqual(f.read(), data)
            with open(packed, "wb") as f:
                f.write(header(4) + bytes([0b01000000]) + b"a\x00\x00")
            err = io.StringIO()
            with redirect_stderr(err):
                self.assertEqual(lz77.main(["decompress", packed, back]), 1)
            self.assertIn("distance 1", err.getvalue())


if __name__ == "__main__":
    unittest.main()
