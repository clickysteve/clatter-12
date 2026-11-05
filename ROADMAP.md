# Roadmap to CLATTER 12 v0.1

v0.1: a seven-voice LXR drum machine with a 16 × 16 touch sequencer, running on a Plinky 12.

## Done (host, simulator, cross-compile and QEMU only)

- 0.0.1-exp: LXR engine in one panel file; 44.14 → 32 kHz conversion; 16-step grid with pressure velocity;
  GEN / MUT / KIT / STYLE / UNDO / CLR; Plinky transport and tempo; DSP load page and log; first emulator run.
- 0.0.2-exp (docs/DESIGN.md):
  - [x] Pages on Plinky's convention: SEQ, SOUND, GEN, MIX, FX, SAVE; row 15 the same on all.
  - [x] Variation lanes removed; tap on / tap off / hold to select and edit; several held steps edited together.
  - [x] Conditions on steps (Pocket's: 1:2 … 4:4, 25/50/75 %), counted over whole passes.
  - [x] Ratchets 1, 2, 3, 4, 6, 8 strokes; even, accelerating, decelerating, random spacing; CLATTER Web's
        ratchet decay and roll velocity shapes.
  - [x] SOUND: every LXR parameter of a voice (CLATTER Web's table), a row each, three banks, fine ±, audition.
  - [x] Real kit generation: new kit, new sound, mutate kit or sound, wildness, amount, sound locks; undo/redo.
  - [x] Generator controls: style, density, complexity, mutation strength, track locks, one-track generate.
  - [x] Mixer in the audio path: level, pan, mute, solo, delay and reverb sends, master.
  - [x] FX: the LXR firmware's drive, ring modulator and compressor; bit reduction; the LXR's sample-rate
        reduction; output limiter (the emulator's clipping).
  - [x] Saving: `on_serialise`, one versioned, CRC-checked project; Plinky's save page.
  - [x] Undo/redo: 16 levels, edits of one kind merged.
  - [x] Swing on SEQ.
  - [x] Cortex-M33 build fixed for GCC 13 (`_sram` section conflict); sizes now include the panel.
  - [x] CPU estimate in QEMU's Cortex-M33 model (`make m33-bench`).
  - [x] The project page runs the panel itself (WebAssembly).
- 0.0.3-exp, performance and musicality (docs/DESIGN.md, docs/AUDIT.md):
  - [x] Audit of every page and control, and of the delay and reverb path (sends went only to Plinky's
        effects; nothing set them; the web demo's sends were silent).
  - [x] CLATTER 12's own delay (12 tempo divisions, feedback, filter, wet, ping-pong, freeze) and reverb
        (size, decay, damping, wet, freeze) on the FX page's SPACE bank; FX ENGINE switch to Plinky's.
  - [x] TEMPO on row 15 of every page: tap tempo; held, an overlay with the BPM, ±1/5/10, half and
        double time, presets, nudge, a tap row. One clock: Plinky's.
  - [x] Ratchets: tempo-synchronised 2-8 strokes, never closer than 12 ms; seven velocity curves.
  - [x] Rolls apart from ratchets: flam and drag (graces played ahead of the step), double and triple
        strokes, accelerating and decelerating rolls. Auditioned on edit.
  - [x] PERFORM: momentary mute, solo, kill groups, fills (CLATTER Web's), temporary mutation, density,
        stutter, delay and reverb throws, freezes; LATCH, RELEASE, COMMIT (undoable).
  - [x] REPEAT: note repeat by track with straight/triplet/dotted rates, pressure as velocity, rate or
        chance, curves; rolls by hand; sequencer-level beat repeat with shape and track mask.
  - [x] Scenes: eight mixer + FX snapshots, recalled at the next bar (sample-exact for the mixer).
  - [x] Project version 2 (step layout, FX, REPEAT settings, scenes); version-1 projects convert.
  - [x] Engine trigger log; 66 engine-level timing tests; 201 panel checks; QEMU scenarios for all of it.
- 0.0.4-exp, performance and hardware readiness (TESTING.md "CPU", HARDWARE_TESTING.md):
  - [x] Stage-by-stage QEMU profile (`make m33-profile`); bench scenarios for editing, kits, scenes,
        start/stop; `-Os`/`-O3` sensitivity.
  - [x] Spreading: an LXR block in stages, the next block started after a callback's output, up to its
        average share: busiest blocks 25-30 % lower (QEMU), the samples unchanged.
  - [x] Converter 33 900 → 24 300 a block, bit-identical; Pocket's unused Q23 stage gone; FX setters
        only when their own values move; delay lines interleaved, effects idling separately.
  - [x] Reverb lines (26 KB) from PSRAM to the panel's SRAM arena; delay (256 KB) one PSRAM stream.
  - [x] Golden sessions and `tools/compare_builds.sh`: bit-identical to 0.0.3 with spreading off.
  - [x] Sanitizer run (ASan, UBSan): no memory errors; two negative shifts in CLATTER 12's mix fixed.
  - [x] HARDWARE_TESTING.md: backup, recovery, staged first test, stop conditions.

## 1. Reliable synthesis (needs a device or emulator access)

- [ ] Run this version in Plinky's IDE emulator: pages, saving to a slot and loading it, page scrolling.
- [ ] **First device run (HARDWARE_TESTING.md): the DSP page and log, idle, all tracks, delay and
      reverb on.** QEMU now says at least 65 % (mean) and 73 % (busiest block) of a 150 MHz core 1 with
      the delay and reverb on all tracks, 77 % / 84 % in the worst case tried; cycles will be more.
- [ ] If over budget, in order (each keeps the LXR's synthesis): `clatter_12_sram.cpp` (LXR code in
      SRAM); the converter and the mix stages in SRAM; skip a voice's render while it is silent (the
      six voices are 83 000 of an LXR block's 87 000 instructions even when nothing plays; needs the
      oscillator, filter and smoothing state handled, and a sound comparison); only then a lower LXR
      rate or a shorter converter.
- [ ] Settle table placement (SRAM / PSRAM / inside the panel arena with a custom staged-load commit).
- [ ] Settle source size with Plinky: 483 KB compact now (the IDE's Test took 306 KB; another author
      reports ~200 KB for Flash). Lossless compaction cannot reach 200 KB (renaming every internal
      identifier and removing all removable spaces leaves about 270 KB of engine before the glue):
      a limit that low has to be raised by Plinky, or LXR code dropped.
- [ ] Confirm `SAMPLE_FREQ`, output level through Plinky's chain, and that the sends reach its effects.
- [ ] Make the load readout work in the emulator (`time_us()` is frozen within a block there).

## 2. Reliable sequencing

- [ ] Phase-lock to Plinky's clock: feed Pocket's external-clock input from `clock_phase` (24 ppqn).
- [ ] Per-step pitch (Pocket's cells carry a note; no control yet).
- [ ] Parameter locks per step (Pocket has two one-hit locks per trigger).

## 3. Hardware interface

- [ ] Touch latency and LED cost on device; check the dim tiers are visible.
- [ ] Live recording from the audition pads, quantised like Pocket's.
- [ ] Holding a parameter pad with pressure for fine moves (now: − and + pads).

## 4. Generative features

- [ ] Kit evolution over time (CLATTER Web's `evolveKit`), rolls into hits (CLATTER Web's ROLLS layer).

## 5. Storage

- [ ] Panel preferences (`on_serialise_settings`): none yet.
- [ ] Older project versions: the loader refuses any version but 1 today; a v2 must read v1.

## 6. MIDI / CV

- [ ] MIDI in: notes trigger tracks (queued from `on_midi`, applied in `on_ui`); out: steps as notes.
- [ ] MIDI out 2 as two trigger outputs; CV out accents.

## 7. Effects

- [ ] Measure CLATTER 12's delay and reverb on the device (PSRAM streams); if too slow, put the reverb's
      26 KB in SRAM, or run the delay at half rate, or default FX ENGINE to PLINKY.
- [ ] Plinky's own delay time, feedback and tail on the grid when FX ENGINE is PLINKY (its
      `MIX_PARAM_*` through the mix preset; signatures to confirm against the published headers).
- [ ] An audio-buffer stutter (beat repeat of the sound, not the sequence): needs a buffer and CPU.

## 8. Performance

- [ ] MORPH between two kits (evaluated in docs/DESIGN.md; deferred until a device shows headroom):
      continuous parameters only, rate-limited through the kit pump, per-track enable, pressure.
- [ ] Pattern and kit banks, so that scenes can change them.
- [ ] Sub-block timing for strokes and repeats (now the LXR block, 1.45 ms, as Pocket's steps).
- [ ] Fine tempo below 1 BPM (Pocket's sequencer keeps whole BPM).

## Before sharing

- [ ] Plinky firmware licence vs GPL-3.0 for prebuilt firmware (FEASIBILITY.md §5).
