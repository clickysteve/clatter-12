# Handoff (2026-11-13, fifth session: hardware build, diagnostics, pattern banks)

CLATTER 12 0.0.7-exp. Part 1 (0.0.5): hardware build and diagnostics. Part 2 (0.0.6): pattern banks.
Review (0.0.7): checked against Plinky's real API header; three hardware-breaking faults fixed.

Read this, then docs/HARDWARE_AUDIT.md, HARDWARE_TESTING.md, TESTING.md and docs/DESIGN.md.

**Where it is.** CLATTER 12 0.0.5-exp. Part 1 of this session (its own commit): the hardware compile
errors fixed and checked at `-O3`/`-O2`/`-Os` with warnings as errors (`make m33`, `tools/hw_check.py`);
**static SRAM cut from 143 KB to 1.7 KB**, because Plinky's firmware leaves a panel only about 20 KB
(`tools/firmware_ram.py`, on firmware images Plinky's own pipeline built); seven diagnostic settings
pages and a richer log line. Audio
bit-identical to 0.0.4 (and 0.0.3 with spreading off). Not run in Plinky's emulator or on a device.

**What the next session should do first**
1. Plinky's IDE: paste `build/clatter_12.cpp` (481 KB), **Test**, then **Flash**: HARDWARE_TESTING.md.
   A link error naming RAM would mean the firmware-image analysis was wrong: report it with the
   firmware code. Stage 5's DSP and PEAK readings, and stage 8b's cycle log, decide the CPU question.
2. If PSRAM tables prove slow, the sine table (8 KB, read by every oscillator) is the first candidate
   for the panel object (103 KB of 128 used: 8 KB fits).

**Review (0.0.7, 2026-11-14): checked against Plinky's real header**, which CI can fetch
(`[api-audit]` in a commit message prints the declarations: `tools/api_excerpt.py`). Read
docs/HARDWARE_AUDIT.md section 0 first. In short: Plinky's `panel_t` is about 24 KB and counts against
the 128 KB limit, so the pattern bank lives in PSRAM (two areas; `bank` is a pointer) and nothing in
PSRAM is touched under the pattern lock (`stash`, `qbuf`, `lock_with_slot`, `put_slot`); loads go through
an overridden commit hook that never writes the engine tail (no more "hold"); `new_project()` handles
the blank slot; the engine is owned by the object that brought it up (`engine_ready`). Rule: every
decision about which slot is playing is made inside `seq_lock` (it syncs first).

**Part 2 (0.0.6, its own commit): pattern banks.** Sixteen slots (`bank`; in PSRAM since 0.0.7), the
slot playing edited in the engine as before; switching done by the engine on the step
(`c12_seq_queue`: NOW, BAR, END, STOP; ARCHITECTURE.md "Pattern switching"); the PATTERN overlay
(tap the lit page pad) and PERFORM row 11; chains; project version 3. Rules that matter when changing
it: every pattern write from the glue goes through `edit_begin`/`edit_commit`/`seq_cell`/
`seq_last_page` (the pattern lock, and `bank_sync` first); never put a `slot_t` or a pattern on the
stack (`make m33` fails over 1.5 KB of core-0 stack: use `work`); the engine reads Pocket's
sequencer state (`step`, `phase`, `interval`, `pass`, `live`, `patterns`) to switch on the step, so a
Pocket update must be re-checked against `seq_switch` and `test_perform`'s pattern tests.

**What changed in Part 1 and why**
- The engine's zero-initialised file-scope objects (LXR voices and parameters, Pocket's ring and
  patterns, CLATTER 12's converter and buses: 39 KB then, 41 KB with the queued pattern) are reached through pointers into a generated
  struct (`tools/amalgamate.py` RELOCATE) that the panel holds (`engine_ram`). Same code, same values;
  the tool fails if a relocated name is declared twice or has an initialiser. Adding a large static to
  the engine? Add its name to RELOCATE, or `make m33` fails its 8 KB budget.
- The engine's memory is the tail of the object; loads never write it (0.0.7, above).
- Plinky's server very likely compiles at `-O3` (binary comparison, docs/HARDWARE_AUDIT.md §1).

**How things connect** (as before, plus)
- `third_party/clatter-pocket` is a pinned, unmodified submodule. Never edit it here. `core/c12_engine.c`
  now calls `pocket_lxr.c`'s file-scope functions and data (`drain`, `blockHook`, `voiceData`, `addVoice`,
  `mixL`...) to run its block in stages: it only builds inside the amalgamation, after `pocket_lxr.c`.
  If the submodule moves, re-check `lxr_stage` against Pocket's `renderBlock` and run
  `tools/compare_builds.sh` against the last commit.
- `tests/golden.c` + `tools/compare_builds.sh`: any optimisation must compare bit-identical with
  `nospread`. `tools/hw_check.py` (in `make m33`) is the hardware build gate: keep it passing. `tools/compare_envelope.py` compares renders as sound (10 ms levels) when samples cannot.
- `tests/m33/profile.c` includes the generated engine to time each stage; its stage weights are the
  numbers in `stage_weight` (`c12_engine.c`), which only decide where a callback stops.

**Rules the owner set** (keep): never flash hardware or touch bootloaders; label every build
experimental; no hardware claim without a device; do not change CLATTER Web or Pocket; the sound is
the LXR's, never Plinky's synth renamed.

**Open questions for Plinky** (Discord): the upload size limit for Test and Flash (481 KB now; 483 KB
reached the compiler); how many pages a panel may have (CLATTER 12 has eight, and seven settings
pages); the heap size the firmware reserves and how much static SRAM a panel may count on (firmware
90bd leaves 20 292 B above `.bss`); the core-1 stack size; the real core-1 clock, whether output is
buffered more than one block, and what Plinky's own output chain costs after `on_dsp` returns
`false`; the server's exact compiler flags (`-O3` inferred); `on_commit_staged_load_audio_thread`'s
exact signature (CLATTER 12 does not override it); whether `MIX_PARAM_*` can be set from a panel
outside Plinky's slider widgets; the serialiser's limits (largest base64 field) and semantics;
firmware licence vs GPL-3.0 for prebuilt images.
