#!/usr/bin/env python3
"""Tests for tools/gbafix.py, run by CTest in the host build (gbafix_tool).

The header it writes (title, game code, maker code, fixed value, checksum,
padding) and every value it refuses: the title is 1 to 12 printable ASCII
characters, the game code exactly 4 and the maker code exactly 2, and a
refused value leaves the ROM as it was. Run directly:
python3 tools/gbafix_test.py
"""

import io
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gbafix  # noqa: E402

PRINTABLE = "".join(chr(c) for c in range(0x20, 0x7F))
# A ROM as objcopy leaves it: code, then the header area, zero.
ROM = bytes(range(256)) * 4


def chunks(text, size):
    return [text[i:i + size] for i in range(0, len(text), size)]


class Fix(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.rom = os.path.join(self.tmp.name, "game.gba")

    def tearDown(self):
        self.tmp.cleanup()

    def run_fix(self, *args, rom=ROM):
        """gbafix.main(args) on a new ROM file holding rom: (exit status,
        stderr, the ROM after)."""
        with open(self.rom, "wb") as f:
            f.write(rom)
        err = io.StringIO()
        with redirect_stderr(err):
            status = gbafix.main([self.rom, *args])
        with open(self.rom, "rb") as f:
            return status, err.getvalue(), f.read()

    def header(self, rom):
        return {"title": rom[0xA0:0xAC], "game code": rom[0xAC:0xB0], "maker code": rom[0xB0:0xB2]}

    def assert_fixed(self, rom, title, game_code, maker_code=b"00"):
        self.assertEqual(self.header(rom), {"title": title.ljust(12, b"\0"), "game code": game_code,
                                            "maker code": maker_code})
        self.assertEqual(rom[0xB2], 0x96)
        self.assertEqual(rom[0xBD], (-(sum(rom[0xA0:0xBD]) + 0x19)) & 0xFF)
        self.assertEqual(len(rom), 512 * 1024)
        self.assertEqual(rom[len(ROM):], b"\xff" * (512 * 1024 - len(ROM)))
        # Nothing else changed.
        self.assertEqual(rom[:0xA0], ROM[:0xA0])
        self.assertEqual(rom[0xB3:0xBD], ROM[0xB3:0xBD])
        self.assertEqual(rom[0xBE:len(ROM)], ROM[0xBE:])

    def assert_refused(self, args, message):
        status, err, rom = self.run_fix(*args)
        self.assertEqual(status, 1, err)
        self.assertIn(message, err)
        self.assertEqual(rom, ROM, "a refused value must leave the ROM as it was")

    def test_header(self):
        status, err, rom = self.run_fix("--title", "MY GAME", "--game-code", "MYGM")
        self.assertEqual((status, err), (0, ""))
        self.assert_fixed(rom, b"MY GAME", b"MYGM")

    def test_defaults(self):
        """No --title: an empty title field. Game code 0000, maker code 00."""
        status, err, rom = self.run_fix()
        self.assertEqual((status, err), (0, ""))
        self.assert_fixed(rom, b"", b"0000")

    def test_lengths_that_fit(self):
        for title in ("A", "TWELVE CHARS"):
            with self.subTest(title=title):
                status, err, rom = self.run_fix("--title", title, "--maker-code", "01")
                self.assertEqual((status, err), (0, ""))
                self.assert_fixed(rom, title.encode(), b"0000", b"01")

    def test_every_printable_character(self):
        """Each, as text and as hexadecimal codes, lands in the header as is."""
        for title in chunks(PRINTABLE, 12) + ["  SPACES  ", " ", "-X", "$<1:X>${A};"]:
            forms = [[f"--title={title}"], ["--title-hex", title.encode().hex()]]
            if not title.startswith("-"):  # else argparse takes it for an option
                forms.append(["--title", title])
            for args in forms:
                with self.subTest(args=args):
                    status, err, rom = self.run_fix(*args)
                    self.assertEqual((status, err), (0, ""))
                    self.assert_fixed(rom, title.encode(), b"0000")
        for code in chunks(PRINTABLE, 4)[:-1] + [PRINTABLE[-3:] + " ", "    ", "-ABC"]:
            for args in ([f"--game-code={code}"], ["--game-code-hex", code.encode().hex().upper()]):
                with self.subTest(args=args):
                    status, err, rom = self.run_fix(*args)
                    self.assertEqual((status, err), (0, ""))
                    self.assert_fixed(rom, b"", code.encode())

    def test_hex_with_spaces(self):
        status, err, rom = self.run_fix("--title-hex", "48 49", "--game-code-hex", "41 42 43 44")
        self.assertEqual((status, err), (0, ""))
        self.assert_fixed(rom, b"HI", b"ABCD")

    def test_title_refused(self):
        cases = {
            "empty": (["--title", ""], "title is empty: give 1 to 12 printable ASCII characters, "
                                       "or leave --title out for an empty title field"),
            "empty, as hex": (["--title-hex", ""], "title is empty"),
            "13 characters": (["--title", "THIRTEEN CHRS"],
                              'title can have at most 12 characters; "THIRTEEN CHRS" has 13'),
            "13 as hex": (["--title-hex", b"THIRTEEN CHRS".hex()],
                          'title can have at most 12 characters; "THIRTEEN CHRS" has 13'),
            "a tab": (["--title", "A\tB"], "title can't contain a control character: use ASCII "
                                           "letters, digits, spaces and punctuation"),
            "a newline": (["--title", "A\nB"], "title can't contain a control character"),
            "DEL": (["--title", "A\x7fB"], "title can't contain a control character"),
            "NUL, as hex": (["--title-hex", "410042"], "title can't contain a control character"),
            "not ASCII": (["--title", "CAFÉ"], "title can't contain \"É\": use ASCII letters, "
                                               "digits, spaces and punctuation"),
            "not hexadecimal": (["--title-hex", "4G"],
                                '--title-hex "4G" is not hexadecimal character codes'),
            "half a byte": (["--title-hex", "414"],
                            '--title-hex "414" is not hexadecimal character codes'),
        }
        for name, (args, message) in cases.items():
            with self.subTest(name):
                self.assert_refused(args, message)

    def test_game_code_refused(self):
        """Exactly 4 characters: a shorter code is not padded."""
        cases = {
            "empty": (["--game-code", ""], 'game code must have exactly 4 characters; "" has 0'),
            "3 characters": (["--game-code", "ABC"],
                             'game code must have exactly 4 characters; "ABC" has 3'),
            "3, as hex": (["--game-code-hex", "414243"],
                          'game code must have exactly 4 characters; "ABC" has 3'),
            "5 characters": (["--game-code", "ABCDE"],
                             'game code must have exactly 4 characters; "ABCDE" has 5'),
            "a tab": (["--game-code", "AB\tC"], "game code can't contain a control character"),
            "not ASCII": (["--game-code", "AßCD"], "game code can't contain \"ß\""),
            "not hexadecimal": (["--game-code-hex", "zz"],
                                '--game-code-hex "zz" is not hexadecimal character codes'),
        }
        for name, (args, message) in cases.items():
            with self.subTest(name):
                self.assert_refused(["--title", "OK", *args], message)

    def test_maker_code_refused(self):
        for code, message in (("0", 'maker code must have exactly 2 characters; "0" has 1'),
                              ("012", 'maker code must have exactly 2 characters; "012" has 3'),
                              ("\t0", "maker code can't contain a control character")):
            with self.subTest(code=code):
                self.assert_refused([f"--maker-code={code}"], message)

    def test_text_and_hex_together(self):
        """Usage errors are argparse's (exit status 2)."""
        with open(self.rom, "wb") as f:
            f.write(ROM)
        for args in (["--title", "A", "--title-hex", "41"],
                     ["--game-code", "ABCD", "--game-code-hex", "41424344"]):
            with self.subTest(args=args):
                with redirect_stderr(io.StringIO()) as err, self.assertRaises(SystemExit) as e:
                    gbafix.main([self.rom, *args])
                self.assertEqual(e.exception.code, 2)
                self.assertIn("not allowed with argument", err.getvalue())

    def test_min_size(self):
        self.assert_refused(["--min-size", str(256 * 1024)], "--min-size must exceed")

    def test_too_small(self):
        status, err, rom = self.run_fix("--title", "A", rom=bytes(0xBF))
        self.assertEqual(status, 1)
        self.assertIn("too small to contain a ROM header", err)
        self.assertEqual(rom, bytes(0xBF))

    def test_command_line(self):
        """As a program, without a shell between: exit status and stderr."""
        tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gbafix.py")
        with open(self.rom, "wb") as f:
            f.write(ROM)
        title = "\"'\\$;#<>&{} "
        result = subprocess.run([sys.executable, tool, self.rom, "--title", title,
                                 "--game-code", "AB"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, 'game code must have exactly 4 characters; "AB" has 2\n')
        with open(self.rom, "rb") as f:
            self.assertEqual(f.read(), ROM)
        result = subprocess.run([sys.executable, tool, self.rom, "--title", title,
                                 "--game-code", "`$; "], capture_output=True, text=True)
        self.assertEqual((result.returncode, result.stderr), (0, ""))
        with open(self.rom, "rb") as f:
            self.assert_fixed(f.read(), title.encode(), b"`$; ")


if __name__ == "__main__":
    unittest.main()
