#!/usr/bin/env python3
"""Run Lua-subset programs under real Lua and on the engine's VM, and compare.

docs/lua.md's rule is that every program the compiler accepts means what it
means in Lua 5.4 built with 32-bit integers (LUA_32BITS). This checks it:
each program in tests/svlua/diff/ runs twice, frame by frame, from the same
start and with the same input:
  - under Lua, with tests/svlua/stub.lua standing in for the engine's API
    (objects, instances, behaviours as coroutines, the event queue, the
    frame's order, random_range bit for bit);
  - compiled by tools/svlua.py, assembled by tools/svm.py, on the VM itself
    (svlua_runner, tests/svlua/runner.c, built by the host preset).
After every printed frame the two must agree on every global, every RAM
array's cells, every attached instance's properties and fields, and on the
engine calls made during the frame (text, numbers, sounds, brightness):
integers, booleans and entities exactly, fixed values within the program's
tolerance (lua.md: Lua's floats are 24.8 fixed point on the VM). The VM run
must not warn, unless the program allows it.

A program says how to run it in comment lines starting "-- diff:", each a
list of key=value settings (repeatable ones may appear several times):
  frames=N            frames to run (default 1)
  start=Object        start Object's room_start as a thread (vm_start)
  attach=Object[@X,Y] an instance at (X, Y) pixels (vm_attach), in order
  buttons=F:MASK[:N]  hold MASK (BUTTON_* names joined by |, or a number)
                      from frame F for N frames (default 1)
  collide=Object:Other  Object's instances touching Other's get Collision
  movement            run sys_movement (positions follow velocities)
  seed=N              random_seed(N) first
  print=all|last|F,F  the frames to compare the state at (default all)
  tolerance=X         for fixed values (default 1/256)
  warnings=allowed    the VM run may warn
Names in ALL_CAPS come from the engine's headers (ecs.h, core.h,
sprites.h, path.h, map.h, vm.h).

Usage: svlua_difftest.py [--lua LUA] [--runner RUNNER] [-v] [PROGRAM.lua...]
LUA defaults to $SERVAL_LUA32: a lua binary built with LUA_32BITS
(tools/setup-dev.sh --with-lua32 builds one). Without it the test is skipped
(exit status 77, CTest's SKIP_RETURN_CODE). RUNNER defaults to
$SERVAL_SVLUA_RUNNER, then build/host/tests/svlua_runner. Exit status 1 if a
program differs or fails.
"""

import argparse
import glob
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svlua  # noqa: E402
import svm  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
PROGRAMS = os.path.join(ROOT, "tests", "svlua", "diff")
STUB = os.path.join(ROOT, "tests", "svlua", "stub.lua")
HEADERS = [os.path.join(ROOT, "include", "serval", name)
           for name in ("ecs.h", "core.h", "sprites.h", "path.h", "map.h")]
SKIP = 77

# The engine properties in VM_P_* order (the runner's and the stub's), and
# their types.
PROPS = ("x", "y", "vx", "vy", "sprite", "frame", "flags", "angle", "depth", "scale", "body_w",
         "body_h", "tags", "anim_time", "anim_step")
FIXED_PROPS = ("x", "y", "vx", "vy", "scale")
SYS_ARITY = {"PSG_PLAY": 1, "MUSIC_PLAY": 1, "MUSIC_STOP": 0, "MUSIC_PAUSE": 0,
             "MUSIC_RESUME": 0, "CAMERA_SET": 2, "TEXT_PRINT": 2, "RANDOM_RANGE": 2,
             "BUTTON_DOWN": 1, "BUTTON_PRESSED": 1, "BRIGHTNESS": 1, "PATH_START": 3,
             "TEXT_PRINT_NUMBER": 4, "PATH_STOP": 1}  # TEXT_PRINT: col, row, then its text


class DiffError(Exception):
    pass


# --- A program and how to run it ----------------------------------------------


class Settings:
    def __init__(self):
        self.frames = 1
        self.starts = []  # object names
        self.attaches = []  # (object name, x, y)
        self.buttons = []  # (frame, mask, length)
        self.collides = []  # (object, other)
        self.movement = False
        self.seed = None
        self.prints = "all"
        self.tolerance = 1 / 256
        self.warnings = False


def settings_of(text, constants):
    s = Settings()
    for number, line in enumerate(text.splitlines(), 1):
        if not line.startswith("-- diff:"):
            continue
        for item in line[len("-- diff:"):].split():
            key, _, value = item.partition("=")
            try:
                if key == "frames":
                    s.frames = int(value)
                elif key == "start":
                    s.starts.append(value)
                elif key == "attach":
                    name, _, at = value.partition("@")
                    x, y = (int(v) for v in at.split(",")) if at else (0, 0)
                    s.attaches.append((name, x, y))
                elif key == "buttons":
                    parts = value.split(":")
                    mask = 0
                    for b in parts[1].split("|"):
                        mask |= int(b, 0) if b[0].isdigit() else constants[b]
                    s.buttons.append((int(parts[0]), mask, int(parts[2]) if len(parts) > 2 else 1))
                elif key == "collide":
                    obj, other = value.split(":")
                    s.collides.append((obj, other))
                elif key == "movement":
                    s.movement = True
                elif key == "seed":
                    s.seed = int(value, 0)
                elif key == "print":
                    s.prints = value if value in ("all", "last") else [int(f) for f in
                                                                       value.split(",")]
                elif key == "tolerance":
                    s.tolerance = float(value)
                elif key == "warnings" and value == "allowed":
                    s.warnings = True
                else:
                    raise ValueError(f"unknown setting {item!r}")
            except (ValueError, KeyError, IndexError) as e:
                raise DiffError(f"line {number}: bad setting {item!r}: {e}") from None
    return s


def lua_value(value):
    """A Python value as Lua source."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, str):
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(value, dict):
        return "{" + ", ".join(f"[{lua_value(k)}] = {lua_value(v)}" for k, v in value.items()) + "}"
    return "{" + ", ".join(lua_value(v) for v in value) + "}"


# --- The two runs ---------------------------------------------------------------


class Program:
    """A compiled program: the listing, the blob, and the names and types
    both sides print by."""

    def __init__(self, path, headers, vm):
        self.path = path
        self.name = os.path.basename(path)
        with open(path, encoding="utf-8") as f:
            self.text = f.read()
        constants = headers.constants()
        self.settings = settings_of(self.text, constants)
        try:
            compiled = svlua.compile_program(self.text, path)
        except svlua.CompileError as e:
            raise DiffError(f"doesn't compile: {e}") from None
        self.warnings = compiled.warnings
        p = compiled.program
        try:
            self.assembled = svm.assemble(compiled.listing, vm, headers, self.name)
        except svm.SvmError as e:
            raise DiffError(f"doesn't assemble:\n{e}") from None
        self.objects = [o.name for o in p.objects]  # Lua names, by number
        self.globals = [(g.name, g.ty, g.is_local) for g in p.globals]
        self.ram_arrays = [(a.name, a.elem or "integer", a.is_local)
                           for a in p.arrays if not a.rom]
        self.array_numbers = [k for k, a in enumerate(p.arrays) if not a.rom]
        self.fields = [(f.name, f.ty or "integer")
                       for f in sorted(p.fields.values(), key=lambda f: f.slot)]
        missing = [name for name in p.headers if name not in constants]
        if missing:
            raise DiffError(f"no header defines {', '.join(missing)}")
        self.constants = {name: constants[name] for name in p.headers}

    def object_number(self, name):
        if name not in self.objects:
            raise DiffError(f"no object {name}")
        return self.objects.index(name)


def run_vm(program, runner, tmp):
    s = program.settings
    blob = os.path.join(tmp, "program.bin")
    with open(blob, "wb") as f:
        f.write(program.assembled.blob)
    args = [runner, blob, "--frames", str(s.frames),
            "--print", s.prints if isinstance(s.prints, str) else ",".join(map(str, s.prints))]
    if s.seed is not None:
        args += ["--seed", str(s.seed)]
    for name in s.starts:
        args += ["--start", f"{program.object_number(name)}:ROOM_START"]
    for name, x, y in s.attaches:
        args += ["--attach", f"{program.object_number(name)}:{x}:{y}"]
    for frame, mask, length in s.buttons:
        args += ["--buttons", f"{frame}:{mask}:{length}"]
    for obj, other in s.collides:
        args += ["--collide", f"{program.object_number(obj)}:{program.object_number(other)}"]
    if s.movement:
        args.append("--movement")
    done = subprocess.run(args, capture_output=True, text=True, timeout=120)
    if done.returncode != 0:
        raise DiffError(f"svlua_runner failed ({done.returncode}):\n{done.stderr}")
    return done.stdout, done.stderr


def run_lua(program, lua, engine, tmp):
    s = program.settings
    config = {
        "constants": program.constants,
        "engine": engine,
        "globals": [[name, is_local] for name, _, is_local in program.globals],
        "arrays": [[name, is_local] for name, _, is_local in program.ram_arrays],
        "fields": [[name, ty] for name, ty in program.fields],
        "frames": s.frames,
        "starts": [[program.object_number(n), "room_start"] for n in s.starts],
        "attaches": [[program.object_number(n), x, y] for n, x, y in s.attaches],
        "buttons": [list(b) for b in s.buttons],
        "movement": s.movement,
        "collides": [[program.object_number(a), program.object_number(b)]
                     for a, b in s.collides],
        "prints": s.prints if isinstance(s.prints, str) else {f: True for f in s.prints},
    }
    if s.seed is not None:
        config["seed"] = s.seed
    path = os.path.join(tmp, "config.lua")
    with open(path, "w", encoding="utf-8") as f:
        f.write("return " + lua_value(config) + "\n")
    done = subprocess.run([lua, STUB, path, program.path], capture_output=True, text=True,
                          timeout=120)
    if done.returncode != 0:
        raise DiffError(f"Lua failed ({done.returncode}):\n{done.stderr}")
    return done.stdout


# --- Reading and comparing --------------------------------------------------------


def from_cell(cell, ty):
    """A VM cell as the value Lua would have."""
    if ty == "fixed":
        return cell / 256
    if ty == "boolean":
        return {0: False, 1: True}.get(cell, ("not a boolean", cell))
    return cell


def from_lua(text, ty):
    """A value the stub printed, checked against the compiler's type."""
    if text in ("true", "false"):
        value = text == "true"
        return value if ty == "boolean" else ("a boolean where Lua should have", ty, value)
    if text.startswith(("0x", "-0x")):
        value = float.fromhex(text)
        return value if ty == "fixed" else ("a float where Lua should have", ty, value)
    value = int(text)
    if ty == "boolean":
        return ("an integer where Lua should have a boolean", value)
    return float(value) if ty == "fixed" else value


class Record:
    """One side's run: calls per frame, and the state at printed frames,
    keyed (frame, what, name) -> (type, value)."""

    def __init__(self):
        self.calls = {}  # frame -> [call]
        self.state = {}  # (frame, kind, name) -> (type, value)


def read_vm(program, output):
    r = Record()
    frame = 0
    globals_ = program.globals
    arrays = dict(zip(program.array_numbers, program.ram_arrays))
    warnings = None
    for line in output.splitlines():
        head, _, rest = line.partition(" ")
        if head == "frame":
            frame = int(rest)
            r.calls[frame] = []
        elif head == "call":
            words = rest.split(" ", 5)
            name = VM.sys_names[int(words[0])]
            args = tuple(int(w) for w in words[1:1 + SYS_ARITY[name]])
            if name == "TEXT_PRINT":
                args += (words[5][1:-1],)
            r.calls[frame].append((name,) + args)
        elif head == "global":
            index, cell = (int(w) for w in rest.split())
            name, ty, _ = globals_[index]
            r.state[frame, "global", name] = (ty, from_cell(cell, ty))
        elif head == "array":
            words = rest.split()
            name, ty, _ = arrays[int(words[0])]
            for k, cell in enumerate(words[1:], 1):
                r.state[frame, "array", f"{name}[{k}]"] = (ty, from_cell(int(cell), ty))
        elif head == "entity":
            words = [int(w) for w in rest.split()]
            handle, obj = words[0], program.objects[words[1]]
            r.state[frame, "entity", f"{handle} object"] = ("object", obj)
            for prop, cell in zip(PROPS, words[2:17]):
                ty = "fixed" if prop in FIXED_PROPS else "integer"
                r.state[frame, "entity", f"{handle}.{prop}"] = (ty, from_cell(cell, ty))
            for (field, ty), cell in zip(program.fields, words[17:]):
                r.state[frame, "entity", f"{handle}.{field}"] = (ty, from_cell(cell, ty))
        elif head == "warnings":
            warnings = int(rest)
    return r, warnings


def read_lua(program, output):
    r = Record()
    frame = 0
    types = {name: ty for name, ty, _ in program.globals}
    array_types = {name: ty for name, ty, _ in program.ram_arrays}
    for line in output.splitlines():
        head, _, rest = line.partition(" ")
        if head == "frame":
            frame = int(rest)
            r.calls[frame] = []
        elif head == "call":
            words = rest.split(" ", 3) if rest.startswith("TEXT_PRINT ") else rest.split()
            name = words[0]
            args = tuple(int(w) if not w.startswith('"') else w[1:-1] for w in words[1:])
            r.calls[frame].append((name,) + args)
        elif head == "global":
            name, value = rest.split()
            r.state[frame, "global", name] = (types[name], from_lua(value, types[name]))
        elif head == "array":
            words = rest.split()
            ty = array_types[words[0]]
            for k, value in enumerate(words[1:], 1):
                r.state[frame, "array", f"{words[0]}[{k}]"] = (ty, from_lua(value, ty))
        elif head == "entity":
            words = rest.split()
            handle, obj = int(words[0]), program.objects[int(words[1])]
            r.state[frame, "entity", f"{handle} object"] = ("object", obj)
            for prop, value in zip(PROPS, words[2:17]):
                ty = "fixed" if prop in FIXED_PROPS else "integer"
                r.state[frame, "entity", f"{handle}.{prop}"] = (ty, from_lua(value, ty))
            for (field, ty), value in zip(program.fields, words[17:]):
                r.state[frame, "entity", f"{handle}.{field}"] = (ty, from_lua(value, ty))
        else:
            raise DiffError(f"the stub printed {line!r}")
    return r


def compare(program, vm, lua):
    """The differences, as lines (empty: none)."""
    tolerance = program.settings.tolerance
    out = []
    for frame in sorted(set(vm.calls) | set(lua.calls)):
        a, b = vm.calls.get(frame, []), lua.calls.get(frame, [])
        if a != b:
            out.append(f"frame {frame}: engine calls differ:\n  VM:  {a}\n  Lua: {b}")
    for key in sorted(set(vm.state) | set(lua.state)):
        frame, kind, name = key
        if key not in lua.state or key not in vm.state:
            where = "the VM" if key in vm.state else "Lua"
            value = (vm.state.get(key) or lua.state.get(key))[1]
            out.append(f"frame {frame}: {kind} {name} only in {where} ({value!r})")
            continue
        ty, va = vm.state[key]
        _, vb = lua.state[key]
        if ty == "fixed" and isinstance(va, float) and isinstance(vb, float):
            same = abs(va - vb) <= tolerance
        else:
            same = va == vb and type(va) is type(vb)
        if not same:
            out.append(f"frame {frame}: {kind} {name}: VM {va!r}, Lua {vb!r}")
    return out


# --- Main ---------------------------------------------------------------------


VM = None


def engine_constants(headers):
    k = headers.constants()
    return {"C_POS": k["C_POS"], "C_VEL": k["C_VEL"], "C_SPR": k["C_SPR"],
            "C_MAPBODY": k["C_MAPBODY"], "MAX_ENT": k["MAX_ENT"],
            "VM_CONTEXTS": VM.names["VM_CONTEXTS"]}


def check_lua(lua):
    """The binary must be Lua 5.4 with 32-bit integers."""
    try:
        done = subprocess.run([lua, "-e", "io.write(_VERSION, ' ', math.maxinteger, ' ', "
                               "string.packsize('n'))"], capture_output=True, text=True,
                              timeout=30)
    except OSError as e:
        raise DiffError(f"{lua}: {e}") from None
    if done.stdout != "Lua 5.4 2147483647 4":
        raise DiffError(f"{lua} is not Lua 5.4 built with LUA_32BITS (it says "
                        f"{done.stdout or done.stderr!r}; tools/setup-dev.sh --with-lua32 "
                        "builds one)")


def main(argv=None):
    global VM
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("programs", nargs="*", metavar="PROGRAM.lua",
                        help="the programs (default: tests/svlua/diff/*.lua)")
    parser.add_argument("--lua", default=os.environ.get("SERVAL_LUA32"),
                        help="Lua 5.4 built with LUA_32BITS (default: $SERVAL_LUA32)")
    parser.add_argument("--runner", default=os.environ.get("SERVAL_SVLUA_RUNNER"),
                        help="svlua_runner (default: $SERVAL_SVLUA_RUNNER, then build/host)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="also show what each side printed when a program differs")
    args = parser.parse_args(argv)
    if not args.lua:
        print("svlua_difftest: skipped: SERVAL_LUA32 is not set (a Lua 5.4 built with "
              "LUA_32BITS; tools/setup-dev.sh --with-lua32 builds one)")
        return SKIP
    runner = args.runner or os.path.join(ROOT, "build", "host", "tests", "svlua_runner")
    if not os.path.exists(runner):
        print(f"svlua_difftest: no svlua_runner at {runner}: build the host preset",
              file=sys.stderr)
        return 1
    programs = args.programs or sorted(glob.glob(os.path.join(PROGRAMS, "*.lua")))
    if not programs:
        print(f"svlua_difftest: no programs in {PROGRAMS}", file=sys.stderr)
        return 1
    try:
        check_lua(args.lua)
        VM = svm.load_vm()
        headers = svm.HeaderNames(VM.names)
        for path in HEADERS:
            headers.load(path)
        headers.check()
    except (DiffError, svm.SvmError) as e:
        print(f"svlua_difftest: {e}", file=sys.stderr)
        return 1
    engine = engine_constants(headers)
    failed = 0
    for path in programs:
        name = os.path.basename(path)
        try:
            program = Program(path, headers, VM)
            with tempfile.TemporaryDirectory() as tmp:
                vm_out, vm_log = run_vm(program, runner, tmp)
                lua_out = run_lua(program, args.lua, engine, tmp)
            vm, warnings = read_vm(program, vm_out)
            lua = read_lua(program, lua_out)
            problems = compare(program, vm, lua)
            for _, line, column, message in program.warnings:
                problems.insert(0, f"{name}:{line}:{column}: compiler warning: {message}")
            if warnings and not program.settings.warnings:
                problems.insert(0, f"the VM run warned ({warnings}):\n{vm_log.rstrip()}")
            if not vm.state:
                problems.append("nothing was compared (no state printed)")
        except DiffError as e:
            problems, vm_out, lua_out = [str(e)], "", ""
        if problems:
            failed += 1
            print(f"FAIL {name}")
            for line in problems[:20]:
                print("  " + line.replace("\n", "\n  "))
            if len(problems) > 20:
                print(f"  ... and {len(problems) - 20} more")
            if args.verbose:
                print("--- VM ---\n" + vm_out + "--- Lua ---\n" + lua_out)
        else:
            values = sum(1 for key in vm.state)
            calls = sum(len(c) for c in vm.calls.values())
            print(f"ok   {name}: {program.settings.frames} frames, {values} values and "
                  f"{calls} engine calls agree")
    print(f"svlua_difftest: {len(programs) - failed} of {len(programs)} programs agree")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
