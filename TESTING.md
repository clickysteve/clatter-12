# Testing

`make test` runs the host tests; `make m33` the Cortex-M33 compile and link check; `make m33-bench` the
instruction counts in QEMU and `make m33-profile` the same stage by stage; `make test-wasm` the page's
WebAssembly build; `tools/compare_builds.sh REV` compares this tree's audio with an earlier revision's,
sample for sample. None involves a Plinky 12; none is hardware verification. The first device test is
planned in [HARDWARE_TESTING.md](HARDWARE_TESTING.md).

| Test | What it shows | Result (2026-11-05, performance pass; Linux, clang 18, GCC 13.2 for Arm) |
|---|---|---|
| `test_resampler` | 44 140 → 32 000 Hz converter: gain, alias rejection, exact long-run rate | 13/13 |
| `test_engine` | each trigger converted vs an ideal conversion of the 44 140 Hz LXR; pitch, level, decay; sequencer timing; all triggers with three kits: no underruns, no clipping; block-size independence. **CLATTER 12 now makes its own mix** (for the mixer and FX): these checks hold it to Pocket's, and they pass unchanged, to the same figures | 37/37 |
| `test_perform` | **articulation, performance and the delay and reverb at engine level**, timed to the LXR block (1.45 ms) by the engine's trigger log: a 4x ratchet's strokes on the first block at or after k/4 of the step; FLAT/FADE/ACCENT/ALTERNATING curves; 8x at 300 BPM plays 4, none closer than 12 ms, 8 at 120; version-1 cells convert; FLAM's grace 31 ms ahead with the hit on the plain step's own block, DRAG's two graces 15.6 ms apart, a first-step flam; DOUBLE and TRIPLE spacing and velocities; ACCEL/DECEL gaps closing/opening, velocities rising/falling, never under 12 ms; rolls by hand stopped (at once) and playing (next step); **8 s of a random tempo every 4 ms with ratchets: 0 underruns, 0 clipped**, then steps even at the new tempo; beat repeat 1/4 (kick again on 4, 8, 12, snare held back; released, the next bar as written; pattern unchanged); a 1/32 stutter twice a step, fading; note repeat 1/16 and 1/8T at exact intervals, following a tempo change, on the sequencer's grid while playing, chance 50 %; FILL adds more late in the bar; DENSITY -60 keeps loud over soft, -100 silent, +100 ghosts only on a playing track; a scene's mute waits for the bar and holds the downbeat back; **the delay**: echo a dotted eighth later, repeats, ping-pong sides, 1/4 moves it to 500 ms, feedback 90 vs 0, the filter darkens it, wet 0 silent; **the reverb**: a tail, decay 100 vs 0, size, damping; delay freeze holds 5 s and decays when let go; idle costs nothing; a throw sends a track with no send; PLINKY engine: no return; unbound memory: off. **Real time** (new): a sequenced session renders the same samples with spreading and without; the busiest callback runs 24 LXR stages (two blocks) without spreading and 19 with; a hand-played hit reaches the engine 0 or 1 LXR block later with spreading, never more, over 23 callback phases; reverb sends alone leave the delay idle, delay sends alone the reverb, none and the tails gone neither; 4 000 blocks of an FX value and the tempo moving every block, new kits, scenes, play and stop: 0 underruns, at most 19 stages a callback | 77/77 |
| `test_authentic` | CLATTER 12's C++ engine vs CLATTER Pocket's own build; generator, mutation and kit designer vs Pocket's | 21/21; 880/880 patterns, 880/880 mutations, 1440/1440 kits identical |
| `equiv_c` / `equiv_cxx` | the generated engine text as C and as C++ | 97.8 % of samples identical, max 2 LSB, 87.9 dB |
| `test_panel_sim` | **the real panel source** in the simulator, 201 checks. As before: boot, play, playhead; step on/off/hold, pressure velocity; variations through held steps; conditions over whole passes; ratchets (4x three strokes a pass, auditioned when stopped), rolls and curves; mute and solo silence the output; lock, length, page view and copy, swing; GEN, MUT, UNDO, REDO, CLR; SOUND rows, kit generation, GEN controls; MIX levels, pan, master; COLOUR FX; saving and loading; 0 underruns and 0 clipped samples over the session (132 s). New: **every page reached from each of the seven panel pages**; **TEMPO**: four taps at 500 ms give 120, at 400 ms 150 and the sequencer follows; the held overlay shows the digits, +1, -10, half, double, a preset, a held nudge and its return, release restores the page, a hold is not a tap; rapid changes while playing: 0 underruns; **PERFORM**: held MUTE silences and release restores (the mixer's mute untouched), KILL ALL, FILL adds hits with the pattern unchanged, MUTATE plays a mutation with the stored pattern untouched and gone on release, DENSITY, STUTTER, THROW each held and released, a held pad let go by a page change, LATCH holds and a second tap or RELEASE lets go, a brief COMMIT does nothing, a held COMMIT writes the mutation and mute, UNDO takes it back; **SCENES**: store by hold, recall waits for the bar, then levels, mutes and FX as stored; **REPEAT**: a held pad repeats 1/16, pressure is velocity (light 45+), release stops, RATE mode four times as fast pressed hard, 1/8T, a TRIPLE roll pad, BEAT REPEAT with the pattern unchanged; **FX SPACE**: each of delay time, feedback, filter, wet, size, decay, damping, reverb wet changes the sound's envelope by more than three times the bar-to-bar difference, MONO makes left and right alike, freeze is momentary; sends reach Plinky's buffers only on FX ENGINE PLINKY; **a version-1 project** loads with its ratchet converted, its swing, tempo, sends and the PLINKY engine, and its version-2 save loads back the same | 201/201; panel object 45 448 bytes |
| `test_wasm.mjs` | `docs/assets/c12panel.wasm` under node, as the page drives it: no imports; boot; audio; a step; a held step edited; **all eight pages reached from row 15, and every pad lit on them has a description** (both FX banks, the tempo overlay too); tap tempo; save, change, load; a foreign file refused; tempo | 33/33 |
| `make m33` | GCC compile of engine **and panel** (the panel object constructed, as the firmware does) for Cortex-M33 + FPv5-SP, three variants; section sizes | 120 KB code, 27 KB constants, 40 KB static SRAM + 100 KB tables (`_psram`: tables in PSRAM); `_sram` puts 15 KB of LXR code in SRAM; the delay's 256 KB are PSRAM, bound at run time; the reverb's 26 KB are in the panel object (72 KB of the 128 KB arena) |
| `tools/compare_builds.sh d854eda nospread` | this tree's audio against the commit before the performance pass, four 24 s sessions (`tests/golden.c`: steady, live with every control moving, the LXR FX chain, the delay and reverb one at a time), spreading off | **bit-identical**, all four (6 144 000 samples) |
| `tools/compare_builds.sh d854eda` | the same with spreading on | steady and colour bit-identical (nothing touched after PLAY); live and apart differ from their first control change after PLAY onwards, which reaches the engine up to one LXR block later (oscillator phases and noise then decorrelate) |
| sanitizers (by hand: the engine and `test_engine`, `test_perform`, `golden` built with `-fsanitize=address,undefined`, GCC 13) | memory errors, undefined behaviour | 0 memory errors; all checks pass. UBSan reports: the LXR oscillator's interpolation reading index 1024 of a 1024-entry wavetable row (upstream code: it reads the next row's first sample, inside the bound table memory; the firmware does the same, left as it is); left shifts of negative values in upstream LXR and Pocket code (defined by GCC and Clang) and two in CLATTER 12's mix (now `* 256`) |
| `check_link.py` (in `make m33`) | math functions only from Plinky's known set or defined in the engine; no software double precision | `logf` and `fmodf` (the LXR compressor and fold distortion) are defined in the engine, as `fmaf` was; 0 soft-double calls anywhere. The Plinky API names are checked against `sim/plinky_sim.h` when the published bundle cannot be fetched (in CI it is fetched) |
| `make m33-bench` | instructions per 2 ms output block on a Cortex-M33 in QEMU (below) | see "CPU" |
| `make m33-profile` | the same, stage by stage (each voice, the mix stages, the converter) | see "CPU" |
| `check_api.py` | the Plinky names the glue uses exist in the published API bundle | **52 of 52 present** (GitHub Actions, which can fetch the pinned bundle; this session could not reach plinky12.com). Includes the page, save-page and serialiser names (`get_scroll_page`, `scroll_to_page`, `panel_page_t`, `serialiser_t`, ...) |

## CPU

**Not measured on a Plinky.** Three sources, in order of trust:

1. **On the device**: the DSP settings page (right side button up from SEQ) and the log line every 2 s
   (`c12 dsp avg … max … us (lxr … mix … rs …)`) time each `on_dsp` and each stage with `time_us()`.
   This is the number that decides. Not yet available. It times the panel's `on_dsp` only: Plinky's own
   mix and FX chain run after it on core 1 (the panel returns `false`) and are not included.
2. **Plinky's IDE emulator**: `time_us()` does not advance within an audio block there, so the readout
   was 0. The DSP page now says `n/a` in that case instead of 0 %.
3. **QEMU's Cortex-M33 model** (`make m33-bench`, `make m33-profile`; mps2-an505, `-icount`): the engine
   as C, compiled by GCC 13 `-O2` for the M33 with its FPU, everything in RAM, counting the instructions
   each 64-frame output block executes.

### What the figures are

- **Block and budget.** Plinky's audio block is 64 frames at `SAMPLE_FREQ` 32 000 Hz (the emulator's log
  confirmed 32 kHz): 2.000 ms. The LXR runs at 44 140 Hz in 64-frame blocks (1.450 ms), 1.379 of them
  per output block. The RP2350's standard clock is 150 MHz: 300 000 cycles a block. Plinky's real
  core-1 clock is not published. The percentages below are instructions / 300 000.
- **Instructions, not cycles.** QEMU counts instructions. It does not model wait states: no flash XIP
  cache misses, no PSRAM latency (the bench's delay lines are plain RAM), no multi-cycle loads, branch
  refills or divides. So every figure is a **lower bound** on cycles; a realistic factor is 1.2-1.6.
- **What is included.** All of `c12_engine_render`: the event drain, the sequencer and performance
  layer, the six LXR voices, the mix, the LXR FX, the delay and reverb, the limiter, the converter and the
  output conversion. Not included: the panel's `on_dsp` copy into Plinky's buffers (about 1 000
  instructions), and Plinky's own output chain after it.
- **Average and peak.** The mean over 400-1 500 blocks of a scenario, and its busiest block. The peak
  decides whether audio is glitch-free with one block of buffering.
- **The worst case.** "WORST" changes the tempo every block (no player does), with 8x ratchets on every
  track, flams, beat repeat, three note repeats, the delay, reverb and compressor. The heaviest *DSP*
  configuration is the whole LXR FX chain with bits on every track ("+ bit reduction").

### Before and after this pass (QEMU instructions per 2 ms block; % of 300 000)

Before: commit `d854eda`; after: this tree, spreading on. Same bench, same scenarios
(`tests/m33/bench.c`), GCC 13 `-O2`.

| Scenario | before: mean | before: busiest | after: mean | after: busiest |
|---|---|---|---|---|
| stopped, silent | 169 000 (56 %) | 244 600 (82 %) | 156 600 (52 %) | 178 900 (60 %) |
| playing all seven tracks every 16th | 178 000 (59 %) | 269 900 (90 %) | 165 200 (55 %) | 192 800 (64 %) |
| all tracks, 4x ratchets, 300 BPM | 190 200 (63 %) | 272 600 (91 %) | 177 600 (59 %) | 197 400 (66 %) |
| + sends to Plinky's FX, a level change | 193 100 (64 %) | 277 800 (93 %) | 180 100 (60 %) | 209 400 (70 %) |
| **delay and reverb, all tracks** | **210 200 (70 %)** | **304 600 (102 %)** | **195 700 (65 %)** | **218 400 (73 %)** |
| both frozen | 208 700 (70 %) | 302 300 (101 %) | 194 200 (65 %) | 216 300 (72 %) |
| delay mono | 208 100 (69 %) | 301 700 (101 %) | 193 100 (64 %) | 214 900 (72 %) |
| + LXR drive on all tracks | 224 900 (75 %) | 326 000 (109 %) | 210 700 (70 %) | 234 300 (78 %) |
| + ring modulator | 233 000 (78 %) | 337 500 (113 %) | 218 600 (73 %) | 240 100 (80 %) |
| + compressor (the whole chain) | 258 800 (86 %) | 375 200 (125 %) | 244 600 (82 %) | 271 800 (91 %) |
| + bit reduction | 260 600 (87 %) | 377 500 (126 %) | 246 000 (82 %) | 273 800 (91 %) |
| compressor alone | 238 600 (80 %) | 346 000 (115 %) | 224 300 (75 %) | 257 700 (86 %) |
| delay and reverb + FILL, DENSITY +60 | 210 400 (70 %) | 305 100 (102 %) | 195 800 (65 %) | 218 900 (73 %) |
| + BEAT REPEAT 1/32, all tracks | 212 900 (71 %) | 304 300 (101 %) | 198 400 (66 %) | 218 100 (73 %) |
| + four note repeats at 1/32T | 214 500 (72 %) | 305 900 (102 %) | 199 900 (67 %) | 219 800 (73 %) |
| **WORST** | **252 300 (84 %)** | **358 400 (119 %)** | **232 000 (77 %)** | **251 900 (84 %)** |

New scenarios (after only; delay and reverb on, all tracks): FX values moving every block 200 400 /
222 900 (74 %); a whole new kit queued at once every 100 blocks 198 000 / 232 700 (78 %); a scene
every 50 blocks 190 800 / 216 000; PLAY and STOP every 25 blocks 196 300 / 214 700. Spreading off in
the same build, delay and reverb on, all tracks: mean 195 600, busiest **283 500 (94 %)**; on: 216 100
(72 %).

Against the targets in the brief (average under 65 %, peak under 80 % for normal playback with the
delay and reverb; no overrun and preferably under 90 % for heavy playback): normal playback is at
65 % / 73 % *in instructions*; heavy playback is 77-82 % / 84-91 %. Whether either holds in cycles is
for the device: with 1.2 cycles an instruction the normal case's busiest block would be 88 %, with
1.6, 117 %.

### Where it goes (`make m33-profile`, per LXR block unless noted; all tracks playing)

| Stage | instructions | note |
|---|---|---|
| head: events, sequencer, performance, modulation, LFOs, filter coefficients | 6 700 (max 10 500) | 1 900 stopped; a whole kit queued at once: up to 21 300 (32 events a block) |
| DRUM 1, DRUM 2, DRUM 3 | 14 400, 14 300, 14 300 | the LXR's own code, unchanged; **the same silent as playing** |
| SNARE, CYMBAL, HI-HAT | 11 600, 14 100, 14 000 | as above |
| mix: voices, sends, meters | 2 800 (7 800 with sends, 13 800 with tracks routed to the LXR FX) | |
| mix: delay and reverb | 14 500 when either runs (before: 15 700); 100 idle | |
| mix: LXR FX chain | 29 200 with drive and compressor (two stages of 32 frames) | the LXR's own code |
| mix: master, DC blocker, limiter, sends ring | 7 600 | |
| converter and output, **per output block** | 24 300 (before: 33 900) | |

The six voices are 83 000 of the 87 000 instructions of an LXR block when nothing plays: the LXR renders
every voice every block, as the firmware does. Skipping a silent voice would change its oscillator,
filter and smoothing state (and the shared noise), so it is not done here: it is a candidate only if a
device measurement shows it is needed, with a comparison of the sound.

### What changed (each measured, each bit-identical unless said)

1. **Spreading** (`c12_engine.c`, "the LXR block in stages"). An LXR block is now run as twelve stages,
   Pocket's sequence of calls unchanged and in order. After making a callback's output the engine goes
   on with the next block's stages until the callback has done its average share of the work (1.379
   blocks), never past what the next callback needs and never into a second block beyond its own. The
   busiest callbacks no longer render two whole LXR blocks. With everything here the peaks above fall by
   25-30 %; spreading alone, in one build, delay and reverb on: 283 500 → 216 100. The output
   samples are the same samples; a control change made between callbacks may reach the engine one LXR
   block (1.45 ms) later (`tests/test_perform.c` bounds it). `c12_engine_set_spread(0)` restores the
   first version's timing exactly.
2. **Converter**: the coefficient interpolation fused into the window, same arithmetic in the same order;
   33 900 → 24 300 per output block.
3. **Pocket's Q23 output stage** no longer runs: CLATTER 12 only ever used the voices' sum from it, and
   reads that sum itself (about 4 000 per LXR block).
4. **FX setters**: each group runs only when one of its own values moves; a tempo change only moves the
   delay time. Before, any FX change or tempo change re-ran every LXR FX setter (the compressor's in
   double precision) and the delay and reverb coefficients (`powf`, `expf`).
5. **Delay and reverb**: the delay's lines interleaved (one 32-bit word a sample: one PSRAM stream in
   each direction instead of two, one load per tap), its loop made once per mode; the reverb's read
   positions moved on with the write position; each effect idles on its own. 15 700 → 14 500. On the
   device the larger change should be memory: the reverb's 26 KB of lines moved from PSRAM to the
   panel's SRAM arena (six scattered streams), the delay's 256 KB stay in PSRAM as one sequential stream.
6. The LXR FX chain runs as two stages (its two 32-frame chunks), so it no longer sits in one block.

### Compiler flags (Plinky's server flags are not published)

The same bench with GCC 13 at `-Os`: about 20-24 % more instructions (delay and reverb, all tracks:
mean 235 600, busiest 261 600, 87 %; WORST 271 000 / 297 200, 99 %). At `-O3`: 2-3 % fewer than `-O2`.
`make m33-bench M33_BENCH_OPT=-Os` repeats it.

Host timing in the simulated session (x86-64, this container, NOT predictive): `on_dsp` mean 31 µs per
2000 µs block.

## Authenticity

Sample-by-sample, CLATTER 12 differs from Pocket's reference by ~28 dB on noise voices (snare,
cymbal, hats). This is decorrelation, not a different sound: compiled with CLATTER's own flags
(`-O3 -ffast-math`) the same code matches Pocket at 92.6 dB; Pocket's own single-precision FM-1
configuration diverges the same way under random parameter moves (11 dB, measured here with
Pocket's libraries); and per-voice envelopes and spectra agree to hundredths of a dB. The tonal
voices agree sample-for-sample at 82-89 dB. The criteria in `test_authentic` are therefore envelope
and spectrum, with sample SNR printed for information.

## The simulator is not Plinky

`sim/plinky_sim.*` is written from Plinky's documentation and from published community panels: one
finger per pad, fingers are physical (a page change shows the same finger on the new page, with its
original touch origin), pages stacked vertically as Plinky's, a JSON serialiser with Plinky's
`OBJECT_BEGIN`/`FIELD`/`FIELD_BASE64` shape, a staged load (fresh panel, `on_prepare_staged_load`,
read, then copied over the live one) as Plinky does it, no slides, no debounce, instant transport,
approximate palette, `time_us` advancing with simulated audio (so the DSP page reads `n/a` there). It shows that CLATTER 12's logic does what it should given the documented
API. It cannot show how Plinky's real touch scanning, timing, LEDs or audio behave.

## In Plinky's IDE (2026-11-04, by the owner)

First **Test** in the Plinky 12 code editor (WebAssembly build, Debian clang 21.1.8): the panel
**compiled** (11 warnings, all in the LXR's own code: float/int conversions that every CLATTER build
has), and **failed to link**: `undefined symbol: fmaf`, Plinky's WebAssembly C library has none.
Fixed by defining `fmaf` in the engine (`tools/amalgamate.py`, `CXX_PRELUDE`) and by
`tools/check_link.py`, which now fails the build for any math function not known to exist on Plinky.
Re-tested the same day: **links and runs in Plinky's emulator** (wasm build `7df25fb`, 2026-11-02).

What the emulator log shows (owner's run, about four minutes of playing and one reload):

- `clatter12: engine up, out rate 32000 Hz`: bring-up works, and `SAMPLE_FREQ` is 32 000 there.
- About 100 000 output blocks with **0 underruns** and at most 2 LXR blocks per output block; the
  LXR/output ratio stays 1.379, as in the host tests.
- **`dsp avg 0 max 0 us` throughout.** The timing readout does not work in the emulator
  (`time_us()` evidently does not advance across an audio block there), so the DSP page and log say
  nothing about CPU load until the panel runs on a device. The cause is not confirmed.
- **Clipping: 17 saturated samples in about four minutes**, starting when the owner began editing
  (the engine's 16-bit stage, before Plinky's output chain). The host session and tests had none.
  **Addressed since**: the master keeps its 3 dB of headroom and an output limiter now follows the
  mix (peak follower, instant attack, 80 ms release, threshold -1 dBFS so the converter's ringing
  stays below full scale). With nothing near full scale it multiplies by exactly 1 (the engine tests'
  figures are unchanged); the 40 s simulated editing session, all FX included, clips 0 samples.
- The `failed to open json preset file /PLINKY/BANKS/...` lines are the stock Blocks panel looking
  for sound banks missing from the emulator's disk image. They are not CLATTER 12's.

This log does not say how it sounded or whether touches and LEDs behaved: not yet recorded.

## The browser demo

The page runs the panel itself (`sim/wasm_panel.cpp`: the generated panel file and the simulator, compiled to
WebAssembly), so the panel tests above cover what it does and `test_wasm.mjs` covers the build. In headless Chromium
(Playwright, 2026-11-04) the page was driven end to end: the panel boots silent on the page's thread; the first tap starts
an AudioWorklet at 32 kHz, the project moves into it (saved and loaded with the panel's own serialiser), the tapped step
lights, PLAY plays (level meter peaks about 0.6 of full scale), each of the six page pads changes page with the row labels
following, no page errors; no horizontal scrolling at 390 px wide. Not tested: Firefox and Safari, a real touch screen,
and listening on real speakers. The previous version was tested in a real Chromium the same way (2026-11-04).

## Untested

This version in Plinky's emulator (the first version ran there), and anything on a Plinky 12. Also:
Plinky's real serialiser and save page with CLATTER 12's field (only the simulator's stand-in, written
from the published panels' usage, has read and written it), page scrolling on the device, and whether
Plinky's delay and reverb process the sends as the API describes. Specifically core-1 CPU load (cycles,
not instructions; the core clock; what Plinky's own output chain costs after `on_dsp`), linking beside
the firmware (140 KB of static SRAM), the delay's lines in PSRAM and the reverb's in the panel arena
across a staged load, PSRAM table speed (`_psram`), the IDE's source-size limit and compiler flags,
whether eight pages scroll, touch latency, LED update cost, `SAMPLE_FREQ`'s actual value, transport
behaviour with external clock, and audio level through Plinky's output stage.

## On a device (first session)

[HARDWARE_TESTING.md](HARDWARE_TESTING.md): what to back up, the staged checklist, what to record and
when to stop.
