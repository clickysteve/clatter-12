# Handoff (2026-11-04, second session)

Read this, then docs/DESIGN.md, TESTING.md ("CPU") and ROADMAP.md.

**Where it is.** CLATTER 12 is now a five-page instrument (SEQ, SOUND, GEN, MIX, FX, plus Plinky's save
page). Everything passes on the host (`make`, `make test-wasm`), compiles and link-checks for the
Cortex-M33 (`make m33`), and runs in QEMU's Cortex-M33 model (`make m33-bench`). This version has not
run in Plinky's emulator or on a Plinky 12; nothing has been flashed.

**What the next session should do first**
1. Paste `build/clatter_12.cpp` into Plinky's IDE, **Test**: every page, a save to a slot and its load,
   page scrolling with the side buttons. The save page and the serialiser have only met the simulator's
   stand-in, written from published panels (`sim/plinky_sim.h`).
2. On a device: the DSP page and the log line (`c12 dsp avg … (lxr … mix … rs …)`). QEMU's instruction
   counts say the default engine needs at least 56 % of core 1 on average and 81 % in its busiest blocks
   at 150 MHz; all three LXR effects add up to 24 % more. This decides everything; ROADMAP.md §1 has the order of
   things to try.
3. If the source is too large for the IDE's Flash upload (403 KB now), ask Plinky, then compact.

**How things connect**
- `third_party/clatter-pocket` is a pinned, unmodified submodule: the LXR engine, sequencer, generator,
  kit designer. Never edit it here. CLATTER 12 extends it from outside: its own mix of Pocket's voice
  blocks, ratchets in cell bits Pocket ignores, the LXR FX files added to the amalgamation.
- `core/`: `c12_engine.c` (mix, FX, limiter, sends, ratchets, profile), `c12_params.c` (the LXR parameter
  table, from CLATTER Web's `lxrParams.ts`), `c12_kitgen.c` (kits, locks, mutation), `c12_exports.c`
  (macros the C++ glue cannot see after amalgamation).
- `panel/clatter12.cpp` is the only Plinky-specific file. Pages draw at `page * 16` (Plinky's canvas).
- `sim/` is our stand-in for the Plinky API. `sim/wasm_panel.cpp` + `sim/wasm/` build it with the panel
  for the project page; `sim/panel_help.h` is the pads' descriptions (the wasm test checks every lit pad
  has one: change it with the panel).
- `tools/check_link.py`: `logf` and `fmodf` are defined in the engine (Plinky's libraries may lack them).
- `tests/m33/`: the bare-metal QEMU bench.

**Rules the owner set** (keep): never flash hardware or touch bootloaders; label every build
experimental; no hardware claim without a device; do not change CLATTER Web or Pocket; the sound is
the LXR's, never Plinky's synth renamed.

**Open questions for Plinky** (Discord): upload size limit for Flash (403 KB); free static SRAM for panels
(38 KB + 100 KB tables); the real core-1 clock and whether output is buffered more than one block; the
serialiser's exact semantics (does a `false` from `on_serialise` abort a load?); firmware licence vs
GPL-3.0 for prebuilt images.
