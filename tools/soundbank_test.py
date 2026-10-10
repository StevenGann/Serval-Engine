#!/usr/bin/env python3
"""Tests for tools/soundbank.py, run by CTest in the host build
(soundbank_tool).

The names mmutil gives files, and what the build refuses before mmutil runs
(a sample named "none", whose SFX_NONE would clash with audio.h's; two files
giving one define; another kind of file) and after (a module's sample named
"#none", a define twice). With mmutil (SERVAL_MMUTIL, at serval.json's
version), also a whole bank: its C source and header, and the version check.
Run directly: python3 tools/soundbank_test.py
"""

import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import soundbank  # noqa: E402

with open(os.path.join(TOOLS, "..", "serval.json"), encoding="utf-8") as f:
    MMUTIL_VERSION = json.load(f)["toolchain"]["mmutil"]
MMUTIL = os.environ.get("SERVAL_MMUTIL", "")


class Names(unittest.TestCase):
    def test_mmutil_names(self):
        for path, name in [("music/theme.mod", "THEME"), ("Boss Fight.xm", "BOSS_FIGHT"),
                           ("a-b.wav", "A_B"), ("jump.v2.wav", "JUMP"), ("x@y[z]{w}.it", "X_Y_Z__W_"),
                           ("dir/sub/coin.WAV", "COIN"), ("café.wav", "CAF__")]:
            with self.subTest(path=path):
                self.assertEqual(soundbank.mmutil_name(path), name)

    def test_the_defines(self):
        self.assertEqual(soundbank.check_inputs(["a.mod", "b.S3M", "c.wav"]),
                         {"MOD_A": "a.mod", "MOD_B": "b.S3M", "SFX_C": "c.wav"})


class Refused(unittest.TestCase):
    def refused(self, files, pattern):
        with self.assertRaisesRegex(soundbank.BankError, pattern):
            soundbank.check_inputs(files)

    def test_a_sample_named_none(self):
        for name in ("none.wav", "None.wav", "sfx/NONE.WAV", "none.v1.wav"):
            with self.subTest(name=name):
                self.refused(["song.mod", name], r"can't be named \"none\": its SFX_NONE")
        # A module named none is MOD_NONE: no clash.
        soundbank.check_inputs(["none.mod"])

    def test_two_files_one_define(self):
        self.refused(["a-b.wav", "a_b.wav"], r"a_b\.wav and a-b\.wav both give SFX_A_B")
        self.refused(["x.mod", "sub/x.it"], r"both give MOD_X")
        soundbank.check_inputs(["x.mod", "x.wav"])  # MOD_X and SFX_X

    def test_other_kinds_of_file(self):
        for name in ("song.mid", "noise.ogg", "readme"):
            with self.subTest(name=name):
                self.refused([name], r"not a module \(.mod, .s3m, .xm, .it\) or a WAV")
        self.refused([".hidden.wav"], r"gives mmutil no define")

    def test_mmutils_header(self):
        text = "#define MOD_A    0\r\n#define SFX_B    3\r\n#define MSL_NSONGS    1\r\n"
        self.assertEqual(soundbank.read_defines(text, "h"),
                         [("MOD_A", 0), ("SFX_B", 3), ("MSL_NSONGS", 1)])
        with self.assertRaisesRegex(soundbank.BankError, r"sample is named \"#none\""):
            soundbank.read_defines("#define SFX_NONE    2\r\n", "h")
        with self.assertRaisesRegex(soundbank.BankError, r"defined SFX_B twice"):
            soundbank.read_defines("#define SFX_B 1\n#define SFX_B 2\n", "h")


@unittest.skipUnless(MMUTIL and os.path.exists(MMUTIL), "SERVAL_MMUTIL names no mmutil")
class Bank(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)
        subprocess.run([sys.executable, os.path.join(TOOLS, "make-example-audio.py"), "tests",
                        str(self.dir / "in")], check=True)

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, *files, version=MMUTIL_VERSION):
        return subprocess.run(
            [sys.executable, os.path.join(TOOLS, "soundbank.py"), "--mmutil", MMUTIL,
             "--version", version, "--name", "bank", "--out-dir", str(self.dir / "out"),
             *[str(self.dir / "in" / f) for f in files]], capture_output=True, text=True)

    def test_a_bank(self):
        r = self.build("tone.mod", "beep.wav", "hum.wav")
        self.assertEqual((r.returncode, r.stderr), (0, ""))
        header = (self.dir / "out" / "bank.h").read_text()
        source = (self.dir / "out" / "bank.c").read_text()
        # tone.mod's samples come first: its square wave and an empty slot.
        for line in ("#define MOD_TONE 0", "#define SFX_BEEP 2", "#define SFX_HUM 3",
                     "#define MSL_NSONGS 1", "#define MSL_NSAMPS 4", "#define MSL_BANKSIZE 5"):
            self.assertIn(line + "\n", header)
        self.assertNotIn("\r", header)
        size = int(header.split("extern const unsigned char bank[")[1].split("]")[0])
        self.assertIn(f"const unsigned char bank[{size}] __attribute__((aligned(4))) = {{", source)
        self.assertEqual(source.count("0x"), size)
        # The bank's mark, after the two counts: "*maxmod*".
        data = bytes(int(b, 16) for b in re.findall(r"0x([0-9A-F]{2})", source))
        self.assertEqual(data[4:12], b"*maxmod*")
        # No temporary files of mmutil's left behind.
        self.assertEqual(sorted(p.name for p in (self.dir / "out").iterdir()), ["bank.c", "bank.h"])

    def test_another_version_is_refused(self):
        r = self.build("beep.wav", version="0.0.1-blocks")
        self.assertEqual(r.returncode, 1)
        self.assertIn(f"says 'mmutil v{MMUTIL_VERSION}', not 'mmutil v0.0.1-blocks'", r.stderr)

    def test_none_is_refused_before_mmutil_runs(self):
        os.rename(self.dir / "in" / "beep.wav", self.dir / "in" / "none.wav")
        r = self.build("none.wav")
        self.assertEqual(r.returncode, 1)
        self.assertIn("can't be named \"none\"", r.stderr)
        self.assertFalse((self.dir / "out").exists())


if __name__ == "__main__":
    unittest.main()
