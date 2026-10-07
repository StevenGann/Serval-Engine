#!/usr/bin/env python3
"""Assemble and disassemble script blobs for Serval Engine's VM.

The reference implementation of the blob format in docs/vm.md (format v1), the
spec made executable: `asm` turns a text listing into a blob, `dis` turns a
blob back into a listing that reassembles to the same bytes. Every number
(opcodes, events, properties, engine calls, limits) is read from
include/serval/vm.h when the tool starts; the one table kept here is each
opcode's operand layout, which is checked against vm.h's opcode list at start.

This is an assembler, not a language: one mnemonic per opcode, labels,
constants and directives for the blob's tables. There are no expressions
beyond integer constant arithmetic, no if or while, no variables and no event
blocks. Those are the job of the compilers that emit this format: the Lua
subset's (tools/svlua.py, docs/lua.md), and Studio Advance's event editor
through it.

Usage:
  svm.py asm LISTING [--header FILE]... [-o OUT.bin] [--c OUT.c --symbol NAME]
                     [--defs OUT.h] [--prefix P]
  svm.py dis BLOB.bin [-o OUT.svm]

Listing syntax, one statement per line; `;` starts a comment; case matters:

  .const NAME expr                     a constant for expressions
  .object NAME mask=expr sprite=expr   an object, numbered from 0 in order of
                                       appearance (mask and sprite default to 0)
  .string NAME "text"                  a string, numbered from 0: printable
                                       ASCII, with \\" and \\\\ for those two
  .globals NAME[=expr] ...             globals, numbered from 0 (may repeat);
                                       NAME=expr starts one at expr instead of
                                       0 (spaces only inside parentheses)
  .array NAME length [at=expr]         a RAM array, numbered from 0 with the
                                       .rom arrays; its cells follow the previous
                                       .array's in the pool unless at= says where
  .rom NAME kind expr, expr, ...       a ROM array of s8, u8, s16, u16 or s32
                                       values (range-checked), numbered likewise
  .handler OBJECT EVENT                the code that follows is OBJECT's handler
                                       for EVENT: CREATE STEP DESTROY COLLISION
                                       ANIM_END ROOM_START
  label:                               a label at the next byte (a jump or CALL
                                       target); it may precede an op on its line
  MNEMONIC [operand]                   one opcode: a VM_OP_* name without the
                                       prefix; PUSH expr picks PUSH8, PUSH16 or
                                       PUSH32, the smallest that holds the value
  .byte expr, expr, ...                raw bytes, where they appear
  .strings [NAME ...]                  the named strings' bytes (none named: all
                                       not yet placed) here instead of after the
                                       code
  .data [NAME ...]                     the same for ROM arrays' data

Operands: GETP and SETP take a property (X, BODY_W, FIELD0, ...), SYS an
engine call (TEXT_PRINT, ...), SPAWN and NEXTI an object, TRACE a string, LDG
and STG a global, LDA, STA and LEN an array, LDL and STL a number, ENTER two
numbers (ENTER 0, 3), JMP, JZ, JNZ and CALL a label, PUSH8/16/32 an
expression; a number or an expression works wherever a name does. In
expressions, objects, strings, globals and arrays are named OBJ_NAME,
STR_NAME, G_NAME and ARR_NAME, as the header --defs writes them. Expressions:
integers (decimal or 0x hex, a trailing u ignored), names (the listing's, then
--header constants in the order given, then vm.h's VM_* names), the operators
+ - * / << >> | & ~ and parentheses with C precedence, and the engine's macros
FX(n) = n * 256 and C_GAME(n) = 1 << (16 + n). Names must be defined before
they are used. The result must fit in
32 bits (signed or unsigned). Layout: the header, the object, string and
array tables, the globals' initial values (only if one isn't 0: header flag
bit 0), the code in listing order, then the ROM arrays' data, then the
string bytes.

--header FILE scrapes integer constants from a C header, nothing more:
`#define NAME expr` with expr in the grammar above, and the enumerators of
enum blocks, valued as C values them. Everything else (function-like macros,
casts, #if, which is ignored so both branches are read) is skipped. A name
defined twice with different values is an error.

Errors name the listing's file and line; exit status 1 and nothing written.
"""

import argparse
import os
import re
import sys

TOOL = "tools/svm.py"
VM_H = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "include", "serval", "vm.h")

MAGIC = b"SVMB"
MAX_TABLE_ENTRIES = 0xFFFF  # objects and strings: 16-bit counts

# The one hand-written table: each opcode's operand layout from docs/vm.md's
# opcode reference, a sequence of fields (kind, and what a name in that field
# refers to). check_operands() makes sure it matches vm.h's VM_OP_* list, so
# this is the only thing that can drift and it can't drift silently.
NONE = ()
OPERANDS = {
    "NOP": NONE,
    "HALT": NONE,
    "PUSH8": (("s8", None),),
    "PUSH16": (("s16", None),),
    "PUSH32": (("s32", None),),
    "DUP": NONE,
    "DROP": NONE,
    "SWAP": NONE,
    "LDG": (("u8", "global"),),
    "STG": (("u8", "global"),),
    "LDL": (("u8", None),),
    "STL": (("u8", None),),
    "LDA": (("u16", "array"),),
    "STA": (("u16", "array"),),
    "LEN": (("u16", "array"),),
    "ADD": NONE,
    "SUB": NONE,
    "MUL": NONE,
    "DIV": NONE,
    "MOD": NONE,
    "NEG": NONE,
    "FXMUL": NONE,
    "FXDIV": NONE,
    "AND": NONE,
    "OR": NONE,
    "XOR": NONE,
    "BNOT": NONE,
    "SHL": NONE,
    "SHR": NONE,
    "LNOT": NONE,
    "LSH": NONE,
    "EQ": NONE,
    "NE": NONE,
    "LT": NONE,
    "LE": NONE,
    "GT": NONE,
    "GE": NONE,
    "IDIV": NONE,
    "IMOD": NONE,
    "JMP": (("rel16", "label"),),
    "JZ": (("rel16", "label"),),
    "JNZ": (("rel16", "label"),),
    "CALL": (("u32", "label"),),
    "RET": NONE,
    "RETV": NONE,
    "ENTER": (("u8", None), ("u8", None)),  # p arguments, n locals
    "WAIT": NONE,
    "WAIT_ANIM": NONE,
    "WAIT_MOVE": NONE,
    "SELF": NONE,
    "OTHER": NONE,
    "GETP": (("u8", "prop"),),
    "SETP": (("u8", "prop"),),
    "SPAWN": (("u16", "object"),),
    "KILL": NONE,
    "NEXTI": (("u16", "object"),),
    "SYS": (("u8", "sys"),),
    "BRK": NONE,
    "TRACE": (("u16", "string"),),
}
OPERAND_SIZE = {"s8": 1, "u8": 1, "s16": 2, "u16": 2, "rel16": 2, "s32": 4, "u32": 4}
SIGNED = ("s8", "s16", "s32", "rel16")
# Array kinds by their listing name; vm.h names their numbers VM_ARRAY_<KIND>.
ROM_KINDS = ("s8", "u8", "s16", "u16", "s32")
OPERAND_RANGE = {
    "s8": (-0x80, 0x7F),
    "s16": (-0x8000, 0x7FFF),
    "s32": (-0x80000000, 0xFFFFFFFF),  # a u32 bit pattern is accepted for a cell
    "u8": (0, 0xFF),
    "u16": (0, 0xFFFF),
    "u32": (0, 0xFFFFFFFF),
    "rel16": (-0x8000, 0x7FFF),
}
# A handler that ends in one of these can't fall off the end of the blob.
HANDLER_ENDS = ("HALT", "RET", "RETV", "JMP")


def operand_size(mnemonic):
    """The bytes after the opcode."""
    return sum(OPERAND_SIZE[kind] for kind, _ in OPERANDS[mnemonic])


class SvmError(Exception):
    """One or more problems, each "file:line: error: message" (file and line
    optional)."""

    def __init__(self, message, file=None, line=None):
        super().__init__(message)
        self.errors = [(file, line, message)]

    @classmethod
    def many(cls, errors):
        e = cls.__new__(cls)
        Exception.__init__(e, errors[0][2])
        e.errors = list(errors)
        return e

    def __str__(self):
        return "\n".join(_located(f, l, "error", m) for f, l, m in self.errors)


def _located(file, line, kind, message):
    where = ""
    if file is not None:
        where = f"{file}:"
        if line is not None:
            where += f"{line}:"
        where += " "
    return f"{where}{kind}: {message}"


# --- Expressions -------------------------------------------------------------


class ExprError(Exception):
    pass


_TOKEN = re.compile(
    r"\s*(?:(?P<num>0[xX][0-9A-Fa-f]+|[0-9]+)[uU]?|(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"|(?P<op><<|>>|[-+*/|&~(),])|(?P<bad>\S))")

INT32_MIN, UINT32_MAX = -0x80000000, 0xFFFFFFFF


def _tokenize(text):
    tokens = []
    pos = 0
    while pos < len(text):
        m = _TOKEN.match(text, pos)
        if m is None:  # only trailing whitespace is left
            break
        pos = m.end()
        if m.group("num") is not None:
            digits = m.group("num")
            value = int(digits, 16) if digits[:2].lower() == "0x" else int(digits, 10)
            tokens.append(("num", value))
        elif m.group("name") is not None:
            tokens.append(("name", m.group("name")))
        elif m.group("op") is not None:
            tokens.append(("op", m.group("op")))
        elif m.group("bad") is not None:
            raise ExprError(f"unexpected character {m.group('bad')!r}")
    return tokens


def _c_div(a, b):
    """C's integer division: rounds toward zero."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def _function(name, arg):
    if name == "FX":
        return arg * 256
    if name == "C_GAME":
        if not 0 <= arg <= 14:
            raise ExprError(f"C_GAME({arg}): n must be 0 to 14")
        return 1 << (16 + arg)
    raise ExprError(f"unknown function {name}()")


class _Parser:
    """Recursive descent over the token list, C precedence: unary - ~ ; * / ;
    + - ; << >> ; & ; |."""

    def __init__(self, tokens, resolve):
        self.tokens = tokens
        self.pos = 0
        self.resolve = resolve

    def peek(self):
        return self.tokens[self.pos] if self.pos < len(self.tokens) else ("end", None)

    def take(self):
        token = self.peek()
        self.pos += 1
        return token

    def accept(self, *ops):
        kind, value = self.peek()
        if kind == "op" and value in ops:
            self.pos += 1
            return value
        return None

    def expect(self, op):
        if self.accept(op) is None:
            raise ExprError(f"expected {op!r}, found {self.describe(self.peek())}")

    @staticmethod
    def describe(token):
        kind, value = token
        if kind == "end":
            return "the end of the expression"
        if kind == "num":
            return f"the number {value}"
        return repr(value)

    def expr(self):
        value = self.band()
        while self.accept("|"):
            value |= self.band()
        return value

    def band(self):
        value = self.shift()
        while self.accept("&"):
            value &= self.shift()
        return value

    def shift(self):
        value = self.add()
        while True:
            op = self.accept("<<", ">>")
            if op is None:
                return value
            count = self.add()
            if not 0 <= count <= 63:
                raise ExprError(f"shift count {count} is not 0 to 63")
            value = value << count if op == "<<" else value >> count

    def add(self):
        value = self.mul()
        while True:
            op = self.accept("+", "-")
            if op is None:
                return value
            right = self.mul()
            value = value + right if op == "+" else value - right

    def mul(self):
        value = self.unary()
        while True:
            op = self.accept("*", "/")
            if op is None:
                return value
            right = self.unary()
            if op == "*":
                value *= right
            elif right == 0:
                raise ExprError("division by zero")
            else:
                value = _c_div(value, right)

    def unary(self):
        op = self.accept("-", "~", "+")
        if op == "-":
            return -self.unary()
        if op == "~":
            return ~self.unary()
        if op == "+":
            return self.unary()
        return self.primary()

    def primary(self):
        kind, value = self.take()
        if kind == "num":
            return value
        if kind == "name":
            if self.accept("("):
                arg = self.expr()
                self.expect(")")
                return _function(value, arg)
            return self.resolve(value)
        if kind == "op" and value == "(":
            inner = self.expr()
            self.expect(")")
            return inner
        raise ExprError(f"unexpected {self.describe((kind, value))}")


def evaluate(text, resolve):
    """The value of an expression; resolve(name) gives a name's value or raises
    ExprError. Raises ExprError on any problem, including a result outside the
    32-bit range (signed or unsigned)."""
    values = evaluate_list(text, resolve)
    if len(values) != 1:
        raise ExprError("one expression expected, not a list")
    return values[0]


def evaluate_list(text, resolve):
    """The values of a comma-separated list of expressions (at least one)."""
    tokens = _tokenize(text)
    if not tokens:
        raise ExprError("an expression is missing")
    parser = _Parser(tokens, resolve)
    values = [parser.expr()]
    while parser.accept(","):
        values.append(parser.expr())
    if parser.peek()[0] != "end":
        raise ExprError(f"unexpected {parser.describe(parser.peek())}")
    for value in values:
        if not INT32_MIN <= value <= UINT32_MAX:
            raise ExprError(f"{value} is outside the 32-bit range")
    return values


# --- C headers ---------------------------------------------------------------


class _Definition:
    """A #define or an enumerator. expr is the value's text, or None for an
    enumerator without one (the previous enumerator's value plus one, or 0)."""

    __slots__ = ("name", "expr", "prev", "file")

    def __init__(self, name, expr, prev, file):
        self.name = name
        self.expr = expr
        self.prev = prev
        self.file = file


_COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)(?:[ \t]+(.*?))?[ \t]*$", re.M)
_ENUM = re.compile(r"\benum\b(?:\s+[A-Za-z_]\w*)?\s*\{([^{}]*)\}")
_ENUMERATOR = re.compile(r"^([A-Za-z_]\w*)\s*(?:=\s*(.+?))?\s*$", re.S)


class HeaderNames:
    """Integer constants scraped from C headers, resolved lazily so a header
    may use names from a header given after it; `fallback` (vm.h's names) is
    consulted for names no header defines."""

    def __init__(self, fallback=None):
        self.fallback = fallback or {}
        self.definitions = {}  # name -> [_Definition]
        self.order = []  # names in order of first definition
        self._memo = {}  # id(definition) -> value or None (not an integer constant)
        self._resolving = set()

    def load(self, path):
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        self.add_text(text, path)

    def add_text(self, text, file):
        text = _COMMENT.sub(" ", text).replace("\\\n", " ")
        for m in _DEFINE.finditer(text):
            name, expr = m.group(1), m.group(2)
            if expr:
                self._add(_Definition(name, expr, None, file))
        for m in _ENUM.finditer(text):
            prev = None
            for item in m.group(1).split(","):
                e = _ENUMERATOR.match(item.strip())
                if e is None:
                    continue
                definition = _Definition(e.group(1), e.group(2), prev, file)
                self._add(definition)
                prev = definition

    def _add(self, definition):
        if definition.name not in self.definitions:
            self.definitions[definition.name] = []
            self.order.append(definition.name)
        self.definitions[definition.name].append(definition)

    def _value(self, definition):
        key = id(definition)
        if key in self._memo:
            return self._memo[key]
        if key in self._resolving:  # a cycle: not a constant
            return None
        self._resolving.add(key)
        try:
            if definition.expr is None:
                if definition.prev is None:
                    value = 0
                else:
                    before = self._value(definition.prev)
                    value = None if before is None else before + 1
            else:
                try:
                    value = evaluate(definition.expr, self._resolve)
                except ExprError:
                    value = None
        finally:
            self._resolving.discard(key)
        self._memo[key] = value
        return value

    def _resolve(self, name):
        value = self.lookup(name)
        if value is None:
            raise ExprError(f"unknown name {name}")
        return value

    def lookup(self, name):
        """The name's value, or None if no header defines it as an integer
        constant. Raises SvmError if headers define it with different values."""
        values = {}
        for definition in self.definitions.get(name, ()):
            value = self._value(definition)
            if value is not None:
                values.setdefault(value, definition.file)
        if len(values) > 1:
            listed = ", ".join(f"{value} in {file}" for value, file in sorted(values.items()))
            raise SvmError(f"{name} is defined twice with different values: {listed}")
        if values:
            return next(iter(values))
        return self.fallback.get(name)

    def check(self):
        """Resolves every name now, so a conflicting definition is reported
        whether or not a listing uses it."""
        for name in self.order:
            self.lookup(name)

    def constants(self):
        """{name: value} for every name that is an integer constant."""
        result = {}
        for name in self.order:
            value = self.lookup(name)
            if value is not None:
                result[name] = value
        return result


# --- vm.h --------------------------------------------------------------------


class Vm:
    """The numbers vm.h defines, by family."""

    def __init__(self, names):
        self.names = dict(names)  # every VM_* constant, by its full name

        def family(prefix):
            return {n[len(prefix):]: v for n, v in names.items()
                    if n.startswith(prefix) and n != prefix + "COUNT"}

        self.ops = family("VM_OP_")
        self.events = family("VM_EV_")
        self.props = family("VM_P_")
        self.sys = family("VM_SYS_")
        try:
            self.format_version = names["VM_FORMAT_VERSION"]
            self.cell_bytes = names["VM_CELL_BYTES"]
            self.header_size = names["VM_HEADER_SIZE"]
            self.object_size = names["VM_OBJECT_SIZE"]
            self.array_record_size = names["VM_ARRAY_RECORD_SIZE"]
            self.flag_global_values = names["VM_FLAG_GLOBAL_VALUES"]
            self.max_globals = names["VM_GLOBALS"]
            self.array_cells = names["VM_ARRAY_CELLS"]
            self.field0 = names["VM_P_FIELD0"]
            self.fields = names["VM_FIELDS"]
            self.event_count = names["VM_EV_COUNT"]
            self.prop_count = names["VM_P_COUNT"]
            self.sys_count = names["VM_SYS_COUNT"]
            self.array_ram = names["VM_ARRAY_RAM"]
            # kind number -> (listing name, bytes per element)
            self.rom_kinds = {names["VM_ARRAY_" + kind.upper()]: (kind, int(kind[1:]) // 8)
                              for kind in ROM_KINDS}
            self.kind_count = names["VM_ARRAY_KIND_COUNT"]
        except KeyError as e:
            raise SvmError(f"vm.h does not define {e.args[0]}") from None
        if not self.ops or self.event_count <= 0:
            raise SvmError("vm.h defines no opcodes or events")
        self.kind_numbers = {name: number for number, (name, _) in self.rom_kinds.items()}
        self.op_names = {v: n for n, v in self.ops.items()}
        self.event_names = {v: n for n, v in self.events.items()}
        self.prop_names = {v: n for n, v in self.props.items()}
        for k in range(1, self.fields):  # the instance fields after the first
            self.prop_names[self.field0 + k] = f"VM_P_FIELD0 + {k}"
        self.sys_names = {v: n for n, v in self.sys.items()}


def check_operands(vm):
    """Every VM_OP_* in vm.h has a row in OPERANDS and every row names an
    opcode; otherwise the table has drifted from the spec."""
    missing = sorted(set(vm.ops) - set(OPERANDS))
    stale = sorted(set(OPERANDS) - set(vm.ops))
    problems = []
    if missing:
        problems.append("no operand row for " + ", ".join(missing))
    if stale:
        problems.append("rows for opcodes vm.h doesn't have: " + ", ".join(stale))
    if problems:
        raise SvmError(f"{TOOL}: the operand table is out of date with vm.h: " + "; ".join(problems))


def load_vm(path=VM_H):
    """Reads vm.h and checks the operand table against it."""
    if not os.path.exists(path):
        raise SvmError(f"{path}: not found ({TOOL} expects include/serval/vm.h next to tools/)")
    headers = HeaderNames()
    headers.load(path)
    headers.check()
    vm = Vm(headers.constants())
    check_operands(vm)
    return vm


# --- Assembler ---------------------------------------------------------------

_IDENT = r"[A-Za-z_]\w*"
_LABEL_LINE = re.compile(rf"^({_IDENT}):\s*(.*)$")
_STATEMENT = re.compile(rf"^(\.?{_IDENT})\s*(.*)$")
_NAME_OR_INDEX = re.compile(rf"^({_IDENT}|\d+)$")
_STRING_DIRECTIVE = re.compile(rf'^({_IDENT}|\d+)\s+"((?:[^"\\]|\\.)*)"\s*$')


def _split_top_level(text):
    """Whitespace-separated items, keeping spaces inside parentheses: the
    items of .globals, where an initial value with spaces is parenthesized."""
    items, depth, current = [], 0, ""
    for c in text:
        if c.isspace() and depth == 0:
            if current:
                items.append(current)
            current = ""
            continue
        depth += {"(": 1, ")": -1}.get(c, 0)
        current += c
    if current:
        items.append(current)
    return items


def svalue(value):
    """A 32-bit bit pattern as a signed number."""
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def _strip_comment(line):
    """The line without its `;` comment; a `;` inside a "string" is kept."""
    quoted = False
    i = 0
    while i < len(line):
        c = line[i]
        if quoted:
            if c == "\\":
                i += 1
            elif c == '"':
                quoted = False
        elif c == '"':
            quoted = True
        elif c == ";":
            return line[:i]
        i += 1
    return line


def _split_operands(text):
    """An operand list's fields, split at the commas outside parentheses."""
    fields, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == "," and depth == 0:
            fields.append(text[start:i].strip())
            start = i + 1
    fields.append(text[start:].strip())
    return fields


def _unescape_string(text):
    """The bytes of a .string literal: printable ASCII, with \\" and \\\\."""
    out = bytearray()
    i = 0
    while i < len(text):
        c = text[i]
        if c == "\\":
            if i + 1 < len(text) and text[i + 1] in '"\\':
                c = text[i + 1]
                i += 1
            else:
                raise SvmError(f"unknown escape {text[i:i + 2]!r} in a string "
                               '(only \\" and \\\\ exist; strings are printable ASCII)')
        code = ord(c)
        if not 0x20 <= code <= 0x7E:
            raise SvmError(f"character {c!r} (0x{code:02X}) is not printable ASCII")
        out.append(code)
        i += 1
    return bytes(out)


def escape_string(data):
    """A .string literal's text for these bytes."""
    return "".join("\\" + chr(b) if chr(b) in '"\\' else chr(b) for b in data)


class _Handler:
    """A .handler line (or several in a row sharing their code), for the check
    that the code ends in HALT or RET."""

    def __init__(self, line, label):
        self.line = line
        self.label = label  # "OBJECT EVENT" for messages
        self.last_op = None  # the last instruction's mnemonic (.byte and .strings don't count)
        self.emitted = False  # any bytes at all


class Assembled:
    """What assemble() returns."""

    def __init__(self, blob, objects, strings, globals_, arrays, code_size, warnings,
                 global_values=None):
        self.blob = blob
        self.objects = objects  # names (None for a numbered one), in order
        self.strings = strings
        self.globals = globals_
        self.global_values = global_values or [0] * len(globals_)  # their initial values
        self.arrays = arrays
        self.code_size = code_size  # bytes between the tables and the end
        self.warnings = warnings  # [(file, line, message)]


class _Array:
    """An .array (RAM: data is None) or a .rom (data: its bytes)."""

    __slots__ = ("name", "kind", "length", "first", "data")

    def __init__(self, name, kind, length, first=0, data=None):
        self.name = name
        self.kind = kind
        self.length = length
        self.first = first  # RAM: the first cell in the pool
        self.data = data


class Assembler:
    def __init__(self, vm, headers=None, file="listing"):
        self.vm = vm
        self.headers = headers or HeaderNames(vm.names)
        self.file = file
        self.consts = {}  # the listing's expression names: .const, OBJ_, STR_, G_
        self.objects = []  # [name or None, mask, sprite, [handler code offset or None] * events]
        self.object_index = {}
        self.strings = []  # [name or None, bytes]
        self.string_index = {}
        self.globals = []
        self.global_values = []  # each global's initial value
        self.global_index = {}
        self.arrays = []  # _Array
        self.array_index = {}
        self.next_cell = 0  # where the next .array without at= starts
        self.code = bytearray()
        self.labels = {}  # name -> code offset
        self.label_lines = {}
        self.fixups = []  # (code offset of the operand, kind, label, line)
        self.placed = {}  # string index -> code offset of its bytes
        self.placed_data = {}  # ROM array index -> code offset of its bytes
        self.handlers = []  # _Handler groups in order
        self.handler_lines = {}  # (object, event) -> line
        self.errors = []
        self.warnings = []
        self.line = 0

    # Errors are collected and reported together; a line with an error emits
    # nothing more.
    def error(self, message, line=None):
        self.errors.append((self.file, self.line if line is None else line, message))
        raise _LineError()

    def warn(self, message, line=None):
        self.warnings.append((self.file, self.line if line is None else line, message))

    def resolve(self, name):
        if name in self.consts:
            return self.consts[name][0]
        value = self.headers.lookup(name)
        if value is None:
            raise ExprError(f"unknown name {name}")
        return value

    def value(self, text):
        try:
            return evaluate(text, self.resolve)
        except ExprError as e:
            self.error(str(e))

    def define(self, name, value):
        if name in self.consts:
            self.error(f"{name} is already defined (line {self.consts[name][1]})")
        self.consts[name] = (value, self.line)

    # --- Statements ---

    def assemble(self, text):
        for number, raw in enumerate(text.splitlines(), 1):
            self.line = number
            line = _strip_comment(raw).strip()
            if not line:
                continue
            try:
                self.statement(line)
            except _LineError:
                pass
        self.line = None
        try:
            return self.finish()
        except _LineError:
            pass
        raise SvmError.many(self.errors)

    def statement(self, line):
        m = _LABEL_LINE.match(line)
        if m:
            self.bind_label(m.group(1))
            line = m.group(2).strip()
            if not line:
                return
        m = _STATEMENT.match(line)
        if not m:
            self.error(f"can't parse {line!r}")
        head, rest = m.group(1), m.group(2).strip()
        if head.startswith("."):
            handler = getattr(self, "directive_" + head[1:], None)
            if handler is None:
                self.error(f"unknown directive {head}")
            handler(rest)
        else:
            self.instruction(head, rest)

    def name_or_index(self, text, what, index):
        """A table entry's NAME, or its index as a number (dis output), which
        must match. Returns the name, or None for a number."""
        if not _NAME_OR_INDEX.match(text):
            self.error(f"{what}: {text!r} is not a name")
        if text[0].isdigit():
            if int(text) != index:
                self.error(f"{what}: this is {what} {index}, not {text}")
            return None
        return text

    def directive_const(self, rest):
        parts = rest.split(None, 1)
        if len(parts) != 2 or not re.fullmatch(_IDENT, parts[0]):
            self.error(".const takes a NAME and an expression")
        self.define(parts[0], self.value(parts[1]))

    def directive_object(self, rest):
        parts = rest.split(None, 1)
        if not parts:
            self.error(".object takes a NAME, then mask=expr sprite=expr")
        index = len(self.objects)
        if index >= MAX_TABLE_ENTRIES:
            self.error(f"more than {MAX_TABLE_ENTRIES} objects")
        name = self.name_or_index(parts[0], "object", index)
        fields = {"mask": 0, "sprite": 0}
        if len(parts) > 1:
            for field in re.split(rf"\s+(?={_IDENT}=)", parts[1].strip()):
                m = re.match(rf"^({_IDENT})=(.*)$", field)
                if not m or m.group(1) not in fields:
                    self.error(f".object: expected mask=expr or sprite=expr, not {field!r}")
                fields[m.group(1)] = self.value(m.group(2))
        if not 0 <= fields["mask"] <= 0xFFFFFFFF:
            self.error(f"mask {fields['mask']} doesn't fit 32 bits")
        if not 0 <= fields["sprite"] <= 0xFFFF:
            self.error(f"sprite {fields['sprite']} doesn't fit 16 bits")
        if name is not None:
            if name in self.object_index:
                self.error(f"object {name} is already defined")
            self.define("OBJ_" + name, index)
            self.object_index[name] = index
        self.objects.append([name, fields["mask"], fields["sprite"], [None] * self.vm.event_count])

    def directive_string(self, rest):
        m = _STRING_DIRECTIVE.match(rest)
        if not m:
            self.error('.string takes a NAME and a "quoted string"')
        index = len(self.strings)
        if index >= MAX_TABLE_ENTRIES:
            self.error(f"more than {MAX_TABLE_ENTRIES} strings")
        name = self.name_or_index(m.group(1), "string", index)
        try:
            data = _unescape_string(m.group(2))
        except SvmError as e:
            self.error(str(e.errors[0][2]))
        if name is not None:
            if name in self.string_index:
                self.error(f"string {name} is already defined")
            self.define("STR_" + name, index)
            self.string_index[name] = index
        self.strings.append([name, data])

    def directive_globals(self, rest):
        if not rest:
            self.error(".globals takes one or more NAMEs (NAME=expr: an initial value)")
        for text in _split_top_level(rest):
            index = len(self.globals)
            if index >= self.vm.max_globals:
                self.error(f"more than {self.vm.max_globals} globals (VM_GLOBALS)")
            text, eq, expr = text.partition("=")
            name = self.name_or_index(text, "global", index)
            value = 0
            if eq:
                if not expr:
                    self.error(f"global {text}= has no initial value")
                value = self.value(expr)  # 32 bits, signed or unsigned
            if name is not None:
                if name in self.global_index:
                    self.error(f"global {name} is already defined")
                self.define("G_" + name, index)
                self.global_index[name] = index
            self.globals.append(name)
            self.global_values.append(value)

    def new_array(self, text):
        """The array number for a new .array or .rom, and its name (None for a
        number)."""
        index = len(self.arrays)
        if index >= MAX_TABLE_ENTRIES:
            self.error(f"more than {MAX_TABLE_ENTRIES} arrays")
        name = self.name_or_index(text, "array", index)
        if name is not None:
            if name in self.array_index:
                self.error(f"array {name} is already defined")
            self.define("ARR_" + name, index)
            self.array_index[name] = index
        return index, name

    def directive_array(self, rest):
        m = re.match(rf"^(\S+)\s+(.+?)(?:\s+at=(.+))?$", rest)
        if not m:
            self.error(".array takes a NAME and a length, then at=expr if it doesn't start where "
                       "the previous one ended")
        length = self.value(m.group(2))
        if not 0 <= length <= 0xFFFF:
            self.error(f"array length {length} doesn't fit 16 bits")
        first = self.next_cell if m.group(3) is None else self.value(m.group(3))
        cells = self.vm.array_cells
        if not 0 <= first <= cells or length > cells - first:
            self.error(f"cells {first} to {first + length - 1} are outside the RAM arrays' pool "
                       f"(0 to {cells - 1}, VM_ARRAY_CELLS)")
        _, name = self.new_array(m.group(1))
        self.arrays.append(_Array(name, self.vm.array_ram, length, first))
        self.next_cell = first + length

    def directive_rom(self, rest):
        parts = rest.split(None, 2)
        if len(parts) < 2:
            self.error(".rom takes a NAME, a kind (" + " ".join(ROM_KINDS) + ") and its values")
        if parts[1] not in self.vm.kind_numbers:
            self.error(f"no array kind {parts[1]} (" + " ".join(ROM_KINDS) + ")")
        kind = self.vm.kind_numbers[parts[1]]
        size = self.vm.rom_kinds[kind][1]
        values = []
        if len(parts) > 2:
            try:
                values = evaluate_list(parts[2], self.resolve)
            except ExprError as e:
                self.error(str(e))
        low, high = OPERAND_RANGE[parts[1]]
        for value in values:
            if not low <= value <= high:
                self.error(f".rom: {value} doesn't fit {parts[1]} ({low} to {high})")
        if len(values) > 0xFFFF:
            self.error(f"{len(values)} values; an array holds at most 65535")
        data = b"".join((v & ((1 << (8 * size)) - 1)).to_bytes(size, "little") for v in values)
        _, name = self.new_array(parts[0])
        self.arrays.append(_Array(name, kind, len(values), data=data))

    def directive_data(self, rest):
        if rest:
            indices = [self.operand_value(text, "u16", "array") for text in rest.split()]
        else:
            indices = [i for i, a in enumerate(self.arrays)
                       if a.data is not None and i not in self.placed_data]
        for index in indices:
            if self.arrays[index].data is None:
                self.error(f"array {self.array_label(index)} is a RAM array: it has no data")
            if index in self.placed_data:
                self.error(f"array {self.array_label(index)}'s data is already placed")
            self.place_data(index)
        self.emitted(".data")

    def place_data(self, index):
        self.placed_data[index] = len(self.code)
        self.code.extend(self.arrays[index].data)

    def directive_handler(self, rest):
        parts = rest.split()
        if len(parts) != 2:
            self.error(".handler takes an OBJECT and an EVENT")
        obj = self.operand_value(parts[0], "u16", "object")
        events = " ".join(name for _, name in sorted(self.vm.event_names.items()))
        if parts[1] in self.vm.events:
            event = self.vm.events[parts[1]]
        else:
            try:
                event = evaluate(parts[1], self.resolve)
            except ExprError:
                self.error(f"no event {parts[1]} ({events})")
            if not 0 <= event < self.vm.event_count:
                self.error(f"no event {parts[1]} ({events})")
        if self.objects[obj][3][event] is not None:
            self.error(f"object {self.object_label(obj)} already has a {self.event_label(event)} "
                       f"handler (line {self.handler_lines[obj, event]})")
        self.objects[obj][3][event] = len(self.code)
        self.handler_lines[obj, event] = self.line
        label = f"{self.object_label(obj)} {self.event_label(event)}"
        if self.handlers and not self.handlers[-1].emitted:
            self.handlers[-1].label += ", " + label  # shares the code that follows
        else:
            self.handlers.append(_Handler(self.line, label))

    def directive_byte(self, rest):
        try:
            values = evaluate_list(rest, self.resolve)
        except ExprError as e:
            self.error(str(e))
        for value in values:
            if not 0 <= value <= 0xFF:
                self.error(f".byte: {value} doesn't fit a byte")
        self.code.extend(values)
        self.emitted(".byte")

    def directive_strings(self, rest):
        if rest:
            indices = [self.operand_value(text, "u16", "string") for text in rest.split()]
        else:
            indices = [i for i in range(len(self.strings)) if i not in self.placed]
        for index in indices:
            if index in self.placed:
                self.error(f"string {self.string_label(index)} is already placed")
            self.place_string(index)
        self.emitted(".strings")

    def place_string(self, index):
        self.placed[index] = len(self.code)
        self.code.extend(self.strings[index][1])
        self.code.append(0)

    def bind_label(self, name):
        if name in self.labels:
            self.error(f"label {name} is already bound (line {self.label_lines[name]})")
        self.labels[name] = len(self.code)
        self.label_lines[name] = self.line

    def emitted(self, what):
        if self.handlers:
            self.handlers[-1].emitted = True
            if not what.startswith("."):
                self.handlers[-1].last_op = what

    # --- Instructions ---

    def instruction(self, mnemonic, operand):
        if mnemonic == "PUSH":
            if not operand:
                self.error("PUSH needs a value")
            value = self.value(operand)
            if OPERAND_RANGE["s8"][0] <= value <= OPERAND_RANGE["s8"][1]:
                mnemonic = "PUSH8"
            elif OPERAND_RANGE["s16"][0] <= value <= OPERAND_RANGE["s16"][1]:
                mnemonic = "PUSH16"
            else:
                mnemonic = "PUSH32"
            operand = str(value)
        if mnemonic not in self.vm.ops:
            self.error(f"unknown mnemonic {mnemonic}")
        fields = OPERANDS[mnemonic]
        texts = _split_operands(operand) if operand else []
        if not fields and texts:
            self.error(f"{mnemonic} takes no operand")
        if fields and fields[0][1] == "label":
            if len(texts) != 1 or not re.fullmatch(_IDENT, texts[0]):
                self.error(f"{mnemonic} takes a label")
            self.fixups.append((len(self.code) + 1, fields[0][0], texts[0], self.line))
            self.emit(self.vm.ops[mnemonic], [(fields[0][0], 0)])
        elif fields:
            if not texts:
                self.error(f"{mnemonic} needs an operand" if len(fields) == 1
                           else f"{mnemonic} needs {len(fields)} operands")
            if len(texts) != len(fields) or not all(texts):
                self.error(f"{mnemonic} takes {len(fields)} operand"
                           f"{'s, separated by commas' if len(fields) > 1 else ''}")
            values = [(kind, self.operand_value(text, kind, names))
                      for (kind, names), text in zip(fields, texts)]
            self.emit(self.vm.ops[mnemonic], values)
        else:
            self.emit(self.vm.ops[mnemonic], [])
        self.emitted(mnemonic)

    def operand_value(self, text, kind, names):
        """A typed operand: a bare name from its table (object, string, global,
        array, property or engine call), else an expression. Range-checked."""
        tables = {"object": self.object_index, "string": self.string_index,
                  "global": self.global_index, "array": self.array_index,
                  "prop": self.vm.props, "sys": self.vm.sys}
        if names in tables and text in tables[names]:
            value = tables[names][text]
        else:
            try:
                value = evaluate(text, self.resolve)
            except ExprError as e:
                if names in tables and re.fullmatch(_IDENT, text):
                    what = {"prop": "property", "sys": "engine call"}.get(names, names)
                    self.error(f"no {what} {text} (and no constant of that name)")
                self.error(str(e))
        low, high = OPERAND_RANGE[kind]
        if not low <= value <= high:
            self.error(f"{value} doesn't fit a {kind} operand ({low} to {high})")
        if names == "object" and value >= len(self.objects):
            self.error(f"no object {text} ({len(self.objects)} declared so far; "
                       "objects must be declared before use)")
        elif names == "string" and value >= len(self.strings):
            self.error(f"no string {text} ({len(self.strings)} declared so far; "
                       "strings must be declared before use)")
        elif names == "array" and value >= len(self.arrays):
            self.error(f"no array {text} ({len(self.arrays)} declared so far; "
                       "arrays must be declared before use)")
        elif names == "global" and value >= len(self.globals):
            self.warn(f"global {value} is past the {len(self.globals)} declared "
                      "(the header's count only matters to vm_reload)")
        elif names == "prop" and not (value < self.vm.prop_count or
                                      0 <= value - self.vm.field0 < self.vm.fields):
            self.warn(f"property {value} is not in vm.h's page (0 to {self.vm.prop_count - 1}, "
                      f"fields {self.vm.field0} to {self.vm.field0 + self.vm.fields - 1})")
        elif names == "sys" and value >= self.vm.sys_count:
            self.warn(f"engine call {value} is not in vm.h's page (0 to {self.vm.sys_count - 1})")
        return value

    def emit(self, opcode, fields):
        """The opcode, then each (kind, value) operand field."""
        self.code.append(opcode)
        for kind, value in fields:
            size = OPERAND_SIZE[kind]
            self.code.extend((value & ((1 << (8 * size)) - 1)).to_bytes(size, "little"))

    # --- The blob ---

    def object_label(self, index):
        name = self.objects[index][0]
        return name if name is not None else str(index)

    def string_label(self, index):
        name = self.strings[index][0]
        return name if name is not None else str(index)

    def array_label(self, index):
        name = self.arrays[index].name
        return name if name is not None else str(index)

    def event_label(self, event):
        return self.vm.event_names.get(event, str(event))

    def finish(self):
        for handler in self.handlers:
            if not handler.emitted:
                self.error(f"handler {handler.label} has no code", handler.line)
            elif handler.last_op is None:
                self.warn(f"handler {handler.label} has no ops, only bytes", handler.line)
            elif handler.last_op not in HANDLER_ENDS:
                self.warn(f"handler {handler.label} doesn't end in HALT, RET or RETV "
                          f"(its last op is {handler.last_op})", handler.line)
        # The ROM arrays' data not placed by .data goes after the code, in
        # order, then the strings not placed by .strings, in order.
        for index, array in enumerate(self.arrays):
            if array.data is not None and index not in self.placed_data:
                self.place_data(index)
        for index in range(len(self.strings)):
            if index not in self.placed:
                self.place_string(index)
        # The globals' initial values follow the array table, only if one isn't
        # 0: a blob without them is laid out as before they existed.
        values = [v & 0xFFFFFFFF for v in self.global_values]
        flags = self.vm.flag_global_values if any(values) else 0
        tables_end = (self.vm.header_size + self.vm.object_size * len(self.objects)
                      + 4 * len(self.strings) + self.vm.array_record_size * len(self.arrays)
                      + (self.vm.cell_bytes * len(values) if flags else 0))
        # Jumps take a rel16 from just after the operand; CALL a blob offset.
        for at, kind, label, line in self.fixups:
            if label not in self.labels:
                self.error(f"label {label} is never bound", line)
            target = self.labels[label]
            if kind == "rel16":
                offset = target - (at + 2)
                low, high = OPERAND_RANGE["rel16"]
                if not low <= offset <= high:
                    self.error(f"{label} is {offset} bytes away, too far for a rel16 jump "
                               f"({low} to {high})", line)
                self.code[at:at + 2] = (offset & 0xFFFF).to_bytes(2, "little")
            else:
                self.code[at:at + 4] = (tables_end + target).to_bytes(4, "little")
        if self.errors:
            raise SvmError.many(self.errors)

        blob = bytearray(MAGIC)
        blob += bytes((self.vm.format_version, self.vm.cell_bytes))
        blob += flags.to_bytes(2, "little")
        blob += len(self.objects).to_bytes(2, "little")
        blob += len(self.strings).to_bytes(2, "little")
        blob += len(self.globals).to_bytes(2, "little")
        blob += len(self.arrays).to_bytes(2, "little")
        for _, mask, sprite, handlers in self.objects:
            blob += mask.to_bytes(4, "little")
            blob += sprite.to_bytes(2, "little")
            blob += (0).to_bytes(2, "little")
            for offset in handlers:
                blob += (0 if offset is None else tables_end + offset).to_bytes(4, "little")
        for index in range(len(self.strings)):
            blob += (tables_end + self.placed[index]).to_bytes(4, "little")
        for index, array in enumerate(self.arrays):
            where = array.first if array.data is None else tables_end + self.placed_data[index]
            blob += array.length.to_bytes(2, "little")
            blob += bytes((array.kind, 0))
            blob += where.to_bytes(4, "little")
        if flags:
            for value in values:
                blob += value.to_bytes(self.vm.cell_bytes, "little")
        assert len(blob) == tables_end
        blob += self.code
        return Assembled(bytes(blob), [o[0] for o in self.objects], [s[0] for s in self.strings],
                         list(self.globals), [a.name for a in self.arrays], len(self.code),
                         self.warnings, [svalue(v) for v in values])


class _LineError(Exception):
    """Stops the current statement after Assembler.error recorded the problem."""


def assemble(text, vm, headers=None, file="listing"):
    """Assembles a listing's text. Returns an Assembled; raises SvmError
    listing every problem with its line."""
    return Assembler(vm, headers, file).assemble(text)


# --- Generated files ---------------------------------------------------------


def c_source(blob, symbol, listing_name):
    lines = [f"// {symbol}: the script blob assembled by {TOOL} from {listing_name}.",
             "// Generated; edit the listing instead.", "",
             f"extern const unsigned char {symbol}[];",
             f"extern const unsigned int {symbol}_size;", "",
             f"const unsigned char {symbol}[] = {{"]
    for offset in range(0, len(blob), 16):
        chunk = ", ".join(f"0x{b:02X}" for b in blob[offset:offset + 16])
        lines.append(f"    /* 0x{offset:04X} */ {chunk},")
    lines += ["};", f"const unsigned int {symbol}_size = {len(blob)};", ""]
    return "\n".join(lines)


def defs_header(assembled, out_name, prefix, symbol, listing_name):
    guard = re.sub(r"\W", "_", os.path.basename(out_name)).upper()
    if not re.match(r"[A-Za-z_]", guard):
        guard = "_" + guard
    lines = [f"// {os.path.basename(out_name)}: the objects, strings, globals and arrays of the "
             f"script blob assembled by {TOOL} from {listing_name}.",
             "// Generated; edit the listing instead.", "",
             f"#ifndef {guard}", f"#define {guard}", ""]
    for title, family, names in (("Objects", "OBJ", assembled.objects),
                                 ("Strings", "STR", assembled.strings),
                                 ("Globals", "G", assembled.globals),
                                 ("Arrays", "ARR", assembled.arrays)):
        lines.append(f"// {title}")
        for index, name in enumerate(names):
            if name is not None:
                lines.append(f"#define {prefix}{family}_{name} {index}")
        lines.append(f"#define {prefix}{family}_COUNT {len(names)}")
        lines.append("")
    if symbol:
        lines += [f"extern const unsigned char {symbol}[];",
                  f"extern const unsigned int {symbol}_size;", ""]
    lines += [f"#endif // {guard}", ""]
    return "\n".join(lines)


# --- Disassembler ------------------------------------------------------------


class Blob:
    """A validated blob's tables."""

    def __init__(self, data, vm):
        self.data = data
        self.vm = vm
        self.objects = []  # (mask, sprite, [handler offsets])
        self.strings = []  # (offset, bytes without the NUL)
        self.arrays = []  # (kind, length, first cell or blob offset)
        self.globals = 0
        self.global_values = None  # initial values (header flag bit 0), else None
        self.tables_end = 0


def validate(data, vm):
    """Checks a blob as vm_load does (magic, version, cell width, flags and
    reserved fields, counts, the tables inside the blob, offsets in range,
    strings NUL-terminated, array records), plus what the assembler can't
    reproduce (strings or ROM arrays that overlap). Returns a Blob; raises
    SvmError."""
    if len(data) < vm.header_size:
        raise SvmError(f"{len(data)} bytes is shorter than the {vm.header_size}-byte header")
    if data[:4] != MAGIC:
        raise SvmError('not a script blob (no "SVMB" at its start)')
    if data[4] != vm.format_version:
        raise SvmError(f"format version {data[4]}; this tool knows version {vm.format_version}")
    if data[5] != vm.cell_bytes:
        raise SvmError(f"{data[5]}-byte cells; this tool knows {vm.cell_bytes}-byte cells")

    def le16(at):
        return int.from_bytes(data[at:at + 2], "little")

    def le32(at):
        return int.from_bytes(data[at:at + 4], "little")

    flags = le16(6)
    if flags & ~vm.flag_global_values:
        raise SvmError(f"the header's flags are 0x{flags:X}; only bit 0 (the globals' initial "
                       "values) is defined, the others must be 0")
    blob = Blob(data, vm)
    objects, strings, blob.globals, arrays = le16(8), le16(10), le16(12), le16(14)
    if blob.globals > vm.max_globals:
        raise SvmError(f"{blob.globals} globals; the most is {vm.max_globals} (VM_GLOBALS)")
    arrays_at = vm.header_size + objects * vm.object_size + strings * 4
    values_at = arrays_at + arrays * vm.array_record_size
    with_values = bool(flags & vm.flag_global_values)
    tables_end = values_at + (vm.cell_bytes * blob.globals if with_values else 0)
    if tables_end > len(data):
        values = ", the globals' initial values" if with_values else ""
        raise SvmError(f"the tables ({objects} objects, {strings} strings, {arrays} arrays"
                       f"{values}) need {tables_end} bytes, but the blob has {len(data)}")
    blob.tables_end = tables_end
    if with_values:
        blob.global_values = [svalue(le32(values_at + vm.cell_bytes * k))
                              for k in range(blob.globals)]
    for index in range(objects):
        record = vm.header_size + index * vm.object_size
        if le16(record + 6):
            raise SvmError(f"object {index}'s reserved field is not 0")
        handlers = []
        for event in range(vm.event_count):
            offset = le32(record + 8 + event * 4)
            if offset and not tables_end <= offset < len(data):
                raise SvmError(f"object {index}'s {vm.event_names.get(event, event)} handler is at "
                               f"0x{offset:X}, outside the code (0x{tables_end:X} to "
                               f"0x{len(data):X})")
            handlers.append(offset)
        blob.objects.append((le32(record), le16(record + 4), handlers))
    regions = []  # (start, end, what): the bytes of strings and ROM arrays
    for index in range(arrays):
        record = arrays_at + index * vm.array_record_size
        length, kind, where = le16(record), data[record + 2], le32(record + 4)
        if kind >= vm.kind_count or data[record + 3]:
            raise SvmError(f"array {index} has kind {kind} and reserved byte {data[record + 3]} "
                           "(the kinds are 0 to {vm.kind_count - 1}; the reserved byte must be 0)")
        if kind == vm.array_ram:
            if where + length > vm.array_cells:
                raise SvmError(f"array {index} (cells {where} to {where + length - 1}) is outside "
                               f"the RAM arrays' pool (VM_ARRAY_CELLS, {vm.array_cells} cells)")
        else:
            end = where + length * vm.rom_kinds[kind][1]
            if not tables_end <= where <= end <= len(data):
                raise SvmError(f"array {index}'s data (0x{where:X} to 0x{end:X}) is outside the "
                               f"blob's data (0x{tables_end:X} to 0x{len(data):X})")
            regions.append((where, end, f"array {index}'s data"))
        blob.arrays.append((kind, length, where))
    for index in range(strings):
        offset = le32(vm.header_size + objects * vm.object_size + index * 4)
        if not tables_end <= offset < len(data):
            raise SvmError(f"string {index} is at 0x{offset:X}, outside the blob's data "
                           f"(0x{tables_end:X} to 0x{len(data):X})")
        end = data.find(b"\0", offset)
        if end < 0:
            raise SvmError(f"string {index} (at 0x{offset:X}) has no NUL before the end of the blob")
        blob.strings.append((offset, data[offset:end]))
        regions.append((offset, end + 1, f"string {index}"))
    # The assembler lays strings and ROM data out one after the other: they
    # can't share bytes, and empty data can't sit inside another's bytes.
    for a_start, a_end, a in regions:
        for b_start, b_end, b in regions:
            if a < b and (a_start < b_end and b_start < a_end
                          or a_start == a_end and b_start < a_start < b_end
                          or b_start == b_end and a_start < b_start < a_end):
                raise SvmError(f"{a} and {b} overlap (0x{a_start:X} to 0x{a_end:X} and "
                               f"0x{b_start:X} to 0x{b_end:X}): the assembler can't lay that out")
    return blob


class _Item:
    __slots__ = ("at", "size", "op", "values")

    def __init__(self, at, size, op=None, values=None):
        self.at = at
        self.size = size
        self.op = op  # mnemonic, or None for a raw byte
        self.values = values  # the operand fields' values


def _rom_values(data, kind, length, where, vm):
    """A ROM array's elements, as numbers."""
    name, size = vm.rom_kinds[kind]
    return [int.from_bytes(data[where + k * size:where + (k + 1) * size], "little",
                           signed=name.startswith("s")) for k in range(length)]


def disassemble(data, vm, source="blob"):
    """A listing that the assembler turns back into exactly these bytes."""
    blob = validate(data, vm)
    if blob.global_values is not None and not any(blob.global_values):
        raise SvmError("the header's flag bit 0 is set, but every initial value is 0: the "
                       "assembler writes the initial values only when one isn't 0, so it can't "
                       "lay this blob out")
    size = len(data)
    # Regions: the bytes of strings ("strings") and ROM arrays ("data"),
    # by where they start (empty arrays first, then the one region with
    # bytes, if any) and where they end.
    region_at = {}
    region_end = {}
    for index, (kind, length, where) in enumerate(blob.arrays):
        if kind != vm.array_ram:
            region_end["data", index] = where + length * vm.rom_kinds[kind][1]
            region_at.setdefault(where, []).append((length > 0, "data", index))
    for index, (offset, text) in enumerate(blob.strings):
        region_end["strings", index] = offset + len(text) + 1
        region_at.setdefault(offset, []).append((True, "strings", index))
    for places in region_at.values():
        places.sort()
    handlers_at = {}
    for index, (_, _, handlers) in enumerate(blob.objects):
        for event, offset in enumerate(handlers):
            if offset:
                handlers_at.setdefault(offset, []).append((index, event))
    for start, places in region_at.items():
        for _, what, index in places:
            if any(start < offset < region_end[what, index] for offset in handlers_at):
                raise SvmError(f"a handler starts inside the bytes of {what} {index}: the "
                               "assembler can't lay that out")
    lowest = min(handlers_at) if handlers_at else None
    cuts = sorted(set(handlers_at) | set(region_at) | set(region_end.values()) | {size})

    # Segments between regions, split at handler offsets; the ones at or past
    # the lowest handler are code, decoded linearly (an instruction that
    # doesn't fit its segment, or an unknown opcode, is a raw byte).
    items = []  # _Item, or (offset, what, index) for a region, in address order
    boundaries = set()
    pos = blob.tables_end
    seen = set()  # region starts already listed (empty arrays don't move pos)
    while True:
        if pos in region_at and pos not in seen:
            seen.add(pos)
            for nonempty, what, index in region_at[pos]:
                items.append((pos, what, index))
                if nonempty:
                    pos = region_end[what, index]
            continue
        if pos >= size:
            break
        end = next(cut for cut in cuts if cut > pos)
        while pos < end:
            op = vm.op_names.get(data[pos]) if lowest is not None and pos >= lowest else None
            length = 1 + operand_size(op) if op else 1
            if op is None or pos + length > end:
                items.append(_Item(pos, 1))
            else:
                values, at = [], pos + 1
                for kind, _ in OPERANDS[op]:
                    n = OPERAND_SIZE[kind]
                    values.append(int.from_bytes(data[at:at + n], "little", signed=kind in SIGNED))
                    at += n
                items.append(_Item(pos, length, op, values))
            boundaries.add(pos)
            pos += length

    # Jumps and calls whose target is an instruction boundary get a label;
    # the rest, and operands naming a table entry that doesn't exist (SPAWN,
    # NEXTI, TRACE, LDA, STA, LEN), are kept as raw bytes, which always
    # round-trip.
    counts = {"object": len(blob.objects), "string": len(blob.strings), "array": len(blob.arrays)}
    targets = set()
    for item in items:
        if not isinstance(item, _Item) or item.op is None:
            continue
        for (kind, names), value in zip(OPERANDS[item.op], item.values):
            if names in counts and value >= counts[names]:
                item.op = None
            elif names == "label":
                target = item.at + item.size + value if kind == "rel16" else value
                if target in boundaries:
                    item.values = [target]
                    targets.add(target)
                else:
                    item.op = None

    # The assembler puts the ROM arrays' data after the code, in index order,
    # then the strings, in index order, unless the listing places them: so
    # .data and .strings lines are needed only when the blob isn't laid out
    # exactly that way (or a handler starts in that tail, which a .handler
    # line with nothing after it can't say).
    code_end = max((item.at + item.size for item in items if isinstance(item, _Item)),
                   default=blob.tables_end)
    expected = code_end
    default_layout = not any(offset >= code_end for offset in handlers_at)
    tail = [("data", index) for index, (kind, _, _) in enumerate(blob.arrays)
            if kind != vm.array_ram] + [("strings", index) for index in range(len(blob.strings))]
    for what, index in tail:
        start = blob.strings[index][0] if what == "strings" else blob.arrays[index][2]
        if start != expected:
            default_layout = False
        expected = region_end[what, index]
    if expected != size:
        default_layout = False

    out = [f"; {source}: {size} bytes, {len(blob.objects)} objects, {len(blob.strings)} strings, "
           f"{blob.globals} globals, {len(blob.arrays)} arrays, disassembled by {TOOL}."]
    for index, (mask, sprite, _) in enumerate(blob.objects):
        out.append(f".object {index} mask=0x{mask:08X} sprite={sprite}")
    for index, (_, text) in enumerate(blob.strings):
        out.append(f'.string {index} "{escape_string(text)}"')
    values = blob.global_values or [0] * blob.globals
    for start in range(0, blob.globals, 16):
        out.append(".globals " + " ".join(f"{g}={values[g]}" if values[g] else str(g)
                                          for g in range(start, min(start + 16, blob.globals))))
    next_cell = 0
    for index, (kind, length, where) in enumerate(blob.arrays):
        if kind == vm.array_ram:
            out.append(f".array {index} {length}" + (f" at={where}" if where != next_cell else ""))
            next_cell = where + length
        else:
            values = _rom_values(data, kind, length, where, vm)
            out.append(f".rom {index} {vm.rom_kinds[kind][0]} {', '.join(map(str, values))}".rstrip())

    def operand_text(names, value):
        if names == "label":
            return f"L_{value}"
        if names == "prop":
            return vm.prop_names.get(value, str(value))
        if names == "sys":
            return vm.sys_names.get(value, str(value))
        return str(value)

    raw = []  # consecutive raw bytes, flushed as .byte lines
    marked = set()

    def flush_raw():
        for start in range(0, len(raw), 16):
            out.append("    .byte " + ", ".join(f"0x{b:02X}" for b in raw[start:start + 16]))
        raw.clear()

    def mark(at):
        """The .handler lines and label at this offset (once)."""
        if at in marked or not (at in handlers_at or at in targets):
            return
        marked.add(at)
        flush_raw()
        for index, event in handlers_at.get(at, ()):
            out.append(f".handler {index} {vm.event_names[event]}")
        out.append(f"L_{at}:")

    for item in items:
        if isinstance(item, tuple):
            offset, what, index = item
            mark(offset)
            if not default_layout:
                flush_raw()
                out.append(f"    .{what} {index}")
            continue
        mark(item.at)
        if item.op is None:
            raw.extend(data[item.at:item.at + item.size])
        else:
            flush_raw()
            text = ", ".join(operand_text(names, value)
                             for (_, names), value in zip(OPERANDS[item.op], item.values))
            out.append(f"    {item.op} {text}".rstrip())
    flush_raw()
    return "\n".join(out) + "\n"


# --- Command line ------------------------------------------------------------


def print_warnings(warnings):
    for file, line, message in warnings:
        print(_located(file, line, "warning", message), file=sys.stderr)


def cmd_asm(args, vm):
    if args.c and not args.symbol:
        raise SvmError("--c needs --symbol NAME (the C array's name)")
    headers = HeaderNames(vm.names)
    for path in args.header:
        headers.load(path)
    headers.check()
    with open(args.listing, encoding="utf-8") as f:
        text = f.read()
    assembled = assemble(text, vm, headers, args.listing)
    print_warnings(assembled.warnings)
    listing_name = os.path.basename(args.listing)
    if args.output:
        with open(args.output, "wb") as f:
            f.write(assembled.blob)
    if args.c:
        with open(args.c, "w", encoding="utf-8") as f:
            f.write(c_source(assembled.blob, args.symbol, listing_name))
    if args.defs:
        with open(args.defs, "w", encoding="utf-8") as f:
            f.write(defs_header(assembled, args.defs, args.prefix, args.symbol, listing_name))
    return 0


def cmd_dis(args, vm):
    with open(args.blob, "rb") as f:
        data = f.read()
    try:
        text = disassemble(data, vm, os.path.basename(args.blob))
    except SvmError as e:
        raise SvmError.many([(args.blob, None, m) for _, _, m in e.errors]) from None
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                     epilog="Run with a command and --help for its options.")
    commands = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")
    asm = commands.add_parser("asm", help="assemble a listing into a blob")
    asm.add_argument("listing", help="the listing (.svm)")
    asm.add_argument("--header", action="append", default=[], metavar="FILE",
                     help="a C header whose integer constants the listing may use (repeatable)")
    asm.add_argument("-o", "--output", metavar="OUT.bin", help="write the raw blob")
    asm.add_argument("--c", metavar="OUT.c", help="write the blob as a C array (needs --symbol)")
    asm.add_argument("--symbol", metavar="NAME", help="the C array's name; NAME_size is its size")
    asm.add_argument("--defs", metavar="OUT.h",
                     help="write a header with OBJ_*, STR_*, G_* and ARR_* defines and the counts")
    asm.add_argument("--prefix", default="", metavar="P", help="prefix for the --defs names")
    dis = commands.add_parser("dis", help="disassemble a blob into a listing")
    dis.add_argument("blob", help="the blob (.bin)")
    dis.add_argument("-o", "--output", metavar="OUT.svm", help="write the listing (default: stdout)")
    args = parser.parse_args(argv)
    try:
        vm = load_vm()
        if args.command == "asm":
            return cmd_asm(args, vm)
        return cmd_dis(args, vm)
    except SvmError as e:
        print(e, file=sys.stderr)
    except OSError as e:
        print(f"error: {e}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
