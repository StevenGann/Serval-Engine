#!/usr/bin/env python3
"""Checks what a ROM's link map shows of the debug link's agent
(docs/debug-link.md#what-debug-builds-add).

Usage: check_link_map.py ROM.map --agent on|off --vm on|off --scratch BYTES

--agent on (a debug build with SERVAL_LINK): the agent's core is linked
(serval_link_init, serval_link_poll, the mailbox in EWRAM). --agent off (a
release build, or SERVAL_LINK off): nothing of it is. --vm on (a game that
links the VM): the agent's VM part too, its scratch included; --vm off: none
of it, and none of the VM. Either way, the agent's files put nothing in IWRAM
(.iwram, .data, .bss): only ROM (.text, .rodata) and EWRAM (.sbss, .ewram).
"""

import argparse
import re
import sys

AGENT_OBJECTS = ("link.c.obj", "vm_link.c.obj", "link_gba.c.obj")


def sections(map_text):
    """(section, size, object) of every input section the link kept."""
    memory_map = map_text.split("Linker script and memory map", 1)[1]
    found = []
    for m in re.finditer(r"^ (\.\S+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)$", memory_map, re.M):
        found.append((m.group(1), int(m.group(3), 16), m.group(4)))
    return [s for s in found if s[1] > 0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("map")
    parser.add_argument("--agent", choices=("on", "off"), required=True)
    parser.add_argument("--vm", choices=("on", "off"), required=True)
    parser.add_argument("--scratch", type=int, default=32768)
    args = parser.parse_args()
    with open(args.map, encoding="utf-8", errors="replace") as f:
        kept = sections(f.read())
    problems = []

    def of(obj):
        return [s for s in kept if s[2].endswith("(" + obj + ")")]

    names = {s[0] for s in kept}
    agent = args.agent == "on"
    for symbol in (".text.serval_link_init", ".text.serval_link_poll"):
        if (symbol in names) != agent:
            problems.append(f"{symbol} is {'missing' if agent else 'linked'}")
    vm_part = of("vm_link.c.obj")
    vm = of("vm.c.obj")
    if args.vm == "on" and agent:
        if ".rodata.serval_link_vm_part" not in names:
            problems.append("the agent's VM part is missing")
        scratch = sum(size for name, size, _ in vm_part if name.startswith(".sbss"))
        if scratch < args.scratch:
            problems.append(f"vm_link.c's EWRAM is {scratch} bytes, less than the scratch's {args.scratch}")
    elif vm_part:
        problems.append("the agent's VM part is linked: " + ", ".join(s[0] for s in vm_part))
    if args.vm == "off" and vm:
        problems.append("the VM is linked: " + ", ".join(s[0] for s in vm))
    if not agent:
        for obj in AGENT_OBJECTS:
            if of(obj) and obj != "link.c.obj":
                problems.append(f"{obj} is linked: " + ", ".join(s[0] for s in of(obj)))
        if any(s[0].startswith((".sbss", ".rodata.serval_link")) for s in of("link.c.obj")):
            problems.append("link.c's agent is linked")
    for obj in AGENT_OBJECTS:
        for name, size, _ in of(obj):
            if name.startswith((".iwram", ".data", ".bss")):
                problems.append(f"{obj} puts {size} bytes in IWRAM ({name})")
    vm_link_rodata = [s for s in of("vm.c.obj") if s[0] == ".rodata.serval_link_vm"]
    if args.vm == "on" and agent and not vm_link_rodata:
        problems.append("vm.c's serval_link_vm is missing")
    if problems:
        sys.exit(f"{args.map}: " + "; ".join(problems))
    print(f"{args.map}: agent {args.agent}, VM part {args.vm}, no IWRAM: ok")


if __name__ == "__main__":
    main()
