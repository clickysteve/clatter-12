# Architecture

```
 Plinky 12 firmware (not public)                       CLATTER 12 panel (build/clatter_12.cpp)
 ────────────────────────────────────                  ──────────────────────────────────────────────
 core 1  audio, 64 frames @ 32 kHz ──── on_dsp ──────▶ c12_engine_render
                                                         while converter needs input:
                                                           pocket_lxr_render_q23 (64 frames @ 44 140 Hz)
                                                             ├ drain ≤ 32 queued events
                                                             ├ block hook: pocket_seq_block, then the
                                                             │   step's hits and ratchet strokes
                                                             └ LXR-02 voices (each kept per block)
                                                           mix_block: level/mute/solo, sends,
                                                             LXR FX (drive, ring, comp) on routed
                                                             tracks, master, DC block, bits, limiter
                                                         c12_rs_process  44 140 → 32 000 Hz
          mix_buffers.dry ◀──────────────────────────── int16 stereo
          mix_buffers.delaysend/reverbsend ◀──────────── sends (linear 44 140 → 32 000 Hz)
          Plinky mix/output (level, EQ, delay, reverb)
 core 0  UI ~250 fps ────────────────── on_ui ───────▶ pages: touches → pocket_seq_set_cell / edit_begin+commit,
                                                         c12_kit_set_target, c12_mix_*, c12_fx_set
                                                        transport/tempo → pocket_seq_play / set_tempo
                                                        kits → c12_kit_pump → event ring
                                                        LEDs ← pattern, kit, mixer, meters
                                                        on_serialise ↔ the project (save page)
```

## Layers

| Layer | Where | Knows about |
|---|---|---|
| LXR-02 firmware DSP, unchanged | `third_party/clatter-pocket/lxr/upstream` | nothing |
| CLATTER's shim and parameter handler | `…/lxr/shim`, `…/lxr/bridge/lxr_params.c` | nothing platform-specific |
| CLATTER Pocket's engine host, sequencer, generator, kit designer | `…/engine/pocket_lxr.c`, `…/seq/*.c` | the layers above |
| CLATTER 12 core: rate conversion, engine wrapper (mix, FX, limiter, ratchets), kit streaming, LXR parameter table, kit generation and mutation | `core/` | Pocket's APIs, the LXR's FX code |
| Plinky glue | `panel/clatter12.cpp` | core + public Plinky API |
| Simulator (host, and WebAssembly for the page) | `sim/` | the Plinky API, as documented and as published panels use it |

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

## CLATTER 12's mix

Pocket mixes the six voices itself and keeps each voice's block for inspection
(`pocket_lxr_voice_block`). CLATTER 12 re-mixes those blocks in `mix_block` (`core/c12_engine.c`)
with Pocket's pan law, int16 conversion, master gain and DC blocker, to the bit, so the mixer and
FX can sit inside the mix without touching Pocket. When the mixer is neutral and no FX are on it
takes Pocket's own mix of the block instead (the same numbers), which keeps the default cost low.
Levels ramp over a block; mute and solo also hold back the sequencer's triggers (`seq_emit`), so a
muted closed hat cannot choke an open one. The hats are one voice; its level and pan follow the hat
track that last played it. Sends are built per voice and converted linearly to the output rate for
Plinky's delay and reverb. The FX chain is the LXR firmware's `DSPAudio/FX` master distortion, ring
modulator and compressor, unchanged (the amalgamator adds them; the FX bus and its 64 KB delay stay
out), run in 32-frame chunks because they keep their working buffers on the stack. Then bit
reduction and a limiter (peak follower, instant attack, -1 dBFS) before the converter.

## Ratchets

Pocket's cell is 32 bits and uses bits 0-19; CLATTER 12 keeps a step's ratchet in bits 20-26
(strokes, spacing, velocity run), which Pocket's sequencer ignores. The sequencer's emit callback
only notes what a step plays; the block hook (`block_hook`), right after `pocket_seq_block` in the
same LXR block, triggers it and schedules the remaining strokes in Pocket's own phase units, so they
follow tempo and swing and are placed at LXR block resolution, as steps are.

## Rate conversion

The LXR's coefficients assume 44 140 Hz, and much of its timing is per sample (envelopes, filter
cutoffs as a fraction of the rate, the decimator, recorded transients), so it is not re-tuned for
32 kHz: it runs at 44 140 Hz and `core/c12_resampler.c` converts. Kaiser-windowed sinc, 32 taps,
64 phases with linear interpolation, -6 dB at 16 kHz, ≈70 dB alias rejection, exact rational
position (no drift). Per Plinky block it renders one or two LXR blocks (mean 1.379; never fewer
than needed, `underruns` counted). About 4 100 multiply-adds per output block (not timed separately).

## Memory

| | Size (GCC 13, Cortex-M33, engine and panel) | Where |
|---|---|---|
| code | 89 KB (`clatter_12_sram.cpp`: 68 KB flash + 13.5 KB SRAM) | flash / `.time_critical` SRAM |
| constants (packed tables, kit recipes, styles, LXR parameter tables, FX tables) | 27 KB | flash |
| LXR tables, unpacked | 100 KB | static SRAM, or PSRAM (`C12_TABLES_IN_PSRAM`) |
| other static state (voices, event ring, patterns, converter, mix and send buffers) | 38 KB | static SRAM |
| panel object (UI state, the kit, 16 undo snapshots, the project) | 38.6 KB | Plinky's 128 KB panel arena |
| source file | 403 KB (`--compact`) | the IDE |

Nothing large is on the stack in CLATTER 12's code (mix buffers are static; the LXR FX's own stack
buffers are kept to 128 bytes by running them 32 frames at a time). No allocation anywhere. The
undo history lives in the panel object, where there is room, not in static SRAM, where there may not be.

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

## The web build

`docs/` (GitHub Pages) runs the panel itself. `sim/wasm_panel.cpp` includes the simulator's API
(`sim/plinky_sim.h`) and the generated panel file (`build/clatter_12_full.cpp`, the same text as the
one pasted into Plinky's IDE) and exports what a host needs: touches, one UI frame (two 64-frame
audio blocks and `on_ui`), LEDs, audio, page and tempo, save and load through `on_serialise`, and
the pad descriptions of `sim/panel_help.h`. A freestanding C library (`sim/wasm/`) and CLATTER's
libm make it one module with no imports (`make wasm` → `docs/assets/c12panel.wasm`). The page runs it
on its own thread until sound is started, then moves it, with its project, into an AudioWorklet
(`docs/assets/c12-worklet.js`), which runs one UI frame per 4 ms of audio and sends LEDs back. So
the controls are not re-implemented in JavaScript: the page draws the panel's LEDs and sends it
touches. It is still not a Plinky (TESTING.md, "The simulator is not Plinky").
