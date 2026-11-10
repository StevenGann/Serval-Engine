#!/usr/bin/env python3
"""Check the cartridge clock's driver against mGBA's clock (CTest rtc_probe_capture).

Runs the serval_rtc_probe ROM (tests/rom/rtc_probe_main.c) in the capture
tool built on mGBA's core (not in this repository; see CLAUDE.md), once with a
clock at 2024-02-29 12:34:56 that jumps a day ahead at frame 120, and once
without a clock. Exits 77 (skipped) when the capture tool is missing.

Usage: rtc_probe_capture.py --capture CAPTURE --rom serval_rtc_probe.gba --out DIR
"""

import argparse
import os
import subprocess
import sys


def run(capture, rom, prefix, *options):
    env = dict(os.environ, CAPTURE_LOG="1")
    result = subprocess.run([capture, rom, "200", prefix, *options, "nosave"], env=env,
                            capture_output=True, text=True, timeout=300, check=False)
    log = result.stdout + result.stderr
    errors = []
    if result.returncode != 0:
        errors.append(f"capture exited with {result.returncode}")
    errors += [f"a warning: {line}" for line in log.splitlines() if "serval:" in line]
    return log, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--capture", required=True)
    parser.add_argument("--rom", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    if not (os.path.isfile(args.capture) and os.access(args.capture, os.X_OK)):
        print(f"{args.capture}: no capture tool; skipped")
        return 77
    os.makedirs(args.out, exist_ok=True)

    errors = []
    log, problems = run(args.capture, args.rom, os.path.join(args.out, "clock"),
                        "rtc=762525296", "rtcstep=120:86400")
    print(log)
    errors += problems
    clock = [line for line in log.splitlines() if "rtc: present=" in line]
    first = "rtc: present=1 reset=0 2024-02-29 12:34:56 seconds=762525296"
    if len(clock) != 2:
        errors.append(f"with a clock: expected 2 'rtc: present=' lines, got {len(clock)}")
    else:
        if not clock[0].endswith(first):
            errors.append(f"with a clock, frame 1: expected '{first}', got '{clock[0]}'")
        if "rtc: present=1 reset=0 2024-03-01 12:34:5" not in clock[1]:
            errors.append(f"with a clock, frame 180: expected 2024-03-01 12:34:5x, got '{clock[1]}'")
    if "control=0" not in log or "control=1" in log:
        errors.append("with a clock: rtc_read() left the GPIO port open (control is not 0)")

    log, problems = run(args.capture, args.rom, os.path.join(args.out, "none"))
    print(log)
    errors += problems
    none = [line for line in log.splitlines() if "rtc: present=" in line]
    if len(none) != 2 or not all("rtc: present=0 reset=0" in line for line in none):
        errors.append(f"without a clock: expected 2 'rtc: present=0 reset=0' lines, got {none}")

    for error in errors:
        print(f"{args.rom}: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
