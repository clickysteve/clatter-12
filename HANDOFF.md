# Handoff (2026-11-04)

State when the first Claude Code session ended; read this, then FEASIBILITY.md and ROADMAP.md.

**Where it is.** CLATTER 12 0.0.1-exp builds, passes every host test (`make`), compiles for the
Cortex-M33 (`make m33`), and **runs in Plinky's IDE emulator**. It has never run on a Plinky 12
and nothing has been flashed.

**What the next session should do first**
1. Ask the owner how it sounded and played in the emulator (touch, LEDs, audio): not yet recorded.
2. Fix clipping (17 samples in ~4 min of editing): headroom or soft limiter in `core/c12_engine.c`
   (`C12_MASTER_Q12` is -3 dB now). Re-run `make test`; the engine test checks pitch and level.
3. Make the DSP load readout work (it reads 0 in the emulator). It decides whether seven voices fit.
4. Then ROADMAP.md section 2 onward.

**How things connect**
- `third_party/clatter-pocket` is a pinned, unmodified submodule: the LXR engine, sequencer,
  generator and kit designer. Never edit it here; change it in its own repo and bump the pin.
- `tools/amalgamate.py` turns it plus `core/` into one `#include`-free C++ block;
  `tools/make_panel.py` adds `panel/clatter12.cpp` (the only Plinky-specific file). Paste
  `build/clatter_12.cpp` into https://plinky12.com/codeed.html. Variants: BUILDING.md.
- Every C-to-C++ rewrite in the amalgamator is count-checked, so a Pocket update fails loudly.
- `sim/` is our own stand-in for the Plinky API, written from the docs. It is not Plinky's
  emulator; `tools/check_api.py` checks its names against the published headers.
- Things Plinky's toolchain taught us (clang 21 for the emulator, Plinky's own libc): no `fmaf`
  there; `tools/check_link.py` guards the symbols. Plinky's real compiler flags are unknown.

**Rules the owner set** (keep): never flash hardware or touch bootloaders; label every build
experimental; no hardware claim without a device; do not change CLATTER Web or Pocket; the
sound is the LXR's, never Plinky's synth renamed.

**Open questions for Plinky** (Discord): upload size limit for Flash; free static SRAM for panels;
the real `SAMPLE_FREQ` and a usable block timer; firmware licence vs GPL-3.0 for prebuilt images.
