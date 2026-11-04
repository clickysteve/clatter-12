# CLATTER 12

**CLATTER's LXR-02 drum synthesis and generative sequencer, as a native Plinky 12 instrument.**

> **EXPERIMENTAL. Not tested on a Plinky 12.** Everything below that says *works* means in host
> tests, in CLATTER 12's own simulator of the Plinky API, or in a Cortex-M33 cross-compile.

CLATTER 12 brings [CLATTER](https://github.com/clickysteve/clatter), through
[CLATTER Pocket](https://github.com/clickysteve/clatter-pocket)'s embedded engine, to the
[Plinky 12](https://plinky12.com/) as a **custom panel**: one C++ file that Plinky's own IDE
compiles into its firmware. It is not a MIDI controller for another device and not Plinky's synth
renamed: the sound is the Erica Synths LXR-02 firmware's sound engine, run unchanged at its own
44 140 Hz on the Plinky's audio core and converted to the Plinky's 32 kHz.

> CLATTER 12 is an independent project, not an Erica Synths or Plinky product, and is not endorsed by
> either. It contains a modified version of the LXR-02 firmware's sound engine. "Erica Synths",
> "LXR" and "LXR-02" are trademarks of Erica Synths.

![The grid in CLATTER 12's simulator: boot, playing, edited, generated, new kit](docs/images/grid_sheet.png)

## Status

| | |
|---|---|
| Feasibility | **Positive**: Plinky panels are native code with a core-1 audio callback. [FEASIBILITY.md](FEASIBILITY.md) |
| LXR engine | all six voices / seven triggers, Pocket's kits, variations, generator; compiles into one `#include`-free C++ file (`tools/amalgamate.py`) |
| Sound, host | matches CLATTER Pocket's reference build: every voice's spectrum within 0.03 dB per third-octave band, envelopes within 0.1 dB (95 % of 10 ms frames); 32 kHz output matches an ideal conversion of the native-rate LXR at 50-79 dB SNR |
| Panel, simulated | 16-step grid for seven tracks with pressure velocity, variation lanes, four pages with copy, length 16-64, GEN / MUT / KIT / STYLE / UNDO / CLR, play/stop with Plinky's transport and tempo: 36 scripted checks pass |
| Browser demo | **plays**: the engine, sequencer and generator as WebAssembly, in an AudioWorklet at 32 kHz. Audio matches the native build sample for sample; the generator matches it byte for byte (99 patterns). [Page](https://clickysteve.github.io/clatter-12/) |
| Plinky IDE emulator | **compiles, links, boots and runs** (owner's Test, 2026-11-04): engine up at 32 kHz, no underruns over ~4 min. The timing readout is 0 there, so CPU load is still unknown; 17 clipped samples while editing |
| Cortex-M33 | compiles with GCC 8.5 (`-mcpu=cortex-m33 -mfpu=fpv5-sp-d16`): 51 KB code, 21 KB constants, 132 KB static SRAM (33 KB with tables in PSRAM). Not linked against Plinky's firmware (not public) |
| On a Plinky 12 | **never run.** CPU load on core 1 is the open question; the panel measures it on its DSP page |
| Saving, MIDI, CV | not yet (roadmap) |

**Project page:** https://clickysteve.github.io/clatter-12/ (source in `docs/`). A working model of the grid in
CLATTER Pocket's style that **plays**: the page runs the panel's engine, sequencer and CLATTER's generator as
WebAssembly in your browser (`docs/assets/c12.wasm`, built by `make wasm`, checked by `make test-wasm`). It is
not a Plinky: no Plinky output stage, pressure or CPU limit. Run it locally with `make site-serve`.

## Playing it (as designed; simulated)

| Grid | |
|---|---|
| rows 1-7 | DRUM 1, DRUM 2, DRUM 3, SNARE, CYMBAL, CL HAT, OP HAT. Tap: step on, as loud as you press. Tap a lit step: off. Hold a lit step: re-take its velocity |
| row 8 | playhead |
| rows 9-15 | variation lane under each track: tap under a lit step to cycle none / A / B (DRUM 3 and SNARE: also their second sound and its A / B) |
| row 16 | pages 1-4 (hold one, tap another: copy) · length 16/32/48/64 · GEN · MUT · KIT · STYLE · UNDO · CLR (hold) · STOP · PLAY |
| side buttons | Plinky's: tempo (left), pages (right; up = CLATTER 12's DSP load page) |

Interface design and rationale: [docs/DESIGN.md](docs/DESIGN.md).

## Building

```bash
git clone --recurse-submodules https://github.com/clickysteve/clatter-12
```

```bash
make
```

That generates `build/clatter_12.cpp` (paste it into the Plinky 12 IDE) and runs every host test.
`make m33` cross-compiles it for the RP2350. Details, variants and installing: [BUILDING.md](BUILDING.md).
Tests and what they do and do not show: [TESTING.md](TESTING.md).

## Documents

- [FEASIBILITY.md](FEASIBILITY.md): how Plinky panels work, resources, LXR portability, obstacles
- [ARCHITECTURE.md](ARCHITECTURE.md): layers, threads, signal path, memory, the amalgamation
- [docs/DESIGN.md](docs/DESIGN.md): the 16 × 16 interface
- [BUILDING.md](BUILDING.md), [TESTING.md](TESTING.md), [ROADMAP.md](ROADMAP.md)
- [NOTICE.md](NOTICE.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md): licences and attribution

## Layout

```
core/             CLATTER 12's portable C: engine wrapper, 44.1 -> 32 kHz converter
panel/            the Plinky glue (C++, the public panel API only)
sim/              a host stand-in for the Plinky API (NOT Plinky's emulator)
tests/            engine, converter, authenticity, C/C++ equivalence, simulated panel, bench
tools/            amalgamator, panel assembler, API check, size report
stubs/sysinc/     empty C library headers for the amalgamation
third_party/      clatter-pocket (submodule, pinned; unmodified)
```

## Licence

GPL-3.0-only with the LXR-02 firmware's additional terms: [NOTICE.md](NOTICE.md).
