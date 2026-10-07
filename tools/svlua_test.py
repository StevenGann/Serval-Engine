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

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svlua  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


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
                self.assertEqual(expr_tree(f"a {op} b"), ("Binary", op, ("Name", "a"), ("Name", "b")))


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
    "varargs_parameter": ("function f(a, ...) end", 1, 15, r"varargs \(\.\.\.\) are not in the subset"),
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
    "function_in_table": ("function a.b() end", 1, 12, r"functions in tables .* are not in the subset"),
    "top_level_code": ("x = 0\nif x == 0 then end", 2, 1, r"if at the top level: only declarations"),
    "top_level_call": ("print(1, 1, 'hi')", 1, 1, r"a call at the top level"),
    "top_level_field": ("A = object {}\nA.x = 1", 2, 1, r"only names are assigned at the top level"),
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


if __name__ == "__main__":
    unittest.main()
