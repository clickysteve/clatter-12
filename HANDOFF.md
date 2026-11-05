# Handoff (2026-11-05, third session)

Read this, then docs/DESIGN.md, docs/AUDIT.md, TESTING.md ("CPU") and ROADMAP.md.

**Where it is.** CLATTER 12 0.0.3-exp is a seven-page instrument (SEQ, SOUND, GEN, MIX, FX, PERFORM,
REPEAT, plus Plinky's save page) with a TEMPO pad on every page, its own delay and reverb (FX: SPACE),
ratchets with velocity curves and a minimum stroke gap, rolls (flam and drag played ahead of the step,
double, triple, accelerating, decelerating), momentary performance controls with LATCH and COMMIT,
note repeat and sequencer beat repeat, and eight scenes recalled at the bar. Everything passes on the
host (`make`: engine 37, perform 66, authentic 21, panel 201; `make test-wasm`: 33), compiles and
link-checks for the Cortex-M33 (`make m33`) and runs in QEMU (`make m33-bench`). Not run in Plinky's
emulator or on a Plinky 12; nothing has been flashed.

**What the next session should do first**
1. Paste `build/clatter_12.cpp` into Plinky's IDE, **Test**: every page, a save to a slot and its load,
   page scrolling with the side buttons. The save page and the serialiser have only met the simulator's
   stand-in, written from published panels (`sim/plinky_sim.h`).
2. On a device: the DSP page and the log line (`c12 dsp avg … (lxr … mix … rs …)`). QEMU's instruction
   counts say the default engine needs at least 56 % of core 1 on average and 82 % in its busiest blocks
   at 150 MHz; CLATTER 12's delay and reverb add about 7 % when used (their lines are in PSRAM: not
   measured anywhere) and take the busiest blocks past the budget; all three LXR effects add up to 17 %
   more. This decides everything; ROADMAP.md §1 and §7 have the order of things to try (FX ENGINE:
   PLINKY costs the panel nothing).
3. If the source is too large for the IDE's Flash upload (474 KB now), ask Plinky, then compact.
4. Check that eight pages scroll on the device (Plinky's canvas size is not published; the simulator
   allows eight).

**How things connect**
- `third_party/clatter-pocket` is a pinned, unmodified submodule: the LXR engine, sequencer, generator,
  kit designer. Never edit it here. CLATTER 12 extends it from outside: its own mix of Pocket's voice
  blocks, ratchets in cell bits Pocket ignores, the LXR FX files added to the amalgamation.
- `core/`: `c12_engine.c` (mix, FX, limiter, sends, ratchets and rolls, the performance layer: fills,
  density, beat and note repeat, scenes, the trigger log, profile), `c12_space.c` (the delay and
  reverb), `c12_params.c` (the LXR parameter
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

**Open questions for Plinky** (Discord): upload size limit for Flash (474 KB); how many pages a panel
may have (CLATTER 12 now reports eight); whether `MIX_PARAM_*` can be set from a panel outside
Plinky's slider widgets (to put Plinky's delay time on the grid when FX ENGINE is PLINKY); free static SRAM for panels
(38 KB + 100 KB tables); the real core-1 clock and whether output is buffered more than one block; the
serialiser's exact semantics (does a `false` from `on_serialise` abort a load?); firmware licence vs
GPL-3.0 for prebuilt images.
