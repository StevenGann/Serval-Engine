#!/usr/bin/env python3
"""Check that every planned API warns at every use (CTest planned_api).

Planned API is declared, but not implemented yet, with SERVAL_PLANNED
(include/serval/platform.h; docs/releases.md#planned-api): the compiler warns
wherever a game uses it. This keeps that true, for one compiler:

1. It finds the planned names the public headers (include/serval/*.h)
   declare: every SERVAL_PLANNED(...) outside preprocessor lines, which must
   come before a function declaration or after an enumerator's name. Anything
   else is an error: the warning is unreliable on struct fields, and a
   planned typedef warns inside the headers (docs/development.md#planned-api).
2. It compiles each tests/planned/*.c (one file per header, using each of its
   header's planned names on a line that ends "// planned") at -O0 and at -O2,
   with the engine's warning flags, and checks that every marked line warns
   with the Serval message, that no other line and no header does, and that
   every planned name warns somewhere. So a planned name added to a header
   without a use fails, and so does a marked use of a name that is no longer
   planned (implemented, and its marker dropped).
3. It compiles each file again with -DSERVAL_NO_PLANNED_WARNINGS -Werror,
   which must print no warning or error.

No planned names and files with no marked lines pass.

Usage: tools/check-planned.py [--include DIR] [PLANNED_DIR] [-- COMPILER [FLAGS...]]

DIR defaults to the repository's include/, PLANNED_DIR to tests/planned/,
the compiler to $CC or cc. COMPILER and FLAGS are the target's, e.g.
  tools/check-planned.py -- arm-none-eabi-gcc -mcpu=arm7tdmi -mthumb -DSERVAL_GBA
Exits non-zero, listing every problem, if any check fails. Needs only Python
3's standard library.
"""

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MESSAGE = "Serval: planned, not implemented in this version"
# GCC and Clang: "file:line:column: warning: 'name' is deprecated: message".
WARNING = re.compile(r"^(?P<file>.+?):(?P<line>\d+):(?:\d+:)? warning: '(?P<name>\w+)' is deprecated: "
                     + re.escape(MESSAGE))
# Any diagnostic (emcc may also print other lines, e.g. about its cache).
DIAGNOSTIC = re.compile(r"\b(?:warning|error):")
MARK = "// planned"
LEVELS = ("-O0", "-O2")
# The engine's own warning flags (serval_warnings, cmake/Serval.cmake).
FLAGS = ["-std=gnu17", "-Wall", "-Wextra", "-Wshadow", "-Wundef", "-Wstrict-prototypes",
         "-Wmissing-prototypes"]
KEYWORDS = {"auto", "bool", "char", "const", "double", "enum", "extern", "float", "inline", "int",
            "long", "register", "restrict", "short", "signed", "static", "struct", "typedef",
            "union", "unsigned", "void", "volatile"}
COMMENT_OR_STRING = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
STRING = re.compile(r'"(?:\\.|[^"\\\n])*"')
PLACEMENT = ("SERVAL_PLANNED goes before a function declaration or after an enumerator's name, "
             "nowhere else (docs/development.md#planned-api)")


def show(path):
    """A path relative to the repository, if it is in it."""
    rel = os.path.relpath(path, ROOT)
    return path if rel.startswith("..") else rel.replace(os.sep, "/")


def code_only(text):
    """The text with comments and preprocessor lines blanked; line numbers kept."""
    text = COMMENT_OR_STRING.sub(
        lambda m: re.sub(r"[^\n]", " ", m[0]) if m[0][0] == "/" else m[0], text)
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        if lines[i].lstrip().startswith("#"):  # the macro's own definitions among them
            while True:
                continued = lines[i].endswith("\\")
                lines[i] = ""
                i += 1
                if not continued or i == len(lines):
                    break
        else:
            i += 1
    return "\n".join(lines)


def planned_names(include):
    """{name: "header:line"} of every planned name, and the errors found."""
    names, errors = {}, []
    headers = sorted(os.path.join(d, f) for d, _, files in os.walk(include) for f in files
                     if f.endswith(".h"))
    for header in headers:
        with open(header, encoding="utf-8") as f:
            text = code_only(f.read())
        for m in re.finditer(r"\bSERVAL_PLANNED\s*\(", text):
            where = f"{show(header)}:{text.count(chr(10), 0, m.start()) + 1}"
            # The end of the macro's argument: its string may hold parentheses.
            depth, i = 1, m.end()
            while depth and i < len(text):
                s = STRING.match(text, i)
                if s:
                    i = s.end()
                    continue
                depth += {"(": 1, ")": -1}.get(text[i], 0)
                i += 1
            after = text[i:].lstrip()
            if after[:1] in ("=", ",", "}"):  # NAME SERVAL_PLANNED("...") = value,
                name = re.search(r"(\w+)\s*$", text[:m.start()])
            else:  # SERVAL_PLANNED("...") type name(parameters);
                declaration = re.match(r"[^;{]*", after)[0]
                name = None
                if "(" in declaration and not re.match(r"typedef\b", declaration):
                    name = re.search(r"(\w+)\s*$", declaration[:declaration.index("(")])
            if not name or name[1] in KEYWORDS or name[1].startswith("__") or name[1][0].isdigit():
                errors.append(f"{where}: {PLACEMENT}")
            elif name[1] in names:
                errors.append(f"{where}: {name[1]} is declared planned twice (also {names[name[1]]})")
            else:
                names[name[1]] = where
    return names, errors


def run(command):
    env = dict(os.environ, LC_ALL="C")  # GCC quotes names with '' instead of curly quotes
    r = subprocess.run(command, capture_output=True, text=True, env=env)
    return r.returncode, r.stdout + r.stderr


def main():
    argv = sys.argv[1:]
    compiler = [os.environ.get("CC", "cc")]
    if "--" in argv:
        split = argv.index("--")
        argv, compiler = argv[:split], argv[split + 1:]
    parser = argparse.ArgumentParser(
        description="Check that every planned API (SERVAL_PLANNED) warns at every use.",
        usage="%(prog)s [--include DIR] [PLANNED_DIR] [-- COMPILER [FLAGS...]]")
    parser.add_argument("--include", default=os.path.join(ROOT, "include"),
                        help="the public headers' include directory (default: include/)")
    parser.add_argument("planned_dir", nargs="?", default=os.path.join(ROOT, "tests", "planned"),
                        help="the files using every planned name (default: tests/planned/)")
    args = parser.parse_args(argv)
    if not compiler:
        parser.error("no compiler after --")

    names, errors = planned_names(args.include)
    sources = sorted(os.path.abspath(os.path.join(args.planned_dir, f))
                     for f in os.listdir(args.planned_dir) if f.endswith(".c"))
    base = compiler + FLAGS + ["-I", os.path.abspath(args.include)]

    with tempfile.TemporaryDirectory() as out, concurrent.futures.ThreadPoolExecutor() as pool:
        jobs = {}
        for n, source in enumerate(sources):
            for level in LEVELS:
                obj = os.path.join(out, f"{n}{level}.o")
                jobs[source, level, "warn"] = pool.submit(
                    run, base + [level, "-Wno-error", "-c", source, "-o", obj])
                jobs[source, level, "quiet"] = pool.submit(
                    run, base + [level, "-DSERVAL_NO_PLANNED_WARNINGS", "-Werror", "-c", source,
                                 "-o", obj + ".quiet.o"])
        results = {key: job.result() for key, job in jobs.items()}

    warned_names = set()
    problems = {}  # message: the levels it happened at
    for source in sources:
        with open(source, encoding="utf-8") as f:
            marked = {n + 1 for n, line in enumerate(f.read().splitlines())
                      if line.rstrip().endswith(MARK)}
        for level in LEVELS:
            code, output = results[source, level, "warn"]
            if code:
                problems.setdefault(f"{show(source)} does not compile:\n{output}", []).append(level)
                continue
            warned = {}
            for line in output.splitlines():
                m = WARNING.match(line)
                if not m:
                    continue
                if os.path.normcase(os.path.abspath(m["file"])) != os.path.normcase(source):
                    problems.setdefault(f"{line}\n  a planned warning outside {show(source)}, "
                                        "the file using the name (in a header?)", []).append(level)
                    continue
                warned.setdefault(int(m["line"]), []).append(m["name"])
                warned_names.add(m["name"])
            for n in sorted(marked - warned.keys()):
                problems.setdefault(f"{show(source)}:{n}: marked planned, but no planned warning "
                                    "(implemented? then drop the line)", []).append(level)
            for n in sorted(warned.keys() - marked):
                problems.setdefault(f"{show(source)}:{n}: a planned warning ({', '.join(warned[n])}) "
                                    f"on a line not marked \"{MARK}\"", []).append(level)
            code, output = results[source, level, "quiet"]
            if code or DIAGNOSTIC.search(output):
                problems.setdefault(f"{show(source)}: not silent with -DSERVAL_NO_PLANNED_WARNINGS "
                                    f"-Werror:\n{output}", []).append(level)

    for message, levels in problems.items():
        errors.append(message if len(levels) == len(LEVELS) else f"{message} (at {' '.join(levels)})")
    for name in sorted(names.keys() - warned_names):
        errors.append(f"{names[name]}: {name} is planned, but no file in "
                      f"{show(args.planned_dir)} uses it (on a line marked \"{MARK}\")")
    for name in sorted(warned_names - names.keys()):
        errors.append(f"{name} warns as planned, but no SERVAL_PLANNED in {show(args.include)} "
                      "declares it")

    for error in errors:
        print(f"check-planned: {error}")
    print(f"check-planned: {len(warned_names & names.keys())} of {len(names)} planned names warn, "
          f"{len(sources)} files, {'FAIL' if errors else 'ok'}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
