#!/usr/bin/env python3
"""m33_sizes.py OBJ... - section sizes of the Cortex-M33 objects `make m33` compiles.

Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only

Groups ELF sections the way the RP2350 (pico-sdk style linker scripts) would place them:
code and constants in flash (XIP), .time_critical.* code copied to SRAM, .data/.bss in SRAM.
These are compile-time sizes of CLATTER 12's own code; the Plinky firmware it would link into is
not available, so nothing here says whether it fits beside that firmware."""
import re, shutil, subprocess, sys

objdump = shutil.which("arm-none-eabi-objdump")
if not objdump:
    sys.exit("m33_sizes: arm-none-eabi-objdump not found")

for obj in sys.argv[1:]:
    out = subprocess.run([objdump, "-h", obj], capture_output=True, text=True, check=True).stdout
    groups = {"flash code (.text)": 0, "flash constants (.rodata)": 0, "SRAM code (.time_critical)": 0,
              "SRAM initialised (.data)": 0, "SRAM zeroed (.bss)": 0}
    for line in out.splitlines():
        m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-f]+)\s", line)
        if not m:
            continue
        name, size = m.group(1), int(m.group(2), 16)
        if name.startswith(".time_critical"):
            groups["SRAM code (.time_critical)"] += size
        elif name.startswith(".text"):
            groups["flash code (.text)"] += size
        elif name.startswith(".rodata"):
            groups["flash constants (.rodata)"] += size
        elif name.startswith(".data"):
            groups["SRAM initialised (.data)"] += size
        elif name.startswith(".bss"):
            groups["SRAM zeroed (.bss)"] += size
    print("m33: %s" % obj)
    for k, v in groups.items():
        print("  %-28s %8d bytes (%6.1f KB)" % (k, v, v / 1024.0))
