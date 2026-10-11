#!/usr/bin/env python3
"""Check a script's debug maps by joining them the way a debugger does.

The debug link's maps (docs/debug-link.md#maps; the format in
docs/lua.md#debug-information) are two side files beside a script's blob:
tools/svm.py's .svmap (each op's pc and its listing line, the blob's size and
FNV-1a 32) and tools/svlua.py's .svdbg (each listing line's place in the
script, statement starts, functions, locals and the program's names). This
tool reads them as a debugger would and checks that they agree with each
other and with the blob:

- the .svmap: its pcs in order, inside the blob's code, each at an op whose
  opcode is the one its listing line names, no op overlapping the next; the
  blob's size and hash (given the blob); every byte of the code covered by
  an op (when the listing places no data among them);
- the round trip: every statement start in the .svdbg is an op in the
  .svmap, and that op's pc maps back to the same line of the script (what a
  breakpoint on a line, then the stop it causes, go through);
- every op has a place in the script and every code line an op; each op lies
  in exactly one function's listing range; each local's scope lies in its
  function's, from op to op; names and numbers are unique.

Usage:
  check-debug-maps.py BASE...

Each BASE is a path without its extension, as serval_add_script() names its
outputs: BASE.svmap, BASE.svdbg (if there is one: a Lua script's), and
BASE_script.c (if there is one: the blob, for the hash and the opcodes). The
listing is the one the .svmap names. Exit status 1 on any problem.

It is also a module: svlua_test.py runs check() and breakpoints() on what it
compiles. Python 3 standard library only.
"""

import bisect
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svm  # noqa: E402

TYPES = ("integer", "fixed", "boolean", "entity")
ARRAY_KINDS = ("ram", "s8", "u8", "s16", "u16", "s32")


class Maps:
    """A .svmap and its .svdbg (or None), read as a debugger reads them."""

    def __init__(self, svmap, svdbg=None):
        self.svmap = svmap
        self.svdbg = svdbg
        self.pcs = [pc for pc, _ in svmap["pcs"]]
        self.listing_lines = [line for _, line in svmap["pcs"]]
        self.pc_of_line = {line: pc for pc, line in svmap["pcs"]}
        self.place = {}  # listing line -> (source, line, column, statement start)
        if svdbg is not None:
            for listing, source, line, column, start in svdbg["lines"]:
                self.place[listing] = (source, line, column, start)

    def listing_line(self, pc):
        """The listing line of the op at or before pc (a pc from the game:
        where a context is, or where it stopped)."""
        i = bisect.bisect_right(self.pcs, pc) - 1
        return self.listing_lines[i] if i >= 0 else None

    def source_line(self, pc):
        """(source file, line) of a pc, through both maps."""
        place = self.place.get(self.listing_line(pc))
        if place is None:
            return None
        return self.svdbg["sources"][place[0]], place[1]

    def function(self, pc):
        """The function or handler a pc is in (its .svdbg entry)."""
        line = self.listing_line(pc)
        for entry in self.svdbg["functions"]:
            if entry["first"] <= line <= entry["last"]:
                return entry
        return None


def breakpoints(maps):
    """{(source file, line): [pc, ...]}: where a breakpoint on each line of
    the script goes, its statement starts' pcs (the BREAK_SET a debugger
    sends)."""
    sites = {}
    for listing, (source, line, _, start) in sorted(maps.place.items()):
        if start:
            pc = maps.pc_of_line.get(listing)
            if pc is not None:
                sites.setdefault((maps.svdbg["sources"][source], line), []).append(pc)
    return sites


def _blob_problems(maps, blob, listing):
    """The .svmap against the blob's bytes and, when given, the listing's
    lines."""
    problems = []
    svmap = maps.svmap
    if svmap["blob"] != {"size": len(blob), "fnv1a32": svm.fnv1a32(blob)}:
        problems.append(f"the blob is {len(blob)} bytes with FNV-1a 0x{svm.fnv1a32(blob):08X}; "
                        f"the map says {svmap['blob']}")
        return problems
    vm = svm.load_vm()
    try:
        tables = svm.validate(blob, vm)
    except svm.SvmError as e:
        return [f"the blob doesn't load: {e}"]
    # The data the assembler places: strings and ROM arrays' elements.
    data = [offset for offset, _ in tables.strings]
    data += [where for kind, _, where in tables.arrays if kind != vm.array_ram]
    inline = listing is not None and any(
        (svm._strip_comment(text).split() or [""])[0] in (".strings", ".data")
        for text in listing)
    covered = 0  # bytes of the code inside an op
    end = None
    for pc, line in svmap["pcs"]:
        if not tables.tables_end <= pc < len(blob):
            problems.append(f"pc 0x{pc:X} (listing line {line}) is outside the code "
                            f"(0x{tables.tables_end:X} to 0x{len(blob):X})")
            continue
        if end is not None and pc < end:
            problems.append(f"pc 0x{pc:X} (listing line {line}) is inside the op before it")
        name = vm.op_names.get(blob[pc])
        size = 1 + svm.operand_size(name) if name in svm.OPERANDS else 1
        if listing is not None:
            text = svm._strip_comment(listing[line - 1]).strip() if line <= len(listing) else ""
            text = re.sub(r"^[A-Za-z_]\w*:\s*", "", text)  # a label before the op
            word = text.split()[0] if text else ""
            if word == ".byte":
                size = len(svm.evaluate_list(text[5:], lambda n: 0))
            elif word not in (name, "PUSH" if name and name.startswith("PUSH") else None):
                problems.append(f"pc 0x{pc:X} holds {name or hex(blob[pc])}, but listing line "
                                f"{line} is {text!r}")
        covered += size
        end = pc + size
    if svmap["pcs"] and not inline:
        code_end = min([o for o in data if o >= svmap["pcs"][0][0]] + [len(blob)])
        if svmap["pcs"][0][0] != tables.tables_end or covered != code_end - tables.tables_end:
            problems.append(f"the ops cover {covered} bytes; the code is 0x{tables.tables_end:X} "
                            f"to 0x{code_end:X}, {code_end - tables.tables_end} bytes")
    return problems


def check(svmap, svdbg=None, blob=None, listing=None):
    """Every problem with the maps, as text (none: they agree). blob: the
    blob's bytes; listing: the listing's lines."""
    problems = []
    for name, doc in (("svmap", svmap), ("svdbg", svdbg)):
        if doc is not None and (doc.get("format"), doc.get("version")) != (name, 1):
            problems.append(f"not a {name} version 1: {doc.get('format')!r}, "
                            f"{doc.get('version')!r}")
    if problems:
        return problems
    maps = Maps(svmap, svdbg)
    if maps.pcs != sorted(set(maps.pcs)):
        problems.append("the .svmap's pcs aren't in increasing order")
    if len(set(maps.listing_lines)) != len(maps.listing_lines):
        problems.append("a listing line has two pcs")
    if blob is not None:
        problems += _blob_problems(maps, blob, listing)
    if svdbg is None:
        return problems

    for entry in svdbg["lines"]:
        if (len(entry) != 5 or not all(type(v) is int for v in entry)
                or not 0 <= entry[1] < len(svdbg["sources"]) or entry[2] < 1 or entry[3] < 1
                or entry[4] not in (0, 1)):
            problems.append(f"lines: {entry} isn't [listing line, source, line, column, 0 or 1]")

    # Each op has its place in the script, and each code line its op; the
    # listing's "; file:line" comments say the same.
    for line in maps.listing_lines:
        if line not in maps.place:
            problems.append(f"listing line {line} has an op but no place in the script")
        elif listing is not None and line <= len(listing):
            source, number = maps.place[line][:2]
            name = re.escape(os.path.basename(svdbg["sources"][source]))
            said = re.search(rf";\s*{name}:(\d+)", listing[line - 1])
            if said and int(said.group(1)) != number:
                problems.append(f"listing line {line} says line {said.group(1)}; the .svdbg "
                                f"says {number}")
    for line in maps.place:
        if line not in maps.pc_of_line:
            problems.append(f"listing line {line} is in the .svdbg's lines but has no op")

    # The round trip: a breakpoint's pc stops at the same line of the script.
    for listing_line, (source, line, column, start) in maps.place.items():
        if not start or listing_line not in maps.pc_of_line:
            continue
        pc = maps.pc_of_line[listing_line]
        back = maps.source_line(pc)
        if back != (svdbg["sources"][source], line):
            problems.append(f"the statement start at {svdbg['sources'][source]}:{line}:{column} "
                            f"(listing line {listing_line}) is pc 0x{pc:X}, which maps back "
                            f"to {back}")

    # Functions: ranges in order, apart, each op in exactly one.
    functions = svdbg["functions"]
    previous = 0
    for entry in functions:
        if not previous < entry["first"] <= entry["last"]:
            problems.append(f"function {entry['name']}: lines {entry['first']} to "
                            f"{entry['last']} overlap the one before or are backwards")
        previous = entry["last"]
        if (entry["object"] is None) != (entry["event"] is None):
            problems.append(f"function {entry['name']}: an object without an event, or the "
                            "reverse")
    firsts = [entry["first"] for entry in functions]
    for line in maps.listing_lines:
        i = bisect.bisect_right(firsts, line) - 1
        if i < 0 or line > functions[i]["last"]:
            problems.append(f"listing line {line}'s op is in no function")

    # Locals: in a function, scoped from op to op inside it.
    for entry in svdbg["locals"]:
        index, (first, last) = entry["function"], entry["scope"]
        if not 0 <= index < len(functions):
            problems.append(f"local {entry['name']}: no function {index}")
            continue
        fn = functions[index]
        if not fn["first"] <= first <= last <= fn["last"]:
            problems.append(f"local {entry['name']}: scope {first} to {last} isn't inside "
                            f"{fn['name']} ({fn['first']} to {fn['last']})")
        if first not in maps.pc_of_line or last not in maps.pc_of_line:
            problems.append(f"local {entry['name']}: its scope doesn't start and end at ops")
        if entry["type"] not in TYPES or entry["slot"] < 0:
            problems.append(f"local {entry['name']}: type {entry['type']}, slot {entry['slot']}")

    # The program's names: unique names and numbers, known types.
    for key in ("globals", "fields", "arrays", "objects", "strings"):
        entries = svdbg[key]
        names = [e["name"] for e in entries]
        numbers = [e["number"] for e in entries]
        if len(set(names)) != len(names) or numbers != sorted(set(numbers)):
            problems.append(f"{key}: a name or a number twice, or not in order")
        for e in entries:
            if key in ("globals", "fields", "arrays") and e["type"] not in TYPES:
                problems.append(f"{key}: {e['name']} has type {e['type']}")
            if key == "arrays" and e["kind"] not in ARRAY_KINDS:
                problems.append(f"arrays: {e['name']} has kind {e['kind']}")
    return problems


def blob_of_c(path):
    """The bytes of the blob in a <name>_script.c (svm.py --c)."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    body = text[text.index("[] = {") + 6:text.index("};")]
    body = re.sub(r"/\*.*?\*/", "", body)
    return bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body))


def check_files(base):
    """Checks BASE.svmap, BASE.svdbg and BASE_script.c; returns (problems, a
    one-line summary)."""
    with open(base + ".svmap", encoding="utf-8") as f:
        svmap = json.load(f)
    svdbg = None
    if os.path.exists(base + ".svdbg"):
        with open(base + ".svdbg", encoding="utf-8") as f:
            svdbg = json.load(f)
    blob = blob_of_c(base + "_script.c") if os.path.exists(base + "_script.c") else None
    listing = None
    if os.path.exists(svmap.get("listing") or ""):
        with open(svmap["listing"], encoding="utf-8") as f:
            listing = f.read().splitlines()
    problems = check(svmap, svdbg, blob, listing)
    summary = f"{os.path.basename(base)}: {svmap['blob']['size']}-byte blob, " \
              f"{len(svmap['pcs'])} ops"
    if svdbg is not None:
        sites = breakpoints(Maps(svmap, svdbg))
        summary += (f"; {sum(len(p) for p in sites.values())} statement starts on {len(sites)} "
                    f"lines, {len(svdbg['functions'])} functions, {len(svdbg['locals'])} locals")
    if blob is None:
        summary += " (no blob: not checked against it)"
    return problems, summary


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if not args or args[0] in ("-h", "--help"):
        print(__doc__.split("Usage:")[1].split("It is also")[0].strip())
        return 0 if args else 2
    failed = False
    for base in args:
        try:
            problems, summary = check_files(base)
        except (OSError, ValueError, KeyError, TypeError) as e:
            problems, summary = [f"can't read the maps: {e!r}"], base
        print(summary)
        for problem in problems:
            print(f"  {problem}")
        failed = failed or bool(problems)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
