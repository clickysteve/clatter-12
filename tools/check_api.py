#!/usr/bin/env python3
"""check_api.py GLUE SIMHEADER - every Plinky name the panel glue uses must exist in the published API.

Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only

The simulator (sim/plinky_sim.h) is CLATTER 12's own stand-in for the Plinky panel API, so a name
invented there would compile and pass every host test and then fail in the real IDE. This check
takes each identifier the stand-in declares, keeps those the glue (panel/clatter12.cpp) actually
uses, and requires each one in the published header bundle (build/plinky_library.h, fetched by
tools/fetch-plinky-headers.sh), printing the published declaration beside the stand-in's for review.
Without the bundle (offline) it says so and exits 0: it cannot check, and must not pretend to."""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
glue_path, sim_path = sys.argv[1], sys.argv[2]
bundle = os.path.join(ROOT, "build", "plinky_library.h")
if not os.path.exists(bundle):
    print("check_api: build/plinky_library.h missing (run tools/fetch-plinky-headers.sh); NOT CHECKED")
    sys.exit(0)

SIM_ONLY = {"sim", "sim_psram", "palette_init", "make_palette_entry"}
# Names the published bundle does not declare but Plinky's own example panels use, so the IDE
# provides them. Each with where it is used.
EXAMPLE_ONLY = {
    "SAMPLE_FREQ": "official example looper.cpp (docs/ide_api/llm.txt): (float)SAMPLE_FREQ * ...",
}
sim = open(sim_path).read()
sim_code = re.sub(r"//.*", "", sim)
declared = set()
declared |= set(re.findall(r"#define\s+([A-Za-z_]\w*)", sim_code))
declared |= set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", sim_code))
declared |= set(re.findall(r"(?:struct|typedef\s+struct)\s+([A-Za-z_]\w*)", sim_code))
declared |= set(re.findall(r"extern\s+[\w\s\*]+?\b([A-Za-z_]\w*)\s*(?:\[|;)", sim_code))
declared |= set(re.findall(r"\b(TRANSPORT_[A-Z_]+)\b", sim_code))
KEYWORDS = {"if", "for", "while", "return", "sizeof", "switch", "static", "inline", "void", "int", "bool",
            "char", "float", "unsigned", "const", "uint8_t", "uint16_t", "uint32_t", "int16_t", "int32_t",
            "memset", "memcpy", "printf", "snprintf", "fopen", "defined", "namespace", "virtual", "abs"}
declared -= KEYWORDS | SIM_ONLY

glue = re.sub(r"//.*", "", open(glue_path).read())
used = sorted(n for n in declared if re.search(r"\b%s\b" % re.escape(n), glue))
pub = open(bundle).read()
missing = []
for name in used:
    m = re.search(r"^[^\n]*\b%s\b[^\n]*$" % re.escape(name), pub, re.M)
    if not m:
        if name in EXAMPLE_ONLY:
            print("  %-34s (not in the bundle) %s" % (name, EXAMPLE_ONLY[name]))
            continue
        missing.append(name)
        continue
    print("  %-34s %s" % (name, m.group(0).strip()[:110]))
if missing:
    print("check_api: used by the glue, declared by the simulator, NOT in the published API: %s" % ", ".join(missing))
    sys.exit(1)
print("check_api: %d Plinky API names used by the panel, all present in the published headers" % len(used))
