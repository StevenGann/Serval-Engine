#!/usr/bin/env python3
"""Tests for tools/svlua.py, the Lua-subset compiler, run by CTest in the host
build (svlua_tool).

Groups: the lexer and the parser (Lua 5.4's tokens, grammar, precedence and
associativity); what the subset rejects (one test per construct, checking the
message and its line and column); names and types (promotion, conflicts,
conditions, inference from call sites, fields, the wait rule); the field
names reserved for later properties, and the engine's function names, planned
and implemented (read from the engine's headers, fake ones too, and checked
against GCC's reading); code generation (golden listings in
tests/svlua/, assembled by svm.py; constant folding, frames, loops); what
compiled programs compute, run on the engine's VM by svlua_runner
(tests/svlua/runner.c, built by the host preset); and the fireflies game in
Lua. Run directly: python3 tools/svlua_test.py (with the
host preset built, or SERVAL_SVLUA_RUNNER naming the runner; without one the
tests that run programs are skipped). SVLUA_UPDATE_GOLDEN=1 rewrites the
golden listings from the compiler.
"""

import importlib.util
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svlua  # noqa: E402
import svm  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
FIXTURES = os.path.join(ROOT, "tests", "svlua")
INT_MIN, INT_MAX = -0x80000000, 0x7FFFFFFF


def tokens(text):
    return [(t.kind, t.value) for t in svlua.tokenize(text, "t.lua")[:-1]]


def tree(text):
    return svlua.dump(svlua.parse(text, "t.lua"))


def expr_tree(text):
    """The tree of one expression, parsed as `x = <text>`."""
    block = svlua.parse(f"x = {text}", "t.lua")
    return svlua.dump(block.stats[0].values[0])


# --- Lexer -------------------------------------------------------------------


class Lexer(unittest.TestCase):
    def test_names_keywords_and_operators(self):
        self.assertEqual(tokens("local x_1 = y"), [
            ("local", "local"), ("name", "x_1"), ("=", "="), ("name", "y")])
        ops = "... .. == ~= <= >= << >> // :: + - * / % ^ # & ~ | < > = ( ) { } [ ] ; : , ."
        self.assertEqual([k for k, _ in tokens(ops)], ops.split())
        self.assertEqual([k for k, _ in tokens("a...b..c.d")], [
            "name", "...", "name", "..", "name", ".", "name"])
        for word in svlua.KEYWORDS:
            self.assertEqual(tokens(word), [(word, word)])

    def test_numerals(self):
        cases = {
            "3": ("int", 3), "0": ("int", 0), "345": ("int", 345),
            "0xff": ("int", 255), "0XBEBADA": ("int", 0xBEBADA),
            "2147483647": ("int", 2147483647),
            "2147483648": ("float", 2147483648.0),  # doesn't fit 32 bits: a float, as in Lua
            "0xFFFFFFFF": ("int", -1),  # hex integers wrap around
            "0x100000001": ("int", 1),
            "3.0": ("float", 3.0), "3.1416": ("float", 3.1416), "314.16e-2": ("float", 3.1416),
            "0.31416E1": ("float", 3.1416), "34e1": ("float", 340.0), ".5": ("float", 0.5),
            "5.": ("float", 5.0), "0x0.1E": ("float", 0.1171875), "0xA23p-4": ("float", 162.1875),
            "0X1.921FB54442D18P+1": ("float", 3.141592653589793), "0x.8": ("float", 0.5),
        }
        for text, value in cases.items():
            with self.subTest(text=text):
                self.assertEqual(tokens(text), [("number", value)])
        for text in ("3x", "0x", "1e", "1e+", "3.4.5", "0xg", "08f", "1..2"):
            with self.subTest(text=text):
                with self.assertRaisesRegex(svlua.CompileError, "malformed number"):
                    tokens(text)

    def test_short_strings(self):
        cases = {
            r'"hi"': b"hi", r"'it''s'": None, r'"a\"b"': b'a"b', r"'a\'b'": b"a'b",
            r'"\a\b\f\n\r\t\v\\"': b"\a\b\f\n\r\t\v\\", r'"\x41\x7e"': b"A~",
            r'"\65\066\0067"': b"AB\x067", r'"\u{48}\u{49}"': b"HI", r'"\u{E9}"': b"\xc3\xa9",
            '"a\\\nb"': b"a\nb", '"a\\z  \n  b"': b"ab", '""': b"", "''": b"",
        }
        for text, value in cases.items():
            if value is None:
                continue
            with self.subTest(text=text):
                self.assertEqual(tokens(text), [("string", value)])
        self.assertEqual(tokens("'it' 's'"), [("string", b"it"), ("string", b"s")])
        errors = {'"abc': "unfinished string", '"ab\ncd"': "unfinished string",
                  r'"\q"': "invalid escape sequence", r'"\xg0"': "hexadecimal digit expected",
                  r'"\256"': "decimal escape too large", r'"\u{zz}"': "malformed",
                  r'"\u48"': "malformed"}
        for text, message in errors.items():
            with self.subTest(text=text):
                with self.assertRaisesRegex(svlua.CompileError, message):
                    tokens(text)

    def test_long_strings(self):
        self.assertEqual(tokens("[[abc]]"), [("string", b"abc")])
        self.assertEqual(tokens("[[\nfirst line break skipped]]"),
                         [("string", b"first line break skipped")])
        self.assertEqual(tokens("[==[a]]b]=]c]==]"), [("string", b"a]]b]=]c")])
        self.assertEqual(tokens("[=[\n\nkeeps the second]=]"), [("string", b"\nkeeps the second")])
        self.assertEqual(tokens(r"[[no \n escapes]]"), [("string", b"no \\n escapes")])
        with self.assertRaisesRegex(svlua.CompileError, "unfinished long string"):
            tokens("[==[abc]=]")
        with self.assertRaisesRegex(svlua.CompileError, "invalid long string delimiter"):
            tokens("[= x")
        self.assertEqual(tokens("a[b]"), [("name", "a"), ("[", "["), ("name", "b"), ("]", "]")])

    def test_comments(self):
        self.assertEqual(tokens("a -- b c\nd"), [("name", "a"), ("name", "d")])
        self.assertEqual(tokens("a --[[ b\nc ]] d"), [("name", "a"), ("name", "d")])
        self.assertEqual(tokens("a --[==[ ]] ]=] ]==] d"), [("name", "a"), ("name", "d")])
        self.assertEqual(tokens("a --[ not long\nd"), [("name", "a"), ("name", "d")])
        self.assertEqual(tokens("a --"), [("name", "a")])
        with self.assertRaisesRegex(svlua.CompileError, "unfinished long comment"):
            tokens("--[[ never closed")

    def test_positions(self):
        text = "a = 1\n  --[[ two\nlines ]] b\r\n\tc = [[x\ny]] d\n#"
        found = [(t.text, t.line, t.column) for t in svlua.tokenize(text, "t.lua")]
        self.assertEqual(found, [("a", 1, 1), ("=", 1, 3), ("1", 1, 5), ("b", 3, 10), ("c", 4, 2),
                                 ("=", 4, 4), ("[[x\ny]]", 4, 6), ("d", 5, 5), ("#", 6, 1),
                                 ("<eof>", 6, 2)])

    def test_shebang_line_is_skipped(self):
        found = [(t.text, t.line) for t in svlua.tokenize("#!/usr/bin/lua\nx", "t.lua")]
        self.assertEqual(found, [("x", 2), ("<eof>", 2)])

    def test_unexpected_symbol(self):
        with self.assertRaises(svlua.CompileError) as caught:
            tokens("a = 1\nb = $")
        error = caught.exception
        self.assertEqual((error.file, error.line, error.column), ("t.lua", 2, 5))
        self.assertIn("unexpected symbol near '$'", str(error))
        self.assertTrue(str(error).startswith("t.lua:2:5: error: "))


# --- Parser ------------------------------------------------------------------


class Precedence(unittest.TestCase):
    # Each expression and the same with Lua's grouping made explicit.
    SAME = [
        ("a or b and c", "a or (b and c)"),
        ("a and b < c", "a and (b < c)"),
        ("a < b | c", "a < (b | c)"),
        ("a | b ~ c", "a | (b ~ c)"),
        ("a ~ b & c", "a ~ (b & c)"),
        ("a & b << c", "a & (b << c)"),
        ("a << b .. c", "a << (b .. c)"),
        ("a .. b + c", "a .. (b + c)"),
        ("a + b * c", "a + (b * c)"),
        ("a * -b", "a * (-b)"),
        ("-a ^ b", "-(a ^ b)"),
        ("not a == b", "(not a) == b"),
        ("#a + 1", "(#a) + 1"),
        ("~a & b", "(~a) & b"),
        ("a ^ -b", "a ^ (-b)"),
        ("a % b // c / d * e", "(((a % b) // c) / d) * e"),
        ("a - b + c", "(a - b) + c"),
        ("a < b >= c ~= d", "((a < b) >= c) ~= d"),
        ("a | b | c", "(a | b) | c"),
        ("a >> b << c", "(a >> b) << c"),
        ("a or b or c", "(a or b) or c"),
        ("a and b and c", "(a and b) and c"),
        ("a and b or c and d", "(a and b) or (c and d)"),
        ("2 ^ 3 ^ 2", "2 ^ (3 ^ 2)"),
        ("a .. b .. c", "a .. (b .. c)"),
        ("a + b .. c + d", "(a + b) .. (c + d)"),
        ("not not a", "not (not a)"),
        ("- - a", "-(-a)"),
        ("a.b.c[d](e)", "((a.b).c)[d](e)"),
    ]

    def test_grouping(self):
        for text, grouped in self.SAME:
            with self.subTest(text=text):
                self.assertEqual(expr_tree(text), expr_tree(grouped))

    def test_the_other_grouping_differs(self):
        """The cases aren't trivially equal: grouping the other way gives a
        different tree."""
        for text, other in [("a or b and c", "(a or b) and c"), ("a - b + c", "a - (b + c)"),
                            ("2 ^ 3 ^ 2", "(2 ^ 3) ^ 2"), ("a .. b .. c", "(a .. b) .. c"),
                            ("-a ^ b", "(-a) ^ b"), ("a & b << c", "(a & b) << c")]:
            with self.subTest(text=text):
                self.assertNotEqual(expr_tree(text), expr_tree(other))

    def test_every_binary_operator_parses(self):
        for op in svlua.BINARY_PRIORITY:
            with self.subTest(op=op):
                self.assertEqual(expr_tree(f"a {op} b"),
                                 ("Binary", op, ("Name", "a"), ("Name", "b")))


class Statements(unittest.TestCase):
    def test_every_statement_form(self):
        n = ("Name", "n")
        one = ("Number", "int", 1, "1")
        two = ("Number", "int", 2, "2")
        cases = {
            ";": ("Block", ()),
            "x = 1": ("Block", (("Assign", (("Name", "x"),), (one,)),)),
            "x, y.z, w[1] = 1, 2": ("Block", (("Assign", (
                ("Name", "x"), ("Field", ("Name", "y"), "z"), ("Index", ("Name", "w"), one)),
                (one, two)),)),
            "f(1)": ("Block", (("CallStat", ("Call", ("Name", "f"), (one,))),)),
            "o:m(1)": ("Block", (("CallStat", ("Method", ("Name", "o"), "m", (one,))),)),
            "f 's'": ("Block", (("CallStat", ("Call", ("Name", "f"), (("String", b"s"),))),)),
            "f {}": ("Block", (("CallStat", ("Call", ("Name", "f"), (("Table", ()),))),)),
            "::top::": ("Block", (("Label", "top"),)),
            "while true do break end": ("Block", (
                ("While", ("Bool", True), ("Block", (("Break",),))),)),
            "goto top": ("Block", (("Goto", "top"),)),
            "do end": ("Block", (("Do", ("Block", ())),)),
            "repeat until n": ("Block", (("Repeat", ("Block", ()), n),)),
            "if n then elseif 1 then else end": ("Block", (
                ("If", (n, one), (("Block", ()), ("Block", ())), ("Block", ())),)),
            "if n then end": ("Block", (("If", (n,), (("Block", ()),), None),)),
            "for i = 1, 2 do end": ("Block", (
                ("NumericFor", ("Name", "i"), one, two, None, ("Block", ())),)),
            "for i = 1, 2, n do end": ("Block", (
                ("NumericFor", ("Name", "i"), one, two, n, ("Block", ())),)),
            "for k, v in f, 1 do end": ("Block", (
                ("GenericFor", (("Name", "k"), ("Name", "v")), (("Name", "f"), one),
                 ("Block", ())),)),
            "function a.b:c(p, ...) end": ("Block", (
                ("FunctionStat", (("Name", "a"), ("Name", "b")), ("Name", "c"),
                 ("FunctionExpr", (("Name", "p"),), ("Vararg",), ("Block", ()))),)),
            "local function f() end": ("Block", (
                ("LocalFunction", ("Name", "f"), ("FunctionExpr", (), None, ("Block", ()))),)),
            "local a <const>, b <close>, c = 1": ("Block", (
                ("Local", (("Name", "a"), ("Name", "b"), ("Name", "c")), ("const", "close", None),
                 (one,)),)),
            "local a": ("Block", (("Local", (("Name", "a"),), (None,), ()),)),
            "return": ("Block", (("Return", ()),)),
            "return 1, 2;": ("Block", (("Return", (one, two)),)),
            "x = function() end": ("Block", (("Assign", (("Name", "x"),), (
                ("FunctionExpr", (), None, ("Block", ())),)),)),
            "x = {1, a = 2; [n] = 1,}": ("Block", (("Assign", (("Name", "x"),), (("Table", (
                ("TableItem", "list", None, one), ("TableItem", "name", "a", two),
                ("TableItem", "expr", n, one))),)),)),
            "x = nil, ..., true, false": ("Block", (("Assign", (("Name", "x"),), (
                ("Nil",), ("Vararg",), ("Bool", True), ("Bool", False))),)),
        }
        for text, expected in cases.items():
            with self.subTest(text=text):
                self.assertEqual(tree(text), expected)

    def test_positions_of_nodes(self):
        block = svlua.parse("x = 1\nif a then\n  y = b + c\nend", "t.lua")
        stat = block.stats[1]
        self.assertEqual((stat.line, stat.column), (2, 1))
        assign = stat.blocks[0].stats[0]
        self.assertEqual((assign.line, assign.column), (3, 3))
        self.assertEqual((assign.values[0].line, assign.values[0].column), (3, 9))  # the +

    def test_syntax_errors(self):
        cases = [
            ("x = = 1", 1, 5, "unexpected symbol near '='"),
            ("if x then\ny = 1\n", 3, 1, "'end' expected (to close 'if' at line 1) near '<eof>'"),
            ("while x do y = 1", 1, 17, "'end' expected near '<eof>'"),
            ("for i do end", 1, 7, "'=' or 'in' expected near 'do'"),
            ("x", 1, 2, "syntax error near '<eof>'"),
            ("f() = 1", 1, 1, "syntax error"),
            ("return 1\nx = 2", 2, 1, "'<eof>' expected near 'x'"),
            ("local x <foo> = 1", 1, 10, "unknown attribute 'foo'"),
            ("x = (1", 1, 7, "')' expected near '<eof>'"),
            ("function f(a,) end", 1, 14, "<name> expected near ')'"),
            ("x = {1 2}", 1, 8, "'}' expected near '2'"),
            ("repeat x = 1", 1, 13, "'until' expected near '<eof>'"),
            ("goto 3", 1, 6, "<name> expected near '3'"),
            ("x = a.1", 1, 6, "unexpected symbol near '.1'"),  # .1 is a number
        ]
        for text, line, column, message in cases:
            with self.subTest(text=text):
                with self.assertRaises(svlua.CompileError) as caught:
                    svlua.parse(text, "t.lua")
                error = caught.exception
                self.assertIn(message, error.message)
                self.assertEqual((error.line, error.column), (line, column))



# --- What the subset rejects -------------------------------------------------

# One case per construct: (script, line, column, a pattern the message must
# match). A is an object with a Step handler (a reaction), B one with a
# Create handler (a behaviour), for the cases that need a body.
OBJ = "A = object {}\nB = object {}\n"
REJECTED = {
    # lua.md's "Not in the subset"
    "keyed_table": ("t = { x = 1 }", 1, 7, r"tables with keys are not in the subset"),
    "keyed_table_brackets": ("t = { [1] = 2 }", 1, 7, r"tables with keys"),
    "nested_table": ("t = { {1}, 2 }", 1, 7, r"nested tables are not in the subset"),
    "table_in_code": (OBJ + "function A:step() local t = {1} end", 3, 29,
                      r"tables are not in the subset, except top-level arrays"),
    "pairs": (OBJ + "t = {1}\nfunction A:step() for k, v in pairs(t) do end end", 4, 31,
              r"pairs \(iterating a table\) is not in the subset"),
    "ipairs": (OBJ + "t = {1}\nfunction A:step() for i, v in ipairs(t) do end end", 4, 31,
               r"ipairs \(iterating a table\) is not in the subset"),
    "metatable": (OBJ + "function A:step() setmetatable(self, self) end", 3, 19,
                  r"setmetatable \(metatables\) is not in the subset"),
    "closure": (OBJ + "function A:step() local f = function() end end", 3, 29,
                r"function expressions \(closures\) are not in the subset"),
    "nested_function": (OBJ + "function A:step() local function f() end end", 3, 19,
                        r"functions are declared at the top level only \(closures"),
    "varargs_parameter": ("function f(a, ...) end", 1, 15,
                          r"varargs \(\.\.\.\) are not in the subset"),
    "varargs_value": (OBJ + "function A:step() text_print(1, 1, ...) end", 3, 36, r"varargs"),
    "multiple_results": ("function f() return 1, 2 end", 1, 24,
                         r"multiple results are not in the subset"),
    "string_at_run_time": (OBJ + "n = 0\nfunction A:step() text_print(1, 1, 'n=' .. n) end", 4,
                           44,
                           r"string operations at run time are not in the subset"),
    "string_library": (OBJ + "function A:step() text_print(1, 1, string.rep('a', 2)) end", 3, 36,
                       r"the string library .* is not in the subset"),
    "string_variable": (OBJ + "function A:step() local s = 'hi' end", 3, 29,
                        r"strings exist only as text_print's argument"),
    "standard_library": (OBJ + "function A:step() local x = math.sin(1) end", 3, 29,
                         r"math\.sin is not in the subset: .*math\.floor, math\.abs, math\.min, "
                         r"math\.max, math\.mininteger and math\.maxinteger"),
    "math_tointeger": (OBJ + "function A:step() local x = math.tointeger(1.0) end", 3, 29,
                       r"math\.tointeger is not in the subset"),
    "tostring": (OBJ + "function A:step() text_print(1, 1, tostring(3)) end", 3, 36,
                 r"tostring .* is not in the subset"),
    "print": (OBJ + "function A:step() print(1, 1, 'hi') end", 3, 19,
              r"print \(console output\) is not in the subset"),
    "coroutines": (OBJ + "function B:create() coroutine.yield() end", 3, 21,
                   r"the coroutine library is not in the subset"),
    "nil": (OBJ + "function A:step() local e = nil end", 3, 29, r"nil is not in the subset"),
    "nil_global": ("e = nil", 1, 5, r"nil is not in the subset"),
    "float_too_big": (OBJ + "function A:step() local x = 1e10 end", 3, 29,
                      r"1e10 doesn't fit fixed point"),
    "float_bitwise": (OBJ + "function A:step() local x = 1.5 & 1 end", 3, 29,
                      r"& works on integers, and 1\.5 is fixed"),
    "power_at_run_time": (OBJ + "n = 0\nfunction A:step() local x = n ^ 2 end", 4, 31,
                          r"\^ \(exponentiation\) is folded for constants only"),
    # The rest of Lua the subset leaves out, or rejects to keep its meaning
    "close_variable": (OBJ + "function A:step() local x <close> = 1 end", 3, 25,
                       r"to-be-closed variables \(<close>\) are not in the subset"),
    "method_call": (OBJ + "function A:step() self:jump() end", 3, 24,
                    r"method calls \(self:jump\(\)\) are not in the subset"),
    "method_definition": ("A = object {}\nfunction A:jump() end", 2, 12,
                          r"A:jump is not an event; methods are not in the subset"),
    "function_in_table": ("function a.b() end", 1, 12,
                          r"functions in tables .* are not in the subset"),
    "top_level_code": ("x = 0\nif x == 0 then end", 2, 1,
                       r"if at the top level: only declarations"),
    "top_level_call": ("text_print(1, 1, 'hi')", 1, 1, r"a call at the top level"),
    "top_level_field": ("A = object {}\nA.x = 1", 2, 1,
                        r"only names are assigned at the top level"),
    "local_without_value": (OBJ + "function A:step() local x end", 3, 25,
                            r"x gets no value, so it would be nil"),
    "missing_value": ("a, b = 1", 1, 4, r"b gets no value, so it would be nil"),
    "extra_value": ("a = 1, 2", 1, 8, r"more values than names"),
    "global_not_constant": ("a = 0\nb = a", 2, 5, r"the initial value of b must be a constant"),
    "math_constant_called": (OBJ + "function A:step() local x = math.maxinteger() end", 3, 29,
                             r"math\.maxinteger is a number, not a function"),
    "math_constant_assigned": (OBJ + "function A:step() math.mininteger = 1 end", 3, 19,
                               r"math\.mininteger can't be assigned"),
    "math_function_uncalled": (OBJ + "function A:step() local x = math.abs end", 3, 29,
                               r"math\.abs is a function: call it"),
    "global_undeclared": (OBJ + "function A:step() count = 1 end", 3, 19, r"count is not defined"),
    "header_assigned": (OBJ + "function A:step() MAX = 1 end", 3, 19,
                        r"MAX is not declared, so it would be a constant from the C headers"),
    "assign_const": (OBJ + "local K <const> = 1\nfunction A:step() K = 2 end", 4, 19,
                     r"attempt to assign to const variable 'K'"),
    "assign_self": (OBJ + "function A:step() self = none end", 3, 19,
                    r"self is the instance the handler runs for; it can't be assigned"),
    "redefine_engine": ("function text_print() end", 1, 10,
                        r"text_print is an engine function"),
    "redefine_print": ("function print() end", 1, 10, r"print is Lua's print \(console output\)"),
    "redeclare": ("a = 0\nlocal a = 0", 2, 7, r"a is already declared \(line 1\)"),
    "local_used_above": (OBJ + "function A:step() local y = speed end\nlocal speed = 0", 3, 29,
                         r"speed is declared below \(line 4\) as a top-level local"),
    "self_in_function": ("function f() self.x = 1 end", 1, 14, r"self exists only in handlers"),
    "goto_into_scope": (OBJ + "function A:step() goto skip; local x = 1; ::skip:: x = 2 end", 3, 19,
                        r"<goto skip> at line 3 jumps into the scope of local 'x'"),
    "goto_no_label": (OBJ + "function A:step() goto nowhere end", 3, 19,
                      r"no visible label 'nowhere' for goto"),
    "label_twice": (OBJ + "function A:step() ::a:: do ::a:: end end", 3, 28,
                    r"label 'a' already defined on line 3"),
    "break_outside_loop": (OBJ + "function A:step() break end", 3, 19, r"break outside a loop"),
    "for_step_zero": (OBJ + "function A:step() for i = 1, 10, 0 do end end", 3, 34,
                      r"'for' step is zero"),
    "divide_by_zero": (OBJ + "function A:step() local q = 7 // 0 end", 3, 34,
                       r"attempt to perform 'n//0'"),
    "generic_for": (OBJ + "function f() end\nfunction A:step() for x in f do end end", 4, 19,
                    r"the generic for works only with instances\(Object\)"),
    "handler_parameters": ("A = object {}\nfunction A:step(dt) end", 2, 1,
                           r"step takes no parameters, not 1"),
    "handler_twice": ("A = object {}\nfunction A:step() end\nfunction A:step() end", 3, 12,
                      r"A:step is already defined \(line 2\)"),
    "handler_before_object": ("function A:step() end\nA = object {}", 1, 10,
                              r"A is declared below \(line 2\)"),
    "handler_returns_value": (OBJ + "function A:step() return 1 end", 3, 26,
                              r"handlers don't return values"),
    "return_value_and_nothing": ("function f(n) if n > 0 then return n end return end", 1, 42,
                                 r"f returns a value at line 1 and nothing here"),
    "object_in_code": (OBJ + "function A:step() local o = object {} end", 3, 29,
                       r"object declares at the top level only"),
    "object_fields": ("A = object { speed = 3 }", 1, 14, r"an object has two fields, components "
                      r"and sprite"),
    "rom_assigned": (OBJ + "t = {1, 2}\nfunction A:step() t[1] = 3 end", 4, 19,
                     r"t is a constant table \(in ROM\): its elements can't be assigned"),
    "array_as_value": (OBJ + "t = array(4)\nfunction A:step() local u = t end", 4, 29,
                       r"t is an array, not a value"),
    "array_index_zero": (OBJ + "t = array(4)\nfunction A:step() t[0] = 1 end", 4, 21,
                         r"t\[0\] is outside the array \(1 to 4\): Lua's arrays start at 1"),
    "instances_outside_for": (OBJ + "function A:step() local i = instances(A) end", 3, 29,
                              r"instances\(\) works only in a generic for"),
    "seventeen_fields": (OBJ + "function A:step()\n" + "".join(
        f"  self.f{i} = 0\n" for i in range(17)) + "end", 20, 3,
        r"a 17th instance field, f16: the VM has 16 \(VM_FIELDS\)"),
    "thread_self": ("Room = object {}\nfunction Room:room_start() self.frame = 1 end", 2, 28,
                    r"Room has no components, so its room_start runs as a thread"),
    "read_only_body_contact": (OBJ + "function A:step() self.body_contact = 0 end", 3, 19,
                               r"body_contact is read-only: the engine sets it"),
    "body_gravity_range": (OBJ + "function A:step() self.body_gravity = BODY_GRAVITY(144) end",
                           3, 52, r"BODY_GRAVITY\(n\) takes a constant n from -112 to 143"),
    "body_gravity_variable": (OBJ + "n = 8\nfunction A:step() self.body_gravity = "
                              "BODY_GRAVITY(n) end", 4, 52, r"BODY_GRAVITY\(n\) takes a constant"),
    "read_only_object": (OBJ + "function A:step() self.object = B end", 3, 19,
                         r"object is read-only: an instance's object is the one it was spawned "
                         r"or attached as"),
    "object_kept": (OBJ + "function A:step() local o = self.object end", 3, 29,
                    r"self\.object is an object, and objects are compared, not kept"),
    "object_compared_with_integer": (OBJ + "function A:step() if self.object == 0 then end end",
                                     3, 37, r"comparing an object with an integer: in Lua they "
                                     r"are never equal"),
    "object_ordered": (OBJ + "function A:step() if self.object < B then end end", 3, 22,
                       r"self\.object is an object, and objects are compared"),
    "header_called": (OBJ + "function A:step() local g = MAX_FALL(2) end", 3, 29,
                      r"MAX_FALL would be a constant from the C headers, and constants aren't "
                      r"functions"),
    # Instance fields named like the properties later engine versions add
    "reserved_field": (OBJ + "function A:step() self.body_speed = 1 end", 3, 19,
                       r"self\.body_speed: body_ is reserved for engine properties, and "
                       r"body_speed isn't one: an instance field can't take the name of a "
                       r"property a later engine version may add"),
    "reserved_field_read": (OBJ + "function A:collision(other) local s = other.path_speed end",
                            3, 39, r"other\.path_speed: path_ is reserved for engine properties"),
    "reserved_field_parameter": (OBJ + "function tint(e) e.spr_palette = 2 end", 3, 18,
                                 r"e\.spr_palette: spr_ is reserved for engine properties"),
    # Top-level names of the engine's planned functions, and their uses (each
    # planned in this version: implementing one moves its case to the C
    # functions' below, or the builtins')
    "planned_function": ("function music_play() end", 1, 10,
                         r"^function music_play: music_play is reserved: it names a planned "
                         r"engine function, which a later engine version may make a builtin$"),
    "planned_global": ("sfx_play = 0", 1, 1, r"^global sfx_play: sfx_play is reserved"),
    "planned_object": ("raster_clear = object {}", 1, 1,
                       r"^object raster_clear: raster_clear is reserved"),
    "planned_array": ("sprite_set_tiles = array(4)", 1, 1,
                      r"^array sprite_set_tiles: sprite_set_tiles is reserved"),
    "planned_rom_array": ("tileset_set_colors = { 1, 2 }", 1, 1,
                          r"^array tileset_set_colors: tileset_set_colors is reserved"),
    "planned_local_function": ("local function music_stop() end", 1, 16,
                               r"^local function music_stop: music_stop is reserved"),
    "planned_top_local": ("local screen_set_blend = 0", 1, 7,
                          r"^local screen_set_blend: screen_set_blend is reserved"),
    "planned_top_const": ("local sfx_stop_all <const> = 0", 1, 7,
                          r"^local sfx_stop_all: sfx_stop_all is reserved"),
    "planned_called": (OBJ + "function A:step() music_play(0, true) end", 3, 19,
                       r"^music_play is planned, not implemented in this engine version \(tracker "
                       r"music, docs/audio\.md#tracker-music\): scripts can't use it yet$"),
    "planned_read": (OBJ + "function A:step() local on = sfx_playing end", 3, 30,
                     r"^sfx_playing is planned, not implemented in this engine version"),
    "planned_assigned": (OBJ + "function A:step() raster_clear = 1 end", 3, 19,
                         r"^raster_clear is planned, not implemented in this engine version"),
    "planned_initial_value": ("x = music_paused", 1, 5,
                              r"^music_paused is planned, not implemented in this engine version"),
    # Top-level names of the engine's other C functions (implemented, no
    # builtin), and their uses
    "c_function": ("function sprite_draw() end", 1, 10,
                   r"^function sprite_draw: sprite_draw is reserved: it is an engine C function, "
                   r"which scripts may get as a builtin in a later version$"),
    "c_function_global": ("frame_count = 0", 1, 1, r"^global frame_count: frame_count is reserved"),
    "c_function_object": ("map_load = object {}", 1, 1,
                          r"^object map_load: map_load is reserved: it is an engine C function"),
    "c_function_inline": ("local fx_mul = 0", 1, 7,
                          r"^local fx_mul: fx_mul is reserved: it is an engine C function"),
    "c_function_called": (OBJ + "function A:step() entity_create(0) end", 3, 19,
                          r"^entity_create is an engine C function, which scripts can't call: this "
                          r"engine version has no builtin for it$"),
    "c_function_read": ("x = frame_count", 1, 5,
                        r"^frame_count is an engine C function, which scripts can't call"),
}


class Rejected(unittest.TestCase):
    """Each construct outside the subset is a compile error that names it,
    at its line and column."""

    def check_rejected(self, source, line, column, pattern):
        with self.assertRaises(svlua.CompileError) as caught:
            svlua.check(source, "t.lua")
        error = caught.exception
        self.assertRegex(error.message, pattern)
        self.assertEqual((error.file, error.line, error.column), ("t.lua", line, column),
                         str(error))
        self.assertTrue(str(error).startswith(f"t.lua:{line}:{column}: error: "))


def _rejected_test(case):
    def test(self):
        self.check_rejected(*case)
    return test


for _name, _case in REJECTED.items():
    setattr(Rejected, "test_" + _name, _rejected_test(_case))


# --- Names and types ---------------------------------------------------------


def local_types(program, body_name):
    """{name: type} of a body's parameters and locals (the first of each
    name)."""
    body = next(b for b in program.bodies if b.name == body_name)
    types = {}
    for sym in body.params + body.locals:
        types.setdefault(sym.name, sym.ty)
    return types


class Types(unittest.TestCase):
    def check(self, text):
        return svlua.check(text, "t.lua")

    def assert_error(self, text, pattern, line=None):
        with self.assertRaises(svlua.CompileError) as caught:
            self.check(text)
        self.assertRegex(str(caught.exception), pattern)
        if line is not None:
            self.assertEqual(caught.exception.line, line)
        return caught.exception

    def test_literals_and_promotion(self):
        p = self.check(OBJ + """function A:step()
  local i = 7
  local f = 1.5
  local sum = i + f          -- integer converted: fixed
  local half = 7 / 2         -- / is always fixed
  local whole = 7 // 2       -- // keeps integers
  local floored = math.floor(f)
  local b = i < f            -- comparisons are booleans
  local e = none
  self.x = 4                 -- an integer stored in a fixed property: converted
  local g = 0.0
  g = 3                      -- and in a fixed variable
end""")
        self.assertEqual(local_types(p, "A:step"), {
            "i": "integer", "f": "fixed", "sum": "fixed", "half": "fixed", "whole": "integer",
            "floored": "integer", "b": "boolean", "e": "entity", "g": "fixed"})

    def test_constant_values(self):
        p = self.check(OBJ + "function A:step() local h = 7 / 2; local m = -7 // 2; "
                       "local r = -7 % 2; local s = 1 << 31; local u = -1 >> 28 end")
        body = next(b for b in p.bodies if b.name == "A:step")
        values = {sym.name: sym.node for sym in body.locals}
        stat = body.func.body.stats
        consts = [s.values[0].const.value for s in stat]
        self.assertEqual(consts, [896, -4, 1, -2147483648, 15])  # 3.5 is 896 in 24.8
        self.assertEqual(set(values), {"h", "m", "r", "s", "u"})

    def test_variables_keep_their_first_type(self):
        self.assert_error(OBJ + "function A:step()\n  local n = 0\n  n = 0.5\nend",
                          r"t\.lua:5:7: error: n is an integer \(from its first value, line 4\), "
                          r"and this is fixed\n  hint: to make it fixed, start it as fixed")
        self.assert_error(OBJ + "flag = false\nfunction A:step() flag = 1 end",
                          r"flag is a boolean .* and this is an integer")
        self.assert_error(OBJ + "function A:step() self.frame = self.x end",
                          r"frame is an integer \(an engine property\), and this is fixed\n"
                          r"  hint: math\.floor")

    def test_conditions_must_be_booleans(self):
        for text in ("if n then end", "while n do end", "repeat until n", "local b = not n",
                     "if n and true then end"):
            with self.subTest(text=text):
                self.assert_error(OBJ + f"n = 0\nfunction A:step() {text} end",
                                  r"(conditions must be booleans|not takes a boolean|and takes "
                                  r"booleans).*n is an integer")
        self.assert_error(OBJ + "function A:step() if self then end end",
                          r"self is an entity\n  hint: compare it: self ~= none")
        self.check(OBJ + "n = 0\nfunction A:step() if n ~= 0 and not (n > 3) then end end")

    def test_and_or_take_booleans(self):
        self.assert_error(OBJ + "function A:step() local x = true and 1 or 2 end",
                          r"and takes booleans, and 1 is an integer\n.*the a and b or c idiom")

    def test_comparing_types(self):
        self.assert_error(OBJ + "function A:step() if self == 0 then end end",
                          r"comparing an entity with an integer: in Lua they are never equal")
        self.check(OBJ + "function A:step() if self.x == 0 and self ~= none then end end")

    def test_functions_are_inferred_from_call_sites(self):
        p = self.check(OBJ + """
function add(a, b) return a + b end
function scale(v, k) return v * k end
function move(e, dx) e.x = e.x + dx end
function A:step()
  local n = add(1, 2)
  local f = scale(self.x, 0.5)
  move(self, 1.5)
end""")
        fns = {f.name: f for f in p.functions}
        self.assertEqual(fns["add"].result, "integer")
        self.assertEqual([s.ty for s in fns["add"].body.params], ["integer", "integer"])
        self.assertEqual(fns["scale"].result, "fixed")
        self.assertEqual([s.ty for s in fns["move"].body.params], ["entity", "fixed"])
        self.assertEqual(fns["move"].result, "no value")
        self.assertEqual(local_types(p, "A:step"), {"n": "integer", "f": "fixed"})

    def test_parameter_types_from_uses(self):
        """A function nobody calls still gets types: from how it uses its
        parameters, else integer."""
        p = self.check(OBJ + "function f(e, b, n, m) local t = m; if b then e.frame = n; "
                       "t.x = 1 end end")
        self.assertEqual([s.ty for s in p.functions[0].body.params],
                         ["entity", "boolean", "integer", "entity"])
        self.assertEqual(p.warnings, [(3, 10, "f is never called from a handler, so the listing "
                                              "leaves it out")])

    def test_inference_through_chains_and_recursion(self):
        p = self.check(OBJ + """
function fact(n) if n <= 1 then return 1 end return n * fact(n - 1) end
function twice(x) return double(double(x)) end
function double(x) return x + x end
function A:step() local a = fact(5); local b = twice(0.25) end""")
        fns = {f.name: f for f in p.functions}
        self.assertEqual(fns["fact"].result, "integer")
        self.assertEqual(fns["twice"].result, "fixed")
        self.assertEqual(fns["double"].body.params[0].ty, "fixed")

    def test_a_function_called_with_integer_and_fixed_is_an_error(self):
        error = self.assert_error(OBJ + """function half(v) return v / 2 end
function A:step()
  local a = half(3)
  local b = half(self.x)
end""", r"half's parameter v is an integer \(from the call at line 5\), and this argument "
                                  r"is fixed\n  hint: .*two versions", line=6)
        self.assertEqual(error.column, 18)

    def test_results_must_agree(self):
        self.assert_error(OBJ + """function pick(b) if b then return 1.5 end return 0 end
function A:step() local x = pick(true) end""",
                          r"pick returns fixed at line 3, and an integer here\n  hint: return one "
                          r"type: write 0\.0")

    def test_arity(self):
        self.assert_error(OBJ + "function f(a, b) end\nfunction A:step() f(1) end",
                          r"f takes 2 arguments, not 1")
        self.assert_error(OBJ + "function A:step() psg_play() end",
                          r"psg_play takes 1 argument, not 0")
        self.assert_error(OBJ + "function A:step() text_print_number(1, 2) end",
                          r"text_print_number takes 3 or 4 arguments, not 2")
        self.assert_error(OBJ + "function A:step() text_print(1, 2, 'HI', 3) end",
                          r"text_print takes 3 arguments, not 4")

    def test_engine_argument_types(self):
        self.assert_error(OBJ + "function A:step() camera_set(self.x, 0) end",
                          r"camera_set's first argument is an integer, and this is fixed\n  hint: "
                          r"math\.floor")
        self.assert_error(OBJ + "function A:step() text_print_number(1, 1, self.x) end",
                          r"text_print_number prints an integer, and this is fixed\n  hint: "
                          r"math\.floor")
        self.assert_error(OBJ + "function A:step() text_print_number(1, 1, 'HI') end",
                          r"text_print_number prints an integer, and this is a string\n  hint: "
                          r"text_print\(col, row, \"text\"\)")
        self.assert_error(OBJ + "function A:step() print(1, 1, 'HI') end",
                          r"print \(console output\) is not in the subset\n  hint: "
                          r"text_print\(col, row, \"text\"\) draws text on the screen, "
                          r"text_print_number\(col, row, n\) a number")
        self.assert_error(OBJ + "function A:step() text_print(1, 1, 7) end",
                          r"text_print draws a literal string, and this is an integer\n  hint: "
                          r"text_print_number\(col, row, n\)")
        self.assert_error(OBJ + "function A:step() kill(3) end",
                          r"kill's first argument is an entity, and this is an integer")
        self.assert_error(OBJ + "function A:step() text_print(1, 1, 'caf\\xe9') end",
                          r"text_print draws printable ASCII \(32 to 126\), and this string has the "
                          r"byte 233")
        p = self.check(OBJ + "function A:step() local e = spawn(A, 10, 20.5); "
                       "local d = button_down(1); local r = random_range(1, 6) end")
        self.assertEqual(local_types(p, "A:step"), {"e": "entity", "d": "boolean", "r": "integer"})

    def test_field_slots_by_name(self):
        p = self.check(OBJ + """function A:step() self.hp = 3; self.speed = 1.5 end
function B:create() self.hp = 1; self.target = none end
function hurt(e) e.hp = e.hp - 1 end
function A:destroy() hurt(self) end""")
        fields = {name: (f.slot, f.ty) for name, f in p.fields.items()}
        self.assertEqual(fields, {"hp": (0, "integer"), "speed": (1, "fixed"),
                                  "target": (2, "entity")})

    def test_field_types_agree_program_wide(self):
        self.assert_error(OBJ + "function A:step() self.hp = 3 end\n"
                          "function B:create() self.hp = true end",
                          r"the field hp is an integer \(from its first value, line 3\), and this "
                          r"is a boolean")

    def test_a_field_read_before_any_write_is_an_integer(self):
        p = self.check(OBJ + "function A:step() local n = self.count end")
        self.assertEqual(p.fields["count"].ty, "integer")

    def test_waits_in_behaviours_and_their_functions(self):
        p = self.check(OBJ + """function hover(n) wait(n) end
function wander() hover(10); wait_move() end
function B:create() wander(); wait_anim() end
Room = object {}
function Room:room_start() hover(60) end""")
        self.assertEqual(p.warnings, [])

    def test_a_wait_reached_from_a_reaction(self):
        error = self.assert_error(OBJ + """function hover(n) wait(n) end
function wander() hover(10) end
function B:create() wander() end
function A:step() wander() end""",
                                  r"wait\(\) can be reached from A:step, a reaction \(A:step calls "
                                  r"wander at line 6, wander calls hover at line 4\)", line=3)
        self.assertEqual(error.column, 19)

    def test_waits_in_every_reaction(self):
        for event in ("step", "destroy", "anim_end", "collision"):
            with self.subTest(event=event):
                param = "other" if event == "collision" else ""
                self.assert_error(f"A = object {{}}\nfunction A:{event}({param}) wait_anim() end",
                                  rf"wait_anim\(\) in A:{event}, a reaction")

    def test_body_properties(self):
        """The body's tuning as C has it: body_max_fall a speed (fixed, as vy),
        body_bounce, body_friction and body_gravity integers (body_gravity
        written with BODY_GRAVITY(n), as in C), body_contact an integer to
        read."""
        p = self.check(OBJ + """function A:step()
  self.body_max_fall = 4      -- an integer: 4 pixels per frame
  self.body_max_fall = 2.5
  self.body_bounce = 224; self.body_friction = 16
  self.body_gravity = BODY_GRAVITY(8)
  local fall = self.body_max_fall
  local bounce = self.body_bounce
  local g = self.body_gravity
  local on_floor = self.body_contact & MAP_CONTACT_FLOOR ~= 0
end""")
        self.assertEqual(local_types(p, "A:step"), {"fall": "fixed", "bounce": "integer",
                                                    "g": "integer", "on_floor": "boolean"})
        self.assertNotIn("body_max_fall", p.fields)  # properties, not instance fields
        self.assert_error(OBJ + "function A:step() self.body_bounce = 0.5 end",
                          r"body_bounce is an integer \(an engine property\), and this is fixed")
        self.assert_error(OBJ + "function A:step() local n = 0; n = self.body_max_fall end",
                          r"n is an integer .*, and this is fixed")
        self.assert_error(OBJ + "function A:step() local g = GRAVITY(2) end",
                          r"constants aren't functions\n  hint: C_GAME\(n\) and BODY_GRAVITY\(n\) "
                          r"are the macros a script can call")

    def test_objects_compare(self):
        """An entity's object compares with an object's name, or with
        another entity's object; two names fold."""
        p = self.check(OBJ + """function A:collision(other)
  local coin = other.object == B
  local mine = other.object ~= self.object
  local folded = (A == B)
end""")
        self.assertEqual(local_types(p, "A:collision"), {"coin": "boolean", "mine": "boolean",
                                                         "folded": "boolean"})
        self.assert_error(OBJ + "function A:step() spawn(A, 0, 0).object = B end",
                          r"object is read-only")
        self.assert_error(OBJ + "function A:step() if self.object == none then end end",
                          r"comparing an object with an entity")
        self.assert_error(OBJ + "function f(o) end\nfunction A:step() f(self.object) end",
                          r"self\.object is an object, and objects are compared")

    def test_header_constants_pass_through(self):
        p = self.check(OBJ + "function A:step() if button_down(BUTTON_A | BUTTON_B) then "
                       "psg_play(SND_JUMP) end end")
        self.assertEqual(list(p.headers), ["BUTTON_A", "BUTTON_B", "SND_JUMP"])

    def test_scopes(self):
        p = self.check(OBJ + """local K <const> = 2
function A:step()
  local x = 1
  do local x = 1.5; self.x = x end
  for i = 1, K do local x = true end
  local y = x + K
end""")
        body = next(b for b in p.bodies if b.name == "A:step")
        self.assertEqual([(s.name, s.ty) for s in body.locals], [
            ("x", "integer"), ("x", "fixed"), ("i", "integer"), ("x", "boolean"),
            ("y", "integer")])


class ReservedNames(unittest.TestCase):
    """Instance fields can't start with the prefixes kept for later engine
    properties (lua.md, "Reserved names"); today's properties keep their
    names."""

    # The 1.0 properties without a reserved prefix. Every later one takes one.
    UNPREFIXED = {"x", "y", "vx", "vy", "sprite", "frame", "flags", "angle", "depth", "scale",
                  "tags", "object"}

    def refused(self, text):
        with self.assertRaises(svlua.CompileError) as caught:
            svlua.check(text, "t.lua")
        return caught.exception

    def test_every_prefix_is_refused_on_any_entity(self):
        self.assertEqual(svlua.RESERVED_PREFIXES, ("anim_", "body_", "ent_", "map_", "path_",
                                                   "pos_", "spr_", "vel_", "vm_"))
        for prefix in svlua.RESERVED_PREFIXES:
            name = prefix + "mine"
            for code in (f"self.{name} = 1", f"local n = self.{name}",
                         f"for e in instances(B) do e.{name} = 1 end",
                         f"spawn(B, 0, 0).{name} = 1"):
                with self.subTest(code=code):
                    error = self.refused(OBJ + f"function A:step() {code} end")
                    self.assertRegex(error.message, rf"\.{name}: {prefix} is reserved for engine "
                                     rf"properties, and {name} isn't one")
                    self.assertEqual(error.line, 3)

    def test_the_prefix_alone_is_refused(self):
        error = self.refused(OBJ + "function A:step() self.vm_ = 1 end")
        self.assertRegex(error.message, r"self\.vm_: vm_ is reserved")

    def test_properties_keep_their_names(self):
        p = svlua.check(OBJ + """function A:collision(other)
  self.body_w = 8; self.body_h = 8; self.anim_time = 0; self.anim_step = 0
  self.body_bounce = 224; self.body_friction = 16; self.body_max_fall = 2.5
  self.body_gravity = BODY_GRAVITY(8)
  local floor = other.body_contact & MAP_CONTACT_FLOOR ~= 0
end""", "t.lua")
        self.assertEqual(p.fields, {})

    def test_a_prefix_later_in_the_name_is_a_field(self):
        names = ("my_body_x", "nobody_w", "bodyx", "body", "entry", "sprite_x", "position",
                 "velocity", "mapped", "vmax", "paths", "animal", "Body_x", "BODY_X", "x_pos_")
        p = svlua.check(OBJ + "function A:step()\n" + "".join(
            f"  self.{name} = 1\n" for name in names) + "end", "t.lua")
        self.assertEqual(list(p.fields), list(names))

    def test_hints(self):
        error = self.refused(OBJ + "function A:step() self.body_speed = 1 end")
        self.assertEqual(error.hint, "rename the field, e.g. my_body_speed or bodyspeed (instance "
                         "fields can't start with anim_, body_, ent_, map_, path_, pos_, spr_, "
                         "vel_ or vm_)")
        # A typo of a property is told so; a C pool's name, its Lua name.
        error = self.refused(OBJ + "function A:step() self.body_bouce = 200 end")
        self.assertRegex(error.hint, r"^did you mean body_bounce\? If not, rename the field, "
                         r"e\.g\. my_body_bouce or bodybouce")
        error = self.refused(OBJ + "function A:collision(other) other.spr_angle = 0 end")
        self.assertEqual(error.hint, "spr_angle is C's name for it: in a script it is other.angle")
        for c_name, lua in svlua.C_POOL_PROPERTIES.items():
            with self.subTest(c_name=c_name):
                self.assertIn(lua, svlua.PROPERTIES)
                with open(ECS_H, encoding="utf-8") as f:
                    self.assertRegex(f.read(), rf"\b{c_name}\[MAX_ENT\]")

    def test_later_properties_take_a_reserved_prefix(self):
        """The promise to scripts: a property added after 1.0.0-rc.1 has a
        reserved prefix, so no script can be using its name as a field."""
        for name in svlua.PROPERTIES:
            with self.subTest(name=name):
                self.assertTrue(name in self.UNPREFIXED or
                                name.startswith(svlua.RESERVED_PREFIXES),
                                f"{name}: a new property's name starts with one of "
                                "RESERVED_PREFIXES (docs/lua.md, \"Reserved names\")")


# A fake engine's header: two planned functions (one with a marker of two
# strings), a planned enumerator, an ordinary function, and markers in a
# comment and a #define, which don't count.
PLANNED_HEADER = """\
#include "platform.h"
// SERVAL_PLANNED("commented out, docs/x.md#x") void fake_commented(void);
#define FAKE_MACRO(x) SERVAL_PLANNED("a macro, docs/x.md#x") void x(void)
typedef int Fake;
enum {
    FAKE_DONE = 0,
    FAKE_WAVE SERVAL_PLANNED("fake waves, docs/fake.md#waves") = 1,
};
void fake_done(int n);
SERVAL_PLANNED("fake things (some), docs/fake.md#fake-things")
void fake_play(int n, const unsigned char* data);
SERVAL_PLANNED(
    "more " "fake things, docs/fake.md#more")
Fake fake_more(void);
"""


class PlannedNames(unittest.TestCase):
    """The engine's planned functions (SERVAL_PLANNED in its headers) are
    reserved: a script can't declare them at the top level, as it can't
    declare today's builtins, and using one says it is planned. Locals may
    take them, as they may shadow builtins (lua.md, "Reserved names")."""

    # Each top-level declaration, NAME for the name: (the construct the
    # message names, the script).
    TOP_LEVEL = {
        "function": ("function", "function NAME() end"),
        "global": ("global", "NAME = 0"),
        "object": ("object", "NAME = object {}"),
        "array": ("array", "NAME = array(2)"),
        "rom array": ("array", "NAME = { 1, 2 }"),
        "local function": ("local function", "local function NAME() end"),
        "top-level local": ("local", "local NAME = 0"),
        "top-level constant": ("local", "local NAME <const> = 0"),
    }
    # Where a script's locals may take a builtin's name: (the script, the
    # body whose local or parameter NAME is).
    LOCALS = {
        "local in a handler": (OBJ + "function A:step() local NAME = 1; NAME = NAME + 1 end",
                               "A:step"),
        "local in a function": (OBJ + "function f() local NAME = 2 return NAME end\n"
                                "function A:step() local n = f() end", "f"),
        "parameter": (OBJ + "function f(NAME) return NAME + 1 end\n"
                      "function A:step() local n = f(1) end", "f"),
        "collision parameter": (OBJ + "function A:collision(NAME) kill(NAME) end", None),
        "numeric for": (OBJ + "function A:step() for NAME = 1, 3 do end end", "A:step"),
        "generic for": (OBJ + "function A:step() for NAME in instances(B) do kill(NAME) end end",
                        "A:step"),
    }

    def refused(self, text, functions=None):
        with self.assertRaises(svlua.CompileError) as caught:
            svlua.check(text, "t.lua", functions)
        return caught.exception

    def test_top_level_names_are_refused_as_builtins_are(self):
        for case, (construct, code) in self.TOP_LEVEL.items():
            with self.subTest(case=case):
                text = code + "\n" + OBJ
                error = self.refused(text.replace("NAME", "psg_play"))
                self.assertRegex(error.message, r"psg_play is an engine function")
                error = self.refused(text.replace("NAME", "music_play"))
                self.assertEqual(error.message, f"{construct} music_play: music_play is reserved: "
                                 "it names a planned engine function, which a later engine "
                                 "version may make a builtin")
                self.assertEqual(error.line, 1)
                for name in ("my_music_play", "music_play_x", "Music_play", "MUSIC_PLAY"):
                    svlua.check(text.replace("NAME", name), "t.lua")

    def test_locals_may_take_them_as_they_may_shadow_builtins(self):
        """A local's meaning can't change when a builtin of its name
        arrives: in its scope the name is the local."""
        for case, (code, body_name) in self.LOCALS.items():
            for name in ("psg_play", "music_play"):
                with self.subTest(case=case, name=name):
                    p = svlua.check(code.replace("NAME", name), "t.lua")
                    if body_name is not None:
                        body = next(b for b in p.bodies if b.name == body_name)
                        self.assertIn(name, [s.name for s in body.params + body.locals])

    def test_a_local_runs_as_a_local(self):
        if not RUNNER:
            self.skipTest("no svlua_runner")
        vm = run_vm(OBJ + "function A:step() local music_play = 6; local sfx_play = 7\n"
                    "  text_print_number(0, 0, music_play * sfx_play) end", attach=["A"])
        self.assertEqual(vm.calls_of("TEXT_PRINT_NUMBER"), [(0, 0, 42, 0)])

    def test_using_one_says_it_is_planned(self):
        uses = {
            "a call": "function A:step() music_play(0, true) end",
            "a value": "function A:step() local f = music_play end",
            "an assignment": "function A:step() music_play = 1 end",
            "past a local's scope": "function A:step() do local music_play = 1 end "
                                    "music_play(0, true) end",
            "a parameter elsewhere": "function f(music_play) return music_play end\n"
                                     "function A:step() local n = f(1); music_play(0, true) end",
        }
        for case, code in uses.items():
            with self.subTest(case=case):
                error = self.refused(OBJ + code)
                self.assertRegex(error.message, r"^music_play is planned, not implemented in this "
                                 r"engine version \(tracker music, docs/audio\.md#tracker-music\)")

    def test_the_messages(self):
        error = self.refused(OBJ + "function music_play(song) end")
        self.assertEqual(str(error), "t.lua:3:10: error: function music_play: music_play is "
                         "reserved: it names a planned engine function, which a later engine "
                         "version may make a builtin\n"
                         "  hint: rename it, e.g. my_music_play (music_play is planned: tracker "
                         "music, docs/audio.md#tracker-music)")
        error = self.refused(OBJ + "function A:step()\n  sfx_play(SFX_JUMP)\nend")
        self.assertEqual(str(error), "t.lua:4:3: error: sfx_play is planned, not implemented in "
                         "this engine version (sampled sound effects, "
                         "docs/audio.md#sampled-sound-effects): scripts can't use it yet\n"
                         "  hint: planned API reaches scripts in the engine version that "
                         "implements it, named as in C (docs/releases.md#planned-api)")

    def headers_planned_functions(self):
        """The planned functions in the engine's headers, found here by a
        rule of the test's own: a SERVAL_PLANNED marker at the start of a
        line, and the declaration below it."""
        found = {}
        for header in sorted(os.listdir(os.path.join(ROOT, "include", "serval"))):
            with open(os.path.join(ROOT, "include", "serval", header), encoding="utf-8") as f:
                text = f.read()
            for m in re.finditer(r'^SERVAL_PLANNED\(\s*"([^"]*)"\s*\)\n[^;]*?(\w+)\(', text, re.M):
                found[m[2]] = m[1]
        return found

    def test_every_planned_function_is_reserved(self):
        planned = self.headers_planned_functions()
        self.assertIn("music_play", planned)
        self.assertEqual(svlua.planned_functions(), planned)
        for name, what in planned.items():
            with self.subTest(name=name):
                self.assertNotIn(name, svlua.ENGINE_NAMES)  # implemented: no longer planned
                error = self.refused(f"function {name}() end")
                self.assertEqual(error.hint, f"rename it, e.g. my_{name} ({name} is planned: "
                                 f"{what})")
                error = self.refused(OBJ + f"function A:step() {name}() end")
                self.assertTrue(error.message.startswith(f"{name} is planned, not implemented in "
                                                         f"this engine version ({what})"))

    def test_nothing_else_is_reserved(self):
        """Every name in the headers but the functions' (types, fields,
        parameters, variables, constants, planned enumerators among them)
        can name a script's function, unless the subset gives scripts that
        name."""
        functions = svlua.engine_functions()
        names = set()
        for header in os.listdir(os.path.join(ROOT, "include", "serval")):
            with open(os.path.join(ROOT, "include", "serval", header), encoding="utf-8") as f:
                names |= set(re.findall(r"\b[A-Za-z_]\w*", svlua._c_code(f.read())))
        names -= set(functions) | svlua.ENGINE_NAMES | set(svlua.STDLIB) | svlua.KEYWORDS
        for name in ("PSG_WAVE", "Entity", "SpriteAsset", "frame_times", "buttons", "pos_x",
                     "ent_mask", "on_entered"):
            self.assertIn(name, names)
        self.assertNotIn("psg_music_set_tempo", names)
        self.assertGreater(len(names), 350)
        for name in sorted(names):
            with self.subTest(name=name):
                svlua.check(f"function {name}() end", "t.lua")
        # The planned constants are constants: a script may name its own.
        p = svlua.check("PSG_WAVE = 1\nlocal MAP_LADDER <const> = 2\nSPRITE_BLEND = object {}",
                        "t.lua")
        self.assertEqual([s.name for s in p.top_order], ["PSG_WAVE", "MAP_LADDER", "SPRITE_BLEND"])

    def test_planned_constants_need_no_reservation(self):
        """A script's own name hides a header's constant, implemented or
        planned alike, so implementing a planned constant changes no
        script."""
        for name in ("MAP_CONTACT_FLOOR", "MAP_CONTACT_LADDER"):
            with self.subTest(name=name):
                listing = svlua.compile_source(
                    OBJ + f"local {name} <const> = 3\nn = 0\nfunction A:step() n = {name} end",
                    "t.lua")
                self.assertIn(f".const {name} 3", listing)
                assembled = assemble(listing, header_names(files=[MAP_H]))
                self.assertTrue(assembled.blob.startswith(svm.MAGIC))

    def write_engine(self, root, header=PLANNED_HEADER, tool=False):
        """A fake engine: include/serval/fake.h, and tools/svlua.py if tool."""
        os.makedirs(os.path.join(root, "include", "serval"))
        with open(os.path.join(root, "include", "serval", "fake.h"), "w") as f:
            f.write(header)
        if tool:
            os.makedirs(os.path.join(root, "tools"))
            with open(svlua.__file__, encoding="utf-8") as f, \
                    open(os.path.join(root, "tools", "svlua.py"), "w", encoding="utf-8") as g:
                g.write(f.read())
        return os.path.join(root, "include")

    def test_the_set_is_the_headers_planned_functions(self):
        with tempfile.TemporaryDirectory() as tmp:
            include = self.write_engine(os.path.join(tmp, "engine"))
            planned = svlua.planned_functions(include)
            self.assertEqual(planned, {"fake_play": "fake things (some), docs/fake.md#fake-things",
                                       "fake_more": "more fake things, docs/fake.md#more"})
            functions = svlua.engine_functions(include)
            self.assertEqual(functions, {"fake_done": None, **planned})
            error = self.refused("function fake_play() end", functions)
            self.assertEqual(error.hint, "rename it, e.g. my_fake_play (fake_play is planned: "
                             "fake things (some), docs/fake.md#fake-things)")
            error = self.refused(OBJ + "function A:step() fake_more() end", functions)
            self.assertRegex(error.message, r"^fake_more is planned, not implemented in this "
                             r"engine version \(more fake things, docs/fake\.md#more\)")
            # Not a function there: an ordinary name.
            svlua.check("function music_play() end\nfunction FAKE_WAVE() end", "t.lua",
                        functions)
            # Implemented: the marker goes, and the name stays reserved, as
            # an engine C function.
            implemented = re.sub(r'SERVAL_PLANNED\(\n[^)]*\)\n', "", PLANNED_HEADER)
            self.assertIn("\nFake fake_more(void);", implemented)
            include = self.write_engine(os.path.join(tmp, "implemented"), implemented)
            self.assertEqual(list(svlua.planned_functions(include)), ["fake_play"])
            functions = svlua.engine_functions(include)
            self.assertIsNone(functions["fake_more"])
            error = self.refused("function fake_more() end", functions)
            self.assertRegex(error.message, r"^function fake_more: fake_more is reserved: it is an "
                             r"engine C function")

    def test_the_tool_reads_the_headers_beside_it(self):
        """svlua.py, wherever it is run from, reads tools/../include/: the
        layout of the repository and of the release archive."""
        with tempfile.TemporaryDirectory() as tmp:
            engine = os.path.join(tmp, "engine")
            self.write_engine(engine, tool=True)
            game = os.path.join(tmp, "game")
            os.makedirs(game)

            def compile_(text):
                with open(os.path.join(game, "game.lua"), "w") as f:
                    f.write(text)
                return subprocess.run(
                    [sys.executable, os.path.join(engine, "tools", "svlua.py"), "compile",
                     "game.lua", "--check"], cwd=game, capture_output=True, text=True)

            r = compile_("function fake_play() end\n")
            self.assertEqual(r.returncode, 1)
            self.assertEqual(r.stderr, "game.lua:1:10: error: function fake_play: fake_play is "
                             "reserved: it names a planned engine function, which a later engine "
                             "version may make a builtin\n  hint: rename it, e.g. my_fake_play "
                             "(fake_play is planned: fake things (some), "
                             "docs/fake.md#fake-things)\n")
            r = compile_("A = object {}\nfunction music_play() end\n"
                         "function A:step() music_play() end\n")
            self.assertEqual((r.returncode, r.stderr), (0, ""))
            # Without the headers it can't know what is planned: an error.
            os.rename(os.path.join(engine, "include"), os.path.join(engine, "moved"))
            r = compile_("A = object {}\n")
            self.assertEqual(r.returncode, 1)
            self.assertIn(f"error: {os.path.join(engine, 'include', 'serval')}: not found "
                          "(tools/svlua.py reads the engine's functions, whose names scripts "
                          "can't take", r.stderr)

    def test_check_planned_reads_them_the_same_way(self):
        """tools/check-planned.py, which checks the markers, finds the names
        with svlua.py's planned_api(): the same planned names."""
        spec = importlib.util.spec_from_file_location(
            "check_planned", os.path.join(ROOT, "tools", "check-planned.py"))
        check_planned = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(check_planned)
        names, errors = check_planned.planned_names(os.path.join(ROOT, "include"))
        self.assertEqual(errors, [])
        self.assertLessEqual(set(svlua.planned_functions()), set(names))
        self.assertIn("PSG_WAVE", names)
        with tempfile.TemporaryDirectory() as tmp:
            bad = 'typedef struct { int x SERVAL_PLANNED("f, docs/x.md#x"); } Bad;\n'
            include = self.write_engine(tmp, PLANNED_HEADER + bad)
            names, errors = check_planned.planned_names(include)
            self.assertEqual(set(names), {"FAKE_WAVE", "fake_play", "fake_more"})
            self.assertEqual(len(errors), 1)
            self.assertRegex(errors[0], r"fake\.h:15: SERVAL_PLANNED goes before a function "
                             r"declaration or after an enumerator's name")


# A fake engine's header with what a header may hold around its functions:
# declarations and definitions in comments and macros, typedefs (of a
# function type too), pointers to functions (variables, fields, parameters),
# extern variables, an enum with a planned enumerator, a static assertion,
# C++'s extern "C" block, two functions in one declaration, an attribute,
# a planned function, a function returning a pointer to a function, and
# static inline functions whose bodies call functions, and a function
# declared again.
C_HEADER = """\
#include "platform.h"
// void fake_commented(void);
/* void fake_block_commented(void);
   static inline int fake_commented_inline(void) { return 0; } */
#define FAKE_MACRO(x) ((x) + 1)
#define fake_lower_macro(x) \\
    void fake_macro_made(x)
#ifdef FAKE_GBA
void fake_conditional(void);
#endif
typedef void (*FakeCallback)(int);
typedef int fake_function_type(int);
typedef struct {
    int (*fake_field)(int);
    void (*fake_other_field)(void);
} Fake;
enum {
    FAKE_A = 0,
    FAKE_B SERVAL_PLANNED("fake b, docs/fake.md#b") = 1,
};
extern int fake_values[FAKE_MACRO(3)], fake_more_values[2];
extern void (*fake_hook)(void);
Fake (*fake_factory)(void);
FakeCallback fake_callback;
_Static_assert(sizeof(Fake) > 0, "fake (size)");
#ifdef __cplusplus
extern "C" {
#endif
void fake_plain(int n);
const char* fake_name(const Fake* f, void (*done)(int code));
int fake_one(void), fake_two(int x);
void fake_noreturn(int code) __attribute__((noreturn));
SERVAL_PLANNED("fake things, docs/fake.md#things")
void fake_planned(int n);
void (*fake_returns_hook(int which))(void);
static inline int fake_inline(int a) {
    struct { int x; } s = { a };
    if (a) { return fake_called(a); }
    return s.x;
}
static inline void fake_inline_empty(void) {}
void fake_plain(int n);
#ifdef __cplusplus
}
#endif
"""
# Its functions, and the lines of their names (the first declaration's).
C_HEADER_FUNCTIONS = {"fake_conditional": 9, "fake_plain": 29, "fake_name": 30, "fake_one": 31,
                      "fake_two": 31, "fake_noreturn": 32, "fake_planned": 34,
                      "fake_returns_hook": 35, "fake_inline": 36, "fake_inline_empty": 41}


class CFunctionNames(unittest.TestCase):
    """Every function of the engine's C API (include/serval/*.h), planned or
    not, is reserved: a script can't declare its name at the top level, as
    a builtin by its name may come in a later version. Builtins keep their
    rules, planned functions their messages, and locals may take the names
    (lua.md, "Reserved names")."""

    # Implemented, no builtin: a function, one a static inline defines, and
    # functions scripts have in another form (spawn, kill, instances).
    NAMES = ("sprite_draw", "fx_mul", "ent_has", "entity_create", "psg_music_set_tempo")

    def refused(self, text, functions=None):
        with self.assertRaises(svlua.CompileError) as caught:
            svlua.check(text, "t.lua", functions)
        return caught.exception

    def test_top_level_names_are_refused(self):
        for case, (construct, code) in PlannedNames.TOP_LEVEL.items():
            for name in self.NAMES:
                with self.subTest(case=case, name=name):
                    text = code.replace("NAME", name) + "\n" + OBJ
                    error = self.refused(text)
                    self.assertEqual(error.message, f"{construct} {name}: {name} is reserved: it "
                                     "is an engine C function, which scripts may get as a "
                                     "builtin in a later version")
                    self.assertEqual(error.hint, f"rename it, e.g. my_{name} (scripts can't take "
                                     "the engine's C function names at the top level, "
                                     "docs/lua.md#c-functions)")
                    self.assertEqual(error.line, 1)
            for name in ("my_sprite_draw", "sprite_draw_x", "Sprite_draw", "SPRITE_DRAW",
                         "fx_mul2", "draw"):
                with self.subTest(case=case, name=name):
                    svlua.check(code.replace("NAME", name) + "\n" + OBJ, "t.lua")

    def test_locals_may_take_them(self):
        """As for a builtin's name, and for the same reason: in its scope
        the name is the local, whatever the engine has."""
        for case, (code, body_name) in PlannedNames.LOCALS.items():
            for name in self.NAMES:
                with self.subTest(case=case, name=name):
                    p = svlua.check(code.replace("NAME", name), "t.lua")
                    if body_name is not None:
                        body = next(b for b in p.bodies if b.name == body_name)
                        self.assertIn(name, [s.name for s in body.params + body.locals])

    def test_a_local_runs_as_a_local(self):
        if not RUNNER:
            self.skipTest("no svlua_runner")
        vm = run_vm(OBJ + "function A:step() local sprite_draw = 6; local fx_mul = 7\n"
                    "  text_print_number(0, 0, sprite_draw * fx_mul) end", attach=["A"])
        self.assertEqual(vm.calls_of("TEXT_PRINT_NUMBER"), [(0, 0, 42, 0)])

    def test_using_one_says_it_has_no_builtin(self):
        uses = {
            "a call": "function A:step() sprite_draw(0, 1, 2, 3) end",
            "a value": "function A:step() local f = sprite_draw end",
            "an assignment": "function A:step() sprite_draw = 1 end",
            "past a local's scope": "function A:step() do local sprite_draw = 1 end "
                                    "sprite_draw(0) end",
        }
        for case, code in uses.items():
            with self.subTest(case=case):
                error = self.refused(OBJ + code)
                self.assertEqual(error.message, "sprite_draw is an engine C function, which "
                                 "scripts can't call: this engine version has no builtin for it")

    def test_the_messages(self):
        error = self.refused(OBJ + "function sprite_draw(id, x, y) end")
        self.assertEqual(str(error), "t.lua:3:10: error: function sprite_draw: sprite_draw is "
                         "reserved: it is an engine C function, which scripts may get as a "
                         "builtin in a later version\n"
                         "  hint: rename it, e.g. my_sprite_draw (scripts can't take the engine's "
                         "C function names at the top level, docs/lua.md#c-functions)")
        error = self.refused(OBJ + "function A:step()\n  map_load(0)\nend")
        self.assertEqual(str(error), "t.lua:4:3: error: map_load is an engine C function, which "
                         "scripts can't call: this engine version has no builtin for it\n"
                         "  hint: scripts call the engine through builtins, named after the C "
                         "functions they call (docs/lua.md#engine-functions)")

    def test_builtins_keep_their_rules(self):
        """A builtin's name is a C function's too (psg_play), and stays a
        builtin: declaring it is the builtins' error, using it calls it, and
        locals may take it."""
        functions = svlua.engine_functions()
        builtins = sorted(set(functions) & svlua.ENGINE_NAMES)
        self.assertIn("psg_play", builtins)
        self.assertIn("camera_set", builtins)
        for name in builtins:
            with self.subTest(name=name):
                self.assertIsNone(functions[name])  # implemented
                error = self.refused(f"function {name}() end")
                self.assertEqual(error.message, f"{name} is an engine function; a script can't "
                                 "redefine it")
                self.assertEqual(error.hint, "choose another name")
                svlua.check(OBJ + f"function A:step() local {name} = 1 end", "t.lua")
        listing = svlua.compile_source(OBJ + "function A:step() psg_play(2) camera_set(1, 2) end",
                                       "t.lua")
        self.assertIn("SYS PSG_PLAY", listing)
        self.assertIn("SYS CAMERA_SET", listing)

    def headers_functions(self):
        """The functions in the engine's headers, found here by a rule of
        the test's own: the headers start each declaration or definition at
        a line's start, as type name(..."""
        rule = re.compile(r"^(?!typedef\b)(?:static inline )?(?:const )?\w+\**[ \t]+\**(\w+)\(",
                          re.M)
        found = {}
        for header in sorted(os.listdir(os.path.join(ROOT, "include", "serval"))):
            with open(os.path.join(ROOT, "include", "serval", header), encoding="utf-8") as f:
                text = f.read()
            for m in rule.finditer(text):
                found.setdefault(m[1], (f"serval/{header}", text.count("\n", 0, m.start(1)) + 1))
        return found

    @staticmethod
    def places(functions):
        """c_functions()' places, relative to include/."""
        include = os.path.join(ROOT, "include")
        return {name: (os.path.relpath(header, include).replace(os.sep, "/"), line)
                for name, (header, line) in functions.items()}

    def test_the_reader_finds_every_function_in_the_headers(self):
        found = self.headers_functions()
        functions = svlua.c_functions(os.path.join(ROOT, "include"))
        self.assertEqual(self.places(functions), found)
        for name in ("serval_init", "sprite_draw", "button_secret_set", "debug_exit", "fx_mul",
                     "ent_has", "body_overlap", "music_play", "psg_play"):
            self.assertIn(name, found)
        for name in ("pos_x", "ent_mask", "on_entered", "Entity", "FX", "ECS_FOR_EACH", "C_GAME",
                     "SERVAL_PLANNED"):
            self.assertNotIn(name, functions)
        self.assertGreater(len(found), 150)
        # engine_functions(): those, the planned ones with their markers' text.
        self.assertEqual(svlua.engine_functions(),
                         {name: svlua.planned_functions().get(name) for name in found})

    def test_every_function_is_reserved(self):
        functions = svlua.engine_functions()
        for name in self.headers_functions():
            with self.subTest(name=name):
                error = self.refused(f"{name} = 0")
                if name in svlua.ENGINE_NAMES:
                    expected = f"{name} is an engine function; a script can't redefine it"
                elif functions[name] is not None:
                    expected = (f"global {name}: {name} is reserved: it names a planned engine "
                                "function, which a later engine version may make a builtin")
                else:
                    expected = (f"global {name}: {name} is reserved: it is an engine C function, "
                                "which scripts may get as a builtin in a later version")
                self.assertEqual(error.message, expected)

    def test_the_reader_agrees_with_gcc(self):
        """GCC's -aux-info lists every function a translation unit declares
        or defines, with its place: for one that includes every header, the
        reader's functions and places."""
        gcc = os.environ.get("SERVAL_GCC") or shutil.which("gcc")
        if not gcc:
            self.skipTest("no gcc")
        include = os.path.join(ROOT, "include")
        with tempfile.TemporaryDirectory() as tmp:
            source, aux = os.path.join(tmp, "all.c"), os.path.join(tmp, "aux.txt")
            with open(source, "w") as f:
                for header in sorted(os.listdir(os.path.join(include, "serval"))):
                    f.write(f'#include "serval/{header}"\n')
            r = subprocess.run([gcc, "-std=gnu17", "-fsyntax-only", "-aux-info", aux, "-I", include,
                                source], capture_output=True, text=True)
            if r.returncode and "aux-info" in r.stderr:
                self.skipTest(f"{gcc} has no -aux-info (not GCC)")
            self.assertEqual(r.returncode, 0, r.stderr)
            with open(aux) as f:
                lines = f.read().splitlines()
        found = {}
        for line in lines:
            m = re.match(r"/\* (.+):(\d+):\w+ \*/ [^(]*?(\w+) \(", line)
            if m and os.path.dirname(os.path.dirname(os.path.abspath(m[1]))) == \
                    os.path.abspath(include):
                path = os.path.relpath(m[1], include).replace(os.sep, "/")
                found.setdefault(m[3], (path, int(m[2])))
        self.assertGreater(len(found), 150)
        self.assertEqual(self.places(svlua.c_functions(include)), found)

    def test_the_reader_on_a_fake_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            os.makedirs(os.path.join(tmp, "serval"))
            path = os.path.join(tmp, "serval", "fake.h")
            with open(path, "w") as f:
                f.write(C_HEADER)
            functions = svlua.c_functions(tmp)
            self.assertEqual({name: line for name, (_, line) in functions.items()},
                             C_HEADER_FUNCTIONS)
            self.assertEqual({header for header, _ in functions.values()}, {path})
            self.assertEqual(svlua.engine_functions(tmp),
                             {name: "fake things, docs/fake.md#things" if name == "fake_planned"
                              else None for name in C_HEADER_FUNCTIONS})
            error = self.refused("function fake_inline() end", svlua.engine_functions(tmp))
            self.assertRegex(error.message, r"fake_inline is reserved: it is an engine C function")
            svlua.check("function fake_called() end\nfunction fake_hook() end\n"
                        "function fake_lower_macro() end\nfunction fake_function_type() end",
                        "t.lua", svlua.engine_functions(tmp))



# --- Assembling the listings -------------------------------------------------

VM = svm.load_vm()


def assemble(listing, headers, name="test.svm"):
    """The blob svm.py makes of a listing."""
    return svm.assemble(listing, VM, headers, name)


def header_names(defines=None, files=()):
    headers = svm.HeaderNames(VM.names)
    for path in files:
        headers.load(path)
    if defines:
        headers.add_text("".join(f"#define {k} {v}\n" for k, v in defines.items()), "test.h")
    headers.check()
    return headers


ECS_H = os.path.join(ROOT, "include", "serval", "ecs.h")
MAP_H = os.path.join(ROOT, "include", "serval", "map.h")
PHYSICS_H = os.path.join(ROOT, "include", "serval", "physics.h")
FIREFLIES_HEADERS = [os.path.join(ROOT, "examples", "fireflies", "game.h")] + [
    os.path.join(ROOT, "include", "serval", name)
    for name in ("ecs.h", "core.h", "sprites.h", "path.h", "screen.h")]


# --- Running compiled programs on the VM ---------------------------------------

# svlua_runner (tests/svlua/runner.c), built by the host preset: it runs a
# blob on the engine's own VM. CTest's svlua_tool says where; run directly,
# the tests look in build/host, and skip what needs it if it isn't there.
RUNNER = os.environ.get("SERVAL_SVLUA_RUNNER") or next(
    (p for p in [os.path.join(ROOT, "build", "host", "tests", "svlua_runner")]
     if os.path.exists(p)), None)
needs_runner = unittest.skipUnless(RUNNER, "no svlua_runner: build the host preset "
                                   "(CTest svlua_tool sets SERVAL_SVLUA_RUNNER)")

# The engine properties in VM_P_* order, as the runner prints them.
PROPS = ("X", "Y", "VX", "VY", "SPR", "FRAME", "FLAGS", "ANGLE", "DEPTH", "SCALE", "BODY_W",
         "BODY_H", "TAGS", "ANIM_TIME", "ANIM_STEP", "BODY_BOUNCE", "BODY_FRICTION",
         "BODY_MAX_FALL", "BODY_GRAVITY", "BODY_CONTACT")
FIELDS_AT = 2 + len(PROPS)  # an entity line: handle, object, the properties, the fields
SYS_ARITY = (1, 1, 0, 0, 0, 2, 3, 2, 1, 1, 1, 3, 4, 1)  # vm.md's SYS page


class VmRun:
    """What a compiled program did on the VM (svlua_runner's output), as
    cells: globals and arrays by their listing names (G_LIVES is "LIVES"),
    entities by handle ({"object": its listing name, "X": ..., and each
    instance field by its Lua name}), all as at the last printed frame;
    `states` has every printed frame's. calls: each engine call the
    platform made, (frame, SYS name, its arguments..., TEXT_PRINT's text).
    log: what the engine logged (warnings, TRACE)."""

    def __init__(self, compiled, assembled, output, log):
        program = compiled.program
        objects = [o.listing for o in program.objects]
        globals_ = list(assembled.globals)
        arrays = list(assembled.arrays)
        fields = sorted(program.fields.values(), key=lambda f: f.slot)
        self.calls, self.states, self.log = [], {}, log
        self.warnings = None
        frame = 0
        for line in output.splitlines():
            head, _, rest = line.partition(" ")
            if head == "frame":
                frame = int(rest)
            elif head == "call":
                words = rest.split(" ", 5)
                fn, args = int(words[0]), [int(w) for w in words[1:5]]
                call = (frame, VM.sys_names[fn], *args[:SYS_ARITY[fn]])
                if len(words) > 5:
                    call += (words[5][1:-1],)
                self.calls.append(call)
            elif head == "warnings":
                self.warnings = int(rest)
            else:
                state = self.states.setdefault(frame, ({}, {}, {}))
                values = [int(w) for w in rest.split()]
                if head == "global":
                    state[0][globals_[values[0]]] = values[1]
                elif head == "array":
                    state[1][arrays[values[0]]] = values[1:]
                elif head == "entity":
                    entity = {"object": objects[values[1]]}
                    entity.update(zip(PROPS, values[2:FIELDS_AT]))
                    entity.update((f.name, values[FIELDS_AT + f.slot]) for f in fields)
                    state[2][values[0]] = entity
        last = self.states[max(self.states)] if self.states else ({}, {}, {})
        self.globals, self.arrays, self.entities = last

    def calls_of(self, name):
        """The calls of one engine function, without their frames and name."""
        return [c[2:] for c in self.calls if c[1] == name]


def run_vm(source, frames=1, start=(), attach=(), buttons=(), set_=None, collide=(),
           movement=False, physics=None, seed=None, printed="last", headers=None, files=(),
           warnings=False):
    """Compiles a script, assembles it and runs it on the VM with
    svlua_runner: `start` objects' room_start threads (OBJ or (OBJ, EVENT),
    by listing name), `attach`ed instances ((OBJ, x, y) in pixels, or OBJ),
    `buttons` held ((frame, mask, length)), globals `set_` ({listing name:
    cell}), `collide` pairs ((OBJ, OTHER)); sys_movement with `movement`,
    and sys_physics with contacts and a gravity of `physics` 256ths when it
    isn't None. Unless `warnings`, the run must not warn. Returns a VmRun."""
    compiled = svlua.compile_program(source, "t.lua")
    assembled = assemble(compiled.listing, header_names(headers, files))
    objects = {name: k for k, name in enumerate(assembled.objects)}
    with tempfile.TemporaryDirectory() as tmp:
        blob = os.path.join(tmp, "t.bin")
        with open(blob, "wb") as f:
            f.write(assembled.blob)
        args = [RUNNER, blob, "--frames", str(frames), "--print", printed]
        if seed is not None:
            args += ["--seed", str(seed)]
        for name, value in (set_ or {}).items():
            args += ["--set", f"{assembled.globals.index(name)}:{value}"]
        for item in start:
            obj, event = (item, "ROOM_START") if isinstance(item, str) else item
            args += ["--start", f"{objects[obj]}:{event}"]
        for item in attach:
            obj, x, y = (item, 0, 0) if isinstance(item, str) else item
            args += ["--attach", f"{objects[obj]}:{x}:{y}"]
        for frame, mask, length in buttons:
            args += ["--buttons", f"{frame}:{mask}:{length}"]
        for obj, other in collide:
            args += ["--collide", f"{objects[obj]}:{objects[other]}"]
        if movement:
            args.append("--movement")
        if physics is not None:
            args += ["--physics", str(physics)]
        done = subprocess.run(args, capture_output=True, text=True, timeout=60)
    if done.returncode != 0:
        raise AssertionError(f"svlua_runner failed ({done.returncode}): {done.stderr}")
    run = VmRun(compiled, assembled, done.stdout, done.stderr)
    if not warnings and run.warnings:
        raise AssertionError(f"the run warned:\n{done.stderr}")
    return run


def lit(n):
    """An integer as a Lua expression: -2147483648 is a float in Lua (the
    literal 2147483648 doesn't fit 32 bits before the minus applies)."""
    return "math.mininteger" if n == INT_MIN else str(n)


# --- Code generation ---------------------------------------------------------


GOLDEN = ("arithmetic", "logic", "control", "frames", "entities", "arrays", "globals")
GOLDEN_HEADERS = {"SCREEN_W": 240, "FLAGS": 0x35, "MASK": 0xF0, "FIELD_TOP": 24, "C_POS": 1,
                  "C_VEL": 2, "C_SPR": 4, "C_BODY": 8, "SPR_BULLET": 0, "SPR_ENEMY": 1,
                  "PATH_MIRROR_X": 1, "BUTTON_A": 1, "BUTTON_B": 2, "SND_SHOOT": 0,
                  "SONG_WIN": 0}


class Golden(unittest.TestCase):
    """Small programs covering lua.md's "What compiles to what", compiled to
    the listings in tests/svlua/ (reviewed by hand; SVLUA_UPDATE_GOLDEN=1
    rewrites them)."""

    def test_listings(self):
        update = os.environ.get("SVLUA_UPDATE_GOLDEN") == "1"
        for name in GOLDEN:
            with self.subTest(name=name):
                with open(os.path.join(FIXTURES, name + ".lua"), encoding="utf-8") as f:
                    listing = svlua.compile_source(f.read(), name + ".lua")
                path = os.path.join(FIXTURES, name + ".svm")
                if update:
                    with open(path, "w", encoding="utf-8") as f:
                        f.write(listing)
                with open(path, encoding="utf-8") as f:
                    self.assertEqual(listing, f.read())

    def test_listings_assemble(self):
        headers = header_names(GOLDEN_HEADERS)
        for name in GOLDEN:
            with self.subTest(name=name):
                with open(os.path.join(FIXTURES, name + ".svm"), encoding="utf-8") as f:
                    result = assemble(f.read(), headers, name + ".svm")
                self.assertGreater(len(result.blob), 16)

    def test_every_row_of_the_table(self):
        """Each row of lua.md's "What compiles to what" shows up in the
        golden listings."""
        text = ""
        for name in GOLDEN:
            with open(os.path.join(FIXTURES, name + ".svm"), encoding="utf-8") as f:
                text += f.read()
        ops = {line.split()[0] for line in text.splitlines()
               if line.startswith("    ") and not line.strip().startswith(";")}
        for op in ("ADD", "SUB", "MUL", "NEG", "FXMUL", "FXDIV", "IDIV", "IMOD", "AND", "OR",
                   "XOR", "LSH", "BNOT", "EQ", "NE", "LT", "LE", "GT", "GE", "LNOT", "ENTER",
                   "LDL", "STL", "CALL", "RET", "RETV", "JMP", "JZ", "JNZ", "GETP", "SETP",
                   "LDA", "STA", "LEN", "NEXTI", "SPAWN", "KILL", "WAIT", "WAIT_ANIM",
                   "WAIT_MOVE", "SYS", "LDG", "STG", "SELF", "OTHER", "HALT"):
            with self.subTest(op=op):
                self.assertIn(op, ops)
        for sys_call in ("PSG_PLAY", "PSG_MUSIC_PLAY", "PSG_MUSIC_STOP", "PSG_MUSIC_PAUSE",
                         "PSG_MUSIC_RESUME",
                         "CAMERA_SET", "TEXT_PRINT", "TEXT_PRINT_NUMBER", "RANDOM_RANGE",
                         "BUTTON_DOWN", "BUTTON_PRESSED", "SCREEN_SET_BRIGHTNESS", "PATH_START",
                         "PATH_STOP"):
            with self.subTest(sys=sys_call):
                self.assertIn(f"SYS {sys_call}", text)
        self.assertIn("SETP FIELD_HP", text)  # an instance field
        self.assertIn(".const FIELD_HP VM_P_FIELD0 + 0", text)


class Listing(unittest.TestCase):
    def compile(self, text):
        return svlua.compile_source(text, "t.lua")

    def code(self, text):
        """The ops of a listing, without comments, one string per line."""
        return [svm._strip_comment(line).strip() for line in self.compile(text).splitlines()
                if svm._strip_comment(line).strip()]

    def test_every_line_maps_to_the_script(self):
        listing = self.compile(OBJ + "n = 0\nfunction A:step()\n  n = n + 1\nend")
        for line in listing.splitlines():
            if line.strip() and not line.lstrip().startswith(";"):
                self.assertRegex(line, r"; t\.lua:\d+")
        self.assertIn("    ; 5: n = n + 1\n", listing)

    def test_handlers_end_in_halt_and_functions_in_ret(self):
        code = self.code(OBJ + "function f() end\nfunction g() return 1 end\n"
                         "function A:step() f(); local x = g() end")
        self.assertEqual(code[code.index("f:") + 1], "RET")
        self.assertEqual(code[code.index("g:") + 1:code.index("g:") + 3], ["PUSH 1", "RETV"])
        self.assertEqual(code[-1], "HALT")
        code = self.code(OBJ + "function B:create() while true do wait(1) end end")
        self.assertEqual(code[-2:], ["JMP b_create_while1", "HALT"])  # not reached, but there

    def test_constant_folding(self):
        cases = {
            "n = 2 + 3 * 4": "PUSH 14", "n = -7 // 2": "PUSH -4", "n = -7 % 3": "PUSH 2",
            "n = 1 << 33": "PUSH 0", "n = -1 >> 1": "PUSH 2147483647",
            "n = 0x7FFFFFFF + 1": "PUSH -2147483648", "n = ~0 ~ 5": "PUSH -6",
            "f = 1.5": "PUSH 384", "f = 7 / 2": "PUSH 896", "f = 0.1 * 10": "PUSH 260",
            "f = 2 ^ -1": "PUSH 128", "f = -0.5 // 0.25": "PUSH -512", "f = 5.5 % 2": "PUSH 384",
            "n = math.floor(-1.5)": "PUSH -2", "n = math.abs(-3)": "PUSH 3",
            "n = math.max(3, 9, 4)": "PUSH 9", "f = 3": "PUSH 768",
            "n = #'four'": "PUSH 4", "b = 3 > 2 and not false": "PUSH 1",
            "n = MAX * 2 + 1": "PUSH MAX * 2 + 1", "n = (A_BIT | B_BIT) & ~C_BIT":
                "PUSH (A_BIT | B_BIT) & ~C_BIT", "f = MAX": "PUSH FX(MAX)",
            "f = MAX + 0.5": "PUSH FX(MAX) + 128", "n = MAX // 4": "PUSH MAX >> 2",
            "n = MAX % 8": "PUSH MAX & 7", "n = MAX << 2": "PUSH MAX << 2",
            "n = C_GAME(3)": "PUSH C_GAME(3)", "n = 5 * (3 ~ 3)": "PUSH 0",
            "n = math.mininteger": "PUSH -2147483648", "n = math.maxinteger": "PUSH 2147483647",
            "n = math.maxinteger + 1": "PUSH -2147483648",
            "n = math.mininteger // -1": "PUSH -2147483648",
            "n = -math.mininteger": "PUSH -2147483648",
            "b = math.mininteger < math.maxinteger": "PUSH 1",
        }
        for stat, op in cases.items():
            with self.subTest(stat=stat):
                code = self.code(OBJ + "n = 0\nf = 0.0\nb = false\n"
                                 f"function A:step() {stat} end")
                start = code.index(".handler A STEP")
                self.assertEqual(code[start + 1], op)

    def test_not_folded_where_the_assembler_would_differ(self):
        """A header constant's >>, // or % by a non-power of two, ^ and
        comparisons are computed at run time, since the assembler's
        integers wouldn't give Lua's result."""
        for stat, ops in {"n = MAX >> 2": ["PUSH MAX", "PUSH -2", "LSH"],
                          "n = MAX // 3": ["PUSH MAX", "PUSH 3", "IDIV"],
                          "n = MAX % 3": ["PUSH MAX", "PUSH 3", "IMOD"],
                          "b = MAX > 3": ["PUSH MAX", "PUSH 3", "GT"],
                          "n = MAX ~ 3": ["PUSH (MAX | 3) - (MAX & 3)"],
                          # a product of header constants may pass 32 bits, which the
                          # assembler wouldn't wrap: shifts and divisions of it run in code
                          "n = (MAX * MAX) // 2": ["PUSH MAX * MAX", "PUSH 2", "IDIV"],
                          "n = math.floor(MAX * 1.5)": ["PUSH MAX * 384", "PUSH 8", "SHR"],
                          "n = (MAX + 1) // 2": ["PUSH (MAX + 1) >> 1"]}.items():
            with self.subTest(stat=stat):
                code = self.code(OBJ + f"n = 0\nb = false\nfunction A:step() {stat} end")
                start = code.index(".handler A STEP")
                self.assertEqual(code[start + 1:start + 1 + len(ops)], ops)

    def test_frames(self):
        code = self.code(OBJ + "function f(a, b) local c = a + b; return c end\n"
                         "function g() return f(1, 2) end\n"
                         "function A:step() local x = g(); do local y = 1 end; local z = 2 end")
        self.assertEqual(code[code.index("f:") + 1], "ENTER 2, 1")
        self.assertNotIn("ENTER", code[code.index("g:") + 1])  # no locals: no frame
        self.assertEqual(code[code.index(".handler A STEP") + 1], "ENTER 0, 2")  # y's slot reused

    def test_scale_is_fixed_point(self):
        """spr_scale's 8.8 has 256 as one, as fixed values do: 1.5 is 384."""
        code = self.code(OBJ + "f = 0.0\nfunction A:step() self.scale = 1.5; "
                         "f = self.scale * 2; self.scale = 1 end")
        start = code.index(".handler A STEP")
        self.assertEqual(code[start + 1:start + 4], ["SELF", "PUSH 384", "SETP SCALE"])
        self.assertEqual(code[start + 4:start + 9],
                         ["SELF", "GETP SCALE", "PUSH 2", "MUL", "STG F"])
        self.assertEqual(code[start + 9:start + 12], ["SELF", "PUSH 256", "SETP SCALE"])
        with self.assertRaisesRegex(svlua.CompileError, "n is an integer .*, and this is fixed"):
            self.compile(OBJ + "n = 0\nfunction A:step() n = self.scale end")

    def test_body_properties_in_the_listing(self):
        """BODY_GRAVITY(n) goes to the assembler as written; body_max_fall
        is fixed point (2.5 is 640)."""
        code = self.code(OBJ + "function A:step() self.body_gravity = BODY_GRAVITY(0); "
                         "self.body_max_fall = 2.5; self.body_bounce = 255; "
                         "local c = self.body_contact end")
        start = code.index(".handler A STEP")
        self.assertEqual(code[start + 2:start + 11], [
            "SELF", "PUSH BODY_GRAVITY(0)", "SETP BODY_GRAVITY", "SELF", "PUSH 640",
            "SETP BODY_MAX_FALL", "SELF", "PUSH 255", "SETP BODY_BOUNCE"])
        self.assertEqual(code[start + 11:start + 14], ["SELF", "GETP BODY_CONTACT", "STL 0"])

    def test_objects_in_the_listing(self):
        """other.object == Coin is GETP OBJECT against the object's number."""
        code = self.code(OBJ + "n = 0\nfunction A:collision(other) if other.object == B then "
                         "n = 1 end; local same = A ~= A end")
        start = code.index(".handler A COLLISION")
        self.assertEqual(code[start + 2:start + 6], ["OTHER", "GETP OBJECT", "PUSH OBJ_B", "EQ"])
        self.assertIn("PUSH 0", code[start + 6:])  # A ~= A: false

    def test_planned_constants_are_refused_by_name(self):
        """A planned constant (MAP_CONTACT_LADDER) passes through the
        compiler, and the assembler says it is planned."""
        listing = self.compile(OBJ + "on = false\nfunction A:step() "
                               "on = self.body_contact & MAP_CONTACT_LADDER ~= 0 end")
        with self.assertRaisesRegex(svm.SvmError, r"MAP_CONTACT_LADDER is planned, not "
                                    r"implemented in this engine version \(ladders"):
            assemble(listing, header_names(files=[MAP_H]))

    def test_initial_values_in_the_blob(self):
        """.globals NAME=value: the blob carries the initial values (header
        flag bit 0), so no code sets them; zeros need none."""
        listing = self.compile(OBJ + "lives = 3\nspeed = 1.5\nalive = true\nhero = none\n"
                               "n = 0\nlow = math.mininteger\nfunction A:step() n = lives end")
        for line in (".globals LIVES=3 ", ".globals SPEED=384 ", ".globals ALIVE=1 ",
                     ".globals HERO ", ".globals N ", ".globals LOW=-2147483648 "):
            self.assertIn(line, listing)
        result = assemble(listing, header_names())
        self.assertEqual(result.global_values, [3, 384, 1, 0, 0, INT_MIN])
        self.assertEqual(result.blob[6], VM.flag_global_values)
        zeros = assemble(self.compile(OBJ + "n = 0\nb = false\nfunction A:step() n = 1 end"),
                         header_names())
        self.assertEqual(zeros.blob[6:8], bytes(2))  # no table: laid out as before

    def test_names_in_the_listing(self):
        listing = self.compile("Firefly = object {}\nlocal flag = false\nscores = array(3)\n"
                               "local K <const> = 3\nfunction Firefly:step() "
                               "text_print(1, 1, \"TIME UP!\"); scores[1] = K; flag = true end")
        for line in (".object FIREFLY mask=0 sprite=0", ".globals FLAG", ".array SCORES 3",
                     ".const K 3", '.string TIME_UP "TIME UP!"', ".handler FIREFLY STEP"):
            self.assertIn(line, listing)

    def test_strings_are_shared(self):
        listing = self.compile(OBJ + "function A:step() text_print(1, 1, 'HI'); "
                               "text_print(2, 2, 'HI'); text_print(3, 3, 'H' .. 'I') end")
        self.assertEqual(listing.count(".string"), 1)
        self.assertEqual(listing.count("PUSH STR_HI"), 3)

    def test_collisions_in_the_listing(self):
        with self.assertRaisesRegex(svlua.CompileError, r"score and SCORE \(line 1\) are both "
                                                        r"SCORE in the listing"):
            self.compile("SCORE = 0\nscore = 0")
        with self.assertRaisesRegex(svlua.CompileError, r"OBJ_A would name both"):
            self.compile("A = object {}\nlocal OBJ_A <const> = 1")


@needs_runner
class Semantics(unittest.TestCase):
    """What the generated code computes, run on the engine's VM
    (svlua_runner) and compared with Lua 5.4's rules (LUA_32BITS), written
    out here. tools/svlua_difftest.py compares with real Lua."""

    @staticmethod
    def lua_idiv(a, b):
        return svlua.wrap32(a // b)

    @staticmethod
    def lua_mod(a, b):
        return a % b

    @staticmethod
    def lua_shl(a, n):
        if n <= -32 or n >= 32:
            return 0
        u = a & 0xFFFFFFFF
        return svlua.wrap32(u << n if n >= 0 else u >> -n)

    def test_integer_operators(self):
        """Every pair of operands from two ROM arrays, a frame each."""
        av = (7, -7, 0, 1, -1, INT_MAX, INT_MIN, 123456789, -98765)
        bv = (2, -2, 3, -3, 1, -1, 31, 32, 33, -31, -32, 40, -40, 7, INT_MAX, INT_MIN)
        n = len(av) * len(bv)
        script = f"""Probe = object {{}}
as = {{ {", ".join(map(lit, av))} }}
bs = {{ {", ".join(map(lit, bv))} }}
q = array({n})
r = array({n})
l = array({n})
s = array({n})
x = array({n})
m = array({n})
function Probe:room_start()
  local k = 1
  for i = 1, #as do
    for j = 1, #bs do
      local a, b = as[i], bs[j]
      q[k] = a // b; r[k] = a % b; l[k] = a << b; s[k] = a >> b; x[k] = a ~ b
      m[k] = -a + a * b
      k = k + 1
      wait(1)
    end
  end
end"""
        vm = run_vm(script, frames=n + 1, start=["PROBE"])
        k = 0
        for a in av:
            for b in bv:
                with self.subTest(a=a, b=b):
                    got = [vm.arrays[name][k] for name in ("Q", "R", "L", "S", "X", "M")]
                    self.assertEqual(got, [self.lua_idiv(a, b), self.lua_mod(a, b),
                                           self.lua_shl(a, b), self.lua_shl(a, -b),
                                           svlua.wrap32(a ^ b), svlua.wrap32(-a + a * b)])
                k += 1

    def test_fixed_point_within_a_256th_per_operation(self):
        av = (1.5, -2.25, 100.0, 0.0039, -0.5, 300.75)
        bv = (0.5, -3.0, 7.25, 1.0, -0.125)
        n = len(av) * len(bv)
        script = f"""Probe = object {{}}
as = {{ {", ".join(map(str, av))} }}
bs = {{ {", ".join(map(str, bv))} }}
m = array({n})
d = array({n})
s = array({n})
i = array({n})
function Probe:room_start()
  local k = 1
  for p = 1, #as do
    for q = 1, #bs do
      local a, b = as[p], bs[q]
      m[k] = a * b; d[k] = a / b; s[k] = a - b + 1; i[k] = math.floor(a)
      k = k + 1
      wait(1)
    end
  end
end"""
        vm = run_vm(script, frames=n + 1, start=["PROBE"])
        k = 0
        for a in av:
            for b in bv:
                with self.subTest(a=a, b=b):
                    fa, fb = round(a * 256) / 256, round(b * 256) / 256
                    self.assertAlmostEqual(vm.arrays["M"][k] / 256, fa * fb, delta=1 / 256)
                    self.assertAlmostEqual(vm.arrays["D"][k] / 256, fa / fb, delta=1 / 256)
                    self.assertEqual(vm.arrays["S"][k] / 256, fa - fb + 1)
                    self.assertEqual(vm.arrays["I"][k], int(fa // 1))
                k += 1

    @staticmethod
    def lua_for(start, limit, step):
        """Lua 5.4's integer for loop (lvm.c forprep and OP_FORLOOP): the
        values it gives, from the count it computes before starting."""
        if step > 0 and start > limit or step < 0 and start < limit:
            return []
        if step > 0:
            count = ((limit - start) & 0xFFFFFFFF) // step
        else:
            count = ((start - limit) & 0xFFFFFFFF) // ((-(step + 1) & 0xFFFFFFFF) + 1)
        return [svlua.wrap32(start + k * step) for k in range(min(count + 1, 50))]

    FOR_CASES = [(1, 10, 1), (10, 1, 1), (1, 1, 1), (10, 1, -1), (1, 10, -1), (1, 10, 3),
                 (10, 1, -4), (0, 0, 7), (INT_MAX - 2, INT_MAX, 1), (INT_MIN + 2, INT_MIN, -1),
                 (INT_MAX - 10, INT_MAX, 4), (INT_MIN, INT_MAX, 1 << 30),
                 (INT_MAX, INT_MIN, -(1 << 30)), (INT_MAX, INT_MIN, INT_MIN),
                 (INT_MIN, INT_MAX, INT_MAX), (-5, 5, INT_MAX), (0, INT_MAX, 1 << 29),
                 (-3, -3, -2), (5, -5, -3)]

    def check_loop(self, header, start, limit, step, consts):
        """Up to 50 rounds of a loop in one go: over the VM's budget, so it
        is spread over frames (a behaviour is throttled, with a warning)."""
        script = f"""Probe = object {{}}
lo = {lit(start)}
hi = {lit(limit)}
st = {lit(step)}
count = 0
last = 0
sum = 0
function Probe:room_start()
  {header}
    count = count + 1
    last = i
    sum = sum + i
    if count == 50 then break end
  end
end"""
        expected = self.lua_for(start, limit, step)
        vm = run_vm(script, frames=10, start=["PROBE"], headers=consts, warnings=True)
        self.assertEqual(vm.globals["COUNT"], len(expected))
        if expected:
            self.assertEqual(vm.globals["LAST"], expected[-1])
        self.assertEqual(vm.globals["SUM"], svlua.wrap32(sum(expected)))

    def test_integer_for_loops_count_as_lua_does(self):
        for start, limit, step in self.FOR_CASES:
            with self.subTest(start=start, limit=limit, step=step, step_is="run time"):
                self.check_loop("for i = lo, hi, st do", start, limit, step, None)
            with self.subTest(start=start, limit=limit, step=step, step_is="constant"):
                self.check_loop(f"for i = lo, hi, {lit(step)} do", start, limit, step, None)
            with self.subTest(start=start, limit=limit, step=step, step_is="a header constant"):
                self.check_loop("for i = lo, hi, STEP do", start, limit, step, {"STEP": step})
            with self.subTest(start=start, limit=limit, step=step, everything="constant"):
                self.check_loop(f"for i = {lit(start)}, {lit(limit)}, {lit(step)} do", start,
                                limit, step, None)
        for start, limit in ((1, 4), (4, 1), (INT_MAX - 1, INT_MAX)):
            with self.subTest(start=start, limit=limit, step="none"):
                self.check_loop("for i = lo, hi do", start, limit, 1, None)

    def test_a_run_time_step_of_zero_stops_the_script(self):
        vm = run_vm(OBJ + "n = 0\nst = 0\nfunction A:room_start()\n"
                    "  for i = 1, 10, st do n = n + 1 end\n  n = 99\nend", start=["A"])
        self.assertEqual(vm.globals["N"], 0)
        self.assertIn("'for' step is zero", vm.log)

    def test_for_evaluates_its_limit_and_step_once(self):
        vm = run_vm(OBJ + "n = 0\nhi = 3\nfunction A:room_start()\n"
                    "  for i = 1, hi do hi = hi + 1; i = i * 10; n = n + i end\nend",
                    start=["A"])
        self.assertEqual(vm.globals["N"], 10 + 20 + 30)

    def test_fixed_point_for_loops(self):
        cases = {"for v = 0.0, 1.0, 0.25 do": [0, 64, 128, 192, 256],
                 "for v = 1, 0, -0.5 do": [256, 128, 0],
                 "for v = 0.5, 2 do": [128, 384],
                 "for v = lo, hi, st do": [256, 192, 128]}
        for header, values in cases.items():
            with self.subTest(header=header):
                vm = run_vm("Probe = object {}\nn = 0\nsum = 0.0\nlo = 1.0\nhi = 0.390625\n"
                            f"st = -0.25\nfunction Probe:room_start()\n  {header}\n"
                            "    n = n + 1; sum = sum + v\n  end\nend", start=["PROBE"])
                self.assertEqual(vm.globals["N"], len(values))
                self.assertEqual(vm.globals["SUM"], sum(values))

    def test_an_integer_loop_with_a_fixed_limit(self):
        """Lua floors the limit going up and rounds it up going down."""
        for header, values in {"for i = 1, x do": [1, 2], "for i = 3, y, -1 do": [3, 2, 1],
                               "for i = 1, 2.5 do": [1, 2], "for i = 3, 0.5, -1 do": [3, 2, 1],
                               "for i = 1, x, st do": [1, 2], "for i = 3, y, -st do": [3, 2, 1]
                               }.items():
            with self.subTest(header=header):
                vm = run_vm("Probe = object {}\nn = 0\nsum = 0\nx = 2.5\ny = 0.5\nst = 1\n"
                            f"function Probe:room_start()\n  {header} n = n + 1; "
                            "sum = sum + i end\nend", start=["PROBE"])
                self.assertEqual((vm.globals["N"], vm.globals["SUM"]), (len(values), sum(values)))

    def test_short_circuit(self):
        script = OBJ + """calls = 0
r = false
function yes() calls = calls + 1; return true end
function no() calls = calls + 10; return false end
function A:room_start()
  r = no() and yes()        -- 10
  r = yes() or no()         -- 1
  if no() or yes() then calls = calls + 100 end   -- 11 + 100
  if yes() and no() then calls = calls + 1000 end -- 11
  r = not (yes() and yes()) -- 2
end"""
        vm = run_vm(script, start=["A"])
        self.assertEqual(vm.globals["CALLS"], 10 + 1 + 111 + 11 + 2)
        self.assertEqual(vm.globals["R"], 0)

    def test_recursion_and_frames(self):
        """fib(12) is thousands of ops: the VM spreads it over frames."""
        script = OBJ + """r1 = 0
r2 = 0
r3 = 0
function fact(n) if n <= 1 then return 1 end return n * fact(n - 1) end
function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end
function mix(a, b, c)
  local s = a * 100 + b * 10 + c
  local t = add3(c, b, a)
  return s - t
end
function add3(x, y, z) local w = x + y; return w + z end
function A:room_start()
  local before = 7
  r1 = fact(10)
  r2 = fib(12)
  r3 = mix(1, 2, 3) + before
end"""
        vm = run_vm(script, frames=60, start=["A"], warnings=True)
        self.assertEqual((vm.globals["R1"], vm.globals["R2"], vm.globals["R3"]),
                         (3628800, 144, 123 - 6 + 7))

    def test_math_functions(self):
        vm = run_vm(OBJ + """a = 0
b = 0
c = 0
d = 0.0
e = 0
function A:room_start()
  local n = -5
  a = math.abs(n) + math.abs(-n)
  b = math.max(n, 3, -9) * 100 + math.min(n, 3, -9)
  c = math.floor(-2.5) * 10 + math.floor(n)
  d = math.max(1.5, -2.0)
  e = math.abs(math.mininteger) + math.maxinteger
end""", start=["A"])
        self.assertEqual((vm.globals["A"], vm.globals["B"], vm.globals["C"], vm.globals["D"],
                          vm.globals["E"]), (10, 291, -35, 384, -1))

    def test_multiple_assignment(self):
        vm = run_vm(OBJ + """a = 1
b = 2
i = 3
t = array(5)
function A:room_start()
  a, b = b, a
  i, t[i] = i + 1, 20   -- Lua's manual: t[3] is set, i becomes 4
end""", start=["A"])
        self.assertEqual((vm.globals["A"], vm.globals["B"], vm.globals["I"]), (2, 1, 4))
        self.assertEqual(vm.arrays["T"], [0, 0, 20, 0, 0])

    def test_arrays(self):
        vm = run_vm(OBJ + """t = array(4)
rom = { 10, -20, 300 }
total = 0
n = 0
function A:room_start()
  for i = 1, #t do t[i] = rom[(i - 1) % #rom + 1] * i end
  for i = 1, #t do total = total + t[i] end
  n = #rom
end""", start=["A"])
        self.assertEqual(vm.arrays["T"], [10, -40, 900, 40])
        self.assertEqual((vm.globals["TOTAL"], vm.globals["N"]), (910, 3))

    def test_goto_continue(self):
        vm = run_vm(OBJ + """n = 0
function A:room_start()
  for i = 1, 5 do
    if i % 2 == 0 then goto continue end
    n = n + i
    ::continue::
  end
  local k = 0
  ::again::
  k = k + 1
  if k < 3 then goto again end
  n = n * 10 + k
end""", start=["A"])
        self.assertEqual(vm.globals["N"], 93)

    def test_instances_and_entities(self):
        """A loop over an object's instances (attached, Create still queued
        included), in slot order, which a kill (queued) doesn't cut short."""
        script = """Enemy = object { components = 1 }
Boss = object {}
Probe = object {}
count = 0
hp = 0
function Probe:room_start()
  for e in instances(Enemy) do
    count = count + 1
    e.hp = e.hp + count
    e.x = e.x + 1
    if count == 2 then kill(e) end
  end
end
function Enemy:step() hp = hp + self.hp end"""
        vm = run_vm(script, start=["PROBE"], attach=["ENEMY", "BOSS", ("ENEMY", 2, 0), "ENEMY"])
        self.assertEqual(vm.globals["COUNT"], 3)
        self.assertEqual(vm.globals["HP"], 1 + 3)  # the Step reactions of the two left
        enemies = {h: e for h, e in vm.entities.items() if e["object"] == "ENEMY"}
        self.assertEqual(sorted((h & 0xFF, e["X"], e["hp"]) for h, e in enemies.items()),
                         [(0, 256, 1), (3, 256, 3)])  # the one at 2 pixels was killed
        self.assertEqual([e["object"] for e in vm.entities.values()], ["ENEMY", "BOSS", "ENEMY"])

    @needs_runner
    def test_body_properties_reach_physics(self):
        """Body properties a script sets are what sys_physics uses, and
        body_contact is what it reported: a body resting on the floor of the
        bounds touches it; one with BODY_GRAVITY(0) floats; one with a
        body_max_fall of 1 falls no faster."""
        run = run_vm("""Rester = object { components = C_POS | C_VEL | C_BODY }
Floater = object { components = C_POS | C_VEL | C_BODY }
Faller = object { components = C_POS | C_VEL | C_BODY }
function Rester:create() self.body_w = 8; self.body_h = 8 end
function Rester:step()
  self.on_floor = self.body_contact & BODY_SIDE_BOTTOM ~= 0
  self.contact = self.body_contact
end
function Floater:create() self.body_w = 8; self.body_h = 8; self.body_gravity = BODY_GRAVITY(0) end
function Faller:create() self.body_w = 8; self.body_h = 8; self.body_max_fall = 1 end
""", frames=12, attach=[("RESTER", 20, 152), ("FLOATER", 60, 20), ("FALLER", 100, 0)],
                     movement=True, physics=64, files=[ECS_H, PHYSICS_H])
        rester, floater, faller = (next(e for e in run.entities.values() if e["object"] == name)
                                   for name in ("RESTER", "FLOATER", "FALLER"))
        self.assertEqual((rester["on_floor"], rester["contact"]), (1, 1))  # BODY_SIDE_BOTTOM
        self.assertEqual((rester["BODY_CONTACT"], rester["Y"]), (1, 152 * 256))
        self.assertEqual((floater["Y"], floater["VY"], floater["BODY_GRAVITY"]), (20 * 256, 0, -16))
        self.assertEqual((faller["VY"], faller["BODY_MAX_FALL"]), (256, 256))

    @needs_runner
    def test_kinematic_objects(self):
        """An object whose components have C_KINEMATIC: its spawns move only
        by their velocity, through sys_physics' gravity and bounds."""
        run = run_vm("""Gun = object {}
Shot = object { components = C_POS | C_VEL | C_BODY | C_KINEMATIC }
function Gun:room_start() spawn(Shot, 200, 20) end
function Shot:create() self.body_w = 4; self.body_h = 4; self.vx = 3 end
""", frames=20, start=["GUN"], movement=True, physics=64, files=[ECS_H, PHYSICS_H])
        shot = next(e for e in run.entities.values() if e["object"] == "SHOT")
        self.assertEqual((shot["X"], shot["Y"]), ((200 + 3 * 20) * 256, 20 * 256))  # past 240
        self.assertEqual((shot["VX"], shot["VY"], shot["BODY_CONTACT"]), (3 * 256, 0, 0))

    @needs_runner
    def test_collisions_tell_objects_apart(self):
        """A Collision reaction tells what it hit by its object, whatever
        the tags; an unattached entity (C's) has none."""
        run = run_vm("""Hero = object { components = C_POS | C_BODY | C_GAME(0) }
Coin = object { components = C_POS | C_BODY | C_GAME(1) }
Spike = object { components = C_POS | C_BODY | C_GAME(1) }
coins = 0
spikes = 0
others = 0
function Hero:create() self.body_w = 8; self.body_h = 8 end
function Coin:create() self.body_w = 8; self.body_h = 8 end
function Spike:create() self.body_w = 8; self.body_h = 8 end
function Hero:collision(other)
  if other.object == Coin then coins = coins + 1
  elseif other.object == Spike then spikes = spikes + 1
  else others = others + 1 end
end
""", frames=2, attach=[("HERO", 10, 10), ("COIN", 12, 12), ("SPIKE", 14, 14), ("COIN", 9, 9),
                       ("COIN", 100, 100)], collide=[("HERO", "COIN"), ("HERO", "SPIKE")],
                     files=[ECS_H])
        self.assertEqual((run.globals["COINS"], run.globals["SPIKES"], run.globals["OTHERS"]),
                         (4, 2, 0))  # two coins and a spike touch, in each of 2 frames

    def test_globals_start_at_their_initial_values(self):
        vm = run_vm("Init = object {}\nlives = 3\nspeed = 1.5\nalive = true\nhero = none\n"
                    "low = math.mininteger\nhigh = math.maxinteger\n"
                    "function Init:room_start() lives = lives + 1 end", start=["INIT"])
        self.assertEqual(vm.globals, {"LIVES": 4, "SPEED": 384, "ALIVE": 1, "HERO": 0,
                                      "LOW": INT_MIN, "HIGH": INT_MAX})
        listing = svlua.compile_source("Init = object {}\nlives = 3\n", "t.lua")
        self.assertNotIn(".handler", listing)  # Init is an object like any other
        self.assertIn(".globals LIVES=3 ", listing)
        vm = run_vm("Init = object {}\nlives = 3\nfunction Init:create() end")
        self.assertEqual(vm.globals, {"LIVES": 3})  # set by vm_load alone

    def test_behaviours_waits_and_reactions(self):
        """A behaviour waits; Step reactions run every frame, on top of it;
        a kill (queued in the resume pass, drained before the Step pass)
        runs Destroy and halts the behaviour. Contexts resume in pool
        order: the thread's before the hero's."""
        script = """Probe = object {}
Hero = object { components = 1 }
ticks = 0
steps = 0
gone = 0
function Probe:room_start()
  wait(3)
  ticks = ticks + 1
  for e in instances(Hero) do kill(e) end
end
function Hero:create()
  while true do wait(1); ticks = ticks + 100 end
end
function Hero:step() steps = steps + 1 end
function Hero:destroy() gone = gone + 1 end"""
        vm = run_vm(script, frames=6, start=["PROBE"], attach=["HERO"], printed="all")
        self.assertEqual([vm.states[f][0]["STEPS"] for f in range(1, 7)], [1, 2, 3, 3, 3, 3])
        self.assertEqual([vm.states[f][0]["TICKS"] for f in range(1, 7)],
                         [0, 100, 200, 301, 301, 301])
        self.assertEqual(vm.globals["GONE"], 1)
        self.assertEqual(vm.entities, {})


class Tool(unittest.TestCase):
    def test_compile_to_a_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, out = os.path.join(tmp, "game.lua"), os.path.join(tmp, "game.svm")
            with open(src, "w") as f:
                f.write("A = object {}\nfunction A:step() end\nfunction unused() end\n")
            err = io.StringIO()
            with redirect_stderr(err):
                self.assertEqual(svlua.main(["compile", src, "-o", out]), 0)
            self.assertIn("game.lua:3:10: warning: unused is never called", err.getvalue())
            with open(out) as f:
                self.assertIn(".handler A STEP", f.read())

    def test_check_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "game.lua")
            with open(src, "w") as f:
                f.write("A = object {}\nfunction A:step() end\n")
            out = io.StringIO()
            with redirect_stdout(out):
                self.assertEqual(svlua.main(["compile", src, "--check"]), 0)
            self.assertEqual(out.getvalue(), "")

    def test_errors_exit_1_and_write_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, out = os.path.join(tmp, "bad.lua"), os.path.join(tmp, "bad.svm")
            with open(src, "w") as f:
                f.write("A = object {}\nfunction A:step()\n  local x = nil\nend\n")
            err = io.StringIO()
            with redirect_stderr(err):
                self.assertEqual(svlua.main(["compile", src, "-o", out]), 1)
            self.assertEqual(err.getvalue(), f"{src}:3:13: error: nil is not in the subset\n"
                             "  hint: use none for entities, and 0 or false for the other "
                             "types\n")
            self.assertFalse(os.path.exists(out))

    def test_api(self):
        listing = svlua.compile_source("A = object {}\nfunction A:step() end", "a.lua")
        self.assertIn("; a.lua:2", listing)
        self.assertEqual(svlua.check_source("A = object {}", "a.lua"), [])
        with self.assertRaises(svlua.CompileError) as caught:
            svlua.compile_source("x = ...", "a.lua")
        self.assertEqual((caught.exception.file, caught.exception.line, caught.exception.column),
                         ("a.lua", 1, 5))


# --- fireflies in Lua --------------------------------------------------------


class Fireflies(unittest.TestCase):
    """examples/fireflies/fireflies.lua, the whole fireflies game, against
    what the example's C glue expects, and the hand-written listing it
    replaced (tests/svm/fireflies.svm)."""

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(ROOT, "examples", "fireflies", "fireflies.lua"),
                  encoding="utf-8") as f:
            cls.source = f.read()
        cls.compiled = svlua.compile_program(cls.source, "fireflies.lua")
        cls.listing = cls.compiled.listing
        cls.headers = header_names(files=FIREFLIES_HEADERS)
        cls.constants = cls.headers.constants()

    def play(self, **kw):
        """The game on the VM. No paths, songs or animations are bound, so
        path_start and psg_music_play warn and do nothing (a firefly stays where
        it appears, and its waits for the path to end don't wait), and
        WAIT_ANIM warns and continues."""
        return run_vm(self.source, files=FIREFLIES_HEADERS, warnings=True, **kw)

    def test_compiles_without_warnings(self):
        self.assertEqual(self.compiled.warnings, [])

    def test_the_tables_match_the_hand_written_listing(self):
        """The objects, their components and sprites, and the globals, in
        the same order: what main.c and game.h rely on (OBJ_ROOM,
        OBJ_SPAWNER, G_RESTART, C_PLAYER, C_FIREFLY)."""
        path = os.path.join(ROOT, "tests", "svm", "fireflies.svm")
        with open(path, encoding="utf-8") as f:
            hand = f.read()

        def tables(text):
            objects, globals_ = [], []
            for line in text.splitlines():
                code = svm._strip_comment(line).strip()
                if code.startswith(".object"):
                    objects.append(code.replace(" ", ""))
                elif code.startswith(".globals"):
                    globals_ += code.split()[1:]
            return objects, globals_

        self.assertEqual(tables(self.listing), tables(hand))

    def test_assembles_with_the_games_headers(self):
        result = assemble(self.listing, self.headers, "fireflies.svm")
        self.assertEqual(result.objects, ["ROOM", "SPAWNER", "PLAYER", "FIREFLY", "SPARKLE",
                                          "RESTING"])
        self.assertEqual(result.globals[4], "RESTART")
        self.assertEqual(result.warnings, [])

    @needs_runner
    def test_a_round(self):
        """The Room's thread, as main.c starts it with the Spawner's: the
        HUD, the serval, the fade in, 60 seconds, time up, START, the fade
        out and the restart request."""
        k = self.constants
        # The fade in ends on frame 9, the 60th second on 3609, PRESS START
        # comes 90 frames later.
        vm = self.play(frames=3740, start=["ROOM", "SPAWNER"],
                      buttons=[(3720, k["BUTTON_START"], 1)])
        g = vm.globals
        self.assertEqual((g["RESTART"], g["PLAYING"], g["TIME"], g["SCORE"]), (1, 0, 0, 0))
        prints = [c[-1] for c in vm.calls_of("TEXT_PRINT")]
        self.assertEqual(prints[:2], ["SCORE", "TIME"])
        self.assertIn("CATCH THE FIREFLIES!", prints)
        self.assertEqual(prints[-3:], ["TIME UP!", "CAUGHT", "PRESS START"])
        start_shown = next(c[0] for c in vm.calls if c[1] == "TEXT_PRINT" and
                           c[-1] == "PRESS START")
        self.assertEqual(start_shown, 3609 + 90)
        levels = [c[0] for c in vm.calls_of("SCREEN_SET_BRIGHTNESS")]
        self.assertEqual(levels, list(range(-16, 1, 2)) + list(range(-2, -17, -2)))
        fade_out = [c[0] for c in vm.calls if c[1] == "SCREEN_SET_BRIGHTNESS"][-8:]
        self.assertEqual(fade_out, list(range(3720, 3728)))  # from the frame START is pressed
        times = [c[2] for c in vm.calls_of("TEXT_PRINT_NUMBER") if c[0] == 28]
        self.assertEqual(times, list(range(60, -1, -1)))
        self.assertEqual(vm.calls_of("PSG_PLAY").count((k["SND_TICK"],)), 10)
        # The serval sat down where it stood; the fireflies faded away.
        x = (k["SCREEN_W"] - k["SERVAL_BODY_W"]) // 2 * 256
        y = (k["FIELD_TOP"] + k["SCREEN_H"] - k["SERVAL_BODY_H"]) // 2 * 256
        self.assertEqual([(e["object"], e["X"], e["Y"]) for e in vm.entities.values()],
                         [("RESTING", x, y)])

    @needs_runner
    def test_a_firefly_life(self):
        k = self.constants
        vm = self.play(frames=200, attach=[("FIREFLY", 100, 50)], set_={"PLAYING": 1},
                      printed="all")
        first = vm.states[1]
        self.assertEqual(first[0]["LIVE"], 1)
        (firefly,) = first[2].values()
        self.assertEqual((firefly["SPR"], firefly["BODY_W"], firefly["BODY_H"],
                          firefly["DEPTH"]), (k["SPR_FIREFLY"], 8, 8, 20))
        self.assertIn(firefly["FRAME"], range(k["FIREFLY_FRAMES"]))
        # Three or four flights, each with a hover of 10 to 40 frames, then
        # it fades (at once here) and is killed.
        life = max(f for f, state in vm.states.items() if state[2])
        self.assertIn(life, range(30, 161))
        self.assertEqual((vm.globals["LIVE"], vm.entities), (0, {}))

    @needs_runner
    def test_a_catch(self):
        """The serval touching a firefly, the collision pair main.c
        reports: the Collision reaction runs on top of the firefly's waiting
        Create."""
        k = self.constants
        vm = self.play(frames=1, attach=[("PLAYER", 100, 50), ("FIREFLY", 104, 54)],
                      set_={"SCORE": 9, "SPAWN_MIN": 40, "SPAWN_MAX": 90, "PLAYING": 1},
                      collide=[("FIREFLY", "PLAYER")])
        g = vm.globals
        self.assertEqual((g["SCORE"], g["SPAWN_MIN"], g["SPAWN_MAX"], g["LIVE"]), (10, 35, 78, 0))
        self.assertEqual(vm.calls_of("PSG_PLAY"), [(k["SND_CHIME"],), (k["SND_JINGLE"],)])
        self.assertIn((7, 0, 10, 0),
                      vm.calls_of("TEXT_PRINT_NUMBER"))
        objects = {e["object"]: e for e in vm.entities.values()}
        self.assertEqual(sorted(objects), ["PLAYER", "SPARKLE"])  # the firefly is gone
        self.assertEqual((objects["SPARKLE"]["X"], objects["SPARKLE"]["Y"]), (100 * 256, 50 * 256))
        self.assertEqual(objects["SPARKLE"]["DEPTH"], 30)
        self.assertEqual(objects["PLAYER"]["FLAGS"] & k["SPRITE_FLIP_H"], k["SPRITE_FLIP_H"])

    @needs_runner
    def test_the_serval_walks(self):
        k = self.constants
        vm = self.play(frames=2, attach=["PLAYER"], printed="all",
                      buttons=[(1, k["BUTTON_LEFT"] | k["BUTTON_DOWN"], 1)])
        (walking,) = vm.states[1][2].values()
        self.assertEqual((walking["VX"], walking["VY"]), (-384, 384))  # 1.5 pixels per frame
        self.assertEqual(walking["SPR"], k["SPR_SERVAL_WALK"])
        self.assertEqual(walking["FLAGS"] & k["SPRITE_FLIP_H"], k["SPRITE_FLIP_H"])
        (standing,) = vm.states[2][2].values()
        self.assertEqual((standing["VX"], standing["VY"], standing["SPR"]),
                         (0, 0, k["SPR_SERVAL_IDLE"]))
        self.assertEqual(standing["FLAGS"] & k["SPRITE_FLIP_H"], k["SPRITE_FLIP_H"])


if __name__ == "__main__":
    unittest.main()
