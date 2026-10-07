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
                expr = Field(name.line, name.column, expr, name.name)
            elif kind == "[":
                self.advance()
                key = self.expr()
                self.expect("]")
                expr = Index(token.line, token.column, expr, key)
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
