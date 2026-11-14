#!/usr/bin/env python3
"""api_excerpt.py HEADER [NAME...] - the declarations CLATTER 12 depends on, as Plinky publishes them.

Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only

sim/plinky_sim.h is CLATTER 12's own stand-in for the Plinky 12 panel API. tools/check_api.py
checks that the names it declares exist in Plinky's published bundle; this prints, for a short list
of names, the lines of the bundle that declare or document them, so their signatures and stated
behaviour can be compared with the stand-in by eye. Short excerpts only, for interoperability: the
bundle is Plinky's and is not redistributed (THIRD_PARTY_NOTICES.md).

Run by CI's `api-audit` job, only for commits whose message contains [api-audit]. With
`--request FILE` (tools/api_audit_request.txt in CI) the file's `--range A-B` and `--grep REGEX`
lines are printed instead of the default list; leave the file empty for the default.
"""
import re, sys

DEFAULT = [
    # project loads and the panel object's lifetime
    "on_commit_staged_load_audio_thread", "on_prepare_staged_load", "on_load_finished", "get_panel_shadow_state",
    "panel_memory_size", "on_setup_default_panel_state", "setup_default_panel_state", "on_panel_selected",
    "on_activate", "on_deactivate", "on_unload",
    # saving
    "define FIELD_BASE64", "define FIELD(", "define OBJECT_BEGIN", "serialise_psram_binary_footer", "saveload(",
    "struct serialiser_t", "bool reading",
    # audio and timing
    "time_us", "SAMPLE_FREQ", "PLINKY_DSP_RAM_FUNC", "on_dsp(", "struct mix_buffers_t", "get_dsp_load", "dsp_load",
    "underrun", "overrun", "cpu_usage",
    # clock and transport
    "get_clock_phase", "has_transport_just_started", "get_clock_source", "set_tempo_bpm",
    # pages
    "get_num_pages", "get_num_panel_settings_pages", "draw_system_style_settings_page", "scroll_to_page",
    # memory
    "get_psram_size", "temp_alloc", "Coding Constraints", "128 KB", "128KB", "128k", "stack",
]


def ranges(path, specs):
    """--range A-B ... : those lines; --grep REGEX ... : matching lines with two lines either side"""
    lines = open(path, errors="replace").read().split("\n")
    it = iter(specs)
    for kind in it:
        arg = next(it)
        if kind == "--range":
            a, b = (int(x) for x in arg.split("-"))
            print("==== lines %d-%d" % (a, b))
            for k in range(a - 1, min(b, len(lines))):
                print("%6d  %s" % (k + 1, lines[k][:240]))
        elif kind == "--grep":
            rx = re.compile(arg)
            hits = [i for i, l in enumerate(lines) if rx.search(l)]
            print("==== /%s/: %d line(s)" % (arg, len(hits)))
            last = -1
            for i in hits[:12]:
                for k in range(max(i - 2, last + 1), min(i + 3, len(lines))):
                    print("%6d  %s" % (k + 1, lines[k][:240]))
                    last = k
                print("        ...")


def main():
    path = sys.argv[1]
    # a request file: --range A-B and --grep REGEX arguments, one word per line
    if len(sys.argv) > 2 and sys.argv[2] == "--request":
        import os
        words = [l.strip() for l in open(sys.argv[3])] if os.path.exists(sys.argv[3]) else []
        words = [w for w in words if w and not w.startswith("#")]
        if words:
            return ranges(path, words)
        sys.argv[2:] = []
    if len(sys.argv) > 2 and sys.argv[2].startswith("--"):
        return ranges(path, sys.argv[2:])
    names = sys.argv[2:] or DEFAULT
    lines = open(path, errors="replace").read().split("\n")
    shown, budget = set(), 700
    for name in names:
        hits = [i for i, l in enumerate(lines) if name in l]
        print("==== %s: %d line(s)" % (name, len(hits)))
        for i in hits[:6]:
            lo = i
            while lo > 0 and i - lo < 8 and lines[lo - 1].strip().startswith(("//", "*", "/*")):
                lo -= 1
            hi = i
            while hi + 1 < len(lines) and hi - i < 10 and lines[hi].strip() and not re.search(r"[;{}]\s*(//.*)?$", lines[hi]):
                hi += 1
            for k in range(lo, hi + 1):
                if k in shown:
                    continue
                shown.add(k)
                print("%6d  %s" % (k + 1, lines[k][:220]))
                budget -= 1
                if budget <= 0:
                    print("==== (excerpt limit reached)")
                    return
            print("        ...")


if __name__ == "__main__":
    main()
