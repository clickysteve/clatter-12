# Roadmap to CLATTER 12 v0.1

v0.1: a seven-voice LXR drum machine with a 16 × 16 touch sequencer, running on a Plinky 12.

## Done (0.0.1-exp, host and cross-compile only)

LXR engine in one panel file; 44.14 → 32 kHz conversion; seven-track 16-step grid with pressure
velocity, variation lanes, four pages with copy, length; GEN / MUT / KIT / STYLE / UNDO / CLR;
Plinky transport and tempo; DSP load page and log; tests; Cortex-M33 compile.

## 1. Reliable synthesis (needs a device or emulator access)

- [ ] Run `clatter_12.cpp` in Plinky's IDE emulator; fix whatever its compiler rejects.
- [ ] First device run: link result, DSP page/log numbers idle and busy, crackle or not.
- [ ] If over budget: `_sram` variant; profile per stage (`time_us` around LXR render and converter);
      skip silent voices; shorter converter. Only then consider rate or voice changes, measured.
- [ ] Settle table placement (SRAM / PSRAM / inside the panel arena with a custom staged-load commit).
- [ ] Settle source size with Plinky (limit, or compaction).
- [ ] Confirm `SAMPLE_FREQ` and output level through Plinky's chain.

## 2. Reliable sequencing

- [ ] Phase-lock to Plinky's clock: feed Pocket's external-clock input from `clock_phase` (24 ppqn),
      so MIDI/CV clock in and out and other panels agree.
- [ ] Swing (Pocket has it) on a control.
- [ ] Conditional steps (Pocket has them) and per-step pitch.

## 3. Hardware interface

- [ ] Touch latency and LED cost measured on device.
- [ ] Second page: per-track mute, lock, generate one track, density/complexity, kit wildness.
- [ ] Live pads: pressure-triggered hits (record into the pattern, quantised like Pocket's).
- [ ] Multi-level undo (Pocket's `ui/pocket_history.c`, possibly in the shadow arena).

## 4. Generative features

- [ ] Density and complexity controls; per-track generate; mutation strength; kit mutation
      (`pocket_kit_voice`); style browsing with names drawn on the grid.

## 5. Storage

- [ ] `on_serialise`: pattern (64 steps × 7), length, tempo, kit, style/seed; Plinky's save/load page
      (`panel_page_t::saveload`) on page 1. Reset every field before reading (a known Plinky gotcha).

## 6. MIDI / CV

- [ ] MIDI in: notes trigger tracks (queued from `on_midi`, applied in `on_ui`); out: steps as notes.
- [ ] MIDI out 2 as two trigger outputs; CV out accents.

## 7. Performance refinements

- [ ] Only after 1-3: LXR FX bus (64 KB delay, in PSRAM?), reverb/delay sends into Plinky's FX.

## Before sharing

- [ ] Plinky firmware licence vs GPL-3.0 for prebuilt firmware (FEASIBILITY.md §5).
- [ ] GitHub repository `clickysteve/clatter-12` and CI (workflow included, untried on GitHub).
