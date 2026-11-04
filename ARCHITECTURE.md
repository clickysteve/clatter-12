# Architecture

```
 Plinky 12 firmware (not public)                       CLATTER 12 panel (build/clatter_12.cpp)
 ────────────────────────────────────                  ──────────────────────────────────────────────
 core 1  audio, 64 frames @ 32 kHz ──── on_dsp ──────▶ c12_engine_render
                                                         while converter needs input:
                                                           pocket_lxr_render_q23 (64 frames @ 44 140 Hz)
                                                             ├ drain ≤ 32 queued events
                                                             ├ pocket_seq_block (the sequencer)
                                                             └ LXR-02 voices, pan, DC block
                                                         c12_rs_process  44 140 → 32 000 Hz
          mix_buffers.dry ◀──────────────────────────── int16 stereo
          Plinky mix/output (level, EQ)
 core 0  UI ~250 fps ────────────────── on_ui ───────▶ touches → pocket_seq_set_cell / edit_begin+commit
                                                        transport/tempo → pocket_seq_play / set_tempo
                                                        kits → c12_kit_pump → event ring
                                                        LEDs ← pocket_seq_pattern / position
```

## Layers

| Layer | Where | Knows about |
|---|---|---|
| LXR-02 firmware DSP, unchanged | `third_party/clatter-pocket/lxr/upstream` | nothing |
| CLATTER's shim and parameter handler | `…/lxr/shim`, `…/lxr/bridge/lxr_params.c` | nothing platform-specific |
| CLATTER Pocket's engine host, sequencer, generator, kit designer | `…/engine/pocket_lxr.c`, `…/seq/*.c` | the layers above |
| CLATTER 12 core: rate conversion, engine wrapper, kit streaming | `core/` | Pocket's APIs |
| Plinky glue | `panel/clatter12.cpp` | core + public Plinky API |
| Simulator (host only) | `sim/` | the Plinky API, as documented |

Plinky-specific code is one file. The portable part is Pocket's, reused through a pinned submodule
with no edits; CLATTER 12 adds only what the Plinky needs (`core/`). A shared library extracted from
Pocket was considered and rejected for now: the submodule already gives one copy of the code, and a
refactor would touch Pocket's FM-1 build for no gain yet.

## Threads

The same two contexts as Pocket's (see `pocket_lxr.h`), mapped onto the Plinky:

- **control context = `on_ui`, core 0, foreground only.** Every `pocket_*`/`c12_*` control call. Not
  `on_sequence` and not `on_midi` (both interrupts that preempt `on_ui`): Pocket's event ring needs
  one producer. MIDI input will be queued and drained in `on_ui`.
- **audio context = `on_dsp`, core 1.** Renders; runs the sequencer inside the LXR's block loop.
- Cross-core sharing is Pocket's: single aligned words, a single-producer/single-consumer ring with
  full fences, and double-buffered patterns published by one byte. The RP2350's SRAM is uncached, so
  fences order the two cores. The engine is brought up in `on_ui` and released to core 1 with a
  release/acquire flag; until then `on_dsp` leaves the (cleared) mix silent.
- The sequencer runs on the LXR's block clock (1.45 ms), as on the LXR-02 and on Pocket. It follows
  Plinky's transport (play/stop) and tempo (polled each UI frame); it is not phase-locked to Plinky's
  clock yet (roadmap: drive Pocket's external-clock input from Plinky's `clock_phase`).

## Rate conversion

The LXR's coefficients assume 44 140 Hz, and much of its timing is per sample (envelopes, filter
cutoffs as a fraction of the rate, the decimator, recorded transients), so it is not re-tuned for
32 kHz: it runs at 44 140 Hz and `core/c12_resampler.c` converts. Kaiser-windowed sinc, 32 taps,
64 phases with linear interpolation, -6 dB at 16 kHz, ≈70 dB alias rejection, exact rational
position (no drift). Per Plinky block it renders one or two LXR blocks (mean 1.379; never fewer
than needed, `underruns` counted). About 4 100 multiply-adds per output block (not timed separately).

## Memory

| | Size (GCC, Cortex-M33) | Where |
|---|---|---|
| code | 51 KB (`clatter_12_sram.cpp`: 33 KB flash + 9.4 KB SRAM) | flash / `.time_critical` SRAM |
| constants (packed tables, kit recipes, styles, param table) | 21 KB | flash |
| LXR tables, unpacked | 102 KB | static SRAM, or PSRAM (`C12_TABLES_IN_PSRAM`) |
| other static state (voices, event ring, patterns, converter) | 28 KB | static SRAM |
| panel object | 2.7 KB | Plinky's 128 KB panel arena |

Nothing large is on the stack (the converter's coefficient row is static; the docs ask for < 128
bytes per object). No allocation anywhere.

## The amalgamation (`tools/amalgamate.py`)

The IDE takes one C++ file without `#include`. The tool preprocesses a unity of Pocket's and
CLATTER 12's C files as C, with empty C-library headers (`stubs/sysinc`) so nothing of the host's
library is inlined, and as GCC would (`-U__clang__`); wraps the result in `namespace c12` so no LXR
name can meet a Plinky symbol; maps the LXR's TCM section attributes to `C12_LXR_CODE`; guards
any Plinky macro that also names something in the engine; and applies a short list of rewrites
for C constructs C++ (or GCC's C++) rejects. It also makes every unsuffixed floating literal
`float` (what Pocket's single-precision build does with `-cl-single-precision-constant`; Plinky's
flags cannot be set), so no software double-precision call remains on the RP2350, and defines
`fmaf`, which Plinky's WebAssembly C library lacks. Every rewrite states how many times it must match, so a
change upstream stops the build rather than slipping through. Array designators are rewritten in
index order with indices evaluated by compiling the C text, not guessed. `tests/test_authentic`
compares the result with Pocket's own build of the same files.

`tools/make_panel.py` adds the library metadata, the licence notice, the build configuration and
`panel/clatter12.cpp`, and with `--compact` strips indentation from the generated part.
