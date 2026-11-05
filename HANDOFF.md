# Handoff (2026-11-05, fourth session: performance and hardware readiness)

Read this, then HARDWARE_TESTING.md, TESTING.md ("CPU"), docs/DESIGN.md and ROADMAP.md.

**Where it is.** CLATTER 12 0.0.4-exp: the same instrument as 0.0.3 (seven pages, Plinky's save page,
TEMPO on every page, CLATTER 12's delay and reverb, ratchets, rolls, PERFORM, REPEAT, scenes), made
cheaper and more even to run. No musical features, no interface changes. Everything passes on the host
(`make`: resampler 13, engine 37, perform 77, authentic 21, panel 201; `make test-wasm`: 33), compiles
and link-checks for the Cortex-M33 (`make m33`), runs in QEMU (`make m33-bench`, `make m33-profile`),
and renders the same audio as 0.0.3 sample for sample with spreading off
(`sh tools/compare_builds.sh d854eda nospread`, also in CI). Not run in Plinky's emulator or on a
Plinky 12; nothing has been flashed.

**What changed and why** (TESTING.md has the figures)
- The busiest callbacks used to render two whole LXR blocks. The engine now runs an LXR block as twelve
  stages (Pocket's calls, in order) and, after a callback's output, starts on the next block up to the
  callback's average share. QEMU, delay and reverb on, all tracks: busiest block 304 600 → 218 400
  instructions (102 % → 73 % of 150 MHz at one cycle each); worst case 358 400 → 251 900.
  `c12_engine_set_spread(0)` gives the old timing exactly; with it on, a control change can reach the
  engine one LXR block (1.45 ms) later, never more (tested).
- Converter bit-identical and a third cheaper; Pocket's unused Q23 stage gone; FX setters only when
  their own values move; delay lines interleaved, effects idling separately.
- The reverb's 26 KB of lines moved from PSRAM into the panel object (SRAM; the last member, kept out
  of `on_prepare_staged_load`'s copy, so a load silences the reverb tail). The delay's 256 KB stay in
  PSRAM as one sequential stream.

**What the next session should do first**
1. Plinky's IDE: paste `build/clatter_12.cpp` (483 KB), **Test**; HARDWARE_TESTING.md stage 0. If the
   IDE refuses the size, ask Plinky (Test has taken 306 KB; ~200 KB is reported for Flash; lossless
   compaction cannot reach 200 KB).
2. On a device, HARDWARE_TESTING.md stages 1-10, recording the DSP page and the log line at each.
   Stage 7 (delay and reverb, all tracks) is the figure this release was optimised for.
3. If the load is too high: `clatter_12_sram.cpp` first; then ROADMAP.md §1.

**How things connect** (as before, plus)
- `third_party/clatter-pocket` is a pinned, unmodified submodule. Never edit it here. `core/c12_engine.c`
  now calls `pocket_lxr.c`'s file-scope functions and data (`drain`, `blockHook`, `voiceData`, `addVoice`,
  `mixL`...) to run its block in stages: it only builds inside the amalgamation, after `pocket_lxr.c`.
  If the submodule moves, re-check `lxr_stage` against Pocket's `renderBlock` and run
  `tools/compare_builds.sh` against the last commit.
- `tests/golden.c` + `tools/compare_builds.sh`: any optimisation must compare bit-identical with
  `nospread`. `tools/compare_envelope.py` compares renders as sound (10 ms levels) when samples cannot.
- `tests/m33/profile.c` includes the generated engine to time each stage; its stage weights are the
  numbers in `stage_weight` (`c12_engine.c`), which only decide where a callback stops.

**Rules the owner set** (keep): never flash hardware or touch bootloaders; label every build
experimental; no hardware claim without a device; do not change CLATTER Web or Pocket; the sound is
the LXR's, never Plinky's synth renamed.

**Open questions for Plinky** (Discord): the upload size limit for Test and Flash (483 KB); how many
pages a panel may have (CLATTER 12 has eight); free static SRAM for panels (40 KB + 100 KB tables); the
real core-1 clock, whether output is buffered more than one block, and what Plinky's own output chain
costs after `on_dsp` returns `false`; the server's compiler flags (`-Os` would cost about 20 % more
instructions); whether `MIX_PARAM_*` can be set from a panel outside Plinky's slider widgets; the
serialiser's exact semantics (does a `false` from `on_serialise` abort a load?); firmware licence vs
GPL-3.0 for prebuilt images.
