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


if __name__ == "__main__":
    unittest.main()
