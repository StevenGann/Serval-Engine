#!/usr/bin/env python3
"""Builds a Maxmod sound bank for a game: mmutil's bank as C source.

    soundbank.py --mmutil PATH --version V --name NAME --out-dir DIR FILE...

FILEs are modules (.mod, .s3m, .xm, .it) and WAV samples (.wav). BlocksDS's
mmutil, which must say it is version V (serval.json's toolchain.mmutil), builds
them into one GBA bank (docs/audio.md#sound-bank). Writes, in DIR:

- NAME.c: the bank, `const unsigned char NAME[]`, word-aligned, for
  audio_bank_set(NAME);
- NAME.h: mmutil's numbers, MOD_<FILE> for each module and SFX_<FILE> for each
  sample, from 0, in the order the files come (a module's samples are numbered
  with the samples, before the WAVs that follow it), and MSL_NSONGS,
  MSL_NSAMPS and MSL_BANKSIZE; plus the bank's extern declaration. The
  defines are mmutil's own, so `svm.py --header NAME.h` gives scripts the same
  names.

Refused, before mmutil runs: a file of another kind, two files whose names
give the same define (mmutil upper-cases the name up to its first dot and
turns punctuation into underscores), and a sample named "none", whose SFX_NONE
would clash with audio.h's. A module's samples named with a leading "#" also
get SFX_ defines from mmutil, so the header is checked again afterwards.

Called by serval_add_soundbank() (cmake/Serval.cmake) at build time.
"""

import argparse
import os
import pathlib
import re
import subprocess
import sys
import tempfile

KINDS = {".mod": "MOD_", ".s3m": "MOD_", ".xm": "MOD_", ".it": "MOD_", ".wav": "SFX_"}
_DEFINE = re.compile(r"^#define[ \t]+([A-Za-z_]\w*)[ \t]+(-?\d+)[ \t]*$")


class BankError(Exception):
    pass


def mmutil_name(path):
    """The name mmutil's header gives a file (MSL_PrintDefinition in mmutil's
    source/msl.c): the file name up to its first dot, upper-cased, with
    punctuation and every byte past 'z' (each byte of a non-ASCII character)
    made '_'."""
    name = []
    for b in os.fsencode(pathlib.PurePath(path).name):
        c = chr(b)
        if c == ".":
            break
        c = c.upper() if c.isascii() else c
        if " " <= c <= "/" or ":" <= c <= "@" or "[" <= c <= "`" or c >= "{":
            c = "_"
        name.append(c)
    return "".join(name)


def check_inputs(files):
    """The defines the files will get; raises BankError for a refused one."""
    seen = {}
    for f in files:
        kind = KINDS.get(pathlib.PurePath(f).suffix.lower())
        if kind is None:
            raise BankError(f"{f}: not a module (.mod, .s3m, .xm, .it) or a WAV sample (.wav)")
        name = mmutil_name(f)
        if not name:
            raise BankError(f"{f}: its name gives mmutil no define (it starts with a dot)")
        define = kind + name
        if define == "SFX_NONE":
            raise BankError(f"{f}: a sample can't be named \"none\": its SFX_NONE would clash "
                            "with audio.h's SFX_NONE (no effect); rename it")
        if define in seen:
            raise BankError(f"{f} and {seen[define]} both give {define}; rename one")
        seen[define] = f
    return seen


def read_defines(text, source):
    """mmutil's header as (name, value) pairs; raises BankError for SFX_NONE or a
    name defined twice."""
    defines = []
    names = set()
    for line in text.splitlines():
        if not line.strip():
            continue
        m = _DEFINE.match(line.strip())
        if m is None:
            raise BankError(f"{source}: unexpected line in mmutil's header: {line!r}")
        name, value = m.group(1), int(m.group(2))
        if name == "SFX_NONE":
            raise BankError(f"{source}: a module's sample is named \"#none\", whose SFX_NONE would "
                            "clash with audio.h's SFX_NONE; rename it")
        if name in names:
            raise BankError(f"{source}: mmutil defined {name} twice; rename one of its sources")
        names.add(name)
        defines.append((name, value))
    return defines


def c_source(name, data):
    lines = [
        f"// The sound bank {name}, built by mmutil (tools/soundbank.py). Generated:",
        "// don't edit.",
        "",
        f"const unsigned char {name}[{len(data)}] __attribute__((aligned(4))) = {{",
    ]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def c_header(name, defines, bank_size):
    guard = f"{name.upper()}_SOUNDBANK_H"
    lines = [
        f"// The sound bank {name}, built by mmutil (tools/soundbank.py). Generated:",
        "// don't edit. Register it with audio_bank_set(" + name + "); play its",
        "// modules (MOD_*) with music_play() and its samples (SFX_*) with sfx_play().",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
    ]
    lines += [f"#define {n} {v}" for n, v in defines]
    lines += ["", f"extern const unsigned char {name}[{bank_size}];", "", f"#endif // {guard}"]
    return "\n".join(lines) + "\n"


def write_if_changed(path, text):
    path = pathlib.Path(path)
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return
    path.write_text(text, encoding="utf-8", newline="\n")


def build(mmutil, version, name, out_dir, files):
    if not re.fullmatch(r"[A-Za-z_]\w*", name):
        raise BankError(f"the bank's name {name!r} is not a C identifier")
    check_inputs(files)
    try:
        says = subprocess.run([mmutil, "-V"], capture_output=True, text=True, check=False)
    except OSError as e:
        raise BankError(f"can't run mmutil ({mmutil}): {e.strerror}") from None
    if says.stdout.strip() != f"mmutil v{version}":
        raise BankError(f"{mmutil} says {says.stdout.strip()!r}, not 'mmutil v{version}': the "
                        "engine's Maxmod reads banks of mmutil " + version +
                        " (serval.json); install it with tools/build-mmutil.sh")
    out_dir = pathlib.Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    sources = [os.path.abspath(f) for f in files]
    # mmutil writes its temporary files into the current directory.
    with tempfile.TemporaryDirectory(dir=out_dir) as work:
        bank_file = os.path.join(work, "bank.bin")
        header_file = os.path.join(work, "bank.h")
        run = subprocess.run([mmutil, *sources, "-o" + bank_file, "-h" + header_file],
                             cwd=work, capture_output=True, text=True, check=False)
        if run.returncode != 0 or not os.path.exists(bank_file):
            raise BankError("mmutil failed:\n" + run.stdout + run.stderr)
        data = pathlib.Path(bank_file).read_bytes()
        defines = read_defines(pathlib.Path(header_file).read_text(encoding="latin-1"),
                               "mmutil's header")
    write_if_changed(out_dir / f"{name}.c", c_source(name, data))
    write_if_changed(out_dir / f"{name}.h", c_header(name, defines, len(data)))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--mmutil", required=True, help="BlocksDS's mmutil")
    parser.add_argument("--version", required=True, help="the version mmutil must be")
    parser.add_argument("--name", required=True, help="the bank's C name")
    parser.add_argument("--out-dir", required=True, help="where NAME.c and NAME.h go")
    parser.add_argument("files", nargs="+", help="modules and WAV samples")
    args = parser.parse_args()
    try:
        build(args.mmutil, args.version, args.name, args.out_dir, args.files)
    except BankError as e:
        print(f"soundbank.py: error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
