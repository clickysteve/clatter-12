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

![CLATTER 12's pages in its simulator: SEQ (with a step selected), SOUND, GEN, MIX, FX (SPACE and COLOUR banks), PERFORM, REPEAT, and the tempo overlay](docs/images/grid_sheet.png)

## Status

| | |
|---|---|
| Feasibility | **Positive**: Plinky panels are native code with a core-1 audio callback. [FEASIBILITY.md](FEASIBILITY.md) |
| LXR engine | all six voices / seven triggers, unchanged; the LXR firmware's own drive, ring modulator and compressor; CLATTER 12's own delay and reverb; compiles into one `#include`-free C++ file (`tools/amalgamate.py`) |
| Sound, host | matches CLATTER Pocket's reference build: every voice's spectrum within 0.03 dB per third-octave band; CLATTER 12's own mix is Pocket's to the bit when the mixer is neutral |
| Panel, simulated | seven pages (SEQ, SOUND, GEN, MIX, FX, PERFORM, REPEAT), Plinky's save page, and a tempo pad on every page: 201 scripted checks pass in CLATTER 12's simulator of the Plinky API; 66 more time ratchets, rolls, repeats and the effects to the engine's block |
| Browser demo | **runs the panel itself**: the firmware source compiled to WebAssembly with the simulator, so the page's grid is the panel's interface. [Page](https://clickysteve.github.io/clatter-12/) |
| Cortex-M33 | compiles with GCC (`-mcpu=cortex-m33 -mfpu=fpv5-sp-d16`), engine and panel: 120 KB code, 27 KB constants, 40 KB static SRAM + 100 KB LXR tables (SRAM or PSRAM) + 256 KB of delay lines (PSRAM); panel object 72 KB of the 128 KB arena (26 KB of it the reverb's lines). Not linked against Plinky's firmware (not public) |
| CPU, estimated | QEMU's Cortex-M33 model, delay and reverb on, all seven tracks playing: **195 700 instructions per 2 ms block on average, 218 400 in the busiest** (65 % / 73 % of a 150 MHz core 1 if every instruction took one cycle); the worst case tried 232 000 / 251 900 (77 % / 84 %). Before this release's performance pass: 210 200 / 304 600 and 252 300 / 358 400. **Not a measurement of a Plinky**: cycles will be more (TESTING.md, "CPU") |
| Plinky IDE emulator | the first version **compiled, linked and ran** (2026-11-04); this version not yet tried there |
| On a Plinky 12 | **never run** |
| MIDI, CV | not yet (roadmap) |

**Project page:** https://clickysteve.github.io/clatter-12/ (source in `docs/`). The page runs CLATTER 12's panel
source (engine and Plinky glue, exactly the file pasted into Plinky's IDE) with CLATTER 12's simulator of the Plinky API,
compiled to WebAssembly (`docs/assets/c12panel.wasm`, `make wasm`, checked by `make test-wasm`). It is an interface
demonstration, not a Plinky: no pressure sensor, no Plinky delay or reverb (CLATTER 12's own run), no CPU limit. `make site-serve` runs it locally.

## Playing it (as designed; simulated)

Eight pages, Plinky's own (the right side buttons scroll them); row 15's first eight pads jump to one. Row 15 is the
same on every page: `SEQ SND GEN MIX FX PRF RPT SAV | TEMPO | page's own | UNDO REDO STOP PLAY`.

| Page | |
|---|---|
| TEMPO | row 15, every page. **Tap** it in time: tap tempo. **Hold** it: the BPM in digits, -10 -5 -1 / +1 +5 +10, half and double time, 16 presets, a held nudge, a tap row. It is Plinky's own tempo |
| SEQ | rows 0-6: the seven LXR triggers' steps. **Tap** an empty step: on, as loud as you press. **Tap** a lit step: off. **Hold** a lit step: it stays, and rows 8-11 edit it: variation, condition (ALWAYS, 1:2 … 4:4, 25/50/75 %), ratchet (1-8 strokes, never closer than 12 ms), roll (flam, drag, double, triple, accelerating, decelerating), velocity curve (flat, fade in/out, accent first/last, alternating, human), exact velocity. Row 12 mute and solo, 13 lock and audition, 14 view page (hold + tap: copy), length, swing; 15 GEN, MUT, CLR (hold) |
| SOUND | one voice's LXR parameters, a row each, value across the row, `-`/`+` at the end; banks SOURCE, SHAPE, MOD; audition, new sound, mutate sound, lock sound |
| GEN | style, density, complexity, mutation strength, track locks, one-track generate; kit character, wildness, mutation amount, sound locks, new sound / mutate sound per voice, NEW KIT, MUTATE KIT |
| MIX | strips for the seven tracks and the master: mute, solo, an 8-row fader with fine `-`/`+`, pan, delay and reverb sends, meters; the master's limiter |
| FX | **SPACE**: CLATTER 12's delay (12 tempo divisions, feedback, low/high-pass filter, wet, ping-pong) and reverb (size, decay, damping, wet), freezes, and where the sends go (CLATTER 12's effects or Plinky's). **COLOUR**: routing; the LXR's drive, ring modulator and compressor; bit and sample-rate reduction |
| PERFORM | all momentary (LATCH holds): mute, solo, kill groups, fills, a temporary mutation, density, stutter, delay and reverb throws, freezes; COMMIT (hold) writes it in, undoably; eight scenes recalled at the next bar |
| REPEAT | hold a track to repeat it (1/4 to 1/64, triplets, dotted; pressure as velocity, rate or chance; curves); roll pads; beat repeat of the sequence (1 bar to 1/32T) with shape and tracks |
| SAVE | Plinky's save/load page: the whole project in a song slot (version 2; version-1 projects convert) |
| side buttons | Plinky's: tempo (left), pages (right; up from SEQ = CLATTER 12's DSP load page) |

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
core/             CLATTER 12's portable C: engine wrapper (mix, FX, ratchets), 44.1 -> 32 kHz converter,
                  LXR parameter table, kit generation and mutation
panel/            the Plinky glue (C++, the public panel API only)
sim/              a host stand-in for the Plinky API (NOT Plinky's emulator); its WebAssembly build for the page
tests/            engine, converter, authenticity, C/C++ equivalence, simulated panel, WebAssembly, bench,
                  Cortex-M33 instruction counts in QEMU (tests/m33)
tools/            amalgamator, panel assembler, API check, size report
stubs/sysinc/     empty C library headers for the amalgamation
third_party/      clatter-pocket (submodule, pinned; unmodified)
```

## Licence

GPL-3.0-only with the LXR-02 firmware's additional terms: [NOTICE.md](NOTICE.md).
