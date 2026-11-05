#!/usr/bin/env python3
"""check_link.py ENGINE_INC M33_OBJ... - what the panel needs from Plinky's libraries at link time.

Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only

Plinky's IDE links a panel against its own C libraries: newlib on the RP2350, a WebAssembly libc
for the in-browser emulator. Neither is published. The first IDE Test of CLATTER 12 failed to link
because the WebAssembly one has no fmaf. So:

  1. Every C math function the engine calls must be one Plinky's own or community panels are known
     to link with (KNOWN_LIBM, with where it was seen), or be defined in the engine itself.
  2. The Cortex-M33 objects may reference only those, mem*, and GCC's integer/float runtime.
  3. No function on the audio path may call software double-precision helpers (__aeabi_d*):
     each is a library call per operation on the RP2350's single-precision FPU.
"""
import re, shutil, subprocess, sys

# Seen in Plinky's official example panels (docs/ide_api/llm.txt) or published community panels
# (plinkysynth/community-panels), which build in Plinky's IDE for both targets.
KNOWN_LIBM = {
    "expf": "community ambiotica, mpe_test", "exp2f": "official examples", "sinf": "community panels",
    "cosf": "community panels", "sin": "community", "cos": "community", "tan": "community",
    "powf": "official worm.cpp", "sqrtf": "official looper.cpp", "sqrt": "community",
    "fabsf": "official examples",
}
LIBM_NAMES = {
    "expf", "exp", "exp2f", "exp2", "logf", "log", "log2f", "log10f", "powf", "pow", "sinf", "sin",
    "cosf", "cos", "tanf", "tan", "tanhf", "tanh", "atanf", "atan2f", "sqrtf", "sqrt", "fabsf",
    "fabs", "floorf", "floor", "ceilf", "ceil", "fmodf", "fmod", "fmaf", "fma", "roundf", "lroundf",
    "truncf", "fminf", "fmaxf", "ldexpf", "frexpf", "modff", "hypotf", "cbrtf", "asinf", "acosf",
}
# called through compiler builtins that never reach the library on these targets
BUILTIN_OK = {"fabsf", "fabs", "sqrtf", "floorf", "ceilf", "truncf"}
MEM = {"memcpy", "memmove", "memset", "memcmp"}
AUDIO_PATH = ["renderBlock", "mixer_calcNextSampleBlock", "slopeEg2_calc(", "SVF_calcBlockZDF",
              "SVF_calcBlockCheapZDF", "calcOnePoleAdaptiveVolume", "c12_engine_render", "c12_rs_process",
              "calcDrumVoiceSyncBlock", "Snare_calcSyncBlock", "Cymbal_calcSyncBlock", "HiHat_calcSyncBlock",
              "pocket_lxr_render_q23", "pocket_seq_block", "lxr_stage", "run_stage", "mix_voices", "mix_space",
              "mix_master", "c12_space_process", "c12_space_set_tempo", "fx_apply"]

failed = False
engine = open(sys.argv[1]).read()
text = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', engine)
called = {n for n in LIBM_NAMES if re.search(r"\b%s\s*\(" % n, text)}
defined = {n for n in called if re.search(r"\b(?:float|double)\s+%s\s*\([^;{]*\)\s*\{" % n, text)}
for n in sorted(called):
    if n in defined:
        print("  %-8s defined in the engine" % n)
    elif n in KNOWN_LIBM:
        print("  %-8s from Plinky's libm (seen: %s)" % (n, KNOWN_LIBM[n]))
    elif n in BUILTIN_OK:
        print("  %-8s compiler builtin" % n)
    else:
        print("  %-8s NOT known to exist in Plinky's libraries" % n)
        failed = True

import os
bundle = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", "plinky_library.h")
api = open(bundle).read() if os.path.exists(bundle) else None
if api is None:
    # Without the published bundle, the Plinky names the glue uses are those the simulator declares
    # (tools/check_api.py checks those against the bundle whenever it is available).
    sim_h = os.path.join(os.path.dirname(bundle), "..", "sim", "plinky_sim.h")
    api = open(sim_h).read()
    print("check_link: Plinky API bundle missing; API names checked against sim/plinky_sim.h instead")
LIBC = {"printf", "puts", "snprintf", "putchar"}
nm = shutil.which("arm-none-eabi-nm")
objdump = shutil.which("arm-none-eabi-objdump")
for obj in sys.argv[2:]:
    if not nm or not objdump:
        print("check_link: arm-none-eabi binutils missing; object checks NOT DONE")
        break
    und = subprocess.run([nm, "-u", "-C", obj], capture_output=True, text=True, check=True).stdout.split("\n")
    und = [u.strip()[2:].strip() if u.strip().startswith("U ") else u.strip() for u in und if u.strip()]

    def plinky_api(u):
        # the Plinky API itself (declared here by the simulator), or the simulator's own stand-in
        # for an inline API detail (sim_*): provided by the firmware on the device
        name = u.split("(")[0]
        return name.startswith("sim_") or (api is not None and re.search(r"\b%s\b" % re.escape(name), api))
    def cxx_runtime(u):
        return u.startswith("operator delete") or u.startswith("operator new")
    bad = [u for u in und if not (u.startswith("__aeabi_") or u in MEM or u in KNOWN_LIBM or u in BUILTIN_OK
                                  or u in LIBC or cxx_runtime(u) or plinky_api(u.split("::")[0]) or plinky_api(u))]
    if bad:
        print("check_link: %s needs unknown symbols: %s" % (obj, " ".join(bad)))
        failed = True
    dis = subprocess.run([objdump, "-dr", "-C", obj], capture_output=True, text=True, check=True).stdout
    fn, hot, cold = "", {}, {}
    for line in dis.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.*)>:$", line)
        if m:
            fn = m.group(1)
        elif re.search(r"R_ARM_THM_(CALL|JUMP24)\s+__aeabi_(d\w+|[fiul]\d?2d)\b", line):
            d = hot if any(h in fn for h in AUDIO_PATH) else cold
            d[fn] = d.get(fn, 0) + 1
    print("check_link: %s: undefined %s; soft-double calls: %d on the audio path, %d elsewhere (control code)"
          % (obj, " ".join(sorted(set(u.split("(")[0] for u in und if not u.startswith("__aeabi_")))), sum(hot.values()),
             sum(cold.values())))
    for f, c in sorted(hot.items(), key=lambda x: -x[1]):
        print("    audio path: %3d in %s" % (c, f[:100]))
        failed = True

if failed:
    sys.exit("check_link: FAILED")
print("check_link: ok")
