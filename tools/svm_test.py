#!/usr/bin/env python3
"""Tests for tools/svm.py, run by CTest in the host build (svm_tool).

Each case is written from docs/vm.md: the golden bytes, the operand layouts,
the rel16 rule, the layout rules. Run directly: python3 tools/svm_test.py
"""

import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svm  # noqa: E402

# docs/vm.md "Worked example (golden bytes)": the string bytes come before the
# code there.
GOLDEN = bytes.fromhex(
    "53564D42 01040000 01000100 01000000"  # header
    "00000000 00000000 37000000 00000000"  # object 0: mask 0, sprite 0, Create @ 0x37
    "00000000 00000000 00000000 00000000"
    "34000000 484900 02 05 02 07 10 09 00 01".replace(" ", ""))

GOLDEN_LISTING = """
.object THING mask=0 sprite=0
.string HI "HI"
.globals SUM
.handler THING CREATE
    PUSH8 5
    PUSH8 7
    ADD
    STG SUM
    HALT
"""

VM = svm.load_vm()


def asm(text, headers=None):
    return svm.assemble(text, VM, headers, "test.svm")


def le16(n):
    return n.to_bytes(2, "little")


def le32(n):
    return n.to_bytes(4, "little")


class Golden(unittest.TestCase):
    def test_listing_assembles_to_the_spec(self):
        """The same program laid out as this assembler does: strings after the
        code. Every field is checked against vm.md's blob format; only the two
        offsets differ from the golden bytes (code at 0x34, string at 0x3C)."""
        blob = asm(GOLDEN_LISTING).blob
        self.assertEqual(len(blob), 63)
        self.assertEqual(blob[0:4], b"SVMB")
        self.assertEqual(blob[4], 1)  # format version
        self.assertEqual(blob[5], 4)  # cell width
        self.assertEqual(blob[6:8], le16(0))  # flags
        self.assertEqual(blob[8:10], le16(1))  # objects
        self.assertEqual(blob[10:12], le16(1))  # strings
        self.assertEqual(blob[12:14], le16(1))  # globals
        self.assertEqual(blob[14:16], le16(0))  # arrays: none, so no array table
        self.assertEqual(blob[16:20], le32(0))  # mask
        self.assertEqual(blob[20:22], le16(0))  # sprite
        self.assertEqual(blob[22:24], le16(0))  # reserved
        self.assertEqual(blob[24:28], le32(0x34))  # Create: right after the tables
        self.assertEqual(blob[28:48], bytes(20))  # no other handler
        self.assertEqual(blob[48:52], le32(0x3C))  # string 0, after the code
        self.assertEqual(blob[0x34:0x3C], bytes.fromhex("0205020710090001"))
        self.assertEqual(blob[0x3C:], b"HI\0")
        # The golden bytes hold the same header, tables and code.
        self.assertEqual(blob[:24], GOLDEN[:24])
        self.assertEqual(blob[0x34:0x3C], GOLDEN[0x37:0x3F])

    def test_golden_bytes_round_trip_with_their_layout(self):
        listing = svm.disassemble(GOLDEN, VM)
        self.assertIn(".strings 0", listing)  # the string is placed before the code
        self.assertIn(".handler 0 CREATE", listing)
        self.assertEqual(asm(listing).blob, GOLDEN)

    def test_my_layout_round_trips_without_placing_strings(self):
        blob = asm(GOLDEN_LISTING).blob
        listing = svm.disassemble(blob, VM)
        self.assertNotIn(".strings", listing)
        self.assertEqual(asm(listing).blob, blob)


class PushWidth(unittest.TestCase):
    CASES = [
        (-128, "PUSH8", b"\x80"),
        (127, "PUSH8", b"\x7f"),
        (128, "PUSH16", b"\x80\x00"),
        (-129, "PUSH16", b"\x7f\xff"),
        (-32768, "PUSH16", b"\x00\x80"),
        (32767, "PUSH16", b"\xff\x7f"),
        (32768, "PUSH32", b"\x00\x80\x00\x00"),
        (-32769, "PUSH32", b"\xff\x7f\xff\xff"),
        (-2147483648, "PUSH32", b"\x00\x00\x00\x80"),
        (2147483647, "PUSH32", b"\xff\xff\xff\x7f"),
        (0xFFFFFFFF, "PUSH32", b"\xff\xff\xff\xff"),  # a u32 bit pattern: the cell -1
    ]

    def test_boundaries(self):
        for value, mnemonic, operand in self.CASES:
            with self.subTest(value=value):
                blob = asm(f".object X\n.handler X CREATE\nPUSH {value}\nHALT\n").blob
                code = blob[16 + 32:]
                self.assertEqual(code, bytes([VM.ops[mnemonic]]) + operand + b"\x01")

    def test_explicit_width_is_range_checked(self):
        for text in ("PUSH8 128", "PUSH8 -129", "PUSH16 32768", "PUSH16 -32769",
                     "PUSH32 4294967296", "PUSH32 -2147483649"):
            with self.subTest(text=text):
                with self.assertRaisesRegex(svm.SvmError,
                                            r"test\.svm:3: error: .*(fit|32-bit range)"):
                    asm(f".object X\n.handler X CREATE\n{text}\nHALT\n")

    def test_expressions(self):
        """C precedence, division toward zero, the engine's two macros."""
        cases = {
            "1 + 2 * 3": 7, "(1 + 2) * 3": 9, "1 << 4 + 1": 32, "0x10 | 1 << 1": 18,
            "6 & 3 | 8": 10, "~0": -1, "-7 / 2": -3, "7 / -2": -3, "FX(3) / 2": 384,
            "C_GAME(1)": 1 << 17, "1 << 31": 1 << 31, "-(1 + 1)": -2, "0x1Fu": 31,
        }
        for text, expected in cases.items():
            with self.subTest(text=text):
                self.assertEqual(svm.evaluate(text, lambda n: 0), expected)
        for text in ("1 / 0", "1 << 32 << 1", "4294967296", "-2147483649", "1 << -1",
                     "C_GAME(15)", "NOPE(1)", "1 +", "(1", "1 % 2", "a b"):
            with self.subTest(text=text):
                with self.assertRaises(svm.ExprError):
                    svm.evaluate(text, lambda n: 1)


class Jumps(unittest.TestCase):
    def test_forward_and_backward_rel16(self):
        """rel16 counts from the byte after the operand (vm.md)."""
        blob = asm("""
.object X
.handler X CREATE
top:
    JMP end     ; at code 0: its operand ends at 3, end is at 9: +6
    NOP
    NOP
    JZ top      ; at code 5: its operand ends at 8, top is at 0: -8
    NOP
end:
    HALT
""").blob
        code = blob[16 + 32:]
        self.assertEqual(code, bytes([VM.ops["JMP"], 6, 0, VM.ops["NOP"], VM.ops["NOP"],
                                      VM.ops["JZ"], 0xF8, 0xFF, VM.ops["NOP"], VM.ops["HALT"]]))

    def test_call_is_a_blob_offset(self):
        blob = asm("""
.object X
.string S "x"
.handler X CREATE
    CALL sub   ; code 0; sub is at code 6, the blob's 16 + 32 + 4 + 6
    HALT
sub:
    RET
""").blob
        code = blob[52:]
        self.assertEqual(code, bytes([VM.ops["CALL"]]) + le32(52 + 6) + bytes([1, VM.ops["RET"]])
                         + b"x\0")

    def test_jump_out_of_range(self):
        padding = "\n".join(".byte " + ", ".join(["0"] * 16) for _ in range(2050))  # 32,800 bytes
        with self.assertRaisesRegex(svm.SvmError, r"test\.svm:4: error: .*too far"):
            asm(f".object X\n.handler X CREATE\ntop:\nJMP end\n{padding}\nend:\nHALT\n")
        # Just inside: 32,767 from the byte after the operand.
        padding = "\n".join(".byte " + ", ".join(["0"] * 16) for _ in range(2047)) + "\n.byte " + \
            ", ".join(["0"] * 15)
        asm(f".object X\n.handler X CREATE\ntop:\nJMP end\n{padding}\nend:\nHALT\n")


class Errors(unittest.TestCase):
    """Every error exits 1 (through main) or raises SvmError naming the line."""

    OK = ".object X\n.handler X CREATE\nHALT\n"

    CASES = {
        "unknown mnemonic": (".object X\n.handler X CREATE\nPUSHY 1\nHALT\n", 3, "unknown mnemonic"),
        "unknown name": (".object X\n.handler X CREATE\nPUSH NOPE\nHALT\n", 3, "unknown name"),
        "operand to an op without one": (".object X\n.handler X CREATE\nHALT 1\n", 3, "no operand"),
        "missing operand": (".object X\n.handler X CREATE\nLDG\nHALT\n", 3, "needs an operand"),
        "number where a label goes": (".object X\n.handler X CREATE\nJMP 5\nHALT\n", 3, "label"),
        "label bound twice": (".object X\n.handler X CREATE\na:\na:\nHALT\n", 4, "already bound"),
        "label never bound": (".object X\n.handler X CREATE\nJMP a\nHALT\n", 3, "never bound"),
        "handler for a missing object": (".handler X CREATE\nHALT\n", 1, "no object"),
        "handler for a missing event": (".object X\n.handler X JUMP\nHALT\n", 2, "no event"),
        "two handlers for one event": (".object X\n.handler X CREATE\nHALT\n.handler X CREATE\nHALT\n",
                                       4, "already has a CREATE handler"),
        "handler with no code": (".object X\n.handler X CREATE\n", 2, "no code"),
        "too many globals": (".globals " + " ".join(f"g{i}" for i in range(257)) + "\n" + OK, 1,
                             "more than 256 globals"),
        "a NUL in a string": ('.string S "a\x00b"\n' + OK, 1, "not printable"),
        "a non-printable byte in a string": ('.string S "a\tb"\n' + OK, 1, "not printable"),
        "an unknown escape": ('.string S "a\\nb"\n' + OK, 1, "unknown escape"),
        "value too big for the operand": (".object X\n.handler X CREATE\nLDG 256\nHALT\n", 3, "fit"),
        "negative u16": (".object X\n.handler X CREATE\nSPAWN -1\nHALT\n", 3, "fit"),
        "spawn of an undeclared object": (".object X\n.handler X CREATE\nSPAWN 1\nHALT\n", 3,
                                          "no object"),
        "trace of an undeclared string": (".object X\n.handler X CREATE\nTRACE 0\nHALT\n", 3,
                                          "no string"),
        "name defined twice": (".const A 1\n.const A 2\n" + OK, 2, "already defined"),
        "object defined twice": (".object X\n.object X\n.handler X CREATE\nHALT\n", 2, "already defined"),
        "unknown directive": (".thing\n" + OK, 1, "unknown directive"),
        "numbered entry out of order": (".object 1\n" + OK, 1, "object 0, not 1"),
        "bad .byte": (".object X\n.handler X CREATE\n.byte 256\nHALT\n", 3, "fit a byte"),
        "string placed twice": (".object X\n.string S \"s\"\n.handler X CREATE\n.strings S\n.strings S\n"
                                "HALT\n", 5, "already placed"),
        "division by zero": (".const A 1 / 0\n" + OK, 1, "division by zero"),
        "ENTER with one operand": (".object X\n.handler X CREATE\nENTER 1\nHALT\n", 3,
                                   "ENTER takes 2 operands"),
        "ENTER with three operands": (".object X\n.handler X CREATE\nENTER 1, 2, 3\nHALT\n", 3,
                                      "ENTER takes 2 operands"),
        "ENTER with an empty operand": (".object X\n.handler X CREATE\nENTER 1,\nHALT\n", 3,
                                        "ENTER takes 2 operands"),
        "ENTER with none": (".object X\n.handler X CREATE\nENTER\nHALT\n", 3,
                            "ENTER needs 2 operands"),
        "ENTER past a u8": (".object X\n.handler X CREATE\nENTER 0, 256\nHALT\n", 3, "fit"),
        "an operand too many": (".object X\n.handler X CREATE\nLDG 1, 2\nHALT\n", 3,
                                "LDG takes 1 operand"),
        "array not declared": (".object X\n.handler X CREATE\nLDA 0\nHALT\n", 3, "no array"),
        "array name unknown": (".object X\n.handler X CREATE\nLEN NOPE\nHALT\n", 3,
                               "no array NOPE"),
        "NEXTI of an undeclared object": (".object X\n.handler X CREATE\nNEXTI 1\nHALT\n", 3,
                                          "no object"),
        ".array past the pool": (".array A 1000\n.array B 25\n" + OK, 2, "outside the RAM"),
        ".array at= past the pool": (".array A 1 at=1024\n" + OK, 1, "outside the RAM"),
        ".array longer than 16 bits": (".array A 65536\n" + OK, 1, "16 bits"),
        ".array without a length": (".array A\n" + OK, 1, ".array takes"),
        ".array defined twice": (".array A 1\n.array A 1\n" + OK, 2, "already defined"),
        ".rom of an unknown kind": (".rom A s64 1\n" + OK, 1, "no array kind s64"),
        ".rom value past s8": (".rom A s8 1, 128\n" + OK, 1, "128 doesn't fit s8"),
        ".rom value past u8": (".rom A u8 -1\n" + OK, 1, "-1 doesn't fit u8"),
        ".rom value past s16": (".rom A s16 -32769\n" + OK, 1, "doesn't fit s16"),
        ".rom value past u16": (".rom A u16 65536\n" + OK, 1, "doesn't fit u16"),
        ".rom value past 32 bits": (".rom A s32 0x100000000\n" + OK, 1, "32-bit range"),
        ".rom without a kind": (".rom A\n" + OK, 1, ".rom takes"),
        ".data of a RAM array": (".array A 1\n.object X\n.handler X CREATE\n.data A\nHALT\n", 4,
                                 "RAM array"),
        ".data placed twice": (".rom A u8 1\n.object X\n.handler X CREATE\n.data A\n.data A\n"
                               "HALT\n", 5, "already placed"),
    }

    def test_each_error_names_its_line(self):
        for name, (text, line, message) in self.CASES.items():
            with self.subTest(case=name):
                with self.assertRaises(svm.SvmError) as caught:
                    asm(text)
                self.assertRegex(str(caught.exception), rf"(^|\n)test\.svm:{line}: error: .*{message}")

    def test_too_many_objects(self):
        text = "".join(f".object o{i}\n" for i in range(65536)) + ".handler o0 CREATE\nHALT\n"
        with self.assertRaisesRegex(svm.SvmError, r"test\.svm:65536: error: more than 65535 objects"):
            asm(text)

    def test_all_errors_are_reported(self):
        with self.assertRaises(svm.SvmError) as caught:
            asm(".object X\n.handler X CREATE\nPUSHY 1\nLDG\nHALT\n")
        self.assertEqual([e[1] for e in caught.exception.errors], [3, 4])

    def test_handler_without_halt_warns(self):
        result = asm(".object X\n.handler X CREATE\nPUSH 1\n.handler X STEP\nHALT\n")
        self.assertEqual(len(result.warnings), 1)
        self.assertEqual(result.warnings[0][1], 2)
        self.assertIn("HALT, RET or RETV", result.warnings[0][2])
        self.assertEqual(asm(".object X\n.handler X CREATE\nJMP a\na:\nRET\n").warnings, [])
        self.assertEqual(asm(".object X\n.handler X CREATE\nPUSH 1\nRETV\n").warnings, [])

    def test_shared_code_is_one_handler_group(self):
        result = asm(".object X\n.handler X CREATE\n.handler X STEP\nHALT\n")
        self.assertEqual(result.warnings, [])
        self.assertEqual(result.blob[24:28], result.blob[28:32])  # Create and Step share an offset

    def test_cli_exits_1_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            listing = os.path.join(tmp, "bad.svm")
            with open(listing, "w") as f:
                f.write(".object X\n.handler X CREATE\nPUSH NOPE\nHALT\n")
            out = os.path.join(tmp, "out.bin")
            stderr = io.StringIO()
            with redirect_stderr(stderr):
                status = svm.main(["asm", listing, "-o", out, "--c", out + ".c", "--symbol", "s",
                                   "--defs", out + ".h"])
            self.assertEqual(status, 1)
            self.assertIn("bad.svm:3: error: unknown name NOPE", stderr.getvalue())
            self.assertEqual(os.listdir(tmp), ["bad.svm"])
            with redirect_stderr(stderr):
                self.assertEqual(svm.main(["asm", listing, "--c", out + ".c"]), 1)
            self.assertIn("--c needs --symbol", stderr.getvalue())

    def test_dis_refuses_a_bad_blob(self):
        good = asm(GOLDEN_LISTING).blob
        bad = {
            "magic": b"SVMX" + good[4:], "version": good[:4] + b"\x02" + good[5:],
            "cell width": good[:5] + b"\x02" + good[6:], "short": good[:10],
            "tables past the end": good[:8] + le16(9) + good[10:],
            "handler offset": good[:24] + le32(len(good)) + good[28:],
            "string offset": good[:48] + le32(5) + good[52:],
            "string without NUL": good[:-1], "globals": good[:12] + le16(257) + good[14:],
        }
        for name, blob in bad.items():
            with self.subTest(case=name):
                with self.assertRaises(svm.SvmError):
                    svm.disassemble(blob, VM)
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "bad.bin")
            with open(path, "wb") as f:
                f.write(bad["magic"])
            stderr = io.StringIO()
            with redirect_stderr(stderr):
                self.assertEqual(svm.main(["dis", path]), 1)
            self.assertIn("bad.bin: error: not a script blob", stderr.getvalue())


class Arrays(unittest.TestCase):
    """vm.md "Array table": the records, the RAM pool positions, the ROM data
    after the code and before the strings, and the names."""

    LISTING = """
.object X
.string S "hi"
.array FIRST 4                 ; pool cells 0-3
.rom LEVELS u8 1, 2, 255
.array SECOND 2                ; cells 4-5: after FIRST
.rom WAVES s32 -1, 0x7FFFFFFF
.array THIRD 3 at=100          ; cells 100-102
.array FOURTH 1                ; cell 103
.rom SIGNED s8 -128, 127
.rom WIDE s16 -2
.rom HALF u16 65535
.handler X CREATE
    PUSH 1
    LDA LEVELS
    LEN ARR_WAVES
    STA FOURTH
    HALT
"""

    def test_tables_and_layout(self):
        result = asm(self.LISTING)
        blob = result.blob
        self.assertEqual(blob[14:16], le16(9))  # the array count
        arrays_at = 16 + 32 + 4
        code_at = arrays_at + 9 * 8
        records = [blob[arrays_at + 8 * n:arrays_at + 8 * n + 8] for n in range(9)]
        code = bytes([VM.ops["PUSH8"], 1, VM.ops["LDA"], 1, 0, VM.ops["LEN"], 3, 0,
                      VM.ops["STA"], 5, 0, VM.ops["HALT"]])
        self.assertEqual(blob[code_at:code_at + len(code)], code)
        data_at = code_at + len(code)  # the ROM data, in array order, after the code
        self.assertEqual(records[0], le16(4) + bytes([0, 0]) + le32(0))
        self.assertEqual(records[1], le16(3) + bytes([2, 0]) + le32(data_at))
        self.assertEqual(records[2], le16(2) + bytes([0, 0]) + le32(4))
        self.assertEqual(records[3], le16(2) + bytes([5, 0]) + le32(data_at + 3))
        self.assertEqual(records[4], le16(3) + bytes([0, 0]) + le32(100))
        self.assertEqual(records[5], le16(1) + bytes([0, 0]) + le32(103))
        self.assertEqual(records[6], le16(2) + bytes([1, 0]) + le32(data_at + 11))
        self.assertEqual(records[7], le16(1) + bytes([3, 0]) + le32(data_at + 13))
        self.assertEqual(records[8], le16(1) + bytes([4, 0]) + le32(data_at + 15))
        self.assertEqual(blob[data_at:data_at + 17],
                         bytes([1, 2, 255]) + le32(0xFFFFFFFF) + le32(0x7FFFFFFF)
                         + bytes([0x80, 0x7F]) + le16(0xFFFE) + le16(0xFFFF))
        string_at = data_at + 17  # the strings after the data
        self.assertEqual(blob[16 + 32:16 + 32 + 4], le32(string_at))
        self.assertEqual(blob[string_at:], b"hi\0")
        self.assertEqual(result.arrays, ["FIRST", "LEVELS", "SECOND", "WAVES", "THIRD", "FOURTH",
                                         "SIGNED", "WIDE", "HALF"])

    def test_round_trip(self):
        blob = asm(self.LISTING).blob
        listing = svm.disassemble(blob, VM)
        self.assertNotIn(".data", listing)  # the default layout
        self.assertNotIn(".strings", listing)
        self.assertIn(".array 0 4\n.rom 1 u8 1, 2, 255\n.array 2 2\n.rom 3 s32 -1, 2147483647\n"
                      ".array 4 3 at=100\n.array 5 1\n.rom 6 s8 -128, 127\n.rom 7 s16 -2\n"
                      ".rom 8 u16 65535\n", listing)
        self.assertIn("    LDA 1\n    LEN 3\n    STA 5\n", listing)
        self.assertEqual(asm(listing).blob, blob)

    def test_placed_data_round_trips(self):
        """.data puts ROM data where it appears (here before the code, with a
        string between); the disassembler says so with .data lines."""
        blob = asm(".object X\n.string S \"s\"\n.rom A u8 9\n.rom B s16 1, 2\n"
                   ".rom EMPTY u8\n.array R 2 at=7\n"
                   ".data B\n.strings S\n.data EMPTY A\n"
                   ".handler X CREATE\nPUSH 0\nLDA B\nHALT\n").blob
        tables = 16 + 32 + 4 + 4 * 8
        self.assertEqual(blob[tables:tables + 4], le16(1) + le16(2))  # B's data first
        self.assertEqual(blob[tables + 4:tables + 6], b"s\0")
        self.assertEqual(blob[tables + 6], 9)  # A's, after the empty array's (no bytes)
        self.assertEqual(blob[16 + 32 + 4 + 16 + 4:16 + 32 + 4 + 16 + 8], le32(tables + 6))  # EMPTY
        self.assertEqual(blob[16 + 32 + 4 + 4:16 + 32 + 4 + 8], le32(tables + 6))  # A
        listing = svm.disassemble(blob, VM)
        self.assertIn("    .data 1\n    .strings 0\n    .data 2\n    .data 0\n", listing)
        self.assertIn(".rom 2 u8\n", listing)
        self.assertEqual(asm(listing).blob, blob)

    def test_defs_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            listing = os.path.join(tmp, "game.svm")
            with open(listing, "w") as f:
                f.write(self.LISTING)
            h = os.path.join(tmp, "out.h")
            with redirect_stdout(io.StringIO()):
                self.assertEqual(svm.main(["asm", listing, "--defs", h]), 0)
            with open(h) as f:
                header = f.read()
            for line in ("#define ARR_FIRST 0", "#define ARR_LEVELS 1", "#define ARR_HALF 8",
                         "#define ARR_COUNT 9"):
                self.assertIn(line, header)

    def test_names_in_expressions(self):
        blob = asm(".array A 1\n.rom B u8 1\n.object X\n.handler X CREATE\nPUSH ARR_B\n"
                   "LDA ARR_B - 1\nHALT\n").blob
        code = blob[16 + 32 + 16:]
        self.assertEqual(code[:5], bytes([VM.ops["PUSH8"], 1, VM.ops["LDA"], 0, 0]))

    def test_the_whole_pool(self):
        asm(f".array A {VM.array_cells}\n" + Errors.OK)  # just fits
        asm(f".array A 0 at={VM.array_cells}\n" + Errors.OK)  # empty, at the end
        with self.assertRaisesRegex(svm.SvmError, "outside the RAM"):
            asm(f".array A {VM.array_cells}\n.array B 1\n" + Errors.OK)

    def test_dis_refuses_bad_records(self):
        good = asm(".rom A u8 1, 2\n.array R 3\n" + Errors.OK).blob
        record = 16 + 32
        bad = {
            "kind": good[:record + 2] + b"\x06" + good[record + 3:],
            "reserved": good[:record + 3] + b"\x01" + good[record + 4:],
            "data past the end": good[:record] + le16(9) + good[record + 2:],
            "data in the tables": good[:record + 4] + le32(record) + good[record + 8:],
            "RAM past the pool": good[:record + 12] + le32(VM.array_cells - 2) + good[record + 16:],
            "flags": good[:6] + le16(1) + good[8:],
            "object reserved": good[:22] + le16(1) + good[24:],
        }
        for name, blob in bad.items():
            with self.subTest(case=name):
                with self.assertRaises(svm.SvmError):
                    svm.disassemble(blob, VM)
        svm.disassemble(good, VM)

    def test_dis_refuses_overlapping_data(self):
        good = asm(".rom A u8 1, 2\n.rom B u8 3\n" + Errors.OK).blob
        b_record = 16 + 32 + 8
        a_data = int.from_bytes(good[16 + 32 + 4:16 + 32 + 8], "little")
        overlap = good[:b_record + 4] + le32(a_data + 1) + good[b_record + 8:]
        with self.assertRaisesRegex(svm.SvmError, "overlap"):
            svm.disassemble(overlap, VM)


HEADER = """
// A header the way games write them.
#define PLAIN 3
#define SHIFTED (1u << 3)      /* a u suffix, as C_* use */
#define DERIVED (PLAIN + SHIFTED) * 2
#define NEGATIVE (-16)
#define FROM_LATER LATER + 1   // defined in a header given after this one
#define GAME C_GAME(2)
#define FUNC(n) ((n) * 2)      // function-like: skipped
#define CAST ((Entity)0)       // not an integer expression: skipped
#define EMPTY                  // an include guard: skipped
#define MULTI \\
    (1 << 5)
enum { FIRST, SECOND, THIRD = 10, FOURTH, FIFTH = FIRST + 100, SIXTH };
typedef enum Named { N_A = 5, N_B } Named;
#ifdef SOMETHING
#define BOTH 1
#else
#define BOTH 1
#endif
"""


class Headers(unittest.TestCase):
    def scrape(self, *texts):
        headers = svm.HeaderNames(VM.names)
        for i, text in enumerate(texts):
            headers.add_text(text, f"h{i}.h")
        headers.check()
        return headers

    def test_defines_and_enums(self):
        h = self.scrape(HEADER, "#define LATER 41\n")
        expected = {"PLAIN": 3, "SHIFTED": 8, "DERIVED": 22, "NEGATIVE": -16, "FROM_LATER": 42,
                    "GAME": 1 << 18, "MULTI": 32, "FIRST": 0, "SECOND": 1, "THIRD": 10,
                    "FOURTH": 11, "FIFTH": 100, "SIXTH": 101, "N_A": 5, "N_B": 6, "BOTH": 1}
        for name, value in expected.items():
            with self.subTest(name=name):
                self.assertEqual(h.lookup(name), value)
        for name in ("FUNC", "CAST", "EMPTY", "Named", "SOMETHING"):
            with self.subTest(name=name):
                self.assertIsNone(h.lookup(name))
        self.assertEqual(h.lookup("VM_STACK"), 64)  # vm.h's names, after the headers
        self.assertEqual(h.constants()["DERIVED"], 22)

    def test_conflicting_redefinition(self):
        with self.assertRaisesRegex(svm.SvmError, "TWICE is defined twice.*1 in h0.h.*2 in h1.h"):
            self.scrape("#define TWICE 1\n", "#define TWICE 2\n")
        self.scrape("#define SAME 1\n", "#define SAME 1\n")  # the same value is fine

    def test_listing_uses_header_names(self):
        h = self.scrape("#define HEALTH 50\nenum { SND_A, SND_B };\n")
        blob = asm(".object X\n.handler X CREATE\nPUSH HEALTH\nPUSH SND_B\nHALT\n", h).blob
        self.assertEqual(blob[48:], bytes([2, 50, 2, 1, 1]))
        # The listing's own names come first, then the headers, then vm.h.
        blob = asm(".const HEALTH 7\n.object X\n.handler X CREATE\nPUSH HEALTH\nPUSH VM_STACK\nHALT\n",
                   h).blob
        self.assertEqual(blob[48:], bytes([2, 7, 2, 64, 1]))

    def test_real_headers(self):
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
        h = svm.HeaderNames(VM.names)
        for name in ("ecs.h", "core.h", "sprites.h", "path.h", "screen.h"):
            h.load(os.path.join(root, "include", "serval", name))
        h.check()
        self.assertEqual(h.lookup("C_ANIM"), 1 << 5)  # a backslash-continued #define
        self.assertEqual(h.lookup("BUTTON_DOWN"), 0x80)
        self.assertEqual(h.lookup("SCREEN_BRIGHTNESS_MIN"), -16)
        self.assertIsNone(h.lookup("COLOR_RGB"))


EVERY_OPCODE = """
.object A mask=0x12345678 sprite=9
.object B mask=C_GAME(3) sprite=0
.string HELLO "say \\"hi\\" \\\\ bye"
.string EMPTY ""
.globals ONE TWO
.array CELLS 4
.rom TABLE s16 -300, 7, FX(2)
.array OVER 2 at=2              ; overlapping CELLS
    .byte 0xFF, 0x00, 0x7F     ; padding between the tables and the code
.handler A CREATE
.handler B STEP                ; two handlers sharing an offset
start:
    NOP
    PUSH8 -1
    PUSH16 -300
    PUSH32 70000
    DUP
    DROP
    SWAP
    LDG ONE
    STG TWO
    LDL 3
    STL 7
    LDA CELLS
    STA ARR_TABLE + 1
    LEN TABLE
    ENTER 2, 5
    ADD
    SUB
    MUL
    DIV
    MOD
    NEG
    FXMUL
    FXDIV
    AND
    OR
    XOR
    BNOT
    SHL
    SHR
    LNOT
    LSH
    EQ
    NE
    LT
    LE
    GT
    GE
    IDIV
    IMOD
    JMP ahead
    JZ start
    JNZ ahead
    CALL sub
    RET
    RETV
ahead:
    WAIT
    WAIT_ANIM
    WAIT_MOVE
    SELF
    OTHER
    GETP BODY_H
    SETP 200                   ; not a property vm.h knows: a number, with a warning
    GETP FIELD0
    SETP VM_P_FIELD0 + 15      ; the last instance field
    GETP TAGS
    SPAWN B
    KILL
    NEXTI A
    SYS TEXT_PRINT_NUMBER
    SYS PATH_STOP
    SYS 99
    BRK
    TRACE HELLO
    HALT
sub:
    RET
    .byte 0xEE, 0xEE           ; between the code and the strings
.handler B DESTROY
.handler A ROOM_START
    HALT
"""


class RoundTrip(unittest.TestCase):
    def test_every_opcode(self):
        """Every mnemonic in vm.h is in the listing, so the disassembler and
        the round trip cover each operand layout."""
        used = {line.split()[0] for line in EVERY_OPCODE.splitlines() if line.startswith("    ")}
        self.assertEqual(set(VM.ops), used - {".byte"})
        result = asm(EVERY_OPCODE)
        self.assertEqual(len(result.warnings), 2)  # SETP 200, SYS 99
        listing = svm.disassemble(result.blob, VM)
        again = asm(listing)
        self.assertEqual(again.blob, result.blob)
        self.assertIn(".byte 0xFF, 0x00, 0x7F", listing)
        self.assertIn(".byte 0xEE, 0xEE", listing)
        self.assertIn(".handler 0 CREATE\n.handler 1 STEP\n", listing)
        self.assertIn(".handler 0 ROOM_START\n.handler 1 DESTROY\n", listing)  # by object, then event
        self.assertIn('.string 0 "say \\"hi\\" \\\\ bye"', listing)
        self.assertIn("    PUSH8 -1\n    PUSH16 -300\n    PUSH32 70000\n", listing)
        self.assertIn("    GETP BODY_H\n    SETP 200\n", listing)
        self.assertIn("    GETP FIELD0\n    SETP VM_P_FIELD0 + 15\n    GETP TAGS\n", listing)
        self.assertIn("    SYS TEXT_PRINT_NUMBER\n    SYS PATH_STOP\n    SYS 99\n", listing)
        self.assertIn("    LDA 0\n    STA 2\n    LEN 1\n    ENTER 2, 5\n", listing)
        self.assertIn("    NEXTI 0\n", listing)
        self.assertIn(".array 0 4\n.rom 1 s16 -300, 7, 512\n.array 2 2 at=2\n", listing)
        self.assertIn("    JZ L_", listing)
        self.assertIn("    CALL L_", listing)

    def test_odd_blobs(self):
        """What the assembler would reject is kept as bytes: a jump into the
        middle of an instruction, a CALL past the end, an unknown opcode."""
        good = asm(".object X\n.handler X CREATE\nPUSH32 0\nJMP a\na:\nHALT\n").blob
        code_at = 16 + 32
        odd = bytearray(good)
        odd[code_at + 6:code_at + 8] = (-4 & 0xFFFF).to_bytes(2, "little")  # into PUSH32's operand
        odd += bytes([VM.ops["CALL"]]) + le32(9999) + bytes([0x99])
        listing = svm.disassemble(bytes(odd), VM)
        self.assertEqual(asm(listing).blob, bytes(odd))
        self.assertIn(".byte 0x28, 0xFC, 0xFF", listing)
        self.assertIn(".byte 0x2B, 0x0F, 0x27, 0x00, 0x00, 0x99", listing)

    def test_fireflies(self):
        """The example's listing, through the real headers and back."""
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
        listing = os.path.join(root, "examples", "fireflies", "fireflies.svm")
        if not os.path.exists(listing):
            self.skipTest("no examples/ (a release archive)")
        headers = svm.HeaderNames(VM.names)
        headers.load(os.path.join(root, "examples", "fireflies", "game.h"))
        for name in ("ecs.h", "core.h", "sprites.h", "path.h", "screen.h"):
            headers.load(os.path.join(root, "include", "serval", name))
        headers.check()
        with open(listing) as f:
            result = svm.assemble(f.read(), VM, headers, listing)
        self.assertEqual(result.warnings, [])
        self.assertEqual(result.objects[:2], ["ROOM", "SPAWNER"])
        self.assertEqual(result.globals[-1], "PLAYER")
        self.assertEqual(asm(svm.disassemble(result.blob, VM)).blob, result.blob)


class OperandTable(unittest.TestCase):
    def test_matches_vm_h(self):
        svm.check_operands(VM)  # the startup check, against the real vm.h

    def test_detects_drift(self):
        names = dict(VM.names)
        names["VM_OP_NEW"] = 0x52
        with self.assertRaisesRegex(svm.SvmError, "no operand row for NEW"):
            svm.check_operands(svm.Vm(names))
        del names["VM_OP_NEW"], names["VM_OP_BRK"]
        with self.assertRaisesRegex(svm.SvmError, "rows for opcodes vm.h doesn't have: BRK"):
            svm.check_operands(svm.Vm(names))

    def test_every_row_has_a_size(self):
        for mnemonic, fields in svm.OPERANDS.items():
            for kind, names in fields:
                with self.subTest(mnemonic=mnemonic):
                    self.assertIn(kind, svm.OPERAND_SIZE)
                    self.assertIn(names, (None, "global", "prop", "sys", "object", "string",
                                          "array", "label"))
                    if names == "label":
                        self.assertEqual(len(fields), 1)  # the fixups assume it

    def test_layouts_from_the_opcode_reference(self):
        """The new operand layouts, byte by byte (vm.md's opcode reference)."""
        sizes = {"ENTER": 2, "LDA": 2, "STA": 2, "LEN": 2, "NEXTI": 2, "LSH": 0, "IDIV": 0,
                 "IMOD": 0, "RETV": 0, "LDL": 1, "STL": 1, "CALL": 4}
        for mnemonic, size in sizes.items():
            with self.subTest(mnemonic=mnemonic):
                self.assertEqual(svm.operand_size(mnemonic), size)
        self.assertEqual(VM.ops["ENTER"], 0x2E)
        self.assertNotIn("INTERRUPTIBLE", VM.ops)
        self.assertNotIn(0x33, VM.op_names)  # unassigned


class GeneratedFiles(unittest.TestCase):
    def test_c_and_defs(self):
        with tempfile.TemporaryDirectory() as tmp:
            listing = os.path.join(tmp, "game.svm")
            with open(listing, "w") as f:
                f.write(GOLDEN_LISTING)
            c, h = os.path.join(tmp, "out.c"), os.path.join(tmp, "out.h")
            with redirect_stdout(io.StringIO()):
                status = svm.main(["asm", listing, "--c", c, "--symbol", "game_script", "--defs", h,
                                   "--prefix", "GAME_"])
            self.assertEqual(status, 0)
            with open(c) as f:
                source = f.read()
            self.assertIn("const unsigned char game_script[] = {", source)
            self.assertIn("/* 0x0000 */ 0x53, 0x56, 0x4D, 0x42,", source)
            self.assertIn("/* 0x0030 */", source)
            self.assertIn("const unsigned int game_script_size = 63;", source)
            self.assertIn("game.svm", source)
            with open(h) as f:
                header = f.read()
            for line in ("#ifndef OUT_H", "#define GAME_OBJ_THING 0", "#define GAME_OBJ_COUNT 1",
                         "#define GAME_STR_HI 0", "#define GAME_STR_COUNT 1", "#define GAME_G_SUM 0",
                         "#define GAME_G_COUNT 1", "extern const unsigned char game_script[];",
                         "extern const unsigned int game_script_size;"):
                self.assertIn(line, header)


if __name__ == "__main__":
    unittest.main()
