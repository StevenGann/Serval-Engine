#!/usr/bin/env python3
"""Tests for tools/svlua.py, the Lua-subset compiler, run by CTest in the host
build (svlua_tool).

Groups: the lexer and the parser (Lua 5.4's tokens, grammar, precedence and
associativity); what the subset rejects (one test per construct, checking the
message and its line and column); names and types (promotion, conflicts,
conditions, inference from call sites, fields, the wait rule); code generation
(golden listings in tests/svlua/, constant folding, frames, loops); and the
fireflies game in Lua. Run directly: python3 tools/svlua_test.py
(SVLUA_UPDATE_GOLDEN=1 rewrites the golden listings from the compiler).
"""

import io
import os
import re
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
    "varargs_value": (OBJ + "function A:step() print(1, 1, ...) end", 3, 31, r"varargs"),
    "multiple_results": ("function f() return 1, 2 end", 1, 24,
                         r"multiple results are not in the subset"),
    "string_at_run_time": (OBJ + "n = 0\nfunction A:step() print(1, 1, 'n=' .. n) end", 4, 39,
                           r"string operations at run time are not in the subset"),
    "string_library": (OBJ + "function A:step() print(1, 1, string.rep('a', 2)) end", 3, 31,
                       r"the string library .* is not in the subset"),
    "string_variable": (OBJ + "function A:step() local s = 'hi' end", 3, 29,
                        r"strings exist only as print's argument"),
    "standard_library": (OBJ + "function A:step() local x = math.sin(1) end", 3, 29,
                         r"math\.sin is not in the subset: .*math\.floor, math\.abs, math\.min "
                         r"and math\.max"),
    "math_tointeger": (OBJ + "function A:step() local x = math.tointeger(1.0) end", 3, 29,
                       r"math\.tointeger is not in the subset"),
    "tostring": (OBJ + "function A:step() print(1, 1, tostring(3)) end", 3, 31,
                 r"tostring .* is not in the subset"),
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
    "top_level_call": ("print(1, 1, 'hi')", 1, 1, r"a call at the top level"),
    "top_level_field": ("A = object {}\nA.x = 1", 2, 1,
                        r"only names are assigned at the top level"),
    "local_without_value": (OBJ + "function A:step() local x end", 3, 25,
                            r"x gets no value, so it would be nil"),
    "missing_value": ("a, b = 1", 1, 4, r"b gets no value, so it would be nil"),
    "extra_value": ("a = 1, 2", 1, 8, r"more values than names"),
    "global_not_constant": ("a = 0\nb = a", 2, 5, r"the initial value of b must be a constant"),
    "global_initial_value": ("speed = 3", 1, 9, r"speed starts at 3, but the VM zeroes globals"),
    "global_undeclared": (OBJ + "function A:step() count = 1 end", 3, 19, r"count is not defined"),
    "header_assigned": (OBJ + "function A:step() MAX = 1 end", 3, 19,
                        r"MAX is not declared, so it would be a constant from the C headers"),
    "assign_const": (OBJ + "local K <const> = 1\nfunction A:step() K = 2 end", 4, 19,
                     r"attempt to assign to const variable 'K'"),
    "assign_self": (OBJ + "function A:step() self = none end", 3, 19,
                    r"self is the instance the handler runs for; it can't be assigned"),
    "redefine_engine": ("function print() end", 1, 10, r"print is an engine function"),
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
        self.assert_error(OBJ + "function A:step() play_sound() end",
                          r"play_sound takes 1 argument, not 0")
        self.assert_error(OBJ + "function A:step() print(1, 2) end",
                          r"print takes 3 or 4 arguments, not 2")

    def test_engine_argument_types(self):
        self.assert_error(OBJ + "function A:step() camera_set(self.x, 0) end",
                          r"camera_set's first argument is an integer, and this is fixed\n  hint: "
                          r"math\.floor")
        self.assert_error(OBJ + "function A:step() print(1, 1, self.x) end",
                          r"print shows integers and literal strings, and this is fixed")
        self.assert_error(OBJ + "function A:step() kill(3) end",
                          r"kill's first argument is an entity, and this is an integer")
        self.assert_error(OBJ + "function A:step() print(1, 1, 'caf\\xe9') end",
                          r"print draws printable ASCII \(32 to 126\), and this string has the "
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

    def test_header_constants_pass_through(self):
        p = self.check(OBJ + "function A:step() if button_down(BUTTON_A | BUTTON_B) then "
                       "play_sound(SND_JUMP) end end")
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



# --- Assembling the listings -------------------------------------------------

# The revised VM (docs/vm.md, milestone 6) is being built on another branch:
# until this tree's svm.py knows its opcodes and array directives, a listing
# is assembled through this shim, which rewrites each new construct into the
# bytes vm.md gives it. Once svm.py has ENTER, listings are assembled as they
# are and the shim goes unused (delete it then).
NEW_OPS = {"LDA": 0x0C, "STA": 0x0D, "LEN": 0x0E, "LSH": 0x1F, "IDIV": 0x26, "IMOD": 0x27,
           "RETV": 0x2D, "ENTER": 0x2E, "NEXTI": 0x3E}
NEW_PROPS = {"TAGS": 12, "ANIM_TIME": 13, "ANIM_STEP": 14}
VM = svm.load_vm()
REVISED = "ENTER" in svm.OPERANDS


def shim(listing):
    out = [".const VM_P_FIELD0 64"]
    arrays = 0
    for raw in listing.splitlines():
        code = svm._strip_comment(raw).strip()
        m = re.match(r"^\.(array|rom)\s+(\w+)", code)
        if m:
            out.append(f".const ARR_{m.group(2)} {arrays}")
            arrays += 1
            continue
        m = re.match(r"^(\w+)(?:\s+(.*))?$", code)
        if m and m.group(1) in NEW_OPS:
            op, operand = m.group(1), (m.group(2) or "").strip()
            if op == "ENTER":
                out.append(f"    .byte {NEW_OPS[op]}, {operand}")
            elif op in ("LDA", "STA", "LEN", "NEXTI"):
                table = "OBJ" if op == "NEXTI" else "ARR"
                out.append(f"    .byte {NEW_OPS[op]}, {table}_{operand} & 255, "
                           f"{table}_{operand} >> 8")
            else:
                out.append(f"    .byte {NEW_OPS[op]}")
        elif code == "SYS PATH_STOP":
            out.append("    SYS 13")
        elif re.match(r"^(GETP|SETP) (TAGS|ANIM_TIME|ANIM_STEP)$", code):
            out.append(f"    {code.split()[0]} {NEW_PROPS[code.split()[1]]}")
        else:
            out.append(raw)
    return "\n".join(out) + "\n"


def assemble(listing, headers, name="test.svm"):
    """The blob svm.py makes of a listing (through the shim until svm.py
    knows the revised VM)."""
    return svm.assemble(listing if REVISED else shim(listing), VM, headers, name)


def header_names(defines=None, files=()):
    headers = svm.HeaderNames(VM.names)
    for path in files:
        headers.load(path)
    if defines:
        headers.add_text("".join(f"#define {k} {v}\n" for k, v in defines.items()), "test.h")
    headers.check()
    return headers


FIREFLIES_HEADERS = [os.path.join(ROOT, "examples", "fireflies", "game.h")] + [
    os.path.join(ROOT, "include", "serval", name)
    for name in ("ecs.h", "core.h", "sprites.h", "path.h", "screen.h")]


# --- A model of the revised VM -----------------------------------------------


class ListingVM:
    """Runs a listing's handlers on a small model of docs/vm.md's revised VM
    (frames, arrays, NEXTI, LSH, IDIV, IMOD), to check what compiled code
    computes, not just how it reads. Waits don't suspend (they are logged),
    every SYS call is logged, random_range gives its low end and the buttons
    are self.buttons. A test model, not the engine: the integrator runs the
    real VM on the same listings."""

    PROPS = {"X": 0, "Y": 1, "VX": 2, "VY": 3, "SPR": 4, "FRAME": 5, "FLAGS": 6, "ANGLE": 7,
             "DEPTH": 8, "SCALE": 9, "BODY_W": 10, "BODY_H": 11, "TAGS": 12, "ANIM_TIME": 13,
             "ANIM_STEP": 14}
    SYS = {"PSG_PLAY": (1, False), "MUSIC_PLAY": (1, False), "MUSIC_STOP": (0, False),
           "MUSIC_PAUSE": (0, False), "MUSIC_RESUME": (0, False), "CAMERA_SET": (2, False),
           "TEXT_PRINT": (3, False), "RANDOM_RANGE": (2, True), "BUTTON_DOWN": (1, True),
           "BUTTON_PRESSED": (1, True), "BRIGHTNESS": (1, False), "PATH_START": (3, False),
           "TEXT_PRINT_NUMBER": (4, False), "PATH_STOP": (1, False)}
    STACK, CALLS = 64, 16

    def __init__(self, listing, headers=None):
        self.headers = headers or {}
        self.consts = {"VM_P_FIELD0": 64}
        self.objects, self.strings, self.globals, self.arrays = [], [], {}, {}
        self.code, self.labels, self.handlers = [], {}, {}
        self.entities = {}  # handle -> {"object": name, property number: value}
        self.log = []
        self.buttons = 0
        for raw in listing.splitlines():
            line = svm._strip_comment(raw).strip()
            if not line:
                continue
            m = re.match(r"^(\w+):$", line)
            if m:
                self.labels[m.group(1)] = len(self.code)
                continue
            head, _, rest = line.partition(" ")
            rest = rest.strip()
            if head == ".const":
                name, expr = rest.split(None, 1)
                self.consts[name] = self.value(expr)
            elif head == ".object":
                self.consts["OBJ_" + rest.split()[0]] = len(self.objects)
                self.objects.append(rest.split()[0])
            elif head == ".string":
                name, text = rest.split(None, 1)
                self.consts["STR_" + name] = len(self.strings)
                self.strings.append(text.strip('"'))
            elif head == ".globals":
                for name in rest.split():
                    self.consts["G_" + name] = len(self.globals)
                    self.globals[name] = 0
            elif head == ".array":
                name, length = rest.split(None, 1)
                self.arrays[name] = (False, [0] * self.value(length))
            elif head == ".rom":
                name, kind, items = rest.split(None, 2)
                self.arrays[name] = (True, svm.evaluate_list(items, self.resolve))
            elif head == ".handler":
                obj, event = rest.split()
                self.handlers[obj, event] = len(self.code)
            else:
                self.code.append((head, rest))

    def resolve(self, name):
        if name in self.consts:
            return self.consts[name]
        if name in self.headers:
            return self.headers[name]
        raise svm.ExprError(f"unknown name {name}")

    def value(self, text):
        return svm.evaluate(text, self.resolve)

    def spawn(self, obj, x=0, y=0):
        handle = len(self.entities) + 1
        self.entities[handle] = {"object": obj, 0: x, 1: y}
        return handle

    def prop(self, operand):
        return self.PROPS[operand] if operand in self.PROPS else self.value(operand)

    def run(self, obj, event, self_entity=0, other=0, limit=500000):
        """Runs a handler to its end; returns the number of ops."""
        pc = self.handlers[obj, event]
        stack, calls, fp = [], [], 0
        w = svlua.wrap32

        def pop():
            if not stack:
                raise AssertionError(f"stack underflow at {pc}")
            return stack.pop()

        def push(v):
            if len(stack) >= self.STACK:
                raise AssertionError("stack overflow")
            stack.append(w(v))

        for ops in range(1, limit):
            op, arg = self.code[pc]
            pc += 1
            if op == "HALT":
                return ops
            elif op == "PUSH":
                push(self.value(arg))
            elif op == "DUP":
                push(stack[-1])
            elif op == "DROP":
                pop()
            elif op == "SWAP":
                b, a = pop(), pop()
                push(b)
                push(a)
            elif op in ("LDG", "STG"):
                if op == "LDG":
                    push(self.globals[arg])
                else:
                    self.globals[arg] = pop()
            elif op in ("LDL", "STL"):
                n = int(arg)
                if op == "STL":
                    v = pop()
                    assert fp + n < len(stack), "STL outside the frame"
                    stack[fp + n] = v
                else:
                    assert fp + n < len(stack), "LDL outside the frame"
                    push(stack[fp + n])
            elif op == "ENTER":
                p, n = (int(x) for x in arg.split(","))
                assert p <= len(stack)
                fp = len(stack) - p
                for _ in range(n):
                    push(0)
            elif op in ("LDA", "STA", "LEN"):
                rom, cells = self.arrays[arg]
                if op == "LEN":
                    push(len(cells))
                elif op == "LDA":
                    i = pop()
                    push(cells[i] if 0 <= i < len(cells) else 0)
                else:
                    v, i = pop(), pop()
                    assert not rom, "STA to a ROM array"
                    if 0 <= i < len(cells):
                        cells[i] = v
                    else:
                        self.log.append(("STA out of range", arg, i))
            elif op in ("JMP", "JZ", "JNZ"):
                if op == "JMP" or (pop() == 0) == (op == "JZ"):
                    pc = self.labels[arg]
            elif op == "CALL":
                assert len(calls) < self.CALLS, "call depth"
                calls.append((pc, fp))
                fp = len(stack)
                pc = self.labels[arg]
            elif op in ("RET", "RETV"):
                v = pop() if op == "RETV" else None
                del stack[fp:]
                if not calls:
                    return ops
                pc, fp = calls.pop()
                if v is not None:
                    push(v)
            elif op == "WAIT":
                self.log.append(("WAIT", pop()))
            elif op in ("WAIT_ANIM", "WAIT_MOVE"):
                self.log.append((op,))
            elif op == "SELF":
                push(self_entity)
            elif op == "OTHER":
                push(other)
            elif op == "GETP":
                e = pop()
                push(self.entities.get(e, {}).get(self.prop(arg), 0))
            elif op == "SETP":
                v, e = pop(), pop()
                self.entities.setdefault(e, {})[self.prop(arg)] = v
            elif op == "SPAWN":
                y, x = pop(), pop()
                handle = self.spawn(arg, x, y)
                self.log.append(("SPAWN", arg, x, y))
                push(handle)
            elif op == "KILL":
                self.log.append(("KILL", pop()))
            elif op == "NEXTI":
                e = pop()
                after = [h for h in sorted(self.entities)
                         if h > e and self.entities[h].get("object") == arg]
                push(after[0] if after else 0)
            elif op == "SYS":
                arity, returns = self.SYS[arg]
                args = [pop() for _ in range(arity)][::-1]
                self.log.append((arg, *args))
                if returns:
                    push(args[0] if arg == "RANDOM_RANGE" else int(bool(self.buttons & args[0])))
            elif op == "TRACE":
                self.log.append(("TRACE", self.strings[self.consts["STR_" + arg]]))
            else:
                if op in ("NEG", "BNOT", "LNOT"):
                    a = pop()
                    push({"NEG": -a, "BNOT": ~a, "LNOT": int(a == 0)}[op])
                    continue
                b, a = pop(), pop()
                if op in ("IDIV", "IMOD", "DIV", "MOD", "FXDIV") and b == 0:
                    push(0)
                    continue
                push({
                    "ADD": lambda: a + b, "SUB": lambda: a - b, "MUL": lambda: a * b,
                    "DIV": lambda: svlua.c_div(a, b), "MOD": lambda: a - svlua.c_div(a, b) * b,
                    "FXMUL": lambda: (a * b) >> 8, "FXDIV": lambda: svlua.c_div(a * 256, b),
                    "AND": lambda: a & b, "OR": lambda: a | b, "XOR": lambda: a ^ b,
                    "SHL": lambda: a << (b & 31), "SHR": lambda: a >> (b & 31),
                    "LSH": lambda: svlua.lua_shift_left(a, b),
                    "IDIV": lambda: a // b, "IMOD": lambda: a % b,
                    "EQ": lambda: int(a == b), "NE": lambda: int(a != b),
                    "LT": lambda: int(a < b), "LE": lambda: int(a <= b),
                    "GT": lambda: int(a > b), "GE": lambda: int(a >= b),
                }[op]())
        raise AssertionError("the handler runs away")


def lit(n):
    """An integer as a Lua expression: -2147483648 is a float in Lua (the
    literal 2147483648 doesn't fit 32 bits before the minus applies)."""
    return "(-2147483647 - 1)" if n == INT_MIN else str(n)


def run_lua(source, obj="PROBE", event="ROOM_START", globals_=None, headers=None, **kw):
    """Compiles a script, sets globals, runs one handler; returns the model."""
    vm = ListingVM(svlua.compile_source(source, "t.lua"), headers)
    vm.globals.update(globals_ or {})
    vm.run(obj, event, **kw)
    return vm


# --- Code generation ---------------------------------------------------------


GOLDEN = ("arithmetic", "logic", "control", "frames", "entities", "arrays", "init")
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
        for sys_call in ("PSG_PLAY", "MUSIC_PLAY", "MUSIC_STOP", "MUSIC_PAUSE", "MUSIC_RESUME",
                         "CAMERA_SET", "TEXT_PRINT", "TEXT_PRINT_NUMBER", "RANDOM_RANGE",
                         "BUTTON_DOWN", "BUTTON_PRESSED", "BRIGHTNESS", "PATH_START",
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

    def test_names_in_the_listing(self):
        listing = self.compile("Firefly = object {}\nlocal flag = false\nscores = array(3)\n"
                               "local K <const> = 3\nfunction Firefly:step() "
                               "print(1, 1, \"TIME UP!\"); scores[1] = K; flag = true end")
        for line in (".object FIREFLY mask=0 sprite=0", ".globals FLAG", ".array SCORES 3",
                     ".const K 3", '.string TIME_UP "TIME UP!"', ".handler FIREFLY STEP"):
            self.assertIn(line, listing)

    def test_strings_are_shared(self):
        listing = self.compile(OBJ + "function A:step() print(1, 1, 'HI'); print(2, 2, 'HI'); "
                               "print(3, 3, 'H' .. 'I') end")
        self.assertEqual(listing.count(".string"), 1)
        self.assertEqual(listing.count("PUSH STR_HI"), 3)

    def test_collisions_in_the_listing(self):
        with self.assertRaisesRegex(svlua.CompileError, r"score and SCORE \(line 1\) are both "
                                                        r"SCORE in the listing"):
            self.compile("SCORE = 0\nscore = 0")
        with self.assertRaisesRegex(svlua.CompileError, r"OBJ_A would name both"):
            self.compile("A = object {}\nlocal OBJ_A <const> = 1")


class Semantics(unittest.TestCase):
    """What the generated code computes, run on the model of the VM and
    compared with Lua 5.4's rules (LUA_32BITS), written out here."""

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
        script = """Probe = object {}
a = 0
b = 0
q = 0
r = 0
l = 0
s = 0
x = 0
n = 0
function Probe:room_start()
  q = a // b; r = a % b; l = a << b; s = a >> b; x = a ~ b; n = -a + a * b
end"""
        listing = svlua.compile_source(script, "t.lua")
        for a in (7, -7, 0, 1, -1, INT_MAX, INT_MIN, 123456789, -98765):
            for b in (2, -2, 3, -3, 1, -1, 31, 32, 33, -31, -32, 40, -40, 7, INT_MAX, INT_MIN):
                with self.subTest(a=a, b=b):
                    vm = ListingVM(listing)
                    vm.globals.update(A=a, B=b)
                    vm.run("PROBE", "ROOM_START")
                    g = vm.globals
                    self.assertEqual(g["Q"], self.lua_idiv(a, b))
                    self.assertEqual(g["R"], self.lua_mod(a, b))
                    self.assertEqual(g["L"], self.lua_shl(a, b))
                    self.assertEqual(g["S"], self.lua_shl(a, -b))
                    self.assertEqual(g["X"], svlua.wrap32(a ^ b))
                    self.assertEqual(g["N"], svlua.wrap32(-a + a * b))

    def test_fixed_point_within_a_256th_per_operation(self):
        script = """Probe = object {}
a = 0.0
b = 0.0
m = 0.0
d = 0.0
s = 0.0
i = 0
function Probe:room_start()
  m = a * b; d = a / b; s = a - b + 1; i = math.floor(a)
end"""
        listing = svlua.compile_source(script, "t.lua")
        for a in (1.5, -2.25, 100.0, 0.0039, -0.5, 300.75):
            for b in (0.5, -3.0, 7.25, 1.0, -0.125):
                with self.subTest(a=a, b=b):
                    vm = ListingVM(listing)
                    ra, rb = round(a * 256), round(b * 256)
                    vm.globals.update(A=ra, B=rb)
                    vm.run("PROBE", "ROOM_START")
                    fa, fb = ra / 256, rb / 256
                    self.assertAlmostEqual(vm.globals["M"] / 256, fa * fb, delta=1 / 256)
                    self.assertAlmostEqual(vm.globals["D"] / 256, fa / fb, delta=1 / 256)
                    self.assertEqual(vm.globals["S"] / 256, fa - fb + 1)
                    self.assertEqual(vm.globals["I"], int(fa // 1))

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
        script = f"""Probe = object {{}}
lo = 0
hi = 0
st = 0
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
        vm = ListingVM(svlua.compile_source(script, "t.lua"), consts)
        vm.globals.update(LO=start, HI=limit, ST=step)
        vm.run("PROBE", "ROOM_START")
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
        vm = run_lua(OBJ + "n = 0\nst = 0\nfunction A:room_start()\n"
                     "  for i = 1, 10, st do n = n + 1 end\n  n = 99\nend", obj="A",
                     globals_={"ST": 0})
        self.assertEqual(vm.globals["N"], 0)
        self.assertEqual(vm.log, [("TRACE", "'for' step is zero")])

    def test_for_evaluates_its_limit_and_step_once(self):
        vm = run_lua(OBJ + "n = 0\nhi = 0\nfunction A:room_start()\n"
                     "  for i = 1, hi do hi = hi + 1; i = i * 10; n = n + i end\nend",
                     obj="A", globals_={"HI": 3})
        self.assertEqual(vm.globals["N"], 10 + 20 + 30)

    def test_fixed_point_for_loops(self):
        cases = {"for v = 0.0, 1.0, 0.25 do": [0, 64, 128, 192, 256],
                 "for v = 1, 0, -0.5 do": [256, 128, 0],
                 "for v = 0.5, 2 do": [128, 384],
                 "for v = lo, hi, st do": [256, 192, 128]}
        for header, values in cases.items():
            with self.subTest(header=header):
                vm = run_lua("Probe = object {}\nn = 0\nsum = 0.0\nlo = 0.0\nhi = 0.0\n"
                             f"st = 0.0\nfunction Probe:room_start()\n  {header}\n"
                             "    n = n + 1; sum = sum + v\n  end\nend",
                             globals_={"LO": 256, "HI": 100, "ST": -64})
                self.assertEqual(vm.globals["N"], len(values))
                self.assertEqual(vm.globals["SUM"], sum(values))

    def test_an_integer_loop_with_a_fixed_limit(self):
        """Lua floors the limit going up and rounds it up going down."""
        for header, values in {"for i = 1, x do": [1, 2], "for i = 3, y, -1 do": [3, 2, 1],
                               "for i = 1, 2.5 do": [1, 2], "for i = 3, 0.5, -1 do": [3, 2, 1],
                               "for i = 1, x, st do": [1, 2], "for i = 3, y, -st do": [3, 2, 1]
                               }.items():
            with self.subTest(header=header):
                vm = run_lua("Probe = object {}\nn = 0\nsum = 0\nx = 0.0\ny = 0.0\nst = 0\n"
                             f"function Probe:room_start()\n  {header} n = n + 1; "
                             "sum = sum + i end\nend",
                             globals_={"X": 640, "Y": 128, "ST": 1})  # 2.5 and 0.5
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
        vm = run_lua(script, obj="A")
        self.assertEqual(vm.globals["CALLS"], 10 + 1 + 111 + 11 + 2)
        self.assertEqual(vm.globals["R"], 0)

    def test_recursion_and_frames(self):
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
        vm = run_lua(script, obj="A")
        self.assertEqual((vm.globals["R1"], vm.globals["R2"], vm.globals["R3"]),
                         (3628800, 144, 123 - 6 + 7))

    def test_math_functions(self):
        vm = run_lua(OBJ + """a = 0
b = 0
c = 0
d = 0.0
function A:room_start()
  local n = -5
  a = math.abs(n) + math.abs(-n)
  b = math.max(n, 3, -9) * 100 + math.min(n, 3, -9)
  c = math.floor(-2.5) * 10 + math.floor(n)
  d = math.max(1.5, -2.0)
end""", obj="A")
        self.assertEqual((vm.globals["A"], vm.globals["B"], vm.globals["C"], vm.globals["D"]),
                         (10, 291, -35, 384))

    def test_multiple_assignment(self):
        vm = run_lua(OBJ + """a = 0
b = 0
i = 0
t = array(5)
function A:room_start()
  a, b = b, a
  i, t[i] = i + 1, 20   -- Lua's manual: t[3] is set, i becomes 4
end""", obj="A", globals_={"A": 1, "B": 2, "I": 3})
        self.assertEqual((vm.globals["A"], vm.globals["B"], vm.globals["I"]), (2, 1, 4))
        self.assertEqual(vm.arrays["T"][1], [0, 0, 20, 0, 0])

    def test_arrays(self):
        vm = run_lua(OBJ + """t = array(4)
rom = { 10, -20, 300 }
total = 0
n = 0
function A:room_start()
  for i = 1, #t do t[i] = rom[(i - 1) % #rom + 1] * i end
  for i = 1, #t do total = total + t[i] end
  n = #rom
end""", obj="A")
        self.assertEqual(vm.arrays["T"][1], [10, -40, 900, 40])
        self.assertEqual((vm.globals["TOTAL"], vm.globals["N"]), (910, 3))

    def test_goto_continue(self):
        vm = run_lua(OBJ + """n = 0
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
end""", obj="A")
        self.assertEqual(vm.globals["N"], 93)

    def test_instances_and_entities(self):
        script = """Enemy = object { components = 1 }
Boss = object {}
count = 0
hp = 0
function Enemy:step()
  for e in instances(Enemy) do
    count = count + 1
    e.hp = e.hp + count
    e.x = e.x + 1
    if count == 2 then kill(e) end
  end
  hp = self.hp
end"""
        vm = ListingVM(svlua.compile_source(script, "t.lua"))
        first = vm.spawn("ENEMY")
        vm.spawn("BOSS")
        vm.spawn("ENEMY", x=512)
        third = vm.spawn("ENEMY")
        vm.run("ENEMY", "STEP", self_entity=first)
        self.assertEqual(vm.globals["COUNT"], 3)
        self.assertEqual(vm.globals["HP"], 1)
        self.assertEqual(vm.entities[3][0], 512 + 256)  # x: one pixel more
        self.assertEqual(vm.entities[third][64], 3)
        self.assertEqual(vm.log, [("KILL", 3)])

    def test_init_sets_the_globals(self):
        vm = run_lua("Init = object {}\nlives = 3\nspeed = 1.5\nalive = true\nhero = none\n"
                     "function Init:room_start() lives = lives + 1 end", obj="INIT")
        self.assertEqual(vm.globals, {"LIVES": 4, "SPEED": 384, "ALIVE": 1, "HERO": 0})
        vm = run_lua("Init = object {}\nlives = 3\n", obj="INIT")  # a generated handler
        self.assertEqual(vm.globals, {"LIVES": 3})


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
    """tests/svlua/fireflies.lua, the whole fireflies game, against what the
    example's C glue and hand-written listing expect."""

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(FIXTURES, "fireflies.lua"), encoding="utf-8") as f:
            cls.compiled = svlua.compile_program(f.read(), "fireflies.lua")
        cls.listing = cls.compiled.listing
        cls.headers = header_names(files=FIREFLIES_HEADERS)
        cls.constants = cls.headers.constants()

    def model(self):
        return ListingVM(self.listing, self.constants)

    def test_compiles_without_warnings(self):
        self.assertEqual(self.compiled.warnings, [])

    def test_the_tables_match_the_hand_written_listing(self):
        """The objects, their components and sprites, and the globals, in
        the same order: what main.c and game.h rely on (OBJ_ROOM,
        OBJ_SPAWNER, G_RESTART, C_PLAYER, C_FIREFLY)."""
        path = os.path.join(ROOT, "examples", "fireflies", "fireflies.svm")
        if not os.path.exists(path):
            self.skipTest("no examples/ (a release archive)")
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

    def test_a_round(self):
        """The Room's thread: the HUD, the serval, the fade in, 60 seconds,
        time up, START, the fade out and the restart request."""
        vm = self.model()
        k = self.constants
        vm.buttons = k["BUTTON_START"]
        vm.run("ROOM", "ROOM_START")
        g = vm.globals
        self.assertEqual((g["RESTART"], g["PLAYING"], g["TIME"], g["SCORE"]), (1, 0, 0, 0))
        prints = [vm.strings[c[3]] for c in vm.log if c[0] == "TEXT_PRINT"]
        self.assertEqual(prints[:2], ["SCORE", "TIME"])
        self.assertIn("CATCH THE FIREFLIES!", prints)
        self.assertEqual(prints[-3:], ["TIME UP!", "CAUGHT", "PRESS START"])
        levels = [c[1] for c in vm.log if c[0] == "BRIGHTNESS"]
        self.assertEqual(levels, list(range(-16, 1, 2)) + list(range(-2, -17, -2)))
        times = [c[3] for c in vm.log if c[0] == "TEXT_PRINT_NUMBER" and c[1] == 28]
        self.assertEqual(times, list(range(60, -1, -1)))
        ticks = [c for c in vm.log if c == ("PSG_PLAY", k["SND_TICK"])]
        self.assertEqual(len(ticks), 10)
        player = g["PLAYER"]
        x = (k["SCREEN_W"] - k["SERVAL_BODY_W"]) // 2 * 256
        self.assertIn(("SPAWN", "PLAYER", x, (k["FIELD_TOP"] + k["SCREEN_H"]
                                              - k["SERVAL_BODY_H"]) // 2 * 256), vm.log)
        self.assertIn(("KILL", player), vm.log)
        self.assertIn(("SPAWN", "RESTING", x, vm.entities[player][1]), vm.log)

    def test_a_firefly_life(self):
        vm = self.model()
        k = self.constants
        vm.globals["PLAYING"] = 1
        firefly = vm.spawn("FIREFLY", 100 * 256, 50 * 256)
        vm.run("FIREFLY", "CREATE", self_entity=firefly)
        self.assertEqual(vm.globals["LIVE"], 1)
        # random_range gives its low end in the model: FLIGHTS_MIN flights
        self.assertEqual(len([c for c in vm.log if c[0] == "PATH_START"]), 3)
        self.assertEqual(vm.log[-2:], [("WAIT_ANIM",), ("KILL", firefly)])
        self.assertEqual(vm.entities[firefly][4], k["SPR_FIREFLY_FADE"])

    def test_a_catch(self):
        vm = self.model()
        k = self.constants
        serval = vm.spawn("PLAYER", 120 * 256, 80 * 256)
        firefly = vm.spawn("FIREFLY", 100 * 256, 50 * 256)
        vm.globals.update(SCORE=9, SPAWN_MIN=40, SPAWN_MAX=90)
        vm.run("FIREFLY", "COLLISION", self_entity=firefly, other=serval)
        self.assertEqual(vm.globals["SCORE"], 10)
        self.assertEqual((vm.globals["SPAWN_MIN"], vm.globals["SPAWN_MAX"]), (35, 78))
        self.assertIn(("SPAWN", "SPARKLE", 96 * 256, 46 * 256), vm.log)
        self.assertIn(("PSG_PLAY", k["SND_JINGLE"]), vm.log)
        self.assertEqual(vm.entities[serval][6], k["SPRITE_FLIP_H"])  # it faces left
        self.assertEqual(vm.log[-1], ("KILL", firefly))

    def test_the_serval_walks(self):
        vm = self.model()
        k = self.constants
        serval = vm.spawn("PLAYER")
        vm.buttons = k["BUTTON_LEFT"] | k["BUTTON_DOWN"]
        vm.run("PLAYER", "STEP", self_entity=serval)
        e = vm.entities[serval]
        self.assertEqual((e[2], e[3]), (-384, 384))  # 1.5 pixels per frame
        self.assertEqual(e[4], k["SPR_SERVAL_WALK"])
        self.assertEqual(e[6] & k["SPRITE_FLIP_H"], k["SPRITE_FLIP_H"])
        vm.buttons = 0
        vm.run("PLAYER", "STEP", self_entity=serval)
        self.assertEqual((e[2], e[3], e[4]), (0, 0, k["SPR_SERVAL_IDLE"]))


if __name__ == "__main__":
    unittest.main()
