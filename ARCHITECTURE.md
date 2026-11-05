# Architecture

```
 Plinky 12 firmware (not public)                       CLATTER 12 panel (build/clatter_12.cpp)
 ────────────────────────────────────                  ──────────────────────────────────────────────
 core 1  audio, 64 frames @ 32 kHz ──── on_dsp ──────▶ c12_engine_render
                                                         while the converter needs input, LXR blocks
                                                         (64 frames @ 44 140 Hz) in twelve stages,
                                                         Pocket's sequence of calls (lxr_stage):
                                                           head: FX values; drain ≤ 32 queued events;
                                                             block hook: pocket_seq_block; a scene
                                                             at the bar; the musical clock; beat
                                                             repeat; the step through FILL,
                                                             DENSITY, the alt pattern; ratchets,
                                                             rolls, graces ahead; note repeats;
                                                             modulation, LFOs, filter coefficients
                                                           six LXR-02 voices, a stage each
                                                           mix: level/mute/solo, sends (+throws);
                                                             CLATTER 12's delay + reverb (SPACE);
                                                             LXR FX (drive, ring, comp) on routed
                                                             tracks, two chunks; master, DC block,
                                                             bits, limiter
                                                         c12_rs_process  44 140 → 32 000 Hz
                                                         spreading: the next block's stages, up to
                                                           this callback's average share of work
          mix_buffers.dry ◀──────────────────────────── int16 stereo
          mix_buffers.delaysend/reverbsend ◀──────────── sends, with FX ENGINE PLINKY only
          Plinky mix/output (level, EQ, delay, reverb)
 core 0  UI ~250 fps ────────────────── on_ui ───────▶ pages: touches → pocket_seq_set_cell / edit_begin+commit,
                                                         c12_kit_set_target, c12_mix_*, c12_fx_set,
                                                         c12_perf_* (PERFORM, REPEAT: every frame)
                                                        transport/tempo → pocket_seq_play / set_tempo
                                                         (TEMPO pad and overlay: set_tempo_bpm)
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
| CLATTER 12 core: rate conversion, engine wrapper (mix, FX, limiter, ratchets and rolls, the performance layer), its delay and reverb (`c12_space.c`), kit streaming, LXR parameter table, kit generation and mutation | `core/` | Pocket's APIs, the LXR's FX code |
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
(`pocket_lxr_voice_block`). CLATTER 12 re-mixes those blocks in its mix stages (`mix_voices`,
`mix_space`, `mix_fx`, `mix_master` in `core/c12_engine.c`) with Pocket's pan law, int16 conversion,
master gain and DC blocker, to the bit, so the mixer and FX can sit inside the mix without touching
Pocket. When the mixer is neutral and no FX are on it takes Pocket's own sum of the voices instead
(the same numbers), which keeps the default cost low; Pocket's Q23 output stage after that sum is not
run, as CLATTER 12 never used more of it.
Levels ramp over a block; mute and solo also hold back the sequencer's triggers (as voices are
struck), so a muted closed hat cannot choke an open one. The hats are one voice; its level and pan
follow the hat track that last played it. Sends are built per voice (a held THROW puts a track's at
full) and feed CLATTER 12's delay and reverb, or, with FX ENGINE PLINKY, are converted linearly to the
output rate for Plinky's. The FX chain is the LXR firmware's `DSPAudio/FX` master distortion, ring
modulator and compressor, unchanged (the amalgamator adds them; the FX bus and its 64 KB delay stay
out), run in 32-frame chunks because they keep their working buffers on the stack. Then bit
reduction and a limiter (peak follower, instant attack, -1 dBFS) before the converter.

## Ratchets, rolls and the performance layer

Pocket's cell is 32 bits and uses bits 0-19; CLATTER 12 keeps a step's articulation in bits 20-28
(ratchet, velocity curve, roll), which Pocket's sequencer ignores. The sequencer's emit callback only
notes what a step plays (mute and solo are applied later, as voices are struck); the block hook
(`block_hook`), right after `pocket_seq_block` in the same LXR block:

1. applies a scene's mixer if one waits for this bar's first step (`c12_mix_schedule`), so the
   downbeat already sounds in it;
2. moves the **musical clock**: straight sixteenths in Pocket's phase units, re-anchored at each step,
   following swing within it (elapsed × 300·rate / the step's real length);
3. runs **beat repeat**: from its anchor step, the captured steps' offsets are fired as the window
   (the chosen length) wraps; the masked tracks' own steps are held back meanwhile;
4. plays a new step: what Pocket emitted, or for tracks under a temporary mutation their cell in the
   alternative pattern (the panel's memory, published by pointer, double-buffered as Pocket's patterns
   are), each through FILL and DENSITY (`c12_perf_cell`, pure, so COMMIT writes what was heard), then
   the hat choke; each hit is scheduled with its ratchet, roll and curve;
5. looks ahead for flams and drags on the next step: when the step's remaining time is within the
   grace's lead (and its condition is predictable), the graces are scheduled to land before it;
6. strikes **note repeats** on their grid (a 64-bit musical time; the grid from PLAY, or free when
   stopped);
7. plays cells requested by hand (`c12_perf_play`: rolls, auditions), at once or at the next step;
8. fires the strokes due (graces, then the step's), in Pocket's own phase units from the moment they
   were scheduled, so they follow tempo and land on the first block at or after their time.

Every voice trigger goes into a 128-entry log (block, track, velocity, why), read by the tests to
check timing to the block. The panel side (`perf_apply`, every UI frame, every page) turns held and
latched pads into those few bytes: nothing of the performance is stored, so releasing a pad restores
the pattern exactly.

## Spreading the work

64 output frames at 32 kHz need 1.379 LXR blocks, so a callback that renders whole blocks does one or
two, and the callbacks that do two set the peak. The engine therefore runs an LXR block as twelve
stages (the head, each voice, the mix stages), the same calls in the same order as
`pocket_lxr_render_q23`. A callback first finishes every block its own output needs, makes its output,
then goes on with the next block's stages until it has done its average share of the work, by
instruction weights measured in QEMU. It never goes past what the next callback will need, and never
starts a second block beyond its own, so the samples are unchanged and a control change reaches the
engine at most one LXR block (1.45 ms) later. In QEMU this takes the busiest blocks with the delay and
reverb on from 283 500 to 216 100 instructions (TESTING.md, "CPU"). `c12_engine_set_spread(0)` turns it
off; the tests compare both.

## CLATTER 12's delay and reverb

`core/c12_space.c`, after the per-voice sends are built in `mix_voices`: the delay at 44 140 Hz on two
64 Ki-sample 16-bit lines (ping-pong), stored interleaved as one 32-bit word a sample (one sequential
stream each way in PSRAM, one load per tap); the reverb at 22 070 Hz (two allpass diffusers, a
four-line Hadamard FDN with a damping low-pass and a T60-derived loss in each line). Their returns are
added to the mix bus before the master gain, the DC blocker and the limiter. Parameters are applied at
an LXR block start when the FX serial moves or the tempo changes (the delay's time is a tempo
division); each coefficient is worked out again only when its own value moves. The panel binds the
delay's 256 KB in PSRAM (128 KB in) and the reverb's 26 KB in its own object, in the SRAM arena
(`c12_space_bind2`): the reverb reads six scattered streams, which PSRAM behind the XIP cache serves
badly. Each effect with nothing sent to it and every line exactly zero (samples truncate towards
zero, so tails end) does nothing.

## Rate conversion

The LXR's coefficients assume 44 140 Hz, and much of its timing is per sample (envelopes, filter
cutoffs as a fraction of the rate, the decimator, recorded transients), so it is not re-tuned for
32 kHz: it runs at 44 140 Hz and `core/c12_resampler.c` converts. Kaiser-windowed sinc, 32 taps,
64 phases with linear interpolation, -6 dB at 16 kHz, ≈70 dB alias rejection, exact rational
position (no drift). About 4 100 multiply-adds per output block, the coefficient interpolation done
in the same pass as the window: 24 300 instructions per output block with the output conversion
(QEMU).

## Memory

| | Size (GCC 13, Cortex-M33, engine and panel) | Where |
|---|---|---|
| code | 120 KB (`clatter_12_sram.cpp`: 98 KB flash + 15 KB SRAM) | flash / `.time_critical` SRAM |
| constants (packed tables, kit recipes, styles, LXR parameter tables, FX tables) | 27 KB | flash |
| LXR tables, unpacked | 100 KB | static SRAM, or PSRAM (`C12_TABLES_IN_PSRAM`) |
| other static state (voices, event ring, patterns, converter (12 KB), mix and send buffers, strokes, the trigger log) | 40 KB | static SRAM |
| delay lines | 256 KB | PSRAM (bound at run time; none: silent) |
| reverb lines | 26 KB | the panel object (the last member, kept out of a staged load's copy, so a load leaves them silent) |
| panel object (UI state, the kit, 16 undo snapshots, the project, two alternative patterns, scenes, the reverb's lines) | 72.1 KB | Plinky's 128 KB panel arena |
| source file | 483 KB (`--compact`) | the IDE |

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
