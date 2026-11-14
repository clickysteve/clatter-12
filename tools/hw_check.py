#!/usr/bin/env python3
"""hw_check.py VARIANT.cpp... - the generated panel as Plinky's hardware build would see it.

Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only

Plinky's "Flash" compiles a panel with ARM GCC on Plinky's server, treating warnings as errors, and
links it into the firmware. Neither the server's exact flags nor the firmware are published, so
this is the closest equivalent that can be built here, and what each part rests on:

  * GCC for the RP2350's Cortex-M33 with its single-precision FPU (-mcpu=cortex-m33 -mthumb
    -mfpu=fpv5-sp-d16 -mfloat-abi=hard), C++17, no exceptions or RTTI, as the published panels.
  * -O3: zone_plate's on_ui in Plinky's own firmware image (firmware 90bd, community-panels-built)
    is instruction for instruction GCC 13's -O3 output (35 of 54 instructions; the rest are its
    prologue and call relocations), and no -O2 or -Os sequence of it appears. -O2 and -Os are
    compiled too: if the server's flags differ, the panel still builds.
  * -Wall -Wextra -Werror -Wno-unused-parameter: the hardware log reported -Werror=missing-field-
    initializers and -Werror=sign-compare (-Wextra, -Wall); unused parameters are not fatal there
    (zone_plate's on_ui(int dt) ignores dt and builds in Plinky's pipeline).
  * The Plinky API comes from sim/plinky_sim.h, CLATTER 12's stand-in written from the published
    documentation (the real headers are injected by the IDE and not published as a set).
  * The link is a garbage-collected link of the panel alone (entry: the panel's construction, so
    its vtable and everything it reaches is kept; Plinky's symbols left undefined). It shows what
    the panel adds to the firmware. It does NOT show that the firmware links or runs.

Checks (any failure exits non-zero):
  - every variant compiles at each optimisation level with warnings as errors;
  - the panel object fits Plinky's 128 KB panel arena (also a static_assert in the glue);
  - static SRAM (.data + .bss + .time_critical code) of the default variant is within
    --sram-budget bytes. Firmware 90bd with Grid and a panel that has no static data leaves
    20 292 bytes between .bss and the end of SRAM, from which the C library's heap (2 KB by
    default in the pico-sdk) is reserved; tools/firmware_ram.py measures this from the images;
  - the deepest call chain from on_dsp (core 1) and from on_ui and the other core-0 hooks stays
    within --dsp-stack / --ui-stack bytes, from GCC's own call graph and frame sizes
    (-fcallgraph-info=su), with the engine's two indirect calls (its clock and the LXR block hook)
    followed to their targets (and any other indirect call on a chain failing the check). The core-1 stack is not published (the pico-sdk's default is 2 KB);
    the limits leave room for the firmware's own frames and interrupts. Frames over 256 bytes on
    those chains are listed (Plinky's guidance: keep stack objects under 128 bytes, 256 at most).
"""
import argparse, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPU = ["-mcpu=cortex-m33", "-mthumb", "-mfpu=fpv5-sp-d16", "-mfloat-abi=hard"]
BASE = ["-std=gnu++17"] + CPU + ["-ffunction-sections", "-fdata-sections", "-fno-exceptions", "-fno-rtti",
                                  "-Wno-psabi", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Werror"]
WRAP = """#include <new>
#include "plinky_sim.h"
#include "%s"
// as the firmware does: the panel object constructed in its arena (so its code is emitted, kept and counted)
extern "C" panel_t* c12_m33_construct(void* arena) { return new (arena) clatter12(); }
// the panel object's size, as the size of a symbol in a section of its own (not counted below)
extern "C" const char c12_panel_object_size[sizeof(clatter12)];
__attribute__((section(".c12meta"))) const char c12_panel_object_size[sizeof(clatter12)] = {0};
"""
# indirect calls GCC's call graph cannot follow, and their targets in the panel
# Indirect calls GCC's call graph cannot follow. The engine (namespace c12) has exactly three
# function pointers: its clock (the panel's clock_us), Pocket's block hook (block_hook) and the
# sequencer's emit (seq_emit); any engine function that calls indirectly is given all three as
# possible targets. An indirect call anywhere else on a chain fails the check.
ENGINE_TARGETS = ["clock_us", "block_hook", "seq_emit"]
ROOTS = {"core 1": ["on_dsp("], "core 0": ["on_ui(", "on_serialise(", "on_prepare_staged_load(", "on_load_finished("]}


def callgraph(ci):
    t = open(ci).read()
    nodes, edges = {}, {}
    for m in re.finditer(r'node: \{ title: "([^"]+)" label: "([^"]*)"', t):
        lab = m.group(2)
        b = re.search(r"\\n(\d+) bytes", lab)
        nodes[m.group(1)] = (lab.split("\\n")[0], int(b.group(1)) if b else 0)
    for m in re.finditer(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"', t):
        edges.setdefault(m.group(1), set()).add(m.group(2))
    by_short = {}
    for k, (lab, _) in nodes.items():
        short = re.sub(r"\(.*", "", lab).split("::")[-1].split(" ")[-1]
        by_short.setdefault(short, []).append(k)
    unknown = set()
    for k, (lab, _) in nodes.items():
        short = re.sub(r"\(.*", "", lab).split("::")[-1].split(" ")[-1]
        if "__indirect_call" in edges.get(k, ()):
            if " c12::" in " " + lab or lab.startswith("c12::"):
                for target in ENGINE_TARGETS:
                    edges[k].update(t for t in by_short.get(target, []) if t != k)
            else:
                unknown.add(k)
    return nodes, edges, unknown


def reach(edges, root):
    seen, todo = set(), [root]
    while todo:
        n = todo.pop()
        if n not in seen:
            seen.add(n)
            todo += list(edges.get(n, ()))
    return seen


def worst_chain(nodes, edges, root):
    memo = {}

    def depth(n, stack):
        if n in stack:
            return (0, [])
        if n in memo:
            return memo[n]
        best = (0, [])
        for c in edges.get(n, ()):
            d = depth(c, stack | {n})
            if d[0] > best[0]:
                best = d
        r = (nodes.get(n, ("?", 0))[1] + best[0], [n] + best[1])
        memo[n] = r
        return r
    return depth(root, frozenset())



def tool(name):
    t = shutil.which("arm-none-eabi-" + name)
    if not t:
        sys.exit("hw_check: arm-none-eabi-%s not found" % name)
    return t


def sections(elf):
    out = subprocess.run([tool("objdump"), "-h", elf], capture_output=True, text=True, check=True).stdout
    g = {"text": 0, "rodata": 0, "tc": 0, "data": 0, "bss": 0}
    for line in out.splitlines():
        m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-f]+)\s", line)
        if not m:
            continue
        n, sz = m.group(1), int(m.group(2), 16)
        if n.startswith(".time_critical"):
            g["tc"] += sz
        elif n.startswith(".text"):
            g["text"] += sz
        elif n.startswith(".rodata"):
            g["rodata"] += sz
        elif n.startswith(".data"):
            g["data"] += sz
        elif n.startswith(".bss"):
            g["bss"] += sz
    return g


def panel_bytes(elf):
    out = subprocess.run([tool("nm"), "-S", elf], capture_output=True, text=True).stdout
    m = re.search(r"^[0-9a-f]+ ([0-9a-f]+) \w c12_panel_object_size$", out, re.M)
    return int(m.group(1), 16) if m else None


def big_statics(elf, n=8):
    out = subprocess.run([tool("nm"), "-S", "-C", "--size-sort", elf], capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        p = line.split(None, 3)
        if len(p) == 4 and p[2] in "bBdD":
            rows.append((int(p[1], 16), p[3]))
    return rows[-n:][::-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("variants", nargs="+")
    ap.add_argument("--opt", default="O3,O2,Os")
    ap.add_argument("--sram-budget", type=int, default=8192,
                    help="static SRAM the default (first) variant may use; the firmware leaves about 18 KB")
    ap.add_argument("--dsp-stack", type=int, default=1024)
    ap.add_argument("--ui-stack", type=int, default=1536)
    ap.add_argument("--report", default=None, help="also write the report here")
    a = ap.parse_args()
    gxx = tool("g++")
    opts = a.opt.split(",")
    lines, fail = [], False

    def say(s):
        lines.append(s)
        print(s)

    ver = subprocess.run([gxx, "--version"], capture_output=True, text=True).stdout.split("\n")[0]
    say("hw_check: %s; flags %s -Wall -Wextra -Wno-unused-parameter -Werror" % (ver, " ".join(CPU)))
    with tempfile.TemporaryDirectory() as tmpdir:
        for vi, v in enumerate(a.variants):
            name = os.path.splitext(os.path.basename(v))[0]
            wrap = os.path.join(tmpdir, "hw_%s.cpp" % name)
            with open(wrap, "w") as f:
                f.write(WRAP % os.path.abspath(v))
            for o in opts:
                obj = os.path.join(tmpdir, "hw_%s_%s.o" % (name, o))
                cmd = [gxx] + BASE + ["-" + o, "-I" + os.path.join(ROOT, "sim"), "-I" + os.path.dirname(os.path.abspath(v)),
                                      "-fcallgraph-info=su", "-c", wrap, "-o", obj]
                r = subprocess.run(cmd, capture_output=True, text=True)
                if r.returncode != 0:
                    errs = [l for l in r.stderr.split("\n") if "error" in l]
                    say("  %-22s -%s  COMPILE FAILED (%d errors)" % (name, o, len(errs)))
                    for l in errs[:20]:
                        say("      " + l.strip()[:200])
                    fail = True
                    continue
                elf = obj[:-2] + ".elf"
                r = subprocess.run([gxx] + CPU + ["-nostdlib", "-Wl,--gc-sections", "-Wl,-e,c12_m33_construct",
                                                  "-Wl,--undefined=c12_panel_object_size",
                                                  "-Wl,--unresolved-symbols=ignore-all", "-Wl,--no-warn-rwx-segments",
                                                  obj, "-o", elf], capture_output=True, text=True)
                if r.returncode != 0:
                    say("  %-22s -%s  LINK FAILED: %s" % (name, o, r.stderr.strip()[:300]))
                    fail = True
                    continue
                g = sections(elf)
                sram = g["data"] + g["bss"] + g["tc"]
                pb = panel_bytes(elf)
                nodes, edges, unknown = callgraph(obj[:-2] + ".ci")
                chains, blind = {}, set()
                for core, pats in ROOTS.items():
                    for k, (lab, _) in nodes.items():
                        if "clatter12::" in lab and any(p in lab for p in pats):
                            d = worst_chain(nodes, edges, k)
                            if d[0] > chains.get(core, (0,))[0]:
                                chains[core] = d
                            blind |= reach(edges, k) & unknown
                # an indirect call this tool cannot follow would make the stack figures an undercount
                # (calls into Plinky's own API through its objects are direct; virtual calls on the
                # panel's base are the hooks themselves)
                blind = sorted(nodes[k][0] for k in blind if "panel_t::" not in nodes[k][0])
                if blind:
                    say("    FAIL: indirect calls not followed (outside the engine): " + "; ".join(b[:80] for b in blind[:5]))
                    fail = True
                say("  %-22s -%s  ok: flash %6.1f KB code + %5.1f KB const; static SRAM %5d B (.bss %d, .data %d, "
                    "RAM code %d); panel object %s; deepest stack: core 1 %d B, core 0 %d B"
                    % (name, o, g["text"] / 1024.0, g["rodata"] / 1024.0, sram, g["bss"], g["data"], g["tc"],
                       "%d B (%.1f of 128 KB)" % (pb, pb / 1024.0) if pb else "?",
                       chains.get("core 1", (0,))[0], chains.get("core 0", (0,))[0]))
                if pb is None or pb > 128 * 1024:
                    say("    FAIL: the panel object does not fit Plinky's 128 KB arena (or its size was not found)")
                    fail = True
                for core, limit in (("core 1", a.dsp_stack), ("core 0", a.ui_stack)):
                    deep, path = chains.get(core, (0, []))
                    big = ["%s %d" % (re.sub(r"\(.*", "", nodes[p][0]).split(" ")[-1], nodes[p][1]) for p in path
                           if p in nodes and nodes[p][1] > 256]
                    if deep > limit:
                        say("    FAIL: %s's deepest call chain %d B is over %d B: %s" % (core, deep, limit, " > ".join(
                            "%s %d" % (re.sub(r"\(.*", "", nodes[p][0]).split(" ")[-1], nodes[p][1]) for p in path if p in nodes)))
                        fail = True
                    elif big and vi == 0 and o == opts[0]:
                        say("    %s chain frames over 256 B: %s" % (core, ", ".join(big)))
                if vi == 0 and o == opts[0]:
                    if sram > a.sram_budget:
                        say("    FAIL: static SRAM %d B is over the budget of %d B" % (sram, a.sram_budget))
                        fail = True
                    say("    largest static objects: " + ", ".join("%s %d" % (n, s) for s, n in big_statics(elf)))
    say("hw_check: %s" % ("FAILED" if fail else "ok (a compile and a panel-only link; not the firmware, not a device)"))
    if a.report:
        with open(a.report, "w") as f:
            f.write("\n".join(lines) + "\n")
    sys.exit(1 if fail else 0)


if __name__ == "__main__":
    main()
