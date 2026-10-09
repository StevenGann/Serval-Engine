#!/usr/bin/env python3
"""Compile a statically checked subset of Lua 5.4 to Serval Engine's VM.

The compiler docs/lua.md specifies: a script in the Lua subset becomes a
listing (.svm, docs/vm.md#listing-syntax) that tools/svm.py assembles into a
script blob. Every program it accepts means what it means in Lua 5.4 built
with 32-bit integers (LUA_32BITS), with one documented approximation: Lua's
floats are 24.8 fixed point. Anything outside the subset is a compile error
that names the construct and gives a hint.

Usage:
  svlua.py compile SCRIPT.lua [-o OUT.svm] [--check]

  -o OUT.svm   write the listing there (default: standard output)
  --check      parse and check the script (names, types, waits) only

The listing keeps the script's names: objects, globals and arrays upper-cased
(Firefly is OBJ_FIREFLY to C, score is G_SCORE), functions as labels, top-level
<const> numbers as .const lines. Names in ALL_CAPS that the script doesn't
define are constants from the game's C headers: they pass through to the
listing, so svm.py needs --header for every header that defines them (the
listing's first lines name them). Every line of code ends with a
"; file:line" comment, and each source line appears as a comment above its
code.

Stages: a lexer and a recursive-descent parser for Lua 5.4's whole grammar
(constructs outside the subset are parsed, then named in the error), the
program model and name resolution, whole-program type inference, the wait
check over the call graph, and stack-machine code generation with constant
folding.

Errors are "file:line:column: error: message", with a hint on the next line;
exit status 1 and nothing written. Python 3.11+, standard library only.
"""

import argparse
import difflib
import math
import os
import re
import sys

TOOL = "tools/svlua.py"


# --- Errors ------------------------------------------------------------------


class CompileError(Exception):
    """A problem in the script, at file:line:column, with an optional hint."""

    def __init__(self, message, file=None, line=None, column=None, hint=None):
        super().__init__(message)
        self.message = message
        self.file = file
        self.line = line
        self.column = column
        self.hint = hint

    def location(self):
        parts = [str(p) for p in (self.file, self.line, self.column) if p is not None]
        return ":".join(parts)

    def __str__(self):
        where = self.location()
        text = f"{where}: error: {self.message}" if where else f"error: {self.message}"
        if self.hint:
            text += f"\n  hint: {self.hint}"
        return text


# --- Lexer -------------------------------------------------------------------

KEYWORDS = frozenset((
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"))

# Longest first, so the first match is the token.
OPERATORS = ("...", "..", "==", "~=", "<=", ">=", "<<", ">>", "//", "::",
             "+", "-", "*", "/", "%", "^", "#", "&", "~", "|", "<", ">", "=",
             "(", ")", "{", "}", "[", "]", ";", ":", ",", ".")

_LETTERS = frozenset("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_")
_DIGITS = frozenset("0123456789")
_HEX = frozenset("0123456789abcdefABCDEF")
_SPACE = frozenset(" \t\n\v\f")
_ESCAPES = {"a": 7, "b": 8, "f": 12, "n": 10, "r": 13, "t": 9, "v": 11,
            "\\": 92, '"': 34, "'": 39, "\n": 10}

INT_MIN, INT_MAX = -0x80000000, 0x7FFFFFFF


def wrap32(value):
    """An integer reduced to a signed 32-bit cell, as LUA_32BITS wraps."""
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


class Token:
    """kind is "name", "number", "string", "<eof>", or the keyword or
    operator itself. value: the name; a number's ("int", n) or ("float", x);
    a string's bytes. text is the source text, for messages."""

    __slots__ = ("kind", "value", "text", "line", "column")

    def __init__(self, kind, value, text, line, column):
        self.kind = kind
        self.value = value
        self.text = text
        self.line = line
        self.column = column

    def near(self):
        return "<eof>" if self.kind == "<eof>" else self.text

    def __repr__(self):
        return f"Token({self.kind!r}, {self.text!r}, {self.line}:{self.column})"


def lua_numeral(text):
    """A Lua 5.4 numeral's value with LUA_32BITS: ("int", n) or ("float", x),
    or None if it is malformed. Hex integers wrap around; a decimal integer
    that doesn't fit 32 bits is a float, as in Lua."""
    m = re.fullmatch(r"0[xX]([0-9a-fA-F]*)(?:(\.)([0-9a-fA-F]*))?(?:[pP]([+-]?[0-9]+))?", text)
    if m:
        whole, dot, fraction, exponent = m.groups()
        fraction = fraction or ""
        if not whole and not fraction:
            return None
        if dot is None and exponent is None:
            return ("int", wrap32(int(whole, 16)))
        mantissa = int(whole + fraction, 16)
        power = (int(exponent) if exponent else 0) - 4 * len(fraction)
        try:
            return ("float", math.ldexp(mantissa, power))
        except OverflowError:
            return ("float", math.inf)
    m = re.fullmatch(r"([0-9]*)(\.[0-9]*)?([eE][+-]?[0-9]+)?", text)
    if not m or not (m.group(1) or (m.group(2) or "")[1:]):
        return None
    if m.group(2) is None and m.group(3) is None:
        value = int(text)
        return ("int", value) if value <= INT_MAX else ("float", float(value))
    return ("float", float(text))


class Lexer:
    def __init__(self, text, file):
        # Lua accepts \r\n and \r as line breaks; a first line starting with
        # # (a shebang) is skipped, its line break kept for the line numbers.
        text = text.replace("\r\n", "\n").replace("\r", "\n")
        if text.startswith("#"):
            end = text.find("\n")
            text = "" if end < 0 else text[end:]
        self.text = text
        self.file = file
        self.pos = 0
        self.line = 1
        self.line_start = 0

    def error(self, message, line=None, column=None, hint=None):
        if line is None:
            line, column = self.line, self.pos - self.line_start + 1
        raise CompileError(message, self.file, line, column, hint)

    def newline(self, at):
        self.line += 1
        self.line_start = at + 1

    def tokens(self):
        out = []
        while True:
            token = self.next_token()
            out.append(token)
            if token.kind == "<eof>":
                return out

    def next_token(self):
        text = self.text
        n = len(text)
        while True:
            # Whitespace and comments.
            while self.pos < n and text[self.pos] in _SPACE:
                if text[self.pos] == "\n":
                    self.newline(self.pos)
                self.pos += 1
            if text.startswith("--", self.pos):
                line, column = self.line, self.pos - self.line_start + 1
                self.pos += 2
                level = self.long_bracket_level()
                if level is not None:
                    self.long_bracket(level, "comment", line, column)
                else:
                    end = text.find("\n", self.pos)
                    self.pos = n if end < 0 else end
                continue
            break
        line, column = self.line, self.pos - self.line_start + 1
        if self.pos >= n:
            return Token("<eof>", None, "<eof>", line, column)
        start = self.pos
        c = text[start]
        if c in _LETTERS:
            end = start + 1
            while end < n and (text[end] in _LETTERS or text[end] in _DIGITS):
                end += 1
            self.pos = end
            word = text[start:end]
            return Token(word if word in KEYWORDS else "name", word, word, line, column)
        if c in _DIGITS or (c == "." and start + 1 < n and text[start + 1] in _DIGITS):
            return self.numeral(line, column)
        if c in "\"'":
            return self.short_string(c, line, column)
        if c == "[":
            level = self.long_bracket_level()
            if level is not None:
                value = self.long_bracket(level, "string", line, column)
                return Token("string", value, text[start:self.pos], line, column)
            if text.startswith("[=", start):
                self.error("invalid long string delimiter near '[='", line, column)
        for op in OPERATORS:
            if text.startswith(op, start):
                self.pos += len(op)
                return Token(op, op, op, line, column)
        self.error(f"unexpected symbol near '{c}'", line, column)

    def numeral(self, line, column):
        # As Lua's read_numeral: digits, letters, dots and signed exponents,
        # then one conversion (which fails on anything malformed).
        text = self.text
        start = self.pos
        exponents = "Ee"
        if text.startswith(("0x", "0X"), start):
            self.pos += 2
            exponents = "Pp"
        while self.pos < len(text):
            c = text[self.pos]
            if c in exponents:
                self.pos += 1
                if self.pos < len(text) and text[self.pos] in "+-":
                    self.pos += 1
            elif c in _HEX or c == ".":
                self.pos += 1
            else:
                break
        if self.pos < len(text) and text[self.pos] in _LETTERS:
            self.pos += 1  # a numeral touching a letter is malformed
        source = text[start:self.pos]
        value = lua_numeral(source)
        if value is None:
            self.error(f"malformed number near '{source}'", line, column)
        return Token("number", value, source, line, column)

    def short_string(self, quote, line, column):
        text = self.text
        start = self.pos
        self.pos += 1
        out = bytearray()
        while True:
            if self.pos >= len(text) or text[self.pos] == "\n":
                self.error(f"unfinished string near '{text[start:self.pos]}'", line, column)
            c = text[self.pos]
            if c == quote:
                self.pos += 1
                break
            if c != "\\":
                out += c.encode("utf-8")
                self.pos += 1
                continue
            self.pos += 1
            e = text[self.pos] if self.pos < len(text) else ""
            if e in _ESCAPES:
                out.append(_ESCAPES[e])
                if e == "\n":
                    self.newline(self.pos)
                self.pos += 1
            elif e == "x":
                digits = text[self.pos + 1:self.pos + 3]
                if len(digits) < 2 or any(d not in _HEX for d in digits):
                    self.error("hexadecimal digit expected in an escape sequence", line, column)
                out.append(int(digits, 16))
                self.pos += 3
            elif e == "z":
                self.pos += 1
                while self.pos < len(text) and text[self.pos] in _SPACE:
                    if text[self.pos] == "\n":
                        self.newline(self.pos)
                    self.pos += 1
            elif e in _DIGITS:
                end = self.pos
                while end < len(text) and end - self.pos < 3 and text[end] in _DIGITS:
                    end += 1
                value = int(text[self.pos:end])
                if value > 255:
                    self.error(f"decimal escape too large near '\\{text[self.pos:end]}'", line,
                               column)
                out.append(value)
                self.pos = end
            elif e == "u":
                m = re.compile(r"u\{([0-9a-fA-F]+)\}").match(text, self.pos)
                if not m or int(m.group(1), 16) > 0x7FFFFFFF:
                    self.error("malformed \\u{XXX} escape sequence", line, column)
                out += _utf8(int(m.group(1), 16))
                self.pos = m.end()
            elif e == "":
                self.error(f"unfinished string near '{text[start:self.pos]}'", line, column)
            else:
                self.error(f"invalid escape sequence '\\{e}'", line, column)
        return Token("string", bytes(out), text[start:self.pos], line, column)

    def long_bracket_level(self):
        """At [, [=, [==...: the level of a long bracket that opens here
        (the number of =), or None. Moves past the opening if there is one."""
        text = self.text
        if not text.startswith("[", self.pos):
            return None
        end = self.pos + 1
        while end < len(text) and text[end] == "=":
            end += 1
        if end < len(text) and text[end] == "[":
            level = end - self.pos - 1
            self.pos = end + 1
            return level
        return None

    def long_bracket(self, level, what, line, column):
        """A long string's or comment's content, after its opening bracket."""
        text = self.text
        if text.startswith("\n", self.pos):  # a first line break is skipped
            self.newline(self.pos)
            self.pos += 1
        close = "]" + "=" * level + "]"
        end = text.find(close, self.pos)
        if end < 0:
            self.error(f"unfinished long {what}", line, column)
        content = text[self.pos:end]
        for i, c in enumerate(content):
            if c == "\n":
                self.newline(self.pos + i)
        self.pos = end + len(close)
        return content.encode("utf-8")


def _utf8(code):
    """Lua's \\u{XXX}: UTF-8, extended to 31 bits as Lua does."""
    if code < 0x80:
        return bytes([code])
    out = []
    limit = 0x3F
    while code > limit:
        out.append(0x80 | (code & 0x3F))
        code >>= 6
        limit >>= 1
    out.append(((~limit << 1) & 0xFF) | code)
    return bytes(reversed(out))


def tokenize(text, file="script.lua"):
    return Lexer(text, file).tokens()


# --- Syntax tree -------------------------------------------------------------


class Node:
    """A syntax tree node: its position and the fields its class names.
    Later stages add annotations (types, symbols, constant values)."""

    fields = ()

    def __init__(self, line, column, *values):
        self.line = line
        self.column = column
        for name, value in zip(self.fields, values, strict=True):
            setattr(self, name, value)

    def __repr__(self):
        inner = ", ".join(f"{name}={getattr(self, name)!r}" for name in self.fields)
        return f"{type(self).__name__}({inner})"


class Expr(Node):
    ty = None  # the static type, set by the checker
    const = None  # a Const if the value is known at compile time


class Nil(Expr):
    pass


class Bool(Expr):
    fields = ("value",)


class Number(Expr):
    fields = ("kind", "value", "text")  # kind "int" or "float"


class String(Expr):
    fields = ("value",)  # bytes


class Vararg(Expr):
    pass


class FunctionExpr(Expr):
    fields = ("params", "vararg", "body")  # params: [Name]; body: Block


class TableItem(Node):
    fields = ("kind", "key", "value")  # kind: "list", "name" (key is a str) or "expr"


class Table(Expr):
    fields = ("items",)


class Binary(Expr):
    fields = ("op", "left", "right")


class Unary(Expr):
    fields = ("op", "operand")  # "-", "not", "#", "~"


class Name(Expr):
    fields = ("name",)
    sym = None  # what the name refers to, set by the resolver


class Index(Expr):
    fields = ("obj", "key")  # obj[key]


class Field(Expr):
    fields = ("obj", "name")  # obj.name
    field = None  # an instance field's FieldSym, set by the resolver


class Call(Expr):
    fields = ("func", "args")


class Method(Expr):
    fields = ("obj", "name", "args")  # obj:name(args)


class Paren(Expr):
    fields = ("expr",)


class Stat(Node):
    pass


class Block(Node):
    fields = ("stats", "end_line", "end_column")  # where the block's closing token is


class Local(Stat):
    fields = ("names", "attribs", "values")  # names: [Name]; attribs: [str or None]


class Assign(Stat):
    fields = ("targets", "values")


class CallStat(Stat):
    fields = ("call",)


class Do(Stat):
    fields = ("body",)


class While(Stat):
    fields = ("cond", "body")


class Repeat(Stat):
    fields = ("body", "cond")


class If(Stat):
    fields = ("tests", "blocks", "orelse")  # orelse: Block or None


class NumericFor(Stat):
    fields = ("var", "start", "limit", "step", "body")  # step: Expr or None


class GenericFor(Stat):
    fields = ("names", "exprs", "body")


class FunctionStat(Stat):
    fields = ("path", "method", "func")  # function a.b:c(): path [a, b], method c


class LocalFunction(Stat):
    fields = ("name", "func")


class Return(Stat):
    fields = ("values",)


class Break(Stat):
    pass


class Goto(Stat):
    fields = ("label",)


class Label(Stat):
    fields = ("name",)


# --- Parser ------------------------------------------------------------------

# Lua 5.4's binary operator priorities (lparser.c): (left, right). Right
# associative operators bind tighter on the left.
BINARY_PRIORITY = {
    "or": (1, 1), "and": (2, 2),
    "<": (3, 3), ">": (3, 3), "<=": (3, 3), ">=": (3, 3), "~=": (3, 3), "==": (3, 3),
    "|": (4, 4), "~": (5, 5), "&": (6, 6), "<<": (7, 7), ">>": (7, 7),
    "..": (9, 8), "+": (10, 10), "-": (10, 10),
    "*": (11, 11), "/": (11, 11), "//": (11, 11), "%": (11, 11),
    "^": (14, 13),
}
UNARY_PRIORITY = 12
UNARY_OPERATORS = ("not", "-", "#", "~")
BLOCK_END = ("else", "elseif", "end", "<eof>")


class Parser:
    def __init__(self, tokens, file):
        self.tokens = tokens
        self.file = file
        self.pos = 0

    @property
    def tok(self):
        return self.tokens[self.pos]

    def peek(self, offset=1):
        return self.tokens[min(self.pos + offset, len(self.tokens) - 1)]

    def advance(self):
        token = self.tokens[self.pos]
        if token.kind != "<eof>":
            self.pos += 1
        return token

    def check(self, kind):
        return self.tok.kind == kind

    def accept(self, kind):
        if self.tok.kind == kind:
            return self.advance()
        return None

    def error(self, message, token=None):
        token = token or self.tok
        raise CompileError(message, self.file, token.line, token.column)

    def error_near(self, message, token=None):
        token = token or self.tok
        self.error(f"{message} near '{token.near()}'", token)

    def expect(self, kind, what=None):
        if self.tok.kind != kind:
            self.error_near(f"'{what or kind}' expected")
        return self.advance()

    def expect_match(self, kind, opener, open_token):
        """The closing token of a construct, with Lua's message naming the
        opener when it is on another line."""
        if self.tok.kind == kind:
            return self.advance()
        if open_token.line == self.tok.line:
            self.error_near(f"'{kind}' expected")
        self.error_near(f"'{kind}' expected (to close '{opener}' at line {open_token.line})")

    def name(self):
        token = self.tok
        if token.kind != "name":
            self.error_near("<name> expected")
        self.advance()
        return Name(token.line, token.column, token.value)

    # --- Blocks and statements ---

    def chunk(self):
        body = self.block()
        if not self.check("<eof>"):
            self.error_near("'<eof>' expected")
        return body

    def block(self, until=False):
        start = self.tok
        stats = []
        while True:
            kind = self.tok.kind
            if kind in BLOCK_END or (until and kind == "until"):
                break
            if kind == "return":
                stats.append(self.return_stat())
                break
            stat = self.statement()
            if stat is not None:
                stats.append(stat)
        return Block(start.line, start.column, stats, self.tok.line, self.tok.column)

    def statement(self):
        token = self.tok
        kind = token.kind
        if kind == ";":
            self.advance()
            return None
        if kind == "if":
            return self.if_stat()
        if kind == "while":
            self.advance()
            cond = self.expr()
            self.expect("do")
            body = self.block()
            self.expect_match("end", "while", token)
            return While(token.line, token.column, cond, body)
        if kind == "do":
            self.advance()
            body = self.block()
            self.expect_match("end", "do", token)
            return Do(token.line, token.column, body)
        if kind == "for":
            return self.for_stat()
        if kind == "repeat":
            self.advance()
            body = self.block(until=True)
            self.expect_match("until", "repeat", token)
            cond = self.expr()
            return Repeat(token.line, token.column, body, cond)
        if kind == "function":
            return self.function_stat()
        if kind == "local":
            self.advance()
            if self.accept("function"):
                name = self.name()
                func = self.func_body(token)
                return LocalFunction(token.line, token.column, name, func)
            return self.local_stat(token)
        if kind == "::":
            self.advance()
            name = self.name()
            self.expect("::")
            return Label(token.line, token.column, name.name)
        if kind == "break":
            self.advance()
            return Break(token.line, token.column)
        if kind == "goto":
            self.advance()
            name = self.name()
            return Goto(token.line, token.column, name.name)
        return self.expr_stat()

    def if_stat(self):
        token = self.advance()  # if
        tests, blocks = [], []
        cond = self.expr()
        self.expect("then")
        tests.append(cond)
        blocks.append(self.block())
        orelse = None
        while True:
            if self.check("elseif"):
                self.advance()
                tests.append(self.expr())
                self.expect("then")
                blocks.append(self.block())
            elif self.check("else"):
                self.advance()
                orelse = self.block()
                self.expect_match("end", "if", token)
                break
            else:
                self.expect_match("end", "if", token)
                break
        return If(token.line, token.column, tests, blocks, orelse)

    def for_stat(self):
        token = self.advance()  # for
        first = self.name()
        if self.check("="):
            self.advance()
            start = self.expr()
            self.expect(",")
            limit = self.expr()
            step = self.expr() if self.accept(",") else None
            self.expect("do")
            body = self.block()
            self.expect_match("end", "for", token)
            return NumericFor(token.line, token.column, first, start, limit, step, body)
        if self.check(",") or self.check("in"):
            names = [first]
            while self.accept(","):
                names.append(self.name())
            self.expect("in")
            exprs = self.expr_list()
            self.expect("do")
            body = self.block()
            self.expect_match("end", "for", token)
            return GenericFor(token.line, token.column, names, exprs, body)
        self.error_near("'=' or 'in' expected")

    def function_stat(self):
        token = self.advance()  # function
        path = [self.name()]
        method = None
        while self.accept("."):
            path.append(self.name())
        if self.accept(":"):
            method = self.name()
        func = self.func_body(token)
        return FunctionStat(token.line, token.column, path, method, func)

    def func_body(self, token):
        """( params ) block end, as a FunctionExpr at the 'function' token."""
        self.expect("(")
        params = []
        vararg = None
        if not self.check(")"):
            while True:
                if self.check("..."):
                    dots = self.advance()
                    vararg = Vararg(dots.line, dots.column)
                    break
                params.append(self.name())
                if not self.accept(","):
                    break
        self.expect(")")
        body = self.block()
        self.expect_match("end", "function", token)
        return FunctionExpr(token.line, token.column, params, vararg, body)

    def local_stat(self, token):
        names, attribs = [], []
        while True:
            names.append(self.name())
            if self.accept("<"):
                attrib = self.name()
                if attrib.name not in ("const", "close"):
                    self.error(f"unknown attribute '{attrib.name}'", attrib)
                self.expect(">")
                attribs.append(attrib.name)
            else:
                attribs.append(None)
            if not self.accept(","):
                break
        values = self.expr_list() if self.accept("=") else []
        return Local(token.line, token.column, names, attribs, values)

    def return_stat(self):
        token = self.advance()  # return
        values = []
        if not (self.tok.kind in BLOCK_END or self.tok.kind in ("until", ";")):
            values = self.expr_list()
        self.accept(";")
        return Return(token.line, token.column, values)

    def expr_stat(self):
        token = self.tok
        expr = self.suffixed_expr()
        if self.check("=") or self.check(","):
            targets = [expr]
            while self.accept(","):
                targets.append(self.suffixed_expr())
            for target in targets:
                if not isinstance(target, (Name, Index, Field)):
                    self.error("syntax error: only names, a.b and a[b] can be assigned to",
                               target)
            self.expect("=")
            values = self.expr_list()
            return Assign(token.line, token.column, targets, values)
        if not isinstance(expr, (Call, Method)):
            self.error_near("syntax error")
        return CallStat(token.line, token.column, expr)

    # --- Expressions ---

    def expr_list(self):
        exprs = [self.expr()]
        while self.accept(","):
            exprs.append(self.expr())
        return exprs

    def expr(self, limit=0):
        token = self.tok
        if token.kind in UNARY_OPERATORS:
            self.advance()
            operand = self.expr(UNARY_PRIORITY)
            left = Unary(token.line, token.column, token.kind, operand)
        else:
            left = self.simple_expr()
        while True:
            op = self.tok
            priority = BINARY_PRIORITY.get(op.kind)
            if priority is None or priority[0] <= limit:
                return left
            self.advance()
            right = self.expr(priority[1])
            left = Binary(op.line, op.column, op.kind, left, right)

    def simple_expr(self):
        token = self.tok
        kind = token.kind
        if kind == "number":
            self.advance()
            return Number(token.line, token.column, token.value[0], token.value[1], token.text)
        if kind == "string":
            self.advance()
            return String(token.line, token.column, token.value)
        if kind == "nil":
            self.advance()
            return Nil(token.line, token.column)
        if kind in ("true", "false"):
            self.advance()
            return Bool(token.line, token.column, kind == "true")
        if kind == "...":
            self.advance()
            return Vararg(token.line, token.column)
        if kind == "{":
            return self.table()
        if kind == "function":
            self.advance()
            return self.func_body(token)
        return self.suffixed_expr()

    def primary_expr(self):
        token = self.tok
        if token.kind == "name":
            return self.name()
        if token.kind == "(":
            self.advance()
            inner = self.expr()
            self.expect_match(")", "(", token)
            return Paren(token.line, token.column, inner)
        self.error_near("unexpected symbol")

    def suffixed_expr(self):
        expr = self.primary_expr()
        while True:
            token = self.tok
            kind = token.kind
            if kind == ".":
                self.advance()
                name = self.name()
                expr = Field(expr.line, expr.column, expr, name.name)
            elif kind == "[":
                self.advance()
                key = self.expr()
                self.expect("]")
                expr = Index(expr.line, expr.column, expr, key)
            elif kind == ":":
                self.advance()
                name = self.name()
                args = self.call_args()
                expr = Method(name.line, name.column, expr, name.name, args)
            elif kind in ("(", "string", "{"):
                expr = Call(expr.line, expr.column, expr, self.call_args())
            else:
                return expr

    def call_args(self):
        token = self.tok
        if token.kind == "string":
            self.advance()
            return [String(token.line, token.column, token.value)]
        if token.kind == "{":
            return [self.table()]
        if token.kind != "(":
            self.error_near("function arguments expected")
        self.advance()
        args = [] if self.check(")") else self.expr_list()
        self.expect_match(")", "(", token)
        return args

    def table(self):
        token = self.expect("{")
        items = []
        while not self.check("}"):
            item = self.tok
            if item.kind == "[":
                self.advance()
                key = self.expr()
                self.expect("]")
                self.expect("=")
                items.append(TableItem(item.line, item.column, "expr", key, self.expr()))
            elif item.kind == "name" and self.peek().kind == "=":
                self.advance()
                self.advance()
                items.append(TableItem(item.line, item.column, "name", item.value, self.expr()))
            else:
                items.append(TableItem(item.line, item.column, "list", None, self.expr()))
            if not (self.accept(",") or self.accept(";")):
                break
        self.expect_match("}", "{", token)
        return Table(token.line, token.column, items)


def parse(text, file="script.lua"):
    """The script's syntax tree: its top-level Block. Raises CompileError."""
    return Parser(tokenize(text, file), file).chunk()


def dump(node):
    """A syntax tree as nested tuples without positions or parentheses, for
    comparing trees."""
    if isinstance(node, Paren):
        return dump(node.expr)
    if isinstance(node, Node):
        return (type(node).__name__,) + tuple(dump(getattr(node, f)) for f in node.fields
                                              if f not in ("end_line", "end_column"))
    if isinstance(node, list):
        return tuple(dump(item) for item in node)
    return node


# --- Types and constants -----------------------------------------------------

INT, FIXED, BOOL, ENTITY, STRING = "integer", "fixed", "boolean", "entity", "string"
ARRAY, OBJECT, FUNCTION, BUILTIN, VOID = "array", "object", "function", "builtin", "no value"
NUMERIC = (INT, FIXED)

_ARTICLE = {INT: "an integer", FIXED: "fixed", BOOL: "a boolean", ENTITY: "an entity",
            STRING: "a string", ARRAY: "an array", OBJECT: "an object", FUNCTION: "a function",
            BUILTIN: "an engine function", VOID: "no value"}


def article(ty):
    return _ARTICLE.get(ty, str(ty))


FX_ONE = 256
FIXED_LIMIT = "-8388608 to 8388607.996"  # 24.8 in a 32-bit cell

# Assembler expression precedence (svm.py's grammar, C's): how tightly an
# expression's text binds, to parenthesize it only where needed.
P_OR, P_AND, P_SHIFT, P_ADD, P_MUL, P_UNARY, P_ATOM = 1, 2, 3, 4, 5, 6, 7


class Const:
    """A value known at compile time. value: an int (a cell: a fixed value
    scaled by 256, a boolean 0 or 1, an entity handle), bytes for a string,
    or None when only the assembler knows it (it uses header constants).
    text: the assembler expression for it, or None to print the value."""

    __slots__ = ("ty", "value", "text", "prec")

    def __init__(self, ty, value=None, text=None, prec=P_ATOM):
        self.ty = ty
        self.value = value
        self.text = text
        self.prec = prec

    @property
    def known(self):
        return self.value is not None

    @property
    def is_zero(self):
        return self.value == 0

    def asm(self):
        return self.text if self.text is not None else str(self.value)

    def asm_prec(self):
        if self.text is not None:
            return self.prec
        return P_ATOM if self.value >= 0 else P_UNARY

    def group(self, prec):
        """The text, parenthesized if it binds less tightly than prec."""
        text = self.asm()
        return f"({text})" if self.asm_prec() < prec else text

    def clear(self, prec):
        """The text as an operand of a shift, & or |: parenthesized unless it
        is a single term or the same operator, as C programmers write it."""
        text = self.asm()
        own = self.asm_prec()
        return f"({text})" if own < P_UNARY and own != prec else text

    def __repr__(self):
        return f"Const({self.ty}, {self.value!r}, {self.text!r})"


def fixed_from_float(x):
    """A float as 24.8 fixed point, rounded to nearest; None if it doesn't fit."""
    if math.isnan(x) or math.isinf(x):
        return None
    raw = math.floor(x * FX_ONE + 0.5)
    return raw if INT_MIN <= raw <= INT_MAX else None


def fixed_text(raw):
    """A fixed value as a decimal for comments: 384 is 1.5."""
    text = f"{raw / FX_ONE:.6f}".rstrip("0")
    return text + "0" if text.endswith(".") else text


def lua_shift_left(a, b):
    """Lua's a << b on 32-bit integers (a >> b is a << -b)."""
    if b <= -32 or b >= 32:
        return 0
    if b >= 0:
        return wrap32((a & 0xFFFFFFFF) << b)
    return wrap32((a & 0xFFFFFFFF) >> -b)


def c_div(a, b):
    """C's division: rounds toward zero."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


# --- Program model -----------------------------------------------------------

# The engine's properties by their Lua field names: (the VM_P_* name, type).
# Each has its C pool's meaning. scale is fixed point: spr_scale's 8.8 has the
# same 256-is-one scaling. body_max_fall is a speed, 24.8 like vy (in a u16).
# body_bounce and body_friction are integers, C's 256ths in a u8 (body_bounce
# 255 is a perfect bounce, not 255/256), and body_gravity is C's s8, written
# with BODY_GRAVITY(n) as in C.
PROPERTIES = {
    "x": ("X", FIXED), "y": ("Y", FIXED), "vx": ("VX", FIXED), "vy": ("VY", FIXED),
    "sprite": ("SPR", INT), "frame": ("FRAME", INT), "flags": ("FLAGS", INT),
    "angle": ("ANGLE", INT), "depth": ("DEPTH", INT), "scale": ("SCALE", FIXED),
    "body_w": ("BODY_W", INT), "body_h": ("BODY_H", INT), "tags": ("TAGS", INT),
    "anim_time": ("ANIM_TIME", INT), "anim_step": ("ANIM_STEP", INT),
    "body_bounce": ("BODY_BOUNCE", INT), "body_friction": ("BODY_FRICTION", INT),
    "body_max_fall": ("BODY_MAX_FALL", FIXED), "body_gravity": ("BODY_GRAVITY", INT),
    "body_contact": ("BODY_CONTACT", INT),
    # The object an entity is attached to: compared with == and ~= only, with
    # an object's name or another entity's object (other.object == Coin).
    "object": ("OBJECT", OBJECT),
}
# The properties a script reads but can't assign (SETP refuses them): why, and
# a hint.
READ_ONLY = {
    "body_contact": ("the engine sets it to what the body touched in the last sys_physics() "
                     "(with physics_set_contacts(true)) or sys_map_movement()",
                     "test its bits: self.body_contact & MAP_CONTACT_FLOOR ~= 0"),
    "object": ("an instance's object is the one it was spawned or attached as",
               "compare it: other.object == Coin"),
}
# Instance fields can't start with these: they are kept for the properties
# later engine versions add. Every property added after 1.0.0-rc.1 has a name
# that starts with one, and no other name will become a property, so a
# script's own fields never change meaning when the engine grows (docs/lua.md,
# "Reserved names"). Today's properties keep their names, prefixed or not.
RESERVED_PREFIXES = ("anim_", "body_", "ent_", "map_", "path_", "pos_", "spr_", "vel_", "vm_")
# The C pools whose properties have other names in Lua, for the hint when a
# script uses the C name.
C_POOL_PROPERTIES = {
    "pos_x": "x", "pos_y": "y", "vel_x": "vx", "vel_y": "vy", "spr_id": "sprite",
    "spr_frame": "frame", "spr_flags": "flags", "spr_angle": "angle", "spr_depth": "depth",
    "spr_scale": "scale", "spr_anim_time": "anim_time", "spr_anim_step": "anim_step",
}
# The engine's macros a script can call (constant arguments only), which the
# assembler knows too: (the range of n, what it is).
MACROS = {
    "C_GAME": ((0, 14), "a game component"),
    "BODY_GRAVITY": ((-112, 143), "a body's gravity scale in 16ths (16 normal, 0 none, "
                                  "-16 reversed)"),
}
VM_FIELDS = 16  # instance fields, VM_P_FIELD0 to VM_P_FIELD0 + 15
VM_ARRAY_CELLS = 1024

# The six events: (the VM_EV_* name, whether it is a behaviour, which may wait).
EVENTS = {
    "create": ("CREATE", True), "step": ("STEP", False), "destroy": ("DESTROY", False),
    "collision": ("COLLISION", False), "anim_end": ("ANIM_END", False),
    "room_start": ("ROOM_START", True),
}

# Engine functions with plain arguments: (argument types, result, op or SYS call).
# Each SYS call's function has its VM_SYS_* name in lower case, which is its C
# function's name (docs/lua.md, "Engine functions").
ENGINE = {
    "psg_play": ((INT,), None, "SYS PSG_PLAY"),
    "psg_music_play": ((INT,), None, "SYS PSG_MUSIC_PLAY"),
    "psg_music_stop": ((), None, "SYS PSG_MUSIC_STOP"),
    "psg_music_pause": ((), None, "SYS PSG_MUSIC_PAUSE"),
    "psg_music_resume": ((), None, "SYS PSG_MUSIC_RESUME"),
    "camera_set": ((INT, INT), None, "SYS CAMERA_SET"),
    "random_range": ((INT, INT), INT, "SYS RANDOM_RANGE"),
    "button_down": ((INT,), BOOL, "SYS BUTTON_DOWN"),
    "button_pressed": ((INT,), BOOL, "SYS BUTTON_PRESSED"),
    "screen_set_brightness": ((INT,), None, "SYS SCREEN_SET_BRIGHTNESS"),
    "path_start": ((ENTITY, INT, INT), None, "SYS PATH_START"),
    "path_stop": ((ENTITY,), None, "SYS PATH_STOP"),
    "kill": ((ENTITY,), None, "KILL"),
    "wait": ((INT,), None, "WAIT"),
    "wait_anim": ((), None, "WAIT_ANIM"),
    "wait_move": ((), None, "WAIT_MOVE"),
}
WAITS = ("wait", "wait_anim", "wait_move")
# Every name the engine gives a script; none is the entity 0.
# text_print and text_print_number are checked and compiled on their own (a
# string argument; an optional width).
TEXT_CALLS = ("text_print", "text_print_number")
ENGINE_NAMES = frozenset(ENGINE) | frozenset(TEXT_CALLS) | {"spawn", "instances", "object",
                                                           "array", "none", "math"}
MATH_FUNCTIONS = ("floor", "abs", "min", "max")
# math's integer limits, with LUA_32BITS.
MATH_CONSTANTS = {"mininteger": INT_MIN, "maxinteger": INT_MAX}

_TABLE_HINT = ("loop over an array with for i = 1, #a do, or over an object's instances "
               "with for e in instances(Object) do")
# Lua's standard library outside the subset: (what to call it, a hint).
STDLIB = {
    "pairs": ("pairs (iterating a table)", _TABLE_HINT),
    "ipairs": ("ipairs (iterating a table)", _TABLE_HINT),
    "next": ("next (iterating a table)", _TABLE_HINT),
    "setmetatable": ("setmetatable (metatables)", None),
    "getmetatable": ("getmetatable (metatables)", None),
    "rawget": ("rawget (raw table access)", None), "rawset": ("rawset (raw table access)", None),
    "rawequal": ("rawequal", None), "rawlen": ("rawlen", None),
    "coroutine": ("the coroutine library",
                  "handlers already are coroutines: create and room_start may wait()"),
    "string": ("the string library (string operations at run time)",
               "draw a literal: text_print(col, row, \"text\"); .. of two literals is "
               "folded"),
    "utf8": ("the utf8 library", None),
    "table": ("the table library", "arrays are declared at the top level: a = array(n)"),
    "os": ("the os library", None), "io": ("the io library", None),
    "debug": ("the debug library", None), "package": ("package", None),
    "require": ("require (modules)", "a script is one file"),
    "load": ("load", None), "loadfile": ("loadfile", None), "dofile": ("dofile", None),
    "tostring": ("tostring (strings at run time)",
                 "text_print_number(col, row, n) prints a number"),
    "print": ("print (console output)",
              "text_print(col, row, \"text\") draws text on the screen, "
              "text_print_number(col, row, n) a number"),
    "tonumber": ("tonumber (strings at run time)", None),
    "type": ("type() (types are checked at compile time)", None),
    "select": ("select (varargs)", None),
    "error": ("error()", None), "assert": ("assert()", None),
    "pcall": ("pcall (errors)", None), "xpcall": ("xpcall (errors)", None),
    "collectgarbage": ("collectgarbage (there is no heap)", None),
    "warn": ("warn()", None), "_G": ("the globals table _G", None),
    "_ENV": ("_ENV (environments)", None), "_VERSION": ("_VERSION", None),
}


def is_header_name(name):
    """ALL_CAPS: a constant from the game's C headers, when the script
    doesn't define the name."""
    return re.fullmatch(r"[A-Z][A-Z0-9_]*", name) is not None


class Sym:
    kind = "?"

    def __init__(self, name, node):
        self.name = name
        self.node = node  # where it is declared
        self.ty = None
        self.origin = None  # the node that decided its type (None: a default)


class GlobalSym(Sym):
    kind = "global"

    def __init__(self, name, node, init, is_local):
        super().__init__(name, node)
        self.init = init  # its initial value's expression
        self.init_const = None
        self.is_local = is_local
        self.listing = name.upper()


class ConstSym(Sym):
    """A top-level local <const>. const: its value, or None when the
    assembler can't compute it and each use evaluates init again (inline)."""

    kind = "const"

    def __init__(self, name, node, init):
        super().__init__(name, node)
        self.init = init
        self.const = None
        self.inline = False


class ObjectSym(Sym):
    kind = "object"

    def __init__(self, name, node, call):
        super().__init__(name, node)
        self.call = call  # the object { ... } call
        self.components = None  # expressions, or None (0)
        self.sprite = None
        self.components_const = None
        self.sprite_const = None
        self.index = None
        self.handlers = {}  # event name -> Body
        self.listing = name.upper()


class ArraySym(Sym):
    kind = "array"

    def __init__(self, name, node, value, rom, is_local=False):
        super().__init__(name, node)
        self.value = value  # the array(n) call or the table constructor
        self.rom = rom
        self.is_local = is_local  # a top-level local
        self.length = None  # Const
        self.items = []  # ROM: Consts
        self.rom_kind = None  # s8 u8 s16 u16 s32
        self.elem = None  # the elements' type (ty is ARRAY)
        self.elem_origin = None
        self.listing = name.upper()


class FunctionSym(Sym):
    kind = "function"

    def __init__(self, name, node, is_local):
        super().__init__(name, node)
        self.is_local = is_local
        self.body = None
        self.result = None  # a type, VOID, or None while unknown
        self.result_origin = None
        self.calls_seen = []  # call nodes, for "never called"


class LocalSym(Sym):
    kind = "local"

    def __init__(self, name, node, readonly=False, param=False):
        super().__init__(name, node)
        self.readonly = readonly  # <const>
        self.param = param
        self.const = None  # a <const> local's value, when known
        self.init = None  # the value it is declared with
        self.assigned = False  # assigned after its declaration
        self.slot = None  # its frame local, set by code generation


class EntitySym(Sym):
    kind = "entity"  # self, or a collision handler's parameter (OTHER)

    def __init__(self, name, node, op):
        super().__init__(name, node)
        self.op = op  # SELF or OTHER
        self.ty = ENTITY


class HeaderSym(Sym):
    kind = "header"


class BuiltinSym(Sym):
    kind = "builtin"


class FieldSym:
    """An instance field: one name, one slot of VM_FIELDS for the program."""

    def __init__(self, name, slot, node):
        self.name = name
        self.slot = slot
        self.node = node
        self.ty = None
        self.origin = None
        self.listing = None  # its .const name


class Body:
    """A handler or a function: the code generator's unit."""

    def __init__(self, kind, name, func, stat):
        self.kind = kind  # "handler" or "function"
        self.name = name  # for messages: "Firefly:create", "print_score"
        self.func = func  # the FunctionExpr
        self.stat = stat
        self.obj = None  # handlers: the ObjectSym, event and kind
        self.event = None
        self.behaviour = False
        self.fn = None  # functions: the FunctionSym
        self.params = []  # LocalSym, the frame's first locals
        self.self_sym = None
        self.other_sym = None
        self.calls = []  # (FunctionSym, call node), in source order
        self.waits = []  # wait calls, in source order
        self.self_uses = []
        self.value_returns = []
        self.bare_returns = []
        self.locals = []  # every LocalSym declared in it
        self.label = None  # functions: the listing label


class Program:
    def __init__(self, file):
        self.file = file
        self.objects = []
        self.globals = []
        self.consts = []
        self.arrays = []
        self.functions = []
        self.bodies = []  # handlers and functions, in source order
        self.top_order = []  # every top-level declaration's symbol, in source order
        self.fields = {}  # name -> FieldSym
        self.headers = {}  # name -> HeaderSym, in order of first use
        self.warnings = []  # (line, column, message)

    def warn(self, node, message):
        self.warnings.append((node.line, node.column, message))


# --- Names -------------------------------------------------------------------


def _rhs_kind(value):
    """What a top-level assignment declares, from its value's shape."""
    if isinstance(value, Call) and isinstance(value.func, Name):
        if value.func.name == "object":
            return "object"
        if value.func.name == "array":
            return "array"
    if isinstance(value, Table):
        return "rom"
    return "global"


def _describe(e):
    """A short description of an expression for messages."""
    if isinstance(e, Paren):
        return _describe(e.expr)
    if isinstance(e, Name):
        return e.name
    if isinstance(e, Field):
        inner = _describe(e.obj)
        return f"{inner}.{e.name}" if inner else e.name
    if isinstance(e, Call) and isinstance(e.func, (Name, Field)):
        return _describe(e.func) + "(...)"
    if isinstance(e, Number):
        return e.text
    return None


class Resolver:
    """The program model from the syntax tree: top-level declarations,
    handlers and functions, each name bound to what it means, and every
    construct outside the subset rejected by name."""

    def __init__(self, chunk, file):
        self.chunk = chunk
        self.file = file
        self.p = Program(file)
        self.globals = {}  # non-local top-level names, visible everywhere
        self.top_locals = {}  # top-level locals declared so far
        self.later_locals = {}  # every top-level local name -> its line
        self.listing_names = {}  # (category, LISTING) -> Sym
        self.builtins = {}

    def error(self, node, message, hint=None):
        raise CompileError(message, self.file, node.line, node.column, hint)

    def run(self):
        stats = self.chunk.stats
        for stat in stats:  # the names top-level locals will have, for messages
            if isinstance(stat, Local):
                for name in stat.names:
                    self.later_locals.setdefault(name.name, name.line)
            elif isinstance(stat, LocalFunction):
                self.later_locals.setdefault(stat.name.name, stat.name.line)
        for stat in stats:
            self.predeclare(stat)
        for stat in stats:
            self.top_statement(stat)
        objects = sorted((s for s in self.p.top_order if s.kind == "object"),
                         key=lambda s: (s.node.line, s.node.column))
        for index, obj in enumerate(objects):
            obj.index = index
        self.p.objects = objects
        return self.p

    # --- Top level ---

    def predeclare(self, stat):
        """Global names are visible everywhere (they exist once the script
        has loaded); this makes their symbols before any code is resolved."""
        if isinstance(stat, Assign):
            for target, value in zip(stat.targets, stat.values):
                if isinstance(target, Name):
                    self.declare_global(target, value, is_local=False)
        elif isinstance(stat, FunctionStat) and stat.method is None and len(stat.path) == 1:
            name = stat.path[0]
            self.check_new_name(name)
            sym = FunctionSym(name.name, name, is_local=False)
            self.globals[name.name] = sym

    def check_new_name(self, name):
        if name.name in ENGINE_NAMES:
            self.error(name, f"{name.name} is an engine function; a script can't redefine it",
                       "choose another name")
        if name.name in STDLIB:
            what, _ = STDLIB[name.name]
            self.error(name, f"{name.name} is Lua's {what}; the subset doesn't use it, "
                       "but a script can't redefine it either", "choose another name")
        previous = self.globals.get(name.name) or self.top_locals.get(name.name)
        if previous is not None:
            self.error(name, f"{name.name} is already declared (line {previous.node.line})",
                       "a top-level name is declared once")

    def declare_global(self, name, value, is_local):
        self.check_new_name(name)
        kind = _rhs_kind(value)
        if kind == "object":
            sym = ObjectSym(name.name, name, value)
        elif kind in ("array", "rom"):
            sym = ArraySym(name.name, name, value, rom=kind == "rom", is_local=is_local)
        else:
            sym = GlobalSym(name.name, name, value, is_local)
        category = {"object": "object", "array": "array", "rom": "array"}.get(kind, "global")
        if hasattr(sym, "listing"):
            key = (category, sym.listing)
            other = self.listing_names.get(key)
            if other is not None:
                self.error(name, f"{name.name} and {other.name} (line {other.node.line}) are "
                           f"both {sym.listing} in the listing, where C sees the names "
                           "upper-cased", "rename one")
            self.listing_names[key] = sym
        if is_local:
            self.top_locals[name.name] = sym
        else:
            self.globals[name.name] = sym
        return sym

    def top_statement(self, stat):
        if isinstance(stat, Local):
            self.top_local(stat)
        elif isinstance(stat, Assign):
            self.top_assign(stat)
        elif isinstance(stat, FunctionStat):
            self.top_function(stat)
        elif isinstance(stat, LocalFunction):
            self.check_new_name(stat.name)
            sym = FunctionSym(stat.name.name, stat.name, is_local=True)
            self.top_locals[stat.name.name] = sym
            self.function_body(sym, stat)
        elif isinstance(stat, Return):
            self.error(stat, "a script doesn't return anything at the top level",
                       "the top level only declares objects, globals, arrays and functions")
        else:
            what = {If: "if", While: "while", Repeat: "repeat", NumericFor: "for",
                    GenericFor: "for", Do: "do", CallStat: "a call", Goto: "goto",
                    Label: "a label", Break: "break"}.get(type(stat), "this statement")
            self.error(stat, f"{what} at the top level: only declarations go there",
                       "the VM never runs a script's top level; put code in a handler, "
                       "e.g. function Game:room_start()")

    def check_values(self, stat, names, values):
        if len(values) < len(names):
            name = names[len(values)]
            self.error(name, f"{name.name} gets no value, so it would be nil, "
                       "and nil is not in the subset", "give it a value: 0, false or none")
        if len(values) > len(names):
            self.error(values[len(names)], "more values than names: the extra values "
                       "would be dropped", "write one value per name")

    def top_local(self, stat):
        self.check_values(stat, stat.names, stat.values)
        for name, attrib, value in zip(stat.names, stat.attribs, stat.values):
            if attrib == "close":
                self.error(name, "to-be-closed variables (<close>) are not in the subset")
            self.check_new_name(name)
            kind = _rhs_kind(value)
            if attrib == "const" and kind == "global":
                self.top_expr(value)
                sym = ConstSym(name.name, name, value)
                self.top_locals[name.name] = sym
                self.p.consts.append(sym)
                self.p.top_order.append(sym)
                continue
            sym = self.declare_global(name, value, is_local=True)
            self.top_value(sym, value)

    def top_assign(self, stat):
        for target in stat.targets:
            if not isinstance(target, Name):
                self.error(target, "only names are assigned at the top level, where "
                           "each assignment declares a global, an object or an array",
                           "set fields in a handler")
        self.check_values(stat, stat.targets, stat.values)
        for target, value in zip(stat.targets, stat.values):
            self.top_value(self.globals[target.name], value)

    def top_value(self, sym, value):
        """A declaration's value: an object's or array's parts, or a global's
        initial value."""
        if sym.kind == "object":
            self.object_decl(sym, value)
        elif sym.kind == "array":
            self.array_decl(sym, value)
        else:
            self.top_expr(value)
            self.p.globals.append(sym)
        self.p.top_order.append(sym)

    def object_decl(self, sym, call):
        if len(call.args) != 1 or not isinstance(call.args[0], Table):
            self.error(call, "object takes a table: object { components = ..., sprite = ... }")
        seen = {}
        for item in call.args[0].items:
            if item.kind != "name" or item.key not in ("components", "sprite"):
                self.error(item, "an object has two fields, components and sprite",
                           "object { components = C_POS | C_SPR, sprite = SPR_X }")
            if item.key in seen:
                self.error(item, f"{item.key} is given twice")
            seen[item.key] = item.value
            self.top_expr(item.value)
        sym.components = seen.get("components")
        sym.sprite = seen.get("sprite")

    def array_decl(self, sym, value):
        if not sym.rom:
            if len(value.args) != 1:
                self.error(value, "array takes one argument, its length: array(8)")
            self.top_expr(value.args[0])
        else:
            if not value.items:
                self.error(value, "an empty table: a constant table needs its elements",
                           "use array(n) for an array the scripts fill")
            for item in value.items:
                if item.kind != "list":
                    self.error(item, "tables with keys are not in the subset",
                               "a constant table lists integers: name = { 3, 5, 8 }")
                if isinstance(item.value, Table):
                    self.error(item.value, "nested tables are not in the subset")
                self.top_expr(item.value)
        self.p.arrays.append(sym)

    def top_function(self, stat):
        if stat.method is not None:
            self.handler(stat)
            return
        if len(stat.path) > 1:
            self.error(stat.path[1], "functions in tables (function a.b()) are not in the "
                       "subset", "declare function b() at the top level")
        self.function_body(self.globals[stat.path[0].name], stat)

    def handler(self, stat):
        if len(stat.path) > 1:
            self.error(stat.path[1], "handlers are declared on an object: "
                       "function Object:event()")
        obj_name = stat.path[0]
        obj = self.top_locals.get(obj_name.name) or self.globals.get(obj_name.name)
        if obj is None or obj.kind != "object":
            what = "not declared" if obj is None else f"not an object (it is {obj.kind} {obj.name})"
            self.error(obj_name, f"{obj_name.name} is {what}",
                       f"declare it first: {obj_name.name} = object {{ ... }}")
        if (obj.node.line, obj.node.column) > (stat.line, stat.column):
            self.error(obj_name, f"{obj.name} is declared below (line {obj.node.line}); "
                       f"in Lua it doesn't exist yet when function {obj.name}:"
                       f"{stat.method.name}() runs", "declare the object above its handlers")
        event = stat.method.name
        if event not in EVENTS:
            self.error(stat.method, f"{obj.name}:{event} is not an event; methods are not in "
                       "the subset", "the events are " + ", ".join(EVENTS) +
                       f"; for a helper write function {event}(self, ...)")
        if event in obj.handlers:
            self.error(stat.method, f"{obj.name}:{event} is already defined (line "
                       f"{obj.handlers[event].stat.line})")
        func = stat.func
        expected = 1 if event == "collision" else 0
        if func.vararg is not None:
            self.error(func.vararg, "varargs (...) are not in the subset")
        if len(func.params) != expected:
            takes = "one parameter, the other entity" if expected else "no parameters"
            self.error(func, f"{event} takes {takes}, not {len(func.params)}",
                       "function Object:collision(other)" if expected else
                       f"function {obj.name}:{event}()")
        body = Body("handler", f"{obj.name}:{event}", func, stat)
        body.obj = obj
        body.event = event
        body.behaviour = EVENTS[event][1]
        body.self_sym = EntitySym("self", func, "SELF")
        if expected:
            body.other_sym = EntitySym(func.params[0].name, func.params[0], "OTHER")
        obj.handlers[event] = body
        BodyResolver(self, body).run()
        self.p.bodies.append(body)

    def function_body(self, sym, stat):
        func = stat.func
        if func.vararg is not None:
            self.error(func.vararg, "varargs (...) are not in the subset",
                       "give the function named parameters")
        body = Body("function", sym.name, func, stat)
        body.fn = sym
        sym.body = body
        seen = set()
        for param in func.params:
            if param.name in seen:
                self.error(param, f"parameter {param.name} is named twice")
            seen.add(param.name)
            body.params.append(LocalSym(param.name, param, param=True))
        BodyResolver(self, body).run()
        self.p.functions.append(sym)
        self.p.bodies.append(body)
        self.p.top_order.append(sym)

    def top_expr(self, e):
        """A top-level value: names resolve in the top-level scope."""
        BodyResolver(self, None).expr(e)

    # --- Lookup ---

    def lookup_top(self, node):
        name = node.name
        if name in self.top_locals:
            return self.top_locals[name]
        if name in self.globals:
            return self.globals[name]
        if name in ENGINE_NAMES:
            if name not in self.builtins:
                self.builtins[name] = BuiltinSym(name, node)
            return self.builtins[name]
        if name in STDLIB:
            what, hint = STDLIB[name]
            self.error(node, f"{what} is not in the subset", hint)
        if name == "self":
            self.error(node, "self exists only in handlers (function Object:event())",
                       "pass the entity to the function: f(self)")
        if name in self.later_locals:
            self.error(node, f"{name} is declared below (line {self.later_locals[name]}) as a "
                       "top-level local, so here it would be an undefined global",
                       "move the declaration up")
        if name in MACROS:  # the engine's macros, which the assembler knows
            if name not in self.builtins:
                self.builtins[name] = HeaderSym(name, node)
            return self.builtins[name]
        if is_header_name(name):
            if name not in self.p.headers:
                self.p.headers[name] = HeaderSym(name, node)
            return self.p.headers[name]
        self.error(node, f"{name} is not defined",
                   f"declare it: a local (local {name} = 0) or a global at the top level "
                   f"({name} = 0); names in ALL_CAPS are constants from the C headers")


class BodyResolver:
    """Names, scopes, labels and the subset's rules inside one handler or
    function (or, with no body, in a top-level value)."""

    def __init__(self, resolver, body):
        self.r = resolver
        self.body = body
        self.scopes = []
        self.loops = 0
        # goto and labels, as Lua 5.4 checks them: visible labels per block,
        # pending forward gotos with the number of locals active at them.
        self.blocks = []  # {"labels": {name: (Label, nactvar)}, "first": int, "nactvar": int}
        self.pending = []  # [Goto, nactvar]
        self.active = []  # the active locals, in order

    def error(self, node, message, hint=None):
        self.r.error(node, message, hint)

    def run(self):
        body = self.body
        scope = {}
        if body.self_sym is not None:
            scope["self"] = body.self_sym
        if body.other_sym is not None:
            scope[body.other_sym.name] = body.other_sym
        for param in body.params:
            scope[param.name] = param
            self.active.append(param)
        self.scopes.append(scope)
        self.block(body.func.body)
        if self.pending:
            goto = self.pending[0][0]
            self.error(goto, f"no visible label '{goto.label}' for goto at line {goto.line}")
        if body.value_returns and body.bare_returns:
            ret = body.value_returns[0]
            self.error(body.bare_returns[0], f"{body.name} returns a value at line {ret.line} "
                       "and nothing here", "a function either always returns a value or "
                       "never does (nil is not in the subset)")

    def lookup(self, node):
        for scope in reversed(self.scopes):
            if node.name in scope:
                return scope[node.name]
        return self.r.lookup_top(node)

    def declare(self, node, readonly=False):
        sym = LocalSym(node.name, node, readonly=readonly)
        self.scopes[-1][node.name] = sym
        self.active.append(sym)
        self.body.locals.append(sym)
        return sym

    # --- Blocks and statements ---

    def block(self, block, repeat=False, until=None):
        self.scopes.append({})
        info = {"labels": {}, "first": len(self.pending), "nactvar": len(self.active)}
        self.blocks.append(info)
        for index, stat in enumerate(block.stats):
            self.statement(stat, block, index, repeat)
        if until is not None:
            self.expr(until)
        self.blocks.pop()
        start = info["nactvar"]
        del self.active[start:]
        for goto in self.pending[info["first"]:]:
            goto[1] = min(goto[1], start)
        self.scopes.pop()

    def statement(self, stat, block, index, repeat):
        if isinstance(stat, Local):
            self.local(stat)
        elif isinstance(stat, Assign):
            for value in stat.values:
                self.expr(value)
            for target in stat.targets:
                self.target(target)
            if len(stat.values) != len(stat.targets):
                self.r.check_values(stat, stat.targets, stat.values)
        elif isinstance(stat, CallStat):
            self.expr(stat.call)
        elif isinstance(stat, Do):
            self.block(stat.body)
        elif isinstance(stat, While):
            self.expr(stat.cond)
            self.loop(stat.body)
        elif isinstance(stat, Repeat):
            self.loops += 1
            self.block(stat.body, repeat=True, until=stat.cond)
            self.loops -= 1
        elif isinstance(stat, If):
            for test, blk in zip(stat.tests, stat.blocks):
                self.expr(test)
                self.block(blk)
            if stat.orelse is not None:
                self.block(stat.orelse)
        elif isinstance(stat, NumericFor):
            for e in (stat.start, stat.limit, stat.step):
                if e is not None:
                    self.expr(e)
            self.loop(stat.body, stat.var)
        elif isinstance(stat, GenericFor):
            self.generic_for(stat)
        elif isinstance(stat, (FunctionStat, LocalFunction)):
            self.error(stat, "functions are declared at the top level only (closures over a "
                       "function's locals are not in the subset)",
                       "move the function to the top level and pass it what it needs")
        elif isinstance(stat, Return):
            self.return_stat(stat)
        elif isinstance(stat, Break):
            if self.loops == 0:
                self.error(stat, f"break outside a loop at line {stat.line}")
        elif isinstance(stat, Goto):
            self.goto(stat)
        elif isinstance(stat, Label):
            self.label(stat, block, index, repeat)
        else:  # pragma: no cover - the parser makes nothing else
            self.error(stat, f"unexpected statement {type(stat).__name__}")

    def loop(self, body, var=None):
        self.loops += 1
        self.scopes.append({})
        mark = len(self.active)
        if var is not None:
            var.sym = self.declare(var)
        self.block(body)
        del self.active[mark:]
        self.scopes.pop()
        self.loops -= 1

    def local(self, stat):
        for value in stat.values:
            self.expr(value)
        self.r.check_values(stat, stat.names, stat.values)
        for name, attrib, value in zip(stat.names, stat.attribs, stat.values):
            if attrib == "close":
                self.error(name, "to-be-closed variables (<close>) are not in the subset")
            name.sym = self.declare(name, readonly=attrib == "const")
            name.sym.init = value

    def target(self, target):
        if isinstance(target, Name):
            sym = self.lookup(target)
            target.sym = sym
            kind = sym.kind
            if kind == "local":
                if sym.readonly:
                    self.error(target, f"attempt to assign to const variable '{target.name}'")
                sym.assigned = True
            elif kind == "global":
                pass
            elif kind == "const":
                self.error(target, f"attempt to assign to const variable '{target.name}'")
            elif kind == "header":
                self.error(target, f"{target.name} is not declared, so it would be a constant "
                           "from the C headers, which can't be assigned",
                           f"declare a global at the top level: {target.name} = 0")
            elif kind == "entity":
                self.error(target, f"{target.name} is the instance the handler runs for; "
                           "it can't be assigned", "use a local: local e = " + target.name)
            elif kind == "builtin":
                self.error(target, f"{target.name} is an engine function; it can't be assigned")
            else:
                self.error(target, f"{target.name} is {article(kind)}; it can't be assigned")
        else:
            obj = target.obj if isinstance(target, Field) else None
            if isinstance(obj, Name) and obj.name == "math" \
                    and self.lookup(obj).kind == "builtin":
                self.error(target, f"math.{target.name} can't be assigned")
            if isinstance(target, Field) and target.name in READ_ONLY:
                why, hint = READ_ONLY[target.name]
                self.error(target, f"{target.name} is read-only: {why}", hint)
            self.expr(target)

    def generic_for(self, stat):
        for e in stat.exprs:
            if isinstance(e, Call) and isinstance(e.func, Name):
                sym = self.lookup(e.func)  # rejects pairs, ipairs, next by name
                if sym.kind == "builtin" and sym.name == "instances":
                    break
        else:
            self.error(stat, "the generic for works only with instances(Object) here",
                       "for e in instances(Firefly) do ... end, or a numeric for over an array")
        call = stat.exprs[0]
        if len(stat.exprs) != 1 or not (isinstance(call, Call) and isinstance(call.func, Name)
                                        and call.func.name == "instances"):
            self.error(stat, "the generic for works only with instances(Object) here",
                       "for e in instances(Firefly) do ... end")
        if len(stat.names) != 1:
            self.error(stat.names[1], "instances() gives one value, the entity",
                       "for e in instances(Firefly) do")
        call.func.sym = self.lookup(call.func)
        if len(call.args) != 1 or not isinstance(call.args[0], Name):
            self.error(call, "instances takes an object: instances(Firefly)")
        arg = call.args[0]
        arg.sym = self.lookup(arg)
        if arg.sym.kind != "object":
            self.error(arg, f"instances takes an object, and {arg.name} is not one")
        self.loops += 1
        self.scopes.append({})
        mark = len(self.active)
        stat.names[0].sym = self.declare(stat.names[0])
        self.block(stat.body)
        del self.active[mark:]
        self.scopes.pop()
        self.loops -= 1

    def return_stat(self, stat):
        body = self.body
        if len(stat.values) > 1:
            self.error(stat.values[1], "multiple results are not in the subset",
                       "return one value")
        for value in stat.values:
            self.expr(value)
        if body.kind == "handler":
            if stat.values:
                self.error(stat.values[0], "handlers don't return values",
                           "return on its own ends the handler")
            return
        (body.value_returns if stat.values else body.bare_returns).append(stat)

    def goto(self, stat):
        for info in reversed(self.blocks):
            if stat.label in info["labels"]:
                stat.target = info["labels"][stat.label][0]  # a backward jump
                return
        self.pending.append([stat, len(self.active)])

    def label(self, stat, block, index, repeat):
        for info in self.blocks:
            if stat.name in info["labels"]:
                other = info["labels"][stat.name][0]
                self.error(stat, f"label '{stat.name}' already defined on line {other.line}")
        info = self.blocks[-1]
        last = not repeat and all(isinstance(s, Label) for s in block.stats[index + 1:])
        nactvar = info["nactvar"] if last else len(self.active)
        info["labels"][stat.name] = (stat, nactvar)
        still = []
        for goto in self.pending[info["first"]:]:
            g, at = goto
            if g.label != stat.name:
                still.append(goto)
                continue
            if at < nactvar:
                name = self.active[at].name
                self.error(g, f"<goto {g.label}> at line {g.line} jumps into the scope of "
                           f"local '{name}'")
            g.target = stat
        self.pending[info["first"]:] = still

    # --- Expressions ---

    def expr(self, e):
        if isinstance(e, Nil):
            self.error(e, "nil is not in the subset",
                       "use none for entities, and 0 or false for the other types")
        elif isinstance(e, Vararg):
            self.error(e, "varargs (...) are not in the subset")
        elif isinstance(e, FunctionExpr):
            self.error(e, "function expressions (closures) are not in the subset",
                       "declare the function at the top level: function name(...) ... end")
        elif isinstance(e, Table):
            self.error(e, "tables are not in the subset, except top-level arrays",
                       "declare an array at the top level: name = array(n) or "
                       "name = { 3, 5, 8 }")
        elif isinstance(e, Method):
            self.error(e, f"method calls ({_describe(e.obj) or 'x'}:{e.name}()) are not in "
                       "the subset", f"call a function: {e.name}(" +
                       (_describe(e.obj) or "x") + ")")
        elif isinstance(e, Name):
            e.sym = self.lookup(e)
            self.use(e)
        elif isinstance(e, Field):
            self.field(e)
        elif isinstance(e, Index):
            self.expr(e.obj)
            self.expr(e.key)
        elif isinstance(e, Call):
            self.call(e)
        elif isinstance(e, Binary):
            self.expr(e.left)
            self.expr(e.right)
        elif isinstance(e, Unary):
            self.expr(e.operand)
        elif isinstance(e, Paren):
            self.expr(e.expr)

    def use(self, name):
        sym = name.sym
        if self.body is None:
            return
        if sym.kind == "entity" and sym.op == "SELF":
            self.body.self_uses.append(name)
        if sym.kind == "builtin" and sym.name in ("object", "array"):
            self.error(name, f"{sym.name} declares at the top level only: "
                       f"Name = {sym.name}" + (" { ... }" if sym.name == "object" else "(n)"))
        if sym.kind == "builtin" and sym.name == "instances":
            self.error(name, "instances() works only in a generic for",
                       "for e in instances(Firefly) do ... end")

    def field(self, e):
        obj = e.obj
        if isinstance(obj, Name):
            obj.sym = self.lookup(obj)
            if obj.sym.kind == "builtin" and obj.sym.name == "math":
                if e.name == "tointeger":
                    self.error(e, "math.tointeger is not in the subset", "use math.floor(x)")
                if e.name not in MATH_FUNCTIONS and e.name not in MATH_CONSTANTS:
                    self.error(e, f"math.{e.name} is not in the subset: the standard library "
                               "stops at math.floor, math.abs, math.min, math.max, "
                               "math.mininteger and math.maxinteger",
                               "random_range(lo, hi) for random numbers" if e.name == "random"
                               else None)
                return
            self.use(obj)
        else:
            self.expr(obj)
        if e.name not in PROPERTIES:
            fields = self.r.p.fields
            if e.name not in fields:
                self.reserved_field(e)
                if len(fields) == VM_FIELDS:
                    self.error(e, f"a {VM_FIELDS + 1}th instance field, {e.name}: the VM has "
                               f"{VM_FIELDS} (VM_FIELDS), shared by name by every object: "
                               + ", ".join(fields), "reuse a field name, or keep the value "
                               "in a global or an array")
                fields[e.name] = FieldSym(e.name, len(fields), e)
            e.field = fields[e.name]

    def reserved_field(self, e):
        """A new instance field's name can't start with a prefix kept for later
        engine properties."""
        name = e.name
        prefix = next((p for p in RESERVED_PREFIXES if name.startswith(p)), None)
        if prefix is None:
            return
        owner = _describe(e.obj) or "e"
        hint = (f"rename the field, e.g. my_{name} or {prefix[:-1]}{name[len(prefix):]} "
                f"(instance fields can't start with {', '.join(RESERVED_PREFIXES[:-1])} or "
                f"{RESERVED_PREFIXES[-1]})")
        if name in C_POOL_PROPERTIES:
            hint = f"{name} is C's name for it: in a script it is {owner}.{C_POOL_PROPERTIES[name]}"
        else:  # a typo of a property? Compared after the prefix, which they share
            rests = {p[len(prefix):]: p for p in PROPERTIES if p.startswith(prefix)}
            close = difflib.get_close_matches(name[len(prefix):], rests, n=1)
            if close:
                hint = f"did you mean {rests[close[0]]}? If not, {hint}"
        self.error(e, f"{_describe(e) or name}: {prefix} is reserved for engine properties, and "
                   f"{name} isn't one: an instance field can't take the name of a property a "
                   "later engine version may add", hint)

    def call(self, e):
        func = e.func
        if isinstance(func, Name):
            func.sym = self.lookup(func)
            sym = func.sym
            if sym.kind == "builtin":
                if sym.name in ("object", "array"):
                    self.use(func)
                if sym.name == "instances":
                    self.use(func)
                if sym.name in WAITS and self.body is not None:
                    self.body.waits.append(e)
            elif sym.kind == "function":
                sym.calls_seen.append(e)
                if self.body is not None:
                    self.body.calls.append((sym, e))
            else:
                self.use(func)
        else:
            self.expr(func)
        for arg in e.args:
            self.expr(arg)


def resolve(chunk, file):
    return Resolver(chunk, file).run()


# --- Types -------------------------------------------------------------------

BITWISE = ("&", "|", "~", "<<", ">>")
COMPARISONS = ("==", "~=", "<", "<=", ">", ">=")
_ORDINALS = ("first", "second", "third", "fourth", "fifth", "sixth")


class _Unknown(Exception):
    """While inferring: this expression's type isn't known yet."""


def _strip(e):
    while isinstance(e, Paren):
        e = e.expr
    return e


def _object_ref(e):
    """An object's name, or an entity's object (e.object): the two forms an
    object takes as a value, which only == and ~= accept."""
    o = _strip(e)
    if isinstance(o, Name):
        return getattr(o, "sym", None) is not None and o.sym.kind == "object"
    return isinstance(o, Field) and o.name == "object" and not (
        isinstance(o.obj, Name) and o.obj.sym.kind == "builtin")


def _promote(c, node, file):
    """An integer constant as fixed point (FX(n), n * 256)."""
    if c.value is not None:
        raw = c.value * FX_ONE
        if not INT_MIN <= raw <= INT_MAX:
            raise CompileError(f"{c.value} doesn't fit fixed point ({FIXED_LIMIT})", file,
                               node.line, node.column)
        text = f"FX({c.text})" if c.text is not None else None
        return Const(FIXED, raw, text)
    return Const(FIXED, None, f"FX({c.text})")


def _may_wrap(c):
    """Whether only the assembler knows c and its text could have grown
    past 32 bits (a product or a left shift of header constants). The
    assembler's integers don't wrap, so + - * << & | ~ on such a value
    still give Lua's result modulo 2^32 (or an assembler error), but a
    shift right or a division would not: those run in code instead."""
    return c.value is None and c.text is not None and ("*" in c.text or "<<" in c.text)


def _log2(value):
    """k if value is 2**k (k >= 0), else None."""
    if value is not None and value > 0 and value & (value - 1) == 0:
        return value.bit_length() - 1
    return None


def _const_truth(e):
    """True or False if a condition is a known constant, else None."""
    c = e.const
    if c is not None and c.ty == BOOL and c.value is not None:
        return bool(c.value)
    return None


def can_fall(block):
    """Whether control can reach the end of a block (and fall out of it)."""
    reachable = True
    for stat in block.stats:
        if isinstance(stat, Label):
            reachable = True  # a goto may land here
        elif reachable and not _stat_falls(stat):
            reachable = False
    return reachable


def _stat_falls(s):
    if isinstance(s, (Return, Goto, Break)):
        return False
    if isinstance(s, Do):
        return can_fall(s.body)
    if isinstance(s, If):
        if s.orelse is None:
            return True
        return any(can_fall(b) for b in s.blocks) or can_fall(s.orelse)
    if isinstance(s, While):
        return _const_truth(s.cond) is not True or _has_break(s.body)
    if isinstance(s, Repeat):
        return ((can_fall(s.body) and _const_truth(s.cond) is not False)
                or _has_break(s.body))
    return True


def _has_break(block):
    """A break in the block that leaves the loop around it (not a nested one)."""
    for s in block.stats:
        if isinstance(s, Break):
            return True
        if isinstance(s, Do) and _has_break(s.body):
            return True
        if isinstance(s, If) and (any(_has_break(b) for b in s.blocks)
                                  or (s.orelse is not None and _has_break(s.orelse))):
            return True
    return False


class Checker:
    """Whole-program types: every expression's type and constant value,
    every variable's, parameter's, result's and field's type, and the wait
    rule. Inference runs to a fixed point with type errors put off (an
    unknown type is None), then a strict pass reports the first error."""

    def __init__(self, program):
        self.p = program
        self.file = program.file
        self.strict = True
        self.changed = False
        self.body = None

    def error(self, node, message, hint=None):
        raise CompileError(message, self.file, node.line, node.column, hint)

    def fail(self, node, message, hint=None):
        if self.strict:
            self.error(node, message, hint)
        raise _Unknown()

    def run(self):
        p = self.p
        for fn in p.functions:
            fn.result = VOID if not fn.body.value_returns else None
        self.strict = True
        for sym in p.top_order:
            getattr(self, "top_" + sym.kind, lambda s: None)(sym)
        self.strict = False
        for _ in range(10000):
            self.changed = False
            for body in p.bodies:
                self.walk(body)
            if not self.changed and not self.defaults():
                break
        self.strict = True
        for body in p.bodies:
            self.walk(body)
            for sym in body.locals:
                if sym.ty is None:  # pragma: no cover - inference always settles them
                    self.error(sym.node, f"can't tell the type of {sym.name}")
        for fn in p.functions:
            if fn.body.value_returns and can_fall(fn.body.func.body):
                block = fn.body.func.body
                raise CompileError(f"{fn.name} can reach its end without returning a value "
                                   "(it would return nil)", self.file, block.end_line,
                                   block.end_column, "end every path with a return")
        self.check_threads()
        self.check_waits()
        self.check_unused()

    # --- The top level ---

    def top_const(self, sym):
        ty = self.value(sym.init, allow_string=True)
        sym.ty = ty
        sym.origin = sym.init
        c = sym.init.const
        if c is not None:
            if ty in NUMERIC:  # a .const line: uses print its name
                sym.definition = c
                sym.const = Const(ty, c.value, sym.name)
            else:
                sym.const = c
        elif self.pure(sym.init):
            sym.inline = True  # each use computes it (the assembler can't)
        else:
            self.error(sym.init, f"{sym.name} <const> needs a constant value at the top level",
                       "constants are literals, header constants and other constants, and "
                       "arithmetic on them")

    def pure(self, e):
        """A constant expression the code computes (each time it is used)."""
        e = _strip(e)
        if isinstance(e, (Number, Bool, String)):
            return True
        if isinstance(e, Name):
            return e.sym.kind in ("const", "header") or (
                e.sym.kind == "builtin" and e.sym.name == "none")
        if isinstance(e, Unary):
            return self.pure(e.operand)
        if isinstance(e, Binary):
            return self.pure(e.left) and self.pure(e.right)
        if isinstance(e, Call) and isinstance(e.func, Field) and isinstance(e.func.obj, Name) \
                and e.func.obj.sym.kind == "builtin":
            return all(self.pure(a) for a in e.args)
        if isinstance(e, Field) and isinstance(e.obj, Name) and e.obj.sym.kind == "builtin":
            return e.name in MATH_CONSTANTS
        return False

    def top_global(self, sym):
        ty = self.value(sym.init)
        c = sym.init.const
        if c is None:
            self.error(sym.init, f"the initial value of {sym.name} must be a constant",
                       f"start it at a constant and set it in a handler")
        sym.ty = ty
        sym.origin = sym.init
        sym.init_const = c

    def top_object(self, sym):
        for part, limit in (("components", 0xFFFFFFFF), ("sprite", 0xFFFF)):
            e = getattr(sym, part)
            if e is None:
                c = Const(INT, 0)
            else:
                ty = self.value(e)
                c = e.const
                if ty != INT or c is None:
                    self.error(e, f"an object's {part} must be a constant integer",
                               "component bits C_* and sprite IDs from the C headers")
                if c.value is not None and part == "components":
                    c = Const(INT, c.value & 0xFFFFFFFF, c.text, c.prec)
                if c.value is not None and not 0 <= c.value <= limit:
                    self.error(e, f"{part} {c.value} doesn't fit (0 to {limit})")
            setattr(sym, part + "_const", c)

    def top_array(self, sym):
        if not sym.rom:
            arg = sym.value.args[0]
            ty = self.value(arg)
            c = arg.const
            if ty != INT or c is None:
                self.error(arg, "an array's length must be a constant integer")
            if c.value is not None and not 1 <= c.value <= VM_ARRAY_CELLS:
                self.error(arg, f"array({c.value}): a length is 1 to {VM_ARRAY_CELLS} "
                           "(VM_ARRAY_CELLS, for every array together)")
            sym.length = c
            total = sum(a.length.value for a in self.p.arrays
                        if not a.rom and a.length is not None and a.length.value is not None)
            if total > VM_ARRAY_CELLS:
                self.error(sym.node, f"the arrays need {total} cells; the VM has "
                           f"{VM_ARRAY_CELLS} (VM_ARRAY_CELLS)")
            sym.ty = ARRAY
            return
        types = set()
        for item in sym.value.items:
            ty = self.value(item.value)
            if ty not in NUMERIC or item.value.const is None:
                self.error(item.value, "a constant table holds constant integers (or fixed "
                           "values)", "name = { 3, 5, 8 }")
            types.add(ty)
            sym.items.append(item.value.const)
        if len(types) > 1:
            self.error(sym.value, "a constant table holds integers or fixed values, not both",
                       "write the integers as 3.0")
        sym.elem = types.pop()
        sym.elem_origin = sym.value
        sym.length = Const(INT, len(sym.items))
        sym.rom_kind = "s32"
        values = [c.value for c in sym.items]
        if None not in values:
            for kind, low, high in (("s8", -0x80, 0x7F), ("u8", 0, 0xFF),
                                    ("s16", -0x8000, 0x7FFF), ("u16", 0, 0xFFFF)):
                if all(low <= v <= high for v in values):
                    sym.rom_kind = kind
                    break
        sym.ty = ARRAY

    def top_function(self, sym):
        pass

    @staticmethod
    def show(c):
        if c.ty == FIXED and c.value is not None:
            return fixed_text(c.value)
        if c.ty == BOOL:
            return "true" if c.value else "false"
        if c.ty == ENTITY:
            return "none" if c.value == 0 else str(c.value)
        return c.asm()

    # --- Inference ---

    def settle(self, sym, ty, node):
        if ty is None or ty in (VOID, BUILTIN) or sym is None:
            return
        if sym.ty is None:
            sym.ty = ty
            sym.origin = node
            self.changed = True

    def want(self, e, ty, depth=0):
        """e must have type ty: if e names a parameter, field or array whose
        type isn't known yet, now it is. A local's type is its first value's,
        so the wish passes on to that value."""
        e = _strip(e)
        if isinstance(e, Name) and e.sym is not None and e.sym.kind == "local":
            sym = e.sym
            if sym.param:
                self.settle(sym, ty, e)
            elif sym.ty is None and sym.init is not None and depth < 50:
                self.want(sym.init, ty, depth + 1)
        elif isinstance(e, Field) and e.field is not None:
            self.settle(e.field, ty, e)
        elif isinstance(e, Index) and isinstance(_strip(e.obj), Name):
            arr = _strip(e.obj).sym
            if arr.kind == "array" and arr.elem is None and ty is not None:
                arr.elem = ty
                arr.elem_origin = e
                self.changed = True

    def defaults(self):
        """When inference is stuck: the unknown types that nothing decides
        become integers, parameters first."""
        changed = False
        for body in self.p.bodies:
            for param in body.params:
                if param.ty is None:
                    param.ty = INT
                    changed = True
        if changed:
            return True
        for field in self.p.fields.values():
            if field.ty is None:
                field.ty = INT
                changed = True
        for arr in self.p.arrays:
            if arr.elem is None:
                arr.elem = INT
                changed = True
        for fn in self.p.functions:
            if fn.result is None:
                fn.result = INT
                changed = True
        if changed:
            return True
        for body in self.p.bodies:
            for sym in body.locals:
                if sym.ty is None:
                    sym.ty = INT
                    changed = True
        return changed

    # --- Statements ---

    def walk(self, body):
        self.body = body
        self.block(body.func.body)
        self.body = None

    def block(self, block):
        for stat in block.stats:
            if self.strict:
                self.statement(stat)
            else:  # an error here just leaves types unknown until the strict pass
                try:
                    self.statement(stat)
                except (_Unknown, CompileError):
                    pass

    def statement(self, s):
        if isinstance(s, Local):
            for name, value in zip(s.names, s.values):
                sym = name.sym
                ty = self.value(value, allow_string=sym.readonly)
                self.settle(sym, ty, value)
                if self.strict and sym.ty != ty:  # pragma: no cover - a local's type is its value's
                    self.error(value, f"{sym.name} is {article(sym.ty)}, and its value is "
                               f"{article(ty)}")
                if self.strict and sym.readonly and value.const is not None:
                    sym.const = value.const
        elif isinstance(s, Assign):
            for target, value in zip(s.targets, s.values):
                self.assign(target, value)
        elif isinstance(s, CallStat):
            self.call(s.call, statement=True)
        elif isinstance(s, Do):
            self.block(s.body)
        elif isinstance(s, While):
            self.cond(s.cond)
            self.block(s.body)
        elif isinstance(s, Repeat):
            self.block(s.body)
            self.cond(s.cond)
        elif isinstance(s, If):
            for test, blk in zip(s.tests, s.blocks):
                self.cond(test)
                self.block(blk)
            if s.orelse is not None:
                self.block(s.orelse)
        elif isinstance(s, NumericFor):
            self.numeric_for(s)
            self.block(s.body)
        elif isinstance(s, GenericFor):
            self.settle(s.names[0].sym, ENTITY, s.names[0])
            self.block(s.body)
        elif isinstance(s, Return):
            self.return_stat(s)

    def assign(self, target, value):
        vt = self.value(value)
        if isinstance(target, Name):
            sym = target.sym
            self.compatible(sym.name, sym.ty, sym.origin, vt, value)
        elif isinstance(target, Field):
            self.entity(target.obj, target)
            if target.name in PROPERTIES:
                self.compatible(target.name, PROPERTIES[target.name][1], "property", vt, value)
            else:
                field = target.field
                if field.ty is None:
                    self.settle(field, vt, value)
                else:
                    self.compatible(f"the field {field.name}", field.ty, field.origin, vt, value)
        else:  # Index
            arr = self.array_of(target.obj)
            if arr.rom:
                self.fail(target, f"{arr.name} is a constant table (in ROM): its elements "
                          "can't be assigned", f"use {arr.name} = array(n) for an array the "
                          "scripts change")
            self.index(target.key, arr)
            if arr.elem is None:
                if vt is not None:
                    arr.elem = vt
                    arr.elem_origin = value
                    self.changed = True
            else:
                self.compatible(f"{arr.name}'s elements", arr.elem, arr.elem_origin, vt, value)

    def compatible(self, name, have, origin, vt, node):
        """Storing a vt value where a have value lives: the same type, or an
        integer into fixed (converted)."""
        if have is None or vt is None or have == vt or (have == FIXED and vt == INT):
            return
        if vt == STRING:
            self.fail(node, "strings exist only as text_print's argument",
                      'text_print(col, row, "text")')
        where = ""
        if origin == "property":
            where = " (an engine property)"
        elif isinstance(origin, Node):
            where = f" (from its first value, line {origin.line})"
        hint = None
        if have == INT and vt == FIXED:
            hint = "math.floor(x) makes a fixed value an integer"
            if origin not in (None, "property"):
                hint = f"to make it fixed, start it as fixed: 0.0, not 0 ({hint})"
        self.fail(node, f"{name} is {article(have)}{where}, and this is {article(vt)}", hint)

    def cond(self, e):
        ty = self.value(e)
        if ty is None:
            self.want(e, BOOL)
        elif ty != BOOL:
            self.not_boolean(e, ty, "conditions must be booleans")

    def not_boolean(self, e, ty, what):
        name = _describe(e) or "this"
        hint = None
        if ty in NUMERIC:
            hint = f"compare it: {name} ~= 0"
        elif ty == ENTITY:
            hint = f"compare it: {name} ~= none"
        self.fail(e, f"{what}: {name} is {article(ty)}" +
                  (", and Lua treats 0 as true where the VM sees false" if ty in NUMERIC else ""),
                  hint)

    def numeric_for(self, s):
        types = []
        for part, e in (("start", s.start), ("limit", s.limit), ("step", s.step)):
            if e is None:
                types.append(INT)
                continue
            ty = self.value(e)
            if ty is not None and ty not in NUMERIC:
                self.fail(e, f"the for loop's {part} is {article(ty)}; it must be a number")
            types.append(ty)
        start, _, step = types
        if start is None or step is None:
            return
        # Lua 5.4: an integer loop if the start and the step are integers
        # (whatever the limit), else every value is a float.
        s.integer = start == INT and step == INT
        if s.step is not None and s.step.const is not None and s.step.const.value == 0:
            self.fail(s.step, "'for' step is zero")
        self.settle(s.var.sym, INT if s.integer else FIXED, s.var)

    def return_stat(self, s):
        if not s.values:
            return
        fn = self.body.fn
        ty = self.value(s.values[0])
        if ty is None:
            if fn.result not in (None, VOID):
                self.want(s.values[0], fn.result)
            return
        if fn.result is None:
            fn.result = ty
            fn.result_origin = s
            self.changed = True
        elif ty != fn.result:
            hint = None
            if NUMERIC == (fn.result, ty) or NUMERIC == (ty, fn.result):
                hint = "return one type: write 0.0 for a fixed zero, math.floor(x) for an integer"
            self.fail(s.values[0], f"{fn.name} returns {article(fn.result)} at line "
                      f"{fn.result_origin.line}, and {article(ty)} here", hint)

    # --- Expressions ---

    def value(self, e, allow_string=False):
        """The type of an expression used as a value: a number, a boolean or
        an entity (or a string, for text_print and constants)."""
        ty = self.expr(e)
        if ty == STRING and not allow_string:
            self.fail(e, "strings exist only as text_print's argument (and as <const> "
                      "values)", 'text_print(col, row, "text")')
        if ty in (OBJECT, ARRAY, FUNCTION, BUILTIN, VOID):
            self.misuse(e, ty)
        return ty

    def misuse(self, e, ty):
        name = _describe(e) or "this"
        if ty == OBJECT and isinstance(_strip(e), Field):
            self.fail(e, f"{name} is an object, and objects are compared, not kept or "
                      "computed with", f"compare it with == or ~=: {name} == Coin")
        if ty == OBJECT:
            self.fail(e, f"{name} is an object, not a value",
                      f"spawn({name}, x, y) makes an instance; for e in instances({name}) "
                      f"visits them; e.object == {name} tells whether e is one")
        if ty == ARRAY:
            self.fail(e, f"{name} is an array, not a value: arrays can't be assigned or "
                      "passed (LDA and STA name their array)",
                      f"use its elements ({name}[i]) or its length (#{name})")
        if ty == FUNCTION:
            self.fail(e, f"{name} is a function, and functions aren't values in the subset",
                      f"call it: {name}(...)")
        if ty == BUILTIN:
            self.fail(e, f"{name} is an engine function: call it")
        self.fail(e, f"{name} returns no value")

    def expr(self, e):
        e.const = None
        method = getattr(self, "x_" + type(e).__name__, None)
        if method is None:  # pragma: no cover - the resolver rejects the others
            self.error(e, f"{type(e).__name__} is not in the subset")
        ty = method(e)
        e.ty = ty
        return ty

    def x_Number(self, e):
        if e.kind == "int":
            e.const = Const(INT, e.value)
            return INT
        raw = fixed_from_float(e.value)
        if raw is None:
            self.fail(e, f"{e.text} doesn't fit fixed point ({FIXED_LIMIT})",
                      "Lua's floats are 24.8 fixed point in the subset")
        if raw == 0 and e.value != 0 and self.strict:
            self.p.warn(e, f"{e.text} is 0 in 24.8 fixed point (the smallest step is 1/256)")
        e.const = Const(FIXED, raw)
        return FIXED

    def x_String(self, e):
        e.const = Const(STRING, e.value)
        return STRING

    def x_Bool(self, e):
        e.const = Const(BOOL, 1 if e.value else 0)
        return BOOL

    def x_Paren(self, e):
        ty = self.expr(e.expr)
        e.const = e.expr.const
        return ty

    def x_Name(self, e):
        sym = e.sym
        kind = sym.kind
        if kind == "local":
            e.const = sym.const
            return sym.ty
        if kind == "global":
            return sym.ty
        if kind == "const":
            e.const = sym.const
            return sym.ty
        if kind == "header":
            e.const = Const(INT, None, sym.name)
            return INT
        if kind == "entity":
            return ENTITY
        if kind == "builtin":
            if sym.name == "none":
                e.const = Const(ENTITY, 0)
                return ENTITY
            return BUILTIN
        if kind == "object":  # a value only in == and ~= (compare_objects)
            e.const = Const(OBJECT, None, f"OBJ_{sym.listing}")
            return OBJECT
        return {"array": ARRAY, "function": FUNCTION}[kind]

    def entity(self, obj, field):
        """obj in obj.field must be an entity."""
        ty = self.value(obj)
        if ty is None:
            self.want(obj, ENTITY)
        elif ty != ENTITY:
            name = _describe(obj) or "this"
            hint = None
            if ty == ARRAY:
                hint = f"elements use brackets: {name}[i]"
            self.fail(field, f"{name}.{field.name}: {name} is {article(ty)}, and only "
                      "entities have fields", hint)

    def x_Field(self, e):
        obj = e.obj
        if isinstance(obj, Name) and obj.sym.kind == "builtin" and obj.sym.name == "math":
            if e.name in MATH_CONSTANTS:
                e.const = Const(INT, MATH_CONSTANTS[e.name])
                return INT
            self.fail(e, f"math.{e.name} is a function: call it")
        self.entity(obj, e)
        if e.name in PROPERTIES:
            return PROPERTIES[e.name][1]
        return e.field.ty

    def array_of(self, obj):
        o = _strip(obj)
        if isinstance(o, Name) and o.sym.kind == "array":
            o.ty = ARRAY
            return o.sym
        ty = self.expr(obj)
        name = _describe(obj) or "this"
        hint = "declare it at the top level: name = array(n) or name = { 3, 5, 8 }"
        if ty == ENTITY:
            hint = "fields use a dot: e.name"
        self.fail(obj, f"{name}[...]: indexing works on arrays declared at the top level, and "
                  f"{name} is {article(ty) if ty else 'not one'}", hint)

    def index(self, key, arr):
        ty = self.value(key)
        if ty is None:
            self.want(key, INT)
            return
        if ty != INT:
            self.fail(key, f"array indices are integers, and this is {article(ty)}",
                      "math.floor(i)" if ty == FIXED else None)
        c = key.const
        if c is not None and c.value is not None and arr.length is not None \
                and arr.length.value is not None and not 1 <= c.value <= arr.length.value:
            self.fail(key, f"{arr.name}[{c.value}] is outside the array (1 to "
                      f"{arr.length.value}): Lua's arrays start at 1")

    def x_Index(self, e):
        arr = self.array_of(e.obj)
        self.index(e.key, arr)
        return arr.elem

    def x_Unary(self, e):
        op = e.op
        if op == "#":
            o = _strip(e.operand)
            ty = self.expr(e.operand)
            if ty == STRING and e.operand.const is not None:
                e.const = Const(INT, len(e.operand.const.value))
                return INT
            if ty == ARRAY:
                return INT
            self.fail(e, "# is the length of an array (or of a literal string)",
                      None if not isinstance(o, Name) else f"{o.name} is {article(ty)}")
        ty = self.value(e.operand)
        c = e.operand.const
        if op == "not":
            if ty is None:
                self.want(e.operand, BOOL)
            elif ty != BOOL:
                self.not_boolean(e.operand, ty, "not takes a boolean")
            if c is not None and c.value is not None:
                e.const = Const(BOOL, 1 - c.value)
            return BOOL
        if ty is None:
            if op == "~":
                self.want(e.operand, INT)
            return None
        if op == "-":
            self.numeric(e.operand, ty, "-")
            if c is not None:
                e.const = self.fold_neg(e, c)
            return ty
        # ~
        self.integer(e.operand, ty, "~")
        if c is not None:
            if c.value is not None:
                text = f"~{c.group(P_UNARY)}" if c.text is not None else None
                e.const = Const(INT, wrap32(~c.value), text, P_UNARY)
            else:
                e.const = Const(INT, None, f"~{c.group(P_UNARY)}", P_UNARY)
        return INT

    def fold_neg(self, e, c):
        text = f"-{c.group(P_UNARY)}" if c.text is not None else None
        if c.value is None:
            return Const(c.ty, None, text, P_UNARY)
        if c.ty == FIXED:
            if c.value == INT_MIN:
                self.fail(e, f"-{fixed_text(c.value)} doesn't fit fixed point ({FIXED_LIMIT})")
            return Const(FIXED, -c.value, text, P_UNARY)
        return Const(INT, wrap32(-c.value), text if c.value != INT_MIN else None, P_UNARY)

    def numeric(self, e, ty, op):
        if ty not in NUMERIC:
            name = _describe(e) or "this"
            self.fail(e, f"{op} needs numbers, and {name} is {article(ty)}",
                      "booleans compare with == and ~=, and combine with and, or, not"
                      if ty == BOOL else None)

    def integer(self, e, ty, op):
        if ty == FIXED:
            self.fail(e, f"{op} works on integers, and {_describe(e) or 'this'} is fixed",
                      "math.floor(x) makes it an integer")
        if ty != INT:
            self.numeric(e, ty, op)

    def x_Binary(self, e):
        op = e.op
        if op == "..":
            return self.concat(e)
        if op in ("and", "or"):
            return self.logical(e)
        if op in ("==", "~=") and (_object_ref(e.left) or _object_ref(e.right)):
            return self.compare_objects(e)
        lt = self.value(e.left)
        rt = self.value(e.right)
        if op in COMPARISONS:
            return self.compare(e, lt, rt)
        if op in BITWISE:
            for side, ty in ((e.left, lt), (e.right, rt)):
                if ty is None:
                    self.want(side, INT)
                else:
                    self.integer(side, ty, op)
            if lt is None or rt is None:
                return INT
            e.const = self.fold_bitwise(e, op, e.left.const, e.right.const)
            return INT
        for side, ty in ((e.left, lt), (e.right, rt)):
            if ty is not None:
                self.numeric(side, ty, op)
        if lt is None or rt is None:
            return FIXED if op == "/" else None
        if op == "^":
            return self.power(e)
        ty = FIXED if op == "/" or FIXED in (lt, rt) else INT
        a, b = e.left.const, e.right.const
        if a is not None and b is not None:
            e.const = self.fold_arithmetic(e, op, ty, a, b)
        elif op in ("//", "%") and ty == INT and b is not None and b.value == 0:
            self.fail(e.right, f"attempt to perform 'n{op}0'")
        return ty

    def logical(self, e):
        lt = self.value(e.left)
        rt = self.value(e.right)
        for side, ty in ((e.left, lt), (e.right, rt)):
            if ty is None:
                self.want(side, BOOL)
            elif ty != BOOL:
                name = _describe(side) or "this"
                self.fail(side, f"{e.op} takes booleans, and {name} is {article(ty)}",
                          "the a and b or c idiom on other types is not in the subset: "
                          "use if ... then ... else ... end")
        a, b = e.left.const, e.right.const
        if a is not None and a.value is not None:
            # Lua's short circuit: false and x is false, true or x is true
            # (x is never evaluated), true and x / false or x is x.
            if (e.op == "and") == (a.value == 0):
                e.const = a
            elif b is not None:
                e.const = b
        return BOOL

    def compare(self, e, lt, rt):
        op = e.op
        if lt is None or rt is None:
            known = lt or rt
            if known in (BOOL, ENTITY):
                self.want(e.left, known)
                self.want(e.right, known)
            return BOOL
        if op in ("==", "~="):
            same = lt == rt or (lt in NUMERIC and rt in NUMERIC)
            if not same:
                self.fail(e, f"comparing {article(lt)} with {article(rt)}: in Lua they are "
                          "never equal", "compare values of one type"
                          + ("; none is the entity that isn't one" if ENTITY in (lt, rt) else ""))
        else:
            for side, ty in ((e.left, lt), (e.right, rt)):
                self.numeric(side, ty, op)
        a, b = e.left.const, e.right.const
        if a is not None and b is not None and a.value is not None and b.value is not None:
            if FIXED in (lt, rt):
                a = a if lt == FIXED else _promote(a, e.left, self.file)
                b = b if rt == FIXED else _promote(b, e.right, self.file)
            x, y = a.value, b.value
            result = {"==": x == y, "~=": x != y, "<": x < y, "<=": x <= y, ">": x > y,
                      ">=": x >= y}[op]
            e.const = Const(BOOL, int(result))
        return BOOL

    def compare_objects(self, e):
        """== and ~= of objects: an object's name or an entity's object, the
        only places an object is a value. Both sides must be objects: in Lua
        an object (a table) is never equal to a number or an entity."""
        for side in (e.left, e.right):
            if not _object_ref(side):
                ty = self.value(side)
                self.fail(side, f"comparing an object with {article(ty) if ty else 'a value'}: "
                          "in Lua they are never equal", "compare an entity's object with an "
                          "object: other.object == Coin")
            if isinstance(_strip(side), Field):
                self.entity(_strip(side).obj, _strip(side))
            self.expr(side)
        a, b = _strip(e.left), _strip(e.right)
        if isinstance(a, Name) and isinstance(b, Name):  # two objects' names
            e.const = Const(BOOL, int((a.sym is b.sym) == (e.op == "==")))
        return BOOL

    def concat(self, e):
        parts = []
        for side in (e.left, e.right):
            ty = self.expr(side)
            c = side.const
            if ty not in (STRING, INT) or c is None or c.value is None:
                self.fail(side, "string operations at run time are not in the subset: .. "
                          "joins literal strings (and integer literals) only",
                          "print the pieces one by one: text_print(col, row, \"text\"), "
                          "text_print_number(col, row, n)")
            parts.append(c.value if ty == STRING else str(c.value).encode())
        e.const = Const(STRING, parts[0] + parts[1])
        return STRING

    def power(self, e):
        a, b = e.left.const, e.right.const
        if a is None or b is None or a.value is None or b.value is None:
            self.fail(e, "^ (exponentiation) is folded for constants only: the VM has no "
                      "power operation", "multiply, or shift (1 << n) for powers of two")
        x = a.value / FX_ONE if a.ty == FIXED else float(a.value)
        y = b.value / FX_ONE if b.ty == FIXED else float(b.value)
        try:
            result = math.pow(x, y)
        except (OverflowError, ValueError):
            result = math.nan
        raw = fixed_from_float(result)
        if raw is None:
            self.fail(e, f"{x:g} ^ {y:g} doesn't fit fixed point ({FIXED_LIMIT})")
        e.const = Const(FIXED, raw)
        return FIXED

    def fold_arithmetic(self, e, op, ty, a, b):
        """+ - * / // % on two constants, as the VM computes them (FXMUL,
        FXDIV, IDIV, IMOD), or as assembler text when only it knows a value."""
        if ty == FIXED and op != "*" and not (op == "/" and a.ty == INT and b.ty == INT):
            if a.ty == INT:
                a = _promote(a, e.left, self.file)
            if b.ty == INT:
                b = _promote(b, e.right, self.file)
        x, y = a.value, b.value
        named = a.text is not None or b.text is not None
        if x is not None and y is not None:
            if op in ("//", "%", "/") and y == 0:
                if ty == INT:
                    self.fail(e.right, f"attempt to perform 'n{op}0'")
                self.fail(e.right, "division by zero (Lua's result, inf or nan, doesn't fit "
                          "fixed point)")
            if ty == INT:
                if op == "+":
                    exact = x + y
                elif op == "-":
                    exact = x - y
                elif op == "*":
                    exact = x * y
                else:
                    exact = x // y if op == "//" else x % y
                value = wrap32(exact)
            else:
                if op in ("+", "-"):
                    exact = x + y if op == "+" else x - y
                elif op == "*":
                    exact = x * y if INT in (a.ty, b.ty) else (x * y) >> 8
                elif op == "/":
                    exact = c_div(x * FX_ONE, y)
                elif op == "//":
                    exact = (x // y) * FX_ONE
                else:
                    exact = x % y
                if not INT_MIN <= exact <= INT_MAX:
                    self.fail(e, f"the result doesn't fit fixed point ({FIXED_LIMIT})")
                value = exact
            text = None
            if named:
                symbolic = self.symbolic_arithmetic(op, ty, a, b)
                if symbolic is not None and exact == value:
                    text = symbolic
            if text is None:
                return Const(ty, value)
            return Const(ty, value, text[0], text[1])
        symbolic = self.symbolic_arithmetic(op, ty, a, b)
        if symbolic is None:
            return None
        return Const(ty, None, symbolic[0], symbolic[1])

    @staticmethod
    def symbolic_arithmetic(op, ty, a, b):
        """(text, precedence) of the assembler expression for a op b, or None
        where the assembler's integers can't say what the VM computes."""
        if op in ("+", "-"):
            return f"{a.group(P_ADD)} {op} {b.group(P_ADD + 1)}", P_ADD
        if op == "*":
            if ty == FIXED and a.ty == FIXED and b.ty == FIXED:
                return f"({a.group(P_MUL)} * {b.group(P_MUL + 1)}) >> 8", P_SHIFT
            return f"{a.group(P_MUL)} * {b.group(P_MUL + 1)}", P_MUL
        if op in ("*", "/", "//", "%") and (_may_wrap(a) or _may_wrap(b)) and not (
                op == "*" and INT in (a.ty, b.ty)):
            return None
        if op == "/":  # FXDIV: (a * 256) / b, rounding toward zero like C's /
            return f"{a.group(P_MUL)} * {FX_ONE} / {b.group(P_MUL + 1)}", P_MUL
        if op in ("//", "%") and ty == INT:
            k = _log2(b.value)
            if k is None:
                return None
            if op == "//":  # floor division by 2^k: the arithmetic shift
                return (a.asm(), a.asm_prec()) if k == 0 else (f"{a.clear(P_SHIFT)} >> {k}",
                                                               P_SHIFT)
            return f"{a.clear(P_AND)} & {b.value - 1}", P_AND
        return None

    def fold_bitwise(self, e, op, a, b):
        if a is None or b is None:
            return None
        x, y = a.value, b.value
        named = a.text is not None or b.text is not None
        value = None
        if x is not None and y is not None:
            value = {"&": x & y, "|": x | y, "~": x ^ y}.get(op)
            if op == "<<":
                value = lua_shift_left(x, y)
            elif op == ">>":
                value = lua_shift_left(x, -y)
            value = wrap32(value)
            if not named:
                return Const(INT, value)
        text = None
        if op == "&":
            text = (f"{a.clear(P_AND)} & {b.clear(P_AND)}", P_AND)
        elif op == "|":
            text = (f"{a.clear(P_OR)} | {b.clear(P_OR)}", P_OR)
        elif op == "~":  # the assembler has no xor: a ^ b = (a | b) - (a & b)
            text = (f"({a.group(P_OR)} | {b.group(P_OR + 1)}) - "
                    f"({a.group(P_AND)} & {b.group(P_AND + 1)})", P_ADD)
        elif op in ("<<", ">>") and y is not None:
            count = y if op == "<<" else -y
            if count <= -32 or count >= 32:
                return Const(INT, 0)
            if count == 0:
                text = (a.asm(), a.asm_prec())
            elif count > 0:
                text = (f"{a.clear(P_SHIFT)} << {count}", P_SHIFT)
        if text is None:
            return None if value is None else Const(INT, value)
        if value is not None:
            exact = {"&": x & y, "|": x | y, "~": x ^ y}.get(op)
            if op in ("<<", ">>"):
                count = y if op == "<<" else -y
                exact = x << count if count >= 0 else None
            if exact != value:
                return Const(INT, value)
        return Const(INT, value, text[0], text[1])

    # --- Calls ---

    def x_Call(self, e):
        return self.call(e, statement=False)

    def call(self, e, statement):
        func = e.func
        if isinstance(func, Field) and isinstance(func.obj, Name) \
                and func.obj.sym.kind == "builtin" and func.obj.sym.name == "math":
            ty = self.math_call(e, func.name)
        elif not isinstance(func, Name):
            self.fail(e, "only functions declared by name can be called")
        else:
            sym = func.sym
            kind = sym.kind
            if kind == "function":
                ty = self.user_call(e, sym)
            elif kind == "builtin":
                ty = self.engine_call(e, sym.name)
            elif kind == "header" and sym.name in MACROS:
                ty = self.macro(e, sym.name)
            elif kind == "header":
                self.fail(func, f"{func.name} would be a constant from the C headers, and "
                          "constants aren't functions", "C_GAME(n) and BODY_GRAVITY(n) are "
                          "the macros a script can call")
            elif kind == "object":
                self.fail(func, f"{func.name} is an object; objects aren't called",
                          f"spawn({func.name}, x, y) makes an instance")
            else:
                what = article(sym.ty) if sym.ty else "a variable"
                self.fail(func, f"{func.name} is {what}, not a function")
        if ty is None:
            return None
        if ty == VOID and not statement:
            self.fail(e, f"{_describe(e.func) or 'this'}() returns no value")
        return ty

    def arity(self, e, name, counts):
        if len(e.args) not in counts:
            want = " or ".join(str(c) for c in counts)
            plural = "" if counts == (1,) else "s"
            self.fail(e, f"{name} takes {want} argument{plural}, not {len(e.args)}")

    def argument(self, arg, want, fname, index):
        ty = self.value(arg)
        if ty is None:
            self.want(arg, want)
            return
        if ty == want or (want == FIXED and ty == INT):
            return
        hint = None
        if want == INT and ty == FIXED:
            hint = "math.floor(x) makes a fixed value an integer"
        self.fail(arg, f"{fname}'s {_ORDINALS[index]} argument is {article(want)}, and this is "
                  f"{article(ty)}", hint)

    def user_call(self, e, fn):
        params = fn.body.params
        if len(e.args) != len(params):
            plural = "" if len(params) == 1 else "s"
            self.fail(e, f"{fn.name} takes {len(params)} argument{plural}, not {len(e.args)}",
                      "a missing argument would be nil and an extra one dropped; neither "
                      "is in the subset")
        for arg, param in zip(e.args, params):
            ty = self.value(arg)
            if ty is None:
                if param.ty is not None:
                    self.want(arg, param.ty)
                continue
            if param.ty is None:
                self.settle(param, ty, arg)
            elif ty != param.ty:
                where = (f" (from the call at line {param.origin.line})"
                         if param.origin is not None else "")
                hint = None
                if param.ty in NUMERIC and ty in NUMERIC:
                    hint = ("a function called with both an integer and a fixed argument needs "
                            "two versions; or convert: math.floor(x), or x + 0.0")
                self.fail(arg, f"{fn.name}'s parameter {param.name} is {article(param.ty)}"
                          f"{where}, and this argument is {article(ty)}", hint)
        return fn.result

    def engine_call(self, e, name):
        if name in TEXT_CALLS:
            return self.text_call(e, name)
        if name == "spawn":
            return self.spawn_call(e)
        if name in ("object", "array"):
            self.fail(e, f"{name} declares at the top level only: "
                      f"Name = {name}" + (" { ... }" if name == "object" else "(n)"))
        if name == "instances":
            self.fail(e, "instances() works only in a generic for",
                      "for e in instances(Firefly) do ... end")
        if name not in ENGINE:
            self.fail(e, f"{name} is not a function")
        params, result, _ = ENGINE[name]
        self.arity(e, name, (len(params),))
        for index, (arg, want) in enumerate(zip(e.args, params)):
            self.argument(arg, want, name, index)
        return result or VOID

    def text_call(self, e, name):
        """text_print(col, row, "text"): a string known at compile time.
        text_print_number(col, row, n [, width]): an integer."""
        number = name == "text_print_number"
        self.arity(e, name, (3, 4) if number else (3,))
        for index in (0, 1):
            self.argument(e.args[index], INT, name, index)
        what = e.args[2]
        ty = self.value(what, allow_string=True)
        if not number:
            if ty == STRING:
                text = what.const.value
                bad = next((b for b in text if not 0x20 <= b <= 0x7E), None)
                if bad is not None:
                    self.fail(what, f"text_print draws printable ASCII (32 to 126), and this "
                              f"string has the byte {bad} (0x{bad:02X})")
            else:
                hint = ("text_print_number(col, row, n) prints a number"
                        if ty in (INT, FIXED, None) else None)
                self.fail(what, f"text_print draws a literal string, and this is "
                          f"{article(ty) if ty else 'a value'}", hint)
        elif ty == STRING:
            self.fail(what, "text_print_number prints an integer, and this is a string",
                      'text_print(col, row, "text") draws a string')
        elif ty is None:
            self.want(what, INT)
        elif ty != INT:
            hint = "math.floor(x) makes a fixed value an integer" if ty == FIXED else None
            self.fail(what, f"text_print_number prints an integer, and this is "
                      f"{article(ty)}", hint)
        if len(e.args) == 4:
            self.argument(e.args[3], INT, name, 3)
        return VOID

    def spawn_call(self, e):
        self.arity(e, "spawn", (3,))
        obj = _strip(e.args[0])
        if not (isinstance(obj, Name) and obj.sym.kind == "object"):
            self.fail(e.args[0], "spawn's first argument is an object: spawn(Firefly, x, y)")
        obj.ty = OBJECT
        for index in (1, 2):
            self.argument(e.args[index], FIXED, "spawn", index)
        return ENTITY

    def macro(self, e, name):
        """C_GAME(n) or BODY_GRAVITY(n): a constant, as C's macro makes it."""
        self.arity(e, name, (1,))
        ty = self.value(e.args[0])
        c = e.args[0].const
        (lo, hi), what = MACROS[name]
        if ty != INT or c is None or c.value is None or not lo <= c.value <= hi:
            self.fail(e.args[0], f"{name}(n) takes a constant n from {lo} to {hi}: {what}")
        n = c.value
        value = 1 << (16 + n) if name == "C_GAME" else n - 16
        e.const = Const(INT, value, f"{name}({n})")
        return INT

    def math_call(self, e, name):
        fname = f"math.{name}"
        if name in MATH_CONSTANTS:
            self.fail(e, f"{fname} is a number, not a function", f"write {fname}")
        if name in ("floor", "abs"):
            self.arity(e, fname, (1,))
            ty = self.value(e.args[0])
            if ty is None:
                return INT if name == "floor" else None
            self.numeric(e.args[0], ty, fname)
            c = e.args[0].const
            if name == "floor":
                if c is not None:
                    if ty == INT:
                        e.const = c
                    elif c.value is not None:
                        text = f"{c.clear(P_SHIFT)} >> 8" if c.text is not None else None
                        e.const = Const(INT, c.value >> 8, text, P_SHIFT)
                    elif not _may_wrap(c):
                        e.const = Const(INT, None, f"{c.clear(P_SHIFT)} >> 8", P_SHIFT)
                return INT
            if c is not None and c.value is not None:
                e.const = Const(ty, wrap32(abs(c.value)))
            return ty
        if not e.args:
            self.fail(e, f"{fname} takes at least one argument")
        types = [self.value(arg) for arg in e.args]
        known = [t for t in types if t is not None]
        if not known:
            return None
        for arg, ty in zip(e.args, types):
            if ty is None:
                self.want(arg, known[0])
            else:
                self.numeric(arg, ty, fname)
        if len(set(known)) > 1:
            self.fail(e, f"{fname} of an integer and a fixed value: Lua returns one of them "
                      "unchanged, so the result's type would depend on the values",
                      "convert one: math.floor(x), or x + 0.0")
        if None in types:
            return known[0]
        consts = [arg.const for arg in e.args]
        if all(c is not None and c.value is not None for c in consts):
            values = [c.value for c in consts]
            e.const = Const(known[0], min(values) if name == "min" else max(values))
        return known[0]

    # --- Program-wide rules ---

    def check_threads(self):
        for obj in self.p.objects:
            body = obj.handlers.get("room_start")
            if body is not None and body.self_uses and obj.components_const.is_zero:
                self.error(body.self_uses[0], f"{obj.name} has no components, so its "
                           "room_start runs as a thread (vm_start) with no entity: self is none "
                           "there", f"give {obj.name} components, or don't use self")

    def check_waits(self):
        """Waits only in behaviours and in functions that only behaviours
        call: a wait must never be reached from a reaction."""
        for body in self.p.bodies:
            if body.kind != "handler" or body.behaviour:
                continue
            if body.waits:
                wait = body.waits[0]
                self.error(wait, f"{wait.func.name}() in {body.name}, a reaction: reactions "
                           "run to completion and can't wait",
                           "waits are for create and room_start (behaviours) and the "
                           "functions only they call")
            seen = set()
            stack = [(fn, call, [(body.name, fn.name, call.line)]) for fn, call in body.calls]
            while stack:
                fn, call, path = stack.pop(0)
                if fn in seen:
                    continue
                seen.add(fn)
                if fn.body.waits:
                    wait = fn.body.waits[0]
                    chain = ", ".join(f"{a} calls {b} at line {line}" for a, b, line in path)
                    self.error(wait, f"{wait.func.name}() can be reached from {body.name}, a "
                               f"reaction ({chain}): reactions run to completion and can't "
                               "wait", "waits are for create and room_start (behaviours) and "
                               "the functions only they call")
                for callee, c in fn.body.calls:
                    stack.append((callee, c, path + [(fn.name, callee.name, c.line)]))

    def check_unused(self):
        reached = set()
        stack = [fn for body in self.p.bodies if body.kind == "handler" for fn, _ in body.calls]
        while stack:
            fn = stack.pop()
            if fn not in reached:
                reached.add(fn)
                stack.extend(callee for callee, _ in fn.body.calls)
        for fn in self.p.functions:
            fn.used = fn in reached
            if not fn.used:
                self.p.warn(fn.node, f"{fn.name} is never called from a handler, so the "
                            "listing leaves it out")


def check(text, file="script.lua"):
    """Parses and checks a script: names, types, the subset's rules. Returns
    the Program; raises CompileError."""
    program = resolve(parse(text, file), file)
    Checker(program).run()
    return program


# --- Code generation ---------------------------------------------------------

COMMENT_COLUMN = 40  # where a line's "; file:line" comment starts
SIGN_BIT = -0x80000000  # XOR with it turns an unsigned comparison into a signed one


def _escape(data):
    """A .string literal's text: printable ASCII, with \\" and \\\\."""
    return "".join("\\" + chr(b) if chr(b) in '"\\' else chr(b) for b in data)


def _string_name(data, used):
    """A readable listing name for a string: SCORE for "SCORE", TIME_UP for
    "TIME UP!", SPACES3 for three spaces."""
    text = data.decode("ascii", "replace")
    if not text.strip():
        base = f"SPACES{len(text)}" if text else "EMPTY"
    else:
        base = re.sub(r"[^A-Z0-9]+", "_", text.upper()).strip("_")[:24].strip("_") or "S"
        if base[0].isdigit():
            base = "S_" + base
    name, n = base, 2
    while name in used:
        name = f"{base}_{n}"
        n += 1
    used.add(name)
    return name


class CodeGen:
    """The listing: the tables (constants, objects, strings, globals,
    arrays), then every handler and function in source order."""

    def __init__(self, program, source):
        self.p = program
        self.base = os.path.basename(program.file)
        self.source = source.replace("\r\n", "\n").replace("\r", "\n").split("\n")
        self.strings = {}  # bytes -> (listing name, line of first use)
        self.string_names = set()
        self.labels = set()
        self.names = {}  # every .const and generated expression name -> what it is

    def error(self, node, message, hint=None):
        raise CompileError(message, self.p.file, node.line, node.column, hint)

    def source_line(self, line):
        if line is not None and 1 <= line <= len(self.source):
            return self.source[line - 1].strip()
        return ""

    def located(self, text, line, note=None):
        where = f"; {self.base}:{line}" if line else ";"
        if note:
            where += " " + note
        return text + " " * max(COMMENT_COLUMN - len(text), 1) + where

    def unique_label(self, base):
        name, n = base, 2
        while name in self.labels:
            name = f"{base}_{n}"
            n += 1
        self.labels.add(name)
        return name

    def string(self, data, line):
        if data not in self.strings:
            self.strings[data] = (_string_name(data, self.string_names), line)
        return self.strings[data][0]

    def claim(self, name, what, node):
        """A name the listing defines for expressions; it must not hide a
        header constant or another of the listing's names."""
        if name in self.names or name in self.p.headers:
            other = self.names.get(name, "a header constant the script uses")
            self.error(node, f"{name} would name both {what} and {other} in the listing",
                       "rename one")
        self.names[name] = what

    def generate(self):
        p = self.p
        for obj in p.objects:
            self.claim("OBJ_" + obj.listing, f"object {obj.name}", obj.node)
        for g in p.globals:
            self.claim("G_" + g.listing, f"global {g.name}", g.node)
        for arr in p.arrays:
            self.claim("ARR_" + arr.listing, f"array {arr.name}", arr.node)
        for c in p.consts:
            if c.ty in NUMERIC and not c.inline:
                self.claim(c.name, f"constant {c.name}", c.node)
        for field in p.fields.values():
            base = "FIELD_" + field.name.upper()
            name, n = base, 2
            while name in self.names or name in p.headers:
                name = f"{base}_{n}"
                n += 1
            field.listing = name
            self.claim(name, f"instance field {field.name}", field.node)
        for fn in p.functions:
            fn.label = self.unique_label(fn.name)
        for obj in p.objects:  # handler labels are prefixes, kept apart from functions
            for event in obj.handlers:
                self.labels.add(f"{obj.listing.lower()}_{event}")

        code = []
        for body in p.bodies:
            if body.kind == "function" and not body.fn.used:
                continue
            code += FuncGen(self, body).generate()
        return "\n".join(self.tables() + code) + "\n"

    def tables(self):
        p = self.p
        out = [f"; {os.path.splitext(self.base)[0]}.svm: {self.base} compiled by {TOOL} (the Lua "
               "subset, docs/lua.md)",
               "; for Serval Engine's VM. Generated: edit the script, not this listing.",
               "; Every line names the script's line it comes from."]
        if p.headers:
            out.append("; Constants from the game's C headers (svm.py asm --header for the "
                       "headers that define them):")
            line = ";  "
            for name in p.headers:
                if len(line) + len(name) + 1 > 78:
                    out.append(line)
                    line = ";  "
                line += " " + name
            out.append(line)
        consts = [c for c in p.consts if c.ty in NUMERIC and not c.inline]
        if consts or p.fields:
            out += ["", "; --- Constants ---", ""]
            for c in consts:
                note = f"{c.name} = {fixed_text(c.definition.value)}" if (
                    c.ty == FIXED and c.definition.value is not None) else None
                out.append(self.located(f".const {c.name} {c.definition.asm()}", c.node.line, note))
            for field in p.fields.values():
                out.append(self.located(f".const {field.listing} VM_P_FIELD0 + {field.slot}",
                                        field.node.line,
                                        f"instance field {field.name} ({field.ty})"))
        if p.objects:
            out += ["", "; --- Objects ---", ""]
            for obj in p.objects:
                mask = obj.components_const
                mask_text = mask.asm() if mask.text is not None or mask.value == 0 \
                    else f"0x{mask.value:08X}"
                out.append(self.located(f".object {obj.listing} mask={mask_text} "
                                        f"sprite={obj.sprite_const.asm()}", obj.node.line,
                                        obj.name))
        if self.strings:
            out += ["", "; --- Strings ---", ""]
            for data, (name, line) in self.strings.items():
                out.append(self.located(f'.string {name} "{_escape(data)}"', line))
        if p.globals:
            out += ["", "; --- Globals (vm_load starts them at their initial values) ---", ""]
            for g in p.globals:
                c = g.init_const
                text = g.listing
                if not c.is_zero:  # an initial value: the blob carries it
                    value = c.asm()
                    text += f"=({value})" if " " in value else f"={value}"
                out.append(self.located(f".globals {text}", g.node.line,
                                        f"{g.name}: {g.ty} = {Checker.show(c)}"))
        if p.arrays:
            out += ["", "; --- Arrays ---", ""]
            for arr in p.arrays:
                if arr.rom:
                    items = ", ".join(c.asm() for c in arr.items)
                    out.append(self.located(f".rom {arr.listing} {arr.rom_kind} {items}",
                                            arr.node.line, f"{arr.name}: {arr.elem}"))
                else:
                    out.append(self.located(f".array {arr.listing} {arr.length.asm()}",
                                            arr.node.line, f"{arr.name}: {arr.elem}"))
        return out


class FuncGen:
    """One handler's or function's code: a frame (ENTER p, n) for its
    parameters, locals and the loops' hidden state, and stack code."""

    def __init__(self, cg, body):
        self.cg = cg
        self.body = body
        self.items = []
        self.next_slot = 0
        self.max_slot = 0
        self.slot_names = {}
        self.breaks = []
        self.counter = 0
        self.user_labels = {}
        self.shown = None
        self.last_op = None
        if body.kind == "handler":
            self.prefix = f"{body.obj.listing.lower()}_{body.event}"
        else:
            self.prefix = body.fn.label

    # --- Output ---

    def show(self, line):
        """The script's line as a comment, when the code moves to a new one."""
        if line is not None and line != self.shown:
            self.shown = line
            text = self.cg.source_line(line)
            if text:
                self.items.append(f"    ; {line}: {text}")

    def op(self, text, node, note=None):
        line = node if isinstance(node, int) else node.line
        self.show(line)
        self.items.append(self.cg.located("    " + text, line, note))
        self.last_op = text.split()[0]

    def label(self, name, node):
        line = node if isinstance(node, int) else node.line
        self.show(line)
        self.items.append(self.cg.located(name + ":", line))

    def labels(self, kind, *suffixes):
        self.counter += 1
        base = f"{self.prefix}_{kind}{self.counter}"
        return [self.cg.unique_label(base + suffix) for suffix in suffixes]

    def alloc(self, name):
        slot = self.next_slot
        self.next_slot += 1
        self.max_slot = max(self.max_slot, self.next_slot)
        names = self.slot_names.setdefault(slot, [])
        if name not in names:
            names.append(name)
        return slot

    # --- The body ---

    def generate(self):
        body = self.body
        func = body.func
        for index, param in enumerate(body.params):
            param.slot = index
            self.slot_names[index] = [param.name]
        self.next_slot = self.max_slot = len(body.params)
        self.block(func.body)
        end = func.body.end_line
        last = "HALT" if body.kind == "handler" else "RET"
        if can_fall(func.body):
            self.op(last, end)
        elif self.last_op not in ("HALT", "RET", "RETV"):
            self.op(last, end, "not reached: every handler and function ends in HALT or RET")
        if self.max_slot > 60:
            self.cg.error(func, f"{body.name} needs {self.max_slot} frame cells; a context's "
                          "stack has 64 for every frame and operand", "use fewer locals")
        head = ["", f"; --- {self.cg.source_line(body.stat.line)} ---"]
        line = body.stat.line
        if body.kind == "handler":
            what = ("a behaviour: it may wait" if body.behaviour
                    else "a reaction: it runs to completion")
            head[1] = f"; --- {body.name}, {what} ---"
            head.append(self.cg.located(f".handler {body.obj.listing} {EVENTS[body.event][0]}",
                                        line))
        else:
            head.append(self.cg.located(f"{body.fn.label}:", line))
        params = len(body.params)
        extra = self.max_slot - params
        if params or extra:
            frame = ", ".join(f"{slot} {'/'.join(names)}"
                              for slot, names in sorted(self.slot_names.items()))
            head.append(f"    ; frame: {frame}")
            head.append(self.cg.located(f"    ENTER {params}, {extra}", line))
        return head + self.items

    def block(self, block, after=None):
        mark = self.next_slot
        for stat in block.stats:
            getattr(self, "s_" + type(stat).__name__)(stat)
        if after is not None:
            after()
        self.next_slot = mark

    # --- Statements ---

    def s_Local(self, s):
        syms = []
        for name, value in zip(s.names, s.values):
            sym = name.sym
            if sym.readonly and sym.const is not None:
                continue  # a constant: its uses push the value
            self.expr(value, sym.ty)
            syms.append(sym)
        for sym in syms:
            sym.slot = self.alloc(sym.name)
        for sym in reversed(syms):
            self.op(f"STL {sym.slot}", s)

    def s_Assign(self, s):
        if len(s.targets) == 1:
            self.assign(s.targets[0], s.values[0])
            return
        # Lua evaluates every value before assigning any, and a target's
        # entity or index before any assignment changes it.
        mark = self.next_slot
        parts = []
        for target in s.targets:
            part = None
            if isinstance(target, Field):
                obj = _strip(target.obj)
                if (isinstance(obj, Name) and obj.sym.kind == "entity") or obj.const is not None:
                    part = ("expr", target.obj)
                else:
                    self.expr(target.obj)
                    part = ("slot", self.alloc("(assignment)"))
                    self.op(f"STL {part[1]}", target)
            elif isinstance(target, Index):
                c = self.index_const(target.key)
                if c is not None:
                    part = ("const", c)
                else:
                    self.index_value(target.key)
                    part = ("slot", self.alloc("(assignment)"))
                    self.op(f"STL {part[1]}", target)
            parts.append(part)
        for target, value in zip(s.targets, s.values):
            self.expr(value, self.target_type(target))
        for target, part in reversed(list(zip(s.targets, parts))):
            if isinstance(target, Name):
                self.store_name(target)
                continue
            kind, what = part
            if kind == "expr":
                self.expr(what)
            elif kind == "slot":
                self.op(f"LDL {what}", target)
            else:
                self.push(what, target)
            self.op("SWAP", target)
            if isinstance(target, Field):
                self.op(f"SETP {self.property(target)}", target)
            else:
                self.op(f"STA {_strip(target.obj).sym.listing}", target)
        self.next_slot = mark

    def target_type(self, target):
        if isinstance(target, Name):
            return target.sym.ty
        if isinstance(target, Field):
            return PROPERTIES[target.name][1] if target.name in PROPERTIES else target.field.ty
        return _strip(target.obj).sym.elem

    def property(self, field):
        if field.name in PROPERTIES:
            return PROPERTIES[field.name][0]
        return field.field.listing

    def store_name(self, target):
        sym = target.sym
        if sym.kind == "local":
            self.op(f"STL {sym.slot}", target)
        else:
            self.op(f"STG {sym.listing}", target)

    def assign(self, target, value):
        if isinstance(target, Name):
            self.expr(value, target.sym.ty)
            self.store_name(target)
        elif isinstance(target, Field):
            self.expr(target.obj)
            self.expr(value, self.target_type(target))
            self.op(f"SETP {self.property(target)}", target)
        else:
            arr = _strip(target.obj).sym
            self.index_value(target.key)
            self.expr(value, arr.elem)
            self.op(f"STA {arr.listing}", target)

    def s_CallStat(self, s):
        self.call(s.call, discard=True)

    def s_Do(self, s):
        self.block(s.body)

    def s_If(self, s):
        clauses = list(zip(s.tests, s.blocks))
        (end,) = self.labels("if", "_end")
        number = self.counter
        jumped_to_end = False
        for index, (test, block) in enumerate(clauses):
            truth = _const_truth(test)
            if truth is False:
                continue  # never runs
            if truth is True:
                self.block(block)
                break
            last = index == len(clauses) - 1
            if not last:
                target = self.cg.unique_label(f"{self.prefix}_if{number}_elseif{index + 1}")
            elif s.orelse is not None:
                target = self.cg.unique_label(f"{self.prefix}_if{number}_else")
            else:
                target = end
            self.jump_if(test, False, target)
            self.block(block)
            if (not last or s.orelse is not None) and can_fall(block):
                self.op(f"JMP {end}", block.end_line)
                jumped_to_end = True
            if target != end:
                next_line = clauses[index + 1][0].line if not last else s.orelse.line
                self.label(target, next_line)
        else:
            if s.orelse is not None:
                self.block(s.orelse)
        if jumped_to_end or any(_const_truth(t) is None for t in s.tests):
            self.label(end, s.blocks[-1].end_line if s.orelse is None else s.orelse.end_line)

    def loop_body(self, body, end, after=None):
        """A loop's body; whether a break jumped to its end."""
        self.breaks.append([end, False])
        self.block(body, after)
        return self.breaks.pop()[1]

    def s_While(self, s):
        truth = _const_truth(s.cond)
        if truth is False:
            return
        top, end = self.labels("while", "", "_end")
        self.label(top, s)
        if truth is None:
            self.jump_if(s.cond, False, end)
        broke = self.loop_body(s.body, end)
        self.op(f"JMP {top}", s.body.end_line)
        if truth is None or broke:
            self.label(end, s.body.end_line)

    def s_Repeat(self, s):
        top, end = self.labels("repeat", "", "_end")
        self.label(top, s)

        def until():  # inside the body's scope: until sees its locals
            truth = _const_truth(s.cond)
            if truth is None:
                self.jump_if(s.cond, False, top)
            elif truth is False:
                self.op(f"JMP {top}", s.cond)

        if self.loop_body(s.body, end, after=until):
            self.label(end, s.cond)

    def s_Return(self, s):
        if self.body.kind == "handler":
            self.op("HALT", s)
        elif s.values:
            self.expr(s.values[0])
            self.op("RETV", s)
        else:
            self.op("RET", s)

    def s_Break(self, s):
        self.breaks[-1][1] = True
        self.op(f"JMP {self.breaks[-1][0]}", s, "break")

    def user_label(self, node):
        if node not in self.user_labels:
            self.user_labels[node] = self.cg.unique_label(f"{self.prefix}_{node.name}")
        return self.user_labels[node]

    def s_Goto(self, s):
        self.op(f"JMP {self.user_label(s.target)}", s, f"goto {s.label}")

    def s_Label(self, s):
        self.label(self.user_label(s), s)

    def s_GenericFor(self, s):
        """for e in instances(Obj): NEXTI from 0 until it gives 0."""
        call = s.exprs[0]
        obj = call.args[0].sym
        var = s.names[0].sym
        mark = self.next_slot
        top, end = self.labels("instances", "", "_end")
        cursor = self.alloc(f"(instances of {obj.name})" if var.assigned else var.name)
        self.op("PUSH 0", s, "none: from the first")
        self.op(f"STL {cursor}", s)
        self.label(top, s)
        self.op(f"LDL {cursor}", s)
        self.op(f"NEXTI {obj.listing}", s)
        self.op("DUP", s)
        self.op(f"STL {cursor}", s)
        self.op(f"JZ {end}", s, "none left")
        if var.assigned:
            var.slot = self.alloc(var.name)
            self.op(f"LDL {cursor}", s)
            self.op(f"STL {var.slot}", s)
        else:
            var.slot = cursor
        self.loop_body(s.body, end)
        self.op(f"JMP {top}", s.body.end_line)
        self.label(end, s.body.end_line)
        self.next_slot = mark

    def s_NumericFor(self, s):
        """Lua 5.4's numeric for: the start, limit and step evaluated once;
        an integer loop runs exactly as many times as Lua's precomputed count
        says, without overflowing; a fixed-point loop adds the step and
        compares, as Lua's float loop does."""
        var = s.var.sym
        mark = self.next_slot
        top, end = self.labels("for", "", "_end")
        want = None if s.integer else FIXED
        self.expr(s.start, want)
        idx = self.alloc(f"(for {var.name})" if var.assigned else var.name)
        self.op(f"STL {idx}", s)
        step_const = Const(INT, 1) if s.step is None else s.step.const
        if step_const is not None and step_const.value is None:
            step_const = None  # only the assembler knows it: a run-time step
        if step_const is not None and not s.integer and step_const.ty == INT:
            step_const = _promote(step_const, s.step, self.cg.p.file)
        sign = None if step_const is None else (1 if step_const.value > 0 else -1)
        limit = self.loop_limit(s, sign)
        step = ("const", step_const) if step_const is not None else self.loop_part(
            s.step, want, "(for step)")
        if step_const is None:
            self.step_checks(s, step, limit)
        self.skip_check(s, idx, limit, step, sign, end)
        self.label(top, s)
        if var.assigned:
            var.slot = self.alloc(var.name)
            self.op(f"LDL {idx}", s)
            self.op(f"STL {var.slot}", s)
        else:
            var.slot = idx  # the loop's own counter: the body never assigns it
        self.loop_body(s.body, end)
        if s.integer:
            self.integer_next(s, idx, limit, step, sign, top, end)
        else:
            self.fixed_next(s, idx, limit, step, sign, top, end)
        self.label(end, s.body.end_line)
        self.next_slot = mark

    def loop_part(self, e, want, name):
        """A loop value evaluated once: a constant, or a hidden local."""
        c = e.const
        if c is not None and (c.value is not None or c.text is not None):
            if want == FIXED and c.ty == INT:
                c = _promote(c, e, self.cg.p.file)
            return ("const", c)
        self.expr(e, want)
        slot = self.alloc(name)
        self.op(f"STL {slot}", e)
        return ("slot", slot)

    def loop_limit(self, s, sign):
        """The limit; in an integer loop a fixed limit becomes an integer as
        Lua's forlimit does: floored going up, rounded up going down."""
        e = s.limit
        if not s.integer or e.ty == INT:
            return self.loop_part(e, None if s.integer else FIXED, "(for limit)")
        c = e.const
        if c is not None and c.value is not None and sign is not None:
            value = c.value >> 8 if sign > 0 else -((-c.value) >> 8)
            return ("const", Const(INT, value))
        self.expr(e)
        if sign is not None:
            self.fixed_to_limit(e, sign)
        slot = self.alloc("(for limit)")
        self.op(f"STL {slot}", e)
        return ("slot", slot)

    def fixed_to_limit(self, e, sign):
        if sign < 0:
            self.op("PUSH 255", e, "rounded up")
            self.op("ADD", e)
        self.op("PUSH 8", e)
        self.op("SHR", e, "an integer limit")

    def load(self, part, node):
        kind, what = part
        if kind == "slot":
            self.op(f"LDL {what}", node)
        else:
            self.push(what, node)

    def step_checks(self, s, step, limit):
        """A run-time step: Lua stops the script if it is 0 ("'for' step is
        zero"); and a fixed limit in an integer loop is converted by its
        sign."""
        (ok,) = self.labels("for", "_step_ok")
        self.load(step, s.step)
        self.op(f"JNZ {ok}", s.step)
        name = self.cg.string(b"'for' step is zero", s.step.line)
        self.op(f"TRACE {name}", s.step, "Lua's error: the script stops")
        self.op("HALT", s.step)
        self.label(ok, s.step)
        if s.integer and s.limit.ty == FIXED:
            down, done = self.labels("for", "_ceil", "_limit")
            self.load(step, s.step)
            self.op("PUSH 0", s.step)
            self.op("LT", s.step)
            self.op(f"JNZ {down}", s.step)
            self.load(limit, s.limit)
            self.fixed_to_limit(s.limit, 1)
            self.op(f"JMP {done}", s.limit)
            self.label(down, s.limit)
            self.load(limit, s.limit)
            self.fixed_to_limit(s.limit, -1)
            self.label(done, s.limit)
            self.op(f"STL {limit[1]}", s.limit)

    def skip_check(self, s, idx, limit, step, sign, end):
        """No iteration at all when the start is already past the limit."""
        start = s.start.const
        if (sign is not None and start is not None and start.value is not None
                and limit[0] == "const" and limit[1].value is not None):
            first = start.value
            if not s.integer and start.ty == INT:
                first *= FX_ONE
            past = first > limit[1].value if sign > 0 else first < limit[1].value
            if past:
                self.op(f"JMP {end}", s, "past the limit: no iteration")
            return
        if sign is not None:
            self.op(f"LDL {idx}", s)
            self.load(limit, s)
            self.op("GT" if sign > 0 else "LT", s)
            self.op(f"JNZ {end}", s, "past the limit: no iteration")
            return
        down, go = self.labels("for", "_down", "_go")
        self.load(step, s)
        self.op("PUSH 0", s)
        self.op("LT", s)
        self.op(f"JNZ {down}", s)
        self.op(f"LDL {idx}", s)
        self.load(limit, s)
        self.op("GT", s)
        self.op(f"JNZ {end}", s, "past the limit: no iteration")
        self.op(f"JMP {go}", s)
        self.label(down, s)
        self.op(f"LDL {idx}", s)
        self.load(limit, s)
        self.op("LT", s)
        self.op(f"JNZ {end}", s, "past the limit: no iteration")
        self.label(go, s)

    def integer_next(self, s, idx, limit, step, sign, top, end):
        """Stop when the next value would pass the limit: when the distance
        left, as an unsigned number (it can be 2^32 - 1), is less than the
        step's size. Never overflows, so the iterations are Lua's count."""
        if sign is not None and abs(step[1].value) == 1:
            self.op(f"LDL {idx}", s)
            self.load(limit, s)
            self.op("EQ", s)
            self.op(f"JNZ {end}", s, "the last iteration")
        elif sign is not None:
            if sign > 0:
                self.load(limit, s)
                self.op(f"LDL {idx}", s)
            else:
                self.op(f"LDL {idx}", s)
                self.load(limit, s)
            self.op("SUB", s, "the distance left")
            self.op(f"PUSH {SIGN_BIT}", s)
            self.op("XOR", s)
            size = wrap32(abs(step[1].value) ^ 0x80000000)
            self.op(f"PUSH {size}", s, f"the step's size {abs(step[1].value)}, biased")
            self.op("LT", s, "unsigned: less than one more step")
            self.op(f"JNZ {end}", s)
        else:
            down, compare = self.labels("for", "_left_down", "_compare")
            self.load(step, s)
            self.op("PUSH 0", s)
            self.op("LT", s)
            self.op(f"JNZ {down}", s)
            self.load(limit, s)
            self.op(f"LDL {idx}", s)
            self.op("SUB", s, "the distance left")
            self.load(step, s)
            self.op(f"JMP {compare}", s)
            self.label(down, s)
            self.op(f"LDL {idx}", s)
            self.load(limit, s)
            self.op("SUB", s, "the distance left")
            self.load(step, s)
            self.op("NEG", s, "the step's size")
            self.label(compare, s)
            self.op(f"PUSH {SIGN_BIT}", s)
            self.op("XOR", s)
            self.op("SWAP", s)
            self.op(f"PUSH {SIGN_BIT}", s)
            self.op("XOR", s)
            self.op("GT", s, "unsigned: less than one more step")
            self.op(f"JNZ {end}", s)
        self.op(f"LDL {idx}", s)
        self.load(step, s)
        self.op("ADD", s)
        self.op(f"STL {idx}", s)
        self.op(f"JMP {top}", s)

    def fixed_next(self, s, idx, limit, step, sign, top, end):
        """Lua's float loop: add the step, go on while within the limit."""
        self.op(f"LDL {idx}", s)
        self.load(step, s)
        self.op("ADD", s)
        self.op("DUP", s)
        self.op(f"STL {idx}", s)
        if sign is not None:
            self.load(limit, s)
            self.op("LE" if sign > 0 else "GE", s)
            self.op(f"JNZ {top}", s)
            return
        (down,) = self.labels("for", "_next_down")
        self.load(step, s)
        self.op("PUSH 0", s)
        self.op("LT", s)
        self.op(f"JNZ {down}", s)
        self.load(limit, s)
        self.op("LE", s)
        self.op(f"JNZ {top}", s)
        self.op(f"JMP {end}", s)
        self.label(down, s)
        self.load(limit, s)
        self.op("GE", s)
        self.op(f"JNZ {top}", s)

    # --- Conditions ---

    def jump_if(self, e, value, label):
        """Jump to label when e is value (true or false), else fall through:
        and, or and not become jumps, as Lua's short circuit."""
        e = _strip(e)
        truth = _const_truth(e)
        if truth is not None:
            if truth == value:
                self.op(f"JMP {label}", e)
            return
        if isinstance(e, Unary) and e.op == "not":
            self.jump_if(e.operand, not value, label)
            return
        if isinstance(e, Binary) and e.op in ("and", "or"):
            if (e.op == "and") != value:  # either side alone decides
                self.jump_if(e.left, value, label)
                self.jump_if(e.right, value, label)
            else:
                (skip,) = self.labels(e.op, "")
                self.jump_if(e.left, not value, skip)
                self.jump_if(e.right, value, label)
                self.label(skip, e)
            return
        if isinstance(e, Binary) and e.op in ("==", "~="):
            other = self.zero_comparison(e)
            if other is not None:
                self.expr(other)
                zero = e.op == "=="  # jump on zero when (x == 0) is value
                self.op(f"{'JZ' if zero == value else 'JNZ'} {label}", e)
                return
        self.expr(e)
        self.op(f"{'JNZ' if value else 'JZ'} {label}", e)

    @staticmethod
    def zero_comparison(e):
        """x in x == 0, 0 ~= x and the like (0, 0.0, false, none): no
        comparison needed, JZ or JNZ tests it."""
        for side, other in ((e.right, e.left), (e.left, e.right)):
            c = side.const
            if c is not None and c.value == 0 and other.const is None:
                return other
        return None

    # --- Expressions ---

    def push(self, c, node):
        note = None
        if c.ty == FIXED and c.value is not None:
            note = f"({fixed_text(c.value)})"
        elif c.ty == BOOL:
            note = "true" if c.value else "false"
        elif c.ty == ENTITY and c.value == 0:
            note = "none"
        self.op(f"PUSH {c.asm()}", node, note)

    def expr(self, e, want=None):
        """Code that pushes e's value; want=FIXED converts an integer."""
        c = e.const
        if c is not None and c.ty != STRING and (c.value is not None or c.text is not None):
            if want == FIXED and c.ty == INT:
                c = _promote(c, e, self.cg.p.file)
            self.push(c, e)
            return
        getattr(self, "g_" + type(e).__name__)(e)
        if want == FIXED and e.ty == INT:
            self.op(f"PUSH {FX_ONE}", e)
            self.op("MUL", e, "to fixed point")

    def g_Paren(self, e):
        self.expr(e.expr)

    def g_Name(self, e):
        sym = e.sym
        if sym.kind == "local":
            self.op(f"LDL {sym.slot}", e, sym.name)
        elif sym.kind == "global":
            self.op(f"LDG {sym.listing}", e)
        elif sym.kind == "entity":
            self.op(sym.op, e)
        elif sym.kind == "const":  # computed where it is used
            self.expr(sym.init)
        else:  # pragma: no cover - the checker allows nothing else here
            self.cg.error(e, f"{sym.name} has no value")

    def g_Field(self, e):
        self.expr(e.obj)
        self.op(f"GETP {self.property(e)}", e)

    def index_const(self, key):
        """The 0-based index as a constant, when the 1-based one is."""
        c = key.const
        if c is None or (c.value is None and c.text is None):
            return None
        if c.value is not None:
            return Const(INT, c.value - 1)
        return Const(INT, None, f"{c.group(P_ADD)} - 1", P_ADD)

    def index_value(self, key):
        """The 0-based index: Lua's index minus 1 (folded into a constant, or
        into the + k or - k the index ends with: a[i + 1] is LDA i)."""
        c = self.index_const(key)
        if c is not None:
            self.push(c, key)
            return
        k = _strip(key)
        if isinstance(k, Binary) and k.op in ("+", "-") and k.right.const is not None \
                and k.right.const.value is not None:
            offset = wrap32((k.right.const.value if k.op == "+" else -k.right.const.value) - 1)
            self.expr(k.left)
            if offset:
                self.op(f"PUSH {abs(offset)}" if offset != INT_MIN else f"PUSH {offset}", key)
                self.op("ADD" if offset > 0 or offset == INT_MIN else "SUB", key, "0-based")
            return
        self.expr(key)
        self.op("PUSH 1", key)
        self.op("SUB", key, "0-based")

    def g_Index(self, e):
        self.index_value(e.key)
        self.op(f"LDA {_strip(e.obj).sym.listing}", e)

    def g_Unary(self, e):
        if e.op == "#":
            self.op(f"LEN {_strip(e.operand).sym.listing}", e)
            return
        self.expr(e.operand)
        self.op({"-": "NEG", "not": "LNOT", "~": "BNOT"}[e.op], e)

    def g_Binary(self, e):
        op = e.op
        if op in ("and", "or"):
            if _const_truth(e.left) is not None:  # true and x, false or x: just x
                self.expr(e.right)
                return
            (end,) = self.labels(op, "")
            self.expr(e.left)
            self.op("DUP", e)
            self.op(f"{'JZ' if op == 'and' else 'JNZ'} {end}", e, "short circuit")
            self.op("DROP", e)
            self.expr(e.right)
            self.label(end, e)
            return
        lt, rt = e.left.ty, e.right.ty
        if op in COMPARISONS:
            want = FIXED if FIXED in (lt, rt) else None
            self.expr(e.left, want)
            self.expr(e.right, want)
            self.op({"==": "EQ", "~=": "NE", "<": "LT", "<=": "LE", ">": "GT", ">=": "GE"}[op], e)
            return
        if op in BITWISE:
            self.expr(e.left)
            if op == ">>":  # a >> b is LSH a, -b
                c = e.right.const
                if c is not None and c.value is not None:
                    self.push(Const(INT, wrap32(-c.value)), e.right)
                else:
                    self.expr(e.right)
                    self.op("NEG", e)
                self.op("LSH", e)
                return
            self.expr(e.right)
            self.op({"&": "AND", "|": "OR", "~": "XOR", "<<": "LSH"}[op], e)
            return
        ty = e.ty
        if op == "*" and ty == FIXED and INT in (lt, rt):
            # integer times fixed: MUL is exact (FXMUL of the converted
            # integer gives the same value)
            self.expr(e.left)
            self.expr(e.right)
            self.op("MUL", e)
        elif op == "/" and lt == INT and rt == INT:
            # FXDIV of two integers is their quotient in fixed point
            self.expr(e.left)
            self.expr(e.right)
            self.op("FXDIV", e)
        else:
            want = FIXED if ty == FIXED else None
            self.expr(e.left, want)
            self.expr(e.right, want)
            if ty == FIXED:
                mnemonic = {"+": "ADD", "-": "SUB", "*": "FXMUL", "/": "FXDIV", "//": "IDIV",
                            "%": "IMOD"}[op]
            else:
                mnemonic = {"+": "ADD", "-": "SUB", "*": "MUL", "//": "IDIV", "%": "IMOD"}[op]
            self.op(mnemonic, e)
            if op == "//" and ty == FIXED:  # Lua's float // is a whole float
                self.op(f"PUSH {FX_ONE}", e)
                self.op("MUL", e, "to fixed point")

    def g_Call(self, e):
        self.call(e, discard=False)

    def call(self, e, discard):
        func = e.func
        if isinstance(func, Field):
            self.math_call(e, func.name)
            if discard:
                self.op("DROP", e, "the result isn't used")
            return
        sym = func.sym
        if sym.kind == "function":
            for arg, param in zip(e.args, sym.body.params):
                self.expr(arg, param.ty)
            self.op(f"CALL {sym.label}", e)
            if discard and sym.result != VOID:
                self.op("DROP", e, "the result isn't used")
            return
        if sym.kind == "header":  # C_GAME(n) or BODY_GRAVITY(n): a constant
            if not discard:
                self.push(e.const, e)
            return
        name = sym.name
        if name in TEXT_CALLS:
            self.expr(e.args[0])
            self.expr(e.args[1])
            if name == "text_print":
                label = self.cg.string(e.args[2].const.value, e.line)
                self.op(f"PUSH STR_{label}", e.args[2])
                self.op("SYS TEXT_PRINT", e)
            else:
                self.expr(e.args[2])
                if len(e.args) == 4:
                    self.expr(e.args[3])
                else:
                    self.op("PUSH 0", e, "no width: just the digits")
                self.op("SYS TEXT_PRINT_NUMBER", e)
            return
        if name == "spawn":
            obj = _strip(e.args[0]).sym
            self.expr(e.args[1], FIXED)
            self.expr(e.args[2], FIXED)
            self.op(f"SPAWN {obj.listing}", e)
            if discard:
                self.op("DROP", e, "the entity isn't used")
            return
        params, result, operation = ENGINE[name]
        for arg, want in zip(e.args, params):
            self.expr(arg, want)
        self.op(operation, e)
        if discard and result is not None:
            self.op("DROP", e, "the result isn't used")

    def math_call(self, e, name):
        ty = e.ty
        if name == "floor":
            self.expr(e.args[0])
            if e.args[0].ty == FIXED:
                self.op("PUSH 8", e)
                self.op("SHR", e, "math.floor: the whole part")
            return
        if name == "abs":
            (done,) = self.labels("abs", "")
            self.expr(e.args[0])
            self.op("DUP", e)
            self.op("PUSH 0", e)
            self.op("LT", e)
            self.op(f"JZ {done}", e)
            self.op("NEG", e)
            self.label(done, e)
            return
        # math.min, math.max: Lua keeps the first value unless a later one is
        # smaller (min) or larger (max).
        mark = self.next_slot
        best = self.alloc(f"(math.{name})")
        self.expr(e.args[0], ty)
        self.op(f"STL {best}", e)
        for arg in e.args[1:]:
            (keep,) = self.labels(name, "")
            other = self.alloc(f"(math.{name})")
            self.expr(arg, ty)
            self.op(f"STL {other}", arg)
            first, second = (best, other) if name == "max" else (other, best)
            self.op(f"LDL {first}", arg)
            self.op(f"LDL {second}", arg)
            self.op("LT", arg)
            self.op(f"JZ {keep}", arg)
            self.op(f"LDL {other}", arg)
            self.op(f"STL {best}", arg)
            self.label(keep, arg)
            self.next_slot = other
        self.op(f"LDL {best}", e)
        self.next_slot = mark


# --- The tool ----------------------------------------------------------------


class Compiled:
    """What compile_program() returns: the listing and the warnings, each
    (file, line, column, message)."""

    def __init__(self, listing, warnings, program):
        self.listing = listing
        self.warnings = warnings
        self.program = program


def compile_program(text, filename="script.lua"):
    """Compiles a script. Returns a Compiled; raises CompileError."""
    program = check(text, filename)
    listing = CodeGen(program, text).generate()
    warnings = [(filename, line, column, message) for line, column, message in program.warnings]
    return Compiled(listing, warnings, program)


def compile_source(text, filename="script.lua"):
    """The listing (.svm text) for a script; raises CompileError with the
    file, line and column of the first problem."""
    return compile_program(text, filename).listing


def check_source(text, filename="script.lua"):
    """Checks a script without generating code. Returns the warnings;
    raises CompileError."""
    program = check(text, filename)
    return [(filename, line, column, message) for line, column, message in program.warnings]


def _print_warnings(warnings):
    for file, line, column, message in warnings:
        print(f"{file}:{line}:{column}: warning: {message}", file=sys.stderr)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                     epilog="Run with a command and --help for its options.")
    commands = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")
    comp = commands.add_parser("compile", help="compile a script to a listing")
    comp.add_argument("script", help="the script (.lua)")
    comp.add_argument("-o", "--output", metavar="OUT.svm",
                      help="write the listing (default: standard output)")
    comp.add_argument("--check", action="store_true",
                      help="check the script (names, types, waits) without writing a listing")
    args = parser.parse_args(argv)
    try:
        with open(args.script, encoding="utf-8") as f:
            text = f.read()
        if args.check:
            _print_warnings(check_source(text, args.script))
            return 0
        compiled = compile_program(text, args.script)
        _print_warnings(compiled.warnings)
        if args.output:
            with open(args.output, "w", encoding="utf-8") as f:
                f.write(compiled.listing)
        else:
            sys.stdout.write(compiled.listing)
        return 0
    except CompileError as e:
        print(e, file=sys.stderr)
    except (OSError, UnicodeDecodeError) as e:
        print(f"error: {e}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
