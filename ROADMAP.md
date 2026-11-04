# Roadmap to CLATTER 12 v0.1

v0.1: a seven-voice LXR drum machine with a 16 × 16 touch sequencer, running on a Plinky 12.

## Done (host, simulator, cross-compile and QEMU only)

- 0.0.1-exp: LXR engine in one panel file; 44.14 → 32 kHz conversion; 16-step grid with pressure velocity;
  GEN / MUT / KIT / STYLE / UNDO / CLR; Plinky transport and tempo; DSP load page and log; first emulator run.
- This version (docs/DESIGN.md):
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

## 1. Reliable synthesis (needs a device or emulator access)

- [ ] Run this version in Plinky's IDE emulator: pages, saving to a slot and loading it, page scrolling.
- [ ] **First device run: the DSP page and log, idle, all tracks, FX on.** QEMU says the default engine
      executes at least 56 % (mean) and 81 % (busiest block) of a 150 MHz core 1's cycles; it may not fit.
- [ ] If over budget, in order (each keeps the LXR's synthesis): `clatter_12_sram.cpp` (LXR code in
      SRAM); the converter in SRAM and with fewer taps (35 000 instructions a block now); skip a voice's
      render while it is silent (Pocket notes the filter/LFO state care needed); spread the two-LXR-block
      peaks with one more block of output buffering if Plinky's DMA allows; only then a lower LXR rate.
- [ ] Settle table placement (SRAM / PSRAM / inside the panel arena with a custom staged-load commit).
- [ ] Settle source size with Plinky: 403 KB compact now (the IDE's Test took 306 KB; another author
      reports ~200 KB for Flash). Identifier compaction or dropping unused LXR paths if needed.
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

- [ ] The LXR's own delay (64 KB half-float memory; PSRAM?) only if CPU allows; Plinky's delay and reverb
      are used through the sends instead.

## Before sharing

- [ ] Plinky firmware licence vs GPL-3.0 for prebuilt firmware (FEASIBILITY.md §5).
