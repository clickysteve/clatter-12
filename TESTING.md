# Testing

`make test` runs the host tests; `make m33` the Cortex-M33 compile and link check; `make m33-bench` the
instruction counts in QEMU; `make test-wasm` the page's WebAssembly build. None involves a Plinky 12;
none is hardware verification.

| Test | What it shows | Result (2026-11-05, Linux, clang 18, GCC 13.2 for Arm) |
|---|---|---|
| `test_resampler` | 44 140 → 32 000 Hz converter: gain, alias rejection, exact long-run rate | 13/13 |
| `test_engine` | each trigger converted vs an ideal conversion of the 44 140 Hz LXR; pitch, level, decay; sequencer timing; all triggers with three kits: no underruns, no clipping; block-size independence. **CLATTER 12 now makes its own mix** (for the mixer and FX): these checks hold it to Pocket's, and they pass unchanged, to the same figures | 37/37 |
| `test_perform` | **articulation, performance and the delay and reverb at engine level**, timed to the LXR block (1.45 ms) by the engine's trigger log: a 4x ratchet's strokes on the first block at or after k/4 of the step; FLAT/FADE/ACCENT/ALTERNATING curves; 8x at 300 BPM plays 4, none closer than 12 ms, 8 at 120; version-1 cells convert; FLAM's grace 31 ms ahead with the hit on the plain step's own block, DRAG's two graces 15.6 ms apart, a first-step flam; DOUBLE and TRIPLE spacing and velocities; ACCEL/DECEL gaps closing/opening, velocities rising/falling, never under 12 ms; rolls by hand stopped (at once) and playing (next step); **8 s of a random tempo every 4 ms with ratchets: 0 underruns, 0 clipped**, then steps even at the new tempo; beat repeat 1/4 (kick again on 4, 8, 12, snare held back; released, the next bar as written; pattern unchanged); a 1/32 stutter twice a step, fading; note repeat 1/16 and 1/8T at exact intervals, following a tempo change, on the sequencer's grid while playing, chance 50 %; FILL adds more late in the bar; DENSITY -60 keeps loud over soft, -100 silent, +100 ghosts only on a playing track; a scene's mute waits for the bar and holds the downbeat back; **the delay**: echo a dotted eighth later, repeats, ping-pong sides, 1/4 moves it to 500 ms, feedback 90 vs 0, the filter darkens it, wet 0 silent; **the reverb**: a tail, decay 100 vs 0, size, damping; delay freeze holds 5 s and decays when let go; idle costs nothing; a throw sends a track with no send; PLINKY engine: no return; unbound memory: off | 66/66 |
| `test_authentic` | CLATTER 12's C++ engine vs CLATTER Pocket's own build; generator, mutation and kit designer vs Pocket's | 21/21; 880/880 patterns, 880/880 mutations, 1440/1440 kits identical |
| `equiv_c` / `equiv_cxx` | the generated engine text as C and as C++ | 97.8 % of samples identical, max 2 LSB, 87.9 dB |
| `test_panel_sim` | **the real panel source** in the simulator, 201 checks. As before: boot, play, playhead; step on/off/hold, pressure velocity; variations through held steps; conditions over whole passes; ratchets (4x three strokes a pass, auditioned when stopped), rolls and curves; mute and solo silence the output; lock, length, page view and copy, swing; GEN, MUT, UNDO, REDO, CLR; SOUND rows, kit generation, GEN controls; MIX levels, pan, master; COLOUR FX; saving and loading; 0 underruns and 0 clipped samples over the session (132 s). New: **every page reached from each of the seven panel pages**; **TEMPO**: four taps at 500 ms give 120, at 400 ms 150 and the sequencer follows; the held overlay shows the digits, +1, -10, half, double, a preset, a held nudge and its return, release restores the page, a hold is not a tap; rapid changes while playing: 0 underruns; **PERFORM**: held MUTE silences and release restores (the mixer's mute untouched), KILL ALL, FILL adds hits with the pattern unchanged, MUTATE plays a mutation with the stored pattern untouched and gone on release, DENSITY, STUTTER, THROW each held and released, a held pad let go by a page change, LATCH holds and a second tap or RELEASE lets go, a brief COMMIT does nothing, a held COMMIT writes the mutation and mute, UNDO takes it back; **SCENES**: store by hold, recall waits for the bar, then levels, mutes and FX as stored; **REPEAT**: a held pad repeats 1/16, pressure is velocity (light 45+), release stops, RATE mode four times as fast pressed hard, 1/8T, a TRIPLE roll pad, BEAT REPEAT with the pattern unchanged; **FX SPACE**: each of delay time, feedback, filter, wet, size, decay, damping, reverb wet changes the sound's envelope by more than three times the bar-to-bar difference, MONO makes left and right alike, freeze is momentary; sends reach Plinky's buffers only on FX ENGINE PLINKY; **a version-1 project** loads with its ratchet converted, its swing, tempo, sends and the PLINKY engine, and its version-2 save loads back the same | 201/201; panel object 45 448 bytes |
| `test_wasm.mjs` | `docs/assets/c12panel.wasm` under node, as the page drives it: no imports; boot; audio; a step; a held step edited; **all eight pages reached from row 15, and every pad lit on them has a description** (both FX banks, the tempo overlay too); tap tempo; save, change, load; a foreign file refused; tempo | 33/33 |
| `make m33` | GCC compile of engine **and panel** (the panel object constructed, as the firmware does) for Cortex-M33 + FPv5-SP, three variants; section sizes | 117 KB code, 27 KB constants, 41 KB static SRAM + 100 KB tables (`_psram`: tables in PSRAM); `_sram` puts 14 KB of LXR code in SRAM; the delay and reverb's 282 KB are PSRAM, bound at run time |
| `check_link.py` (in `make m33`) | math functions only from Plinky's known set or defined in the engine; no software double precision | `logf` and `fmodf` (the LXR compressor and fold distortion) are defined in the engine, as `fmaf` was; 0 soft-double calls anywhere. The Plinky API names are checked against `sim/plinky_sim.h` when the published bundle cannot be fetched (in CI it is fetched) |
| `make m33-bench` | instructions per 2 ms output block on a Cortex-M33 in QEMU (below) | see "CPU" |
| `check_api.py` | the Plinky names the glue uses exist in the published API bundle | **52 of 52 present** (GitHub Actions, which can fetch the pinned bundle; this session could not reach plinky12.com). Includes the page, save-page and serialiser names (`get_scroll_page`, `scroll_to_page`, `panel_page_t`, `serialiser_t`, ...) |

## CPU

**Not measured on a Plinky.** Three sources, in order of trust:

1. **On the device**: the DSP settings page (right side button up from SEQ) and the log line every 2 s
   (`c12 dsp avg … max … us (lxr … mix … rs …)`) time each `on_dsp` and each stage with `time_us()`.
   This is the number that decides. Not yet available.
2. **Plinky's IDE emulator**: `time_us()` does not advance within an audio block there, so the readout
   was 0. The DSP page now says `n/a` in that case instead of 0 %.
3. **QEMU's Cortex-M33 model** (`make m33-bench`, mps2-an505, `-icount`): the engine as C, compiled by
   GCC for the M33 with its FPU, everything in RAM, counting the instructions each 64-frame output block
   executes. Instructions are not cycles: on the RP2350 most instructions take one cycle, but loads,
   branches, divides and especially flash (XIP cache misses) and PSRAM add more. So these are **lower
   bounds** on cycles; a realistic factor is 1.2-1.6.

| Instructions per 2 ms block (QEMU, Cortex-M33), 2026-11-05 | mean | busiest | LXR + seq | mix/FX | of which SPACE | convert |
|---|---|---|---|---|---|---|
| 0.0.2 (before this work): stopped, silent | 168 400 | 243 800 | 119 900 | 13 000 | | 35 500 |
| stopped, silent | 169 000 | 244 600 | 120 400 | 13 000 | 0 | 35 500 |
| playing, all seven tracks every 16th | 178 000 | 269 800 | 129 000 | 13 500 | 0 | 35 500 |
| all tracks, 4x ratchets, 300 BPM | 190 200 | 272 600 | 137 800 | 16 800 | 0 | 35 500 |
| + sends to Plinky's FX and a level change | 193 100 | 277 800 | 129 400 | 25 300 | 0 | 38 400 |
| **sends to CLATTER 12's delay and reverb** | **210 200** | **304 600** | 128 900 | 45 700 | 21 700 | 35 500 |
| both frozen | 208 700 | 302 300 | 128 900 | 44 200 | 20 100 | 35 500 |
| + LXR drive, ring, compressor, bits (everything) | 260 600 | 377 500 | 129 000 | 96 100 | 21 700 | 35 500 |
| delay and reverb + FILL (3 tracks), DENSITY +60 | 210 400 | 305 100 | 129 000 | 45 800 | 21 700 | 35 500 |
| delay and reverb + BEAT REPEAT 1/32, all tracks | 212 900 | 304 300 | 131 600 | 45 700 | 21 700 | 35 500 |
| delay and reverb + four note repeats at 1/32T | 214 500 | 305 900 | 133 200 | 45 800 | 21 700 | 35 500 |
| **worst: tempo changing every block, 8x ratchets, flams, beat and note repeats, delay, reverb, compressor** | 252 300 | 358 400 | 137 800 | 74 300 | 21 700 | 40 200 |

The performance machinery (strokes, graces, the musical clock, repeats, fills, density) costs about
600 instructions a block when nothing is playing (169 000 vs 168 400) and a few thousand when it is
busy (note repeats, beat repeat: +2 000 to +5 000 in the LXR column, which includes the block hook).
CLATTER 12's delay and reverb cost about 21 700 a block when something is sent to them (a first
version cost 35 300; reading positions and slews are now worked out once a block), 7 % of a 150 MHz
core 1 as a lower bound, and nothing when idle. They run on memory QEMU makes as fast as RAM: on the
device their 282 KB are in PSRAM, behind the XIP cache, read as six sequential streams: **not
measured, and the likeliest place for a surprise**.

The "busiest" block is one in which two LXR blocks are rendered (1.379 a block on average, so about
four blocks in ten). At 150 MHz a block is 300 000 cycles. So, as lower bounds: the default engine
uses **56 % of core 1 on average and 81 % in its busiest blocks**; with all three LXR effects, 80 % and
115 %. With a realistic 1.2-1.6 cycles per instruction the default busiest block is at or past the
budget unless Plinky clocks core 1 above 150 MHz or buffers more than one block. The new mixer, limiter
and ratchet machinery cost 4.7 % over the previous version (CLATTER 12's own mix is skipped when the
mixer is neutral, and its integer stages are one `SMULL` each where Pocket's are 64-bit library calls).

What this means: the CPU question raised in FEASIBILITY.md is now sharper, not answered. The effects
are off by default and cost nothing when off. **With CLATTER 12's delay and reverb in use the busiest
blocks pass 300 000 instructions** (lower bound): on a 150 MHz core 1 with one block of buffering that
would crackle. Their sends are 0 by default (the default project costs what 0.0.2 did), and FX ENGINE
PLINKY hands the sends to Plinky's own effects (whose CPU is Plinky's, and not on the panel's
`on_dsp`) at no cost to the panel. Which to use on the device is a measurement for the DSP page. If the device measurement shows overruns, the order of
things to try is in ROADMAP.md (LXR code in SRAM is the prepared first step).

Host timing in the simulated session (x86-64, this container, NOT predictive): `on_dsp` mean 36 µs per
2000 µs block (the session now spends much of its time with the delay and reverb running).

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
Plinky's delay and reverb process the sends as the API describes. Specifically core-1 CPU load, linking beside
the firmware (static SRAM), PSRAM table speed, the IDE's source-size limit and compiler flags,
touch latency, LED update cost, `SAMPLE_FREQ`'s actual value, transport behaviour with external
clock, and audio level through Plinky's output stage.

## On a device (first session)

1. `clatter_12.cpp` in the IDE emulator: default beat plays; every page; edit, GEN, NEW KIT; save to a slot, load it.
2. Flash; note link errors (then try `clatter_12_psram.cpp`).
3. DSP page and log: mean/max µs per block idle, playing all tracks, while loading a kit.
   Over ~1800 µs max means crackle; try `clatter_12_sram.cpp`.
4. Listen for crackle at block boundaries; compare a kit with CLATTER Web.
