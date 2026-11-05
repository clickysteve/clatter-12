#!/usr/bin/env python3
"""
amalgamate.py - build CLATTER 12's single, #include-free C++ panel source.

Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only

A Plinky 12 custom panel is one C++ file with no #include directives: the Plinky IDE injects
its own headers in front of it and compiles it as C++ for the RP2350's Cortex-M33 (and for
WebAssembly, for the in-browser emulator). CLATTER's synthesis is the LXR-02 firmware's C code,
reused unchanged through CLATTER Pocket (third_party/clatter-pocket). This tool turns those C
files, plus CLATTER 12's own portable core (core/), into one block of C++:

  1. One unity translation unit (#include of every .c, in a fixed order) is preprocessed as C
     with -nostdinc against stubs/sysinc, so every #include, #define and #if is resolved now and
     nothing of the C library is pulled in (the IDE supplies it).
  2. The preprocessed text is wrapped in `namespace c12`, so none of the LXR's global names
     (mixer_init, sine_table, ...) can meet a Plinky symbol at link time, and C++ linkage keeps
     them apart from the firmware's C symbols.
  3. The LXR's tightly-coupled-memory section attributes become C12_LXR_CODE / C12_LXR_DATA,
     which panel/prelude.inc defines per build (flash, or Plinky's .time_critical SRAM sections).
  4. Plinky macros whose names also occur as identifiers in the engine are pushed, undefined and
     popped around the block (tools/plinky_macros.txt, from the published plinky_library.h).

The output is build/c12_engine.inc; tools/make_panel.py assembles it with the Plinky glue.
The upstream files are never modified: everything happens on the preprocessed copy.
"""
import argparse, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POCKET = os.path.join(ROOT, "third_party", "clatter-pocket")

LXR = "lxr/upstream/Core/Src"
# The LXR-02 DSP as CLATTER Pocket builds it without the FX bus (Makefile: LXR_NOFX), minus the
# tables when they are packed (as in Pocket's FM-1 build: PACKED_SRC).
LXR_DSP = [
    "Preset/initKit.c",
    "Soundengine/DSPAudio/1PoleLp.c",
    "Soundengine/DSPAudio/BufferTools.c",
    "Soundengine/DSPAudio/CymbalVoice.c",
    "Soundengine/DSPAudio/Decay.c",
    "Soundengine/DSPAudio/DrumVoice.c",
    "Soundengine/DSPAudio/HiHat.c",
    "Soundengine/DSPAudio/Oscillator.c",
    "Soundengine/DSPAudio/ResonantFilter.c",
    "Soundengine/DSPAudio/SlopeEg2.c",
    "Soundengine/DSPAudio/Snare.c",
    "Soundengine/DSPAudio/automationNode.c",
    "Soundengine/DSPAudio/distortion.c",
    "Soundengine/DSPAudio/dither.c",
    "Soundengine/DSPAudio/lfo.c",
    "Soundengine/DSPAudio/mixer.c",
    "Soundengine/DSPAudio/modulationNode.c",
    "Soundengine/DSPAudio/random.c",
    "Soundengine/DSPAudio/snapEg.c",
    "Soundengine/DSPAudio/squareRootLut.c",
    "Soundengine/DSPAudio/transientGenerator.c",
    # The LXR FX bus's master distortion, ring modulator and compressor, unchanged: CLATTER 12 runs
    # them itself on the tracks routed to its FX chain (core/c12_engine.c). The bus (FxBus.c) and
    # its delay, with 64 KB of half-float memory, stay out, as in Pocket's FM-1 build (fx_off.c).
    "Soundengine/DSPAudio/FX/Distortion.c",
    "Soundengine/DSPAudio/FX/Ringmod.c",
    "Soundengine/DSPAudio/FX/Compressor.c",
]
LXR_TABLES = ["Soundengine/DSPAudio/transientTables.c", "Soundengine/DSPAudio/wavetable.c"]
LXR_PARAM_ARRAY = ["Soundengine/MIDI/ParameterArray.c"]

POCKET_SRC = [
    "lxr/bridge/lxr_params.c",
    "engine/pocket_lxr.c",
    "engine/fx_off.c",
    "seq/pocket_seq.c",
    "seq/pocket_gen.c",
    "seq/pocket_kit.c",
]
POCKET_PACKED = ["engine/lxr_tables_packed.c", "engine/lxr_tables_unpack.c", "engine/pocket_param_table.c"]

CORE_SRC = ["core/c12_resampler.c", "core/c12_space.c", "core/c12_engine.c", "core/c12_params.c", "core/c12_kitgen.c", "core/c12_exports.c"]

INCLUDES = [
    os.path.join(ROOT, "core"),
    os.path.join(POCKET, "engine"),
    os.path.join(POCKET, "seq"),
    os.path.join(POCKET, "ui"),
    os.path.join(POCKET, "lxr/shim"),
    os.path.join(POCKET, "lxr/upstream/Core/Inc"),
]

SECTION_RE = re.compile(r'__attribute__\s*\(\(\s*section\s*\(\s*"\.(itcm_text|dtcm|dtcmz)"\s*\)\s*\)\)')


def sources(tables):
    files = [os.path.join(POCKET, LXR, f) for f in LXR_DSP]
    if tables == "raw":
        files += [os.path.join(POCKET, LXR, f) for f in LXR_TABLES + LXR_PARAM_ARRAY]
    files += [os.path.join(POCKET, f) for f in POCKET_SRC]
    if tables == "packed":
        files += [os.path.join(POCKET, f) for f in POCKET_PACKED]
    files += [os.path.join(ROOT, f) for f in CORE_SRC]
    return files


# Separate C translation units may reuse file-local names; one unity unit may not. Each entry
# renames a file's private names for the length of that file only (or ends a file's macro), so
# the code compiled is the same code with distinct spellings.
UNITY_AROUND = {
    # the master distortion's file-scope helpers (clamp, lerp, ...) are named as others are
    "lxr/upstream/Core/Src/Soundengine/DSPAudio/FX/Distortion.c": (
        "".join("#define %s fxdist_%s\n" % (n, n) for n in ["clamp", "lerp", "fnms", "diode", "foldback"]),
        "".join("#undef %s\n" % n for n in ["clamp", "lerp", "fnms", "diode", "foldback"])),
    # fx_off.c stands in for the FX setters the parameter handler calls; with the real compressor
    # and ring modulator in the build (above), its stand-ins take other names for its length.
    "engine/fx_off.c": ("#define Compressor_setRatio pocket_fxoff_Compressor_setRatio\n"
                        "#define Ringmod_setWave pocket_fxoff_Ringmod_setWave\n",
                        "#undef Compressor_setRatio\n#undef Ringmod_setWave\n"),
    "seq/pocket_seq.c": ("", "#undef CLOSED_HAT\n#undef OPEN_HAT\n"),
    # pocket_gen.c and pocket_kit.c each have a static chance() with different parameters: C++
    # would quietly overload them, C rejects them. Distinct names keep both languages honest.
    "seq/pocket_kit.c": ("#define chance kit_chance\n", "#undef chance\n"),
    "seq/pocket_gen.c": (
        "".join("#define %s GEN_%s\n" % (n, n) for n in
                ["KICK", "LOW_TOM", "HIGH_TOM", "SNARE", "CYMBAL", "CLOSED_HAT", "OPEN_HAT"]),
        "".join("#undef %s\n" % n for n in
                ["KICK", "LOW_TOM", "HIGH_TOM", "SNARE", "CYMBAL", "CLOSED_HAT", "OPEN_HAT"])),
}

# C-only constructs that C++ rejects, fixed in the preprocessed copy. Each pattern must match
# exactly the number of times given, so an upstream change cannot slip past unnoticed.
CXX_FIXES = [
    # K&R declaration (ResonantFilter.h); the definition takes one argument.
    (r"\bvoid SVF_init\(\);", "void SVF_init(ResonantFilter* filter);", 1),
    # The FX bus's delay memory (FxRamBuffer.h): declared, never defined or used with the bus off
    # (engine/fx_off.c), and __fp16 is a type GCC accepts for ARM only with -mfp16-format.
    (r"extern\s+C12_LXR_DATA\s+__fp16\s+FX_BUFFER\[[^\]]*\];", "", 1),
    # implicit conversion from void* (pocket_lxr.c, CRC-32)
    (r"const uint8_t\* p = data;", "const uint8_t* p = (const uint8_t*)data;", 1),
]
# With packed tables the arrays are written once at start-up (lxr_tables_unpack.c), which C
# tolerates against the firmware's `extern const` declarations and C++ does not.
PACKED_FIXES = [
    (r"extern const int16_t sine_table\[", "extern int16_t sine_table[", 1),
    (r"extern const int16_t (tri|saw|rec)Table\[", r"extern int16_t \1Table[", 3),
    (r"extern const int8_t transientData\[", "extern int8_t transientData[", 1),
    # ... and the unpacked tables (102 KB) live wherever the panel puts them (c12_tables_bind:
    # static SRAM, or PSRAM): the arrays become pointers to arrays, which every use indexes the
    # same way. Their sizes, taken with sizeof by the unpacker and its CRC check, become constants.
    (r"extern int16_t sine_table\[[^\]]+\];", "extern int16_t* sine_table;", 1),
    (r"extern int16_t (tri|saw|rec)Table\[11\]\[1024\];", r"extern int16_t (*\1Table)[1024];", 3),
    (r"extern int8_t transientData\[12\]\[2205\];", "extern int8_t (*transientData)[2205];", 1),
    (r"\nint16_t sine_table\[4097\];", "\nint16_t* sine_table;", 1),
    (r"\nint16_t (tri|saw|rec)Table\[11\]\[1024\];", r"\nint16_t (*\1Table)[1024];", 3),
    (r"\nint8_t transientData\[12\]\[2205\];", "\nint8_t (*transientData)[2205];", 1),
    (r"sizeof sine_table\b", "(4097 * sizeof(int16_t))", 2),
    (r"sizeof (tri|saw|rec)Table\b", "(11 * 1024 * sizeof(int16_t))", 5),
    (r"sizeof transientData\b", "(12 * 2205)", 2),
]


# ---- C99 array designators -> positional initialisers --------------------------------------------
# GCC's C++ front end (the likely Plinky compiler) rejects `[INDEX] = value` in initialisers
# ("sorry, unimplemented: non-trivial designated initializers"). Such lists are rewritten in index
# order, gaps filled with {} (value-initialised, as C leaves them zero). The index expressions
# (enum constants) are evaluated by compiling the C text with the host compiler, so the positions
# are the compiler's, not this script's guess.
DESIG_RE = re.compile(r"^\s*\[([^\]]+)\]\s*=\s*", re.S)


def split_list(s, i):
    """s[i] == '{'. Returns (elements, j) with s[j-1] == '}'. An element is (designator, value),
    value a string or a nested element list."""
    assert s[i] == "{"
    i += 1
    elems, cur, depth_p, start = [], [], 0, i
    items = []
    while True:
        c = s[i]
        if c in "\"'":
            q = c
            i += 1
            while s[i] != q:
                i += 2 if s[i] == "\\" else 1
            i += 1
            continue
        if c == "{" and depth_p == 0:
            nested, j = split_list(s, i)
            cur.append(nested)
            i = j
            continue
        if c in "([":
            depth_p += 1
        elif c in ")]":
            depth_p -= 1
        elif (c == "," or c == "}") and depth_p == 0:
            cur.append(s[start:i])
            items.append(cur)
            cur = []
            if c == "}":
                return [parse_item(it) for it in items if not (len(it) == 1 and isinstance(it[0], str) and not it[0].strip())], i + 1
            start = i + 1
            i += 1
            continue
        if not cur or isinstance(cur[-1], list):
            if not cur:
                start = start
        i += 1
        if cur and isinstance(cur[-1], list):
            start = i


def parse_item(parts):
    """parts: text pieces and nested lists in order, e.g. ['[K] = ', [..], '']"""
    texts = [p for p in parts if isinstance(p, str)]
    lists = [p for p in parts if isinstance(p, list)]
    head = texts[0] if texts else ""
    des = None
    m = DESIG_RE.match(head)
    if m:
        des = m.group(1).strip()
        head = head[m.end():]
    if lists:
        return (des, lists[0])
    return (des, head.strip())


def has_designator(elems):
    return any(d is not None or (isinstance(v, list) and has_designator(v)) for d, v in elems)


def collect(elems, out):
    for d, v in elems:
        if d is not None:
            out.add(d)
        if isinstance(v, list):
            collect(v, out)


def render(elems, values):
    if not has_designator(elems):
        return "{ " + ", ".join(render(v, values) if isinstance(v, list) else v for d, v in elems) + " }"
    slots, idx = {}, 0
    for d, v in elems:
        if d is not None:
            idx = values[d]
        slots[idx] = render(v, values) if isinstance(v, list) else v
        idx += 1
    top = max(slots) if slots else -1
    return "{ " + ", ".join(slots.get(k, "{}") for k in range(top + 1)) + " }"


def find_designated(text):
    """(start, end, elems) of every brace initialiser containing an array designator."""
    found, i = [], 0
    for m in re.finditer(r"=\s*\{", text):
        b = m.end() - 1
        if b < i:
            continue
        elems, e = split_list(text, b)
        if has_designator(elems):
            found.append((b, e, elems))
            i = e
    return found


def rewrite_designators(text, cc):
    found = find_designated(text)
    if not found:
        return text, 0
    exprs = set()
    for _, _, elems in found:
        collect(elems, exprs)
    exprs = sorted(exprs)
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "eval.c")
        exe = os.path.join(d, "eval")
        with open(src, "w") as f:
            f.write(text.replace("C12_LXR_CODE", "").replace("C12_LXR_DATA", ""))
            f.write("\nint main(void) {\n")
            for e in exprs:
                f.write('  printf("%%ld\\n", (long)(%s));\n' % e)
            f.write("  return 0;\n}\n")
        cmd = [cc, "-std=gnu11", "-w", "-O0"]
        for h in ["stdint.h", "stddef.h", "string.h", "stdlib.h", "stdio.h", "math.h", "assert.h"]:
            cmd += ["-include", h]
        r = subprocess.run(cmd + [src, "-lm", "-o", exe], capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stderr[-3000:])
            sys.exit("amalgamate: could not evaluate designators")
        vals = subprocess.run([exe], capture_output=True, text=True, check=True).stdout.split()
    values = {e: int(v) for e, v in zip(exprs, vals)}
    out, last = [], 0
    for b, e, elems in found:
        out.append(text[last:b])
        out.append(render(elems, values))
        last = e
    out.append(text[last:])
    return "".join(out), len(found)


def apply_fixes(text, fixes):
    for pat, rep, count in fixes:
        text, n = re.subn(pat, rep, text)
        if n != count:
            sys.exit("amalgamate: fix %r matched %d times, expected %d" % (pat, n, count))
    return text


def unity_source(files):
    unity = ""
    for f in files:
        rel = os.path.relpath(f, POCKET)
        pre, post = UNITY_AROUND.get(rel, ("", ""))
        unity += pre + '#include "%s"\n' % f + post
    return unity


def preprocess(files, defines, cc):
    unity = unity_source(files)
    cmd = [cc, "-E", "-P", "-x", "c", "-std=gnu11", "-nostdinc",
           "-isystem", os.path.join(ROOT, "stubs", "sysinc"),
           # CLATTER Pocket's single-precision configuration (engine/single_precision.h): the
           # RP2350's FPU has no double precision, exactly the FM-1 case Pocket measured.
           "-include", os.path.join(POCKET, "engine", "single_precision.h"),
           # CLATTER's freestanding libc headers (lxr/shim/libc, for WebAssembly) are switched
           # off by their include guards: on the Plinky 12 the firmware's C library is used.
           "-DCLATTER_STRING_H", "-DCLATTER_STDIO_H", "-DCLATTER_STDLIB_H", "-DCLATTER_MATH_H",
           "-include", os.path.join(ROOT, "stubs", "sysinc", "math.h"),
           # C++17 has no `register`; the firmware uses it as a hint only.
           "-Dregister=",
           "-D_Static_assert=static_assert",
           "-DC12_AMALGAMATED=1",
           # Resolve compiler-specific branches as for GCC (pico-sdk's compiler), not the clang
           # that happens to run the preprocessor (pocket_lxr.h: minsize is clang-only).
           "-U__clang__"]
    for inc in INCLUDES:
        cmd += ["-I", inc]
    for d in defines:
        cmd += ["-D" + d]
    cmd += ["-"]
    r = subprocess.run(cmd, input=unity, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        sys.exit("amalgamate: preprocessing failed")
    return r.stdout


def postprocess(text):
    def section(m):
        return {"itcm_text": "C12_LXR_CODE", "dtcm": "C12_LXR_DATA", "dtcmz": "C12_LXR_DATA"}[m.group(1)]
    text = SECTION_RE.sub(section, text)
    # C allows `static const` objects to be redeclared `extern`; nothing here needs it, but a
    # stray implicit-int or K&R form would show up in the C++ compile checks, not silently here.
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text


# Unsuffixed floating literals are double in C, so `x * 0.5` on a float computes in double: on the
# RP2350 (single-precision FPU) a software call per operation, on the audio path (envelopes,
# filters, mixer). CLATTER Pocket's single-precision build makes them float with clang's
# -cl-single-precision-constant; Plinky's compiler flags cannot be set, so the text is changed
# instead: every unsuffixed floating literal outside string and character literals gets an `f`.
FLOAT_LIT_RE = re.compile(r"(?<![\w.])((?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?|\d+[eE][+-]?\d+)(?![\w.])")
STRING_RE = re.compile(r"\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'")


def single_precision_constants(text):
    out, last, n = [], 0, 0
    for m in STRING_RE.finditer(text):
        part, k = FLOAT_LIT_RE.subn(r"\1f", text[last:m.start()])
        out.append(part)
        out.append(m.group(0))
        n += k
        last = m.end()
    part, k = FLOAT_LIT_RE.subn(r"\1f", text[last:])
    out.append(part)
    return "".join(out), n + k


# Plinky's WebAssembly C library (the IDE's Test build) has no fmaf: the first Test of CLATTER 12
# failed to link on it. The LXR calls it per sample (filters, snare mix). CLATTER's own definition
# (lxr/shim/libc/math.h) everywhere but on an FPU with a fused multiply-add (the Cortex-M33's VFMA).
CXX_PRELUDE = """#if defined(__ARM_FEATURE_FMA)
static inline float fmaf(float a, float b, float c) { return __builtin_fmaf(a, b, c); }
#else
static inline float fmaf(float a, float b, float c) { return (float)((double)a * (double)b + (double)c); }
#endif
// The LXR FX bus's compressor (logf per sample) and fold distortion (fmod) need two functions no
// Plinky panel is known to link with: defined here, in single precision, so nothing depends on
// Plinky's C libraries having them. logf: mantissa in [0.71, 1.41), atanh series, |error| < 2e-7.
static inline float logf(float x) {
  if (!(x > 0.f)) return x == 0.f ? -__builtin_inff() : __builtin_nanf("");
  union { float f; unsigned u; } v;
  v.f = x;
  int e = (int)((v.u >> 23) & 255u) - 127;
  v.u = (v.u & 0x7fffffu) | 0x3f800000u;
  float m = v.f;
  if (m > 1.41421356f) { m *= 0.5f; e++; }
  const float t = (m - 1.f) / (m + 1.f), t2 = t * t;
  const float s = 2.f * t * (1.f + t2 * (0.333333343f + t2 * (0.2f + t2 * (0.142857149f + t2 * 0.111111112f))));
  return s + (float)e * 0.693147182f;
}
static inline float fmodf(float x, float y) { return y == 0.f ? __builtin_nanf("") : x - __builtin_truncf(x / y) * y; }
// the same in the names the FX code calls, on floats (CLATTER's single-precision build)
static inline float fmod(float x, float y) { return fmodf(x, y); }
static inline float fma(float a, float b, float c) { return fmaf(a, b, c); }
static inline float exp(float x) { return expf(x); }
"""


def plinky_macro_guard(text, names_file):
    if not os.path.exists(names_file):
        return "", ""
    names = [n.strip() for n in open(names_file) if n.strip() and not n.startswith("#")]
    idents = set(re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", text))
    hit = sorted(n for n in names if n in idents)
    push = "".join('#pragma push_macro("%s")\n#undef %s\n' % (n, n) for n in hit)
    pop = "".join('#pragma pop_macro("%s")\n' % n for n in hit)
    return push, pop


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tables", choices=["raw", "packed"], default="packed")
    ap.add_argument("--cc", default=os.environ.get("CC_PP", "clang"))
    ap.add_argument("-D", dest="defines", action="append", default=[])
    ap.add_argument("-o", dest="out", default=os.path.join(ROOT, "build", "c12_engine.inc"))
    ap.add_argument("--c-out", default=None,
                    help="also write the same preprocessed text as plain C (no namespace), for the "
                         "native C reference build that tests/equiv compares with")
    a = ap.parse_args()

    defines = list(a.defines)
    if a.tables == "packed":
        defines += ["POCKET_PACKED_TABLES", "POCKET_PARAM_TABLE"]
    text = postprocess(preprocess(sources(a.tables), defines, a.cc))
    text = apply_fixes(text, CXX_FIXES + (PACKED_FIXES if a.tables == "packed" else []))
    text, rewritten = rewrite_designators(text, a.cc)
    text, literals = single_precision_constants(text)
    push, pop = plinky_macro_guard(text, os.path.join(ROOT, "tools", "plinky_macros.txt"))

    rev = open(os.path.join(POCKET, "lxr", "CLATTER_REVISION")).read().strip() if os.path.exists(
        os.path.join(POCKET, "lxr", "CLATTER_REVISION")) else "?"
    pocket_rev = subprocess.run(["git", "-C", POCKET, "rev-parse", "HEAD"], capture_output=True,
                                text=True).stdout.strip() or "?"
    head = ("// ---- CLATTER 12 engine: generated by tools/amalgamate.py (tables: %s). Do not edit.\n"
            "// LXR-02 sound engine (Erica Synths, GPL-3.0-only + section 7 terms), MODIFIED for\n"
            "// CLATTER: via CLATTER Pocket %s (CLATTER %s). Not an Erica Synths product.\n"
            % (a.tables, pocket_rev[:12], rev[:12]))
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    if a.c_out:
        with open(a.c_out, "w") as f:
            f.write("/* generated by tools/amalgamate.py: the engine text, as C. Do not edit. */\n")
            f.write(text)
    with open(a.out, "w") as f:
        f.write(head)
        f.write(push)
        f.write("namespace c12 {\n")
        f.write(CXX_PRELUDE)
        f.write(text)
        f.write("\n} // namespace c12\n")
        f.write(pop)
    print("amalgamate: %s, %d bytes, %d lines" % (os.path.relpath(a.out, ROOT), os.path.getsize(a.out),
                                                  text.count("\n")))


if __name__ == "__main__":
    main()
