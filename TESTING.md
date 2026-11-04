# Testing

`make test` runs all of these. None involves a Plinky 12; none is hardware verification.

| Test | What it shows | Result (2026-11-03, macOS, Apple clang 21) |
|---|---|---|
| `test_resampler` | 44 140 → 32 000 Hz converter: gain 100 Hz-12 kHz, alias rejection, exact long-run rate, other output rates | 0.003 dB to 12 kHz; aliases -70.7 dB (19 kHz), -73.5 dB (20 kHz), -78.2 dB (22 kHz); no drift over 100 s; 12/12 |
| `test_engine` | each trigger converted vs an independent ideal conversion of the 44 140 Hz LXR (SNR below 11 kHz); pitch, level, decay vs the native rate; sequencer timing at 120 BPM; all triggers every 16th with three kits: underruns, clipping | SNR 50-79 dB; kick 49.00/49.00 Hz, 261/261 ms (LXR clocked at 32 kHz instead: 35.8 Hz, 356 ms); beats within one LXR block (1.45 ms) of 500 ms, no drift; 0 underruns, 0 clipped; 32/32 |
| `test_authentic` | CLATTER 12's C++ engine vs **CLATTER Pocket's own build** (C, double precision, CLATTER's libm), 20 s with six designed kits, all triggers, variations; and the generator, mutation and kit designer vs Pocket's | envelopes within 0.10 dB (95 %), spectra within 0.03 dB per third-octave band; 880/880 patterns, 880/880 mutations, 1440/1440 kits identical; 21/21 |
| `equiv_c` / `equiv_cxx` | the generated engine text compiled as C and as C++, same scenario | 98 % of samples identical, max 2 LSB, 88.8 dB: C++ picks the `float` overloads of `<math.h>` functions where C promotes to `double` |
| `test_panel_sim` | **the real panel source** in the simulator: boot, play, playhead, pressure velocity, toggle, hold, variations, pages, copy, length, GEN/MUT/UNDO/redo/CLR, kits while playing | 36/36; panel object 2 728 bytes; writes `build/sim/*.ppm`, `build/sim/session.wav` |
| `check_api.py` | every Plinky name the glue uses (via the simulator) exists in the published API bundle | 36 names found (`SAMPLE_FREQ` only in Plinky's own example panels) |
| `block size` (in `test_engine`) | the audio must not depend on the block size: 64 (Plinky), 96 and 128 frames (a browser's audio worklet) render bit-identical audio, with no underruns | passes. **It found a latent bug**: the converter's input buffer (256 frames) was too small for 128-frame blocks and stopped the engine for good; the panel's 64-frame blocks never hit it. Buffer now 512 (2 KB more RAM) |
| `test_wasm.mjs` (`make test-wasm`) | the browser build `docs/assets/c12.wasm` under node, called as the page calls it: no imports; style and kit names equal the page's; **generator and mutator reproduce the native ones byte for byte (99 of 99 patterns)**; a 5 s scenario (generate, designed kit loaded mid-render, mutate) renders **100% sample-identical** to the native C build (426 dB); the sequencer position moves and returns 255 on stop; runs at 44.1 and 48 kHz | 15/15 |
| `make m33` | GCC compile for Cortex-M33 + FPv5-SP, three variants, section sizes | compiles; see ARCHITECTURE.md "Memory" |
| `check_link.py` (in `make m33`) | every math function the engine calls is one Plinky's panels are known to link with, or defined in the engine; M33 objects reference nothing else; **no software double-precision call on the audio path** | expf, sinf, sqrtf (+ mem*) from Plinky; fmaf in the engine; 0 soft-double calls (was ~300 before literals were made float) |

## Authenticity

Sample-by-sample, CLATTER 12 differs from Pocket's reference by ~28 dB on noise voices (snare,
cymbal, hats). This is decorrelation, not a different sound: compiled with CLATTER's own flags
(`-O3 -ffast-math`) the same code matches Pocket at 92.6 dB; Pocket's own single-precision FM-1
configuration diverges the same way under random parameter moves (11 dB, measured here with
Pocket's libraries); and per-voice envelopes and spectra agree to hundredths of a dB. The tonal
voices agree sample-for-sample at 82-89 dB. The criteria in `test_authentic` are therefore envelope
and spectrum, with sample SNR printed for information.

## The simulator is not Plinky

`sim/plinky_sim.*` is written from Plinky's documentation: one finger per pad, no slides, no
debounce, instant transport, approximate palette, `time_us` advancing with simulated audio (so the
DSP page reads 0 % there). It shows that CLATTER 12's logic does what it should given the documented
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
  Not yet addressed: more headroom or a soft limiter (ROADMAP).
- The `failed to open json preset file /PLINKY/BANKS/...` lines are the stock Blocks panel looking
  for sound banks missing from the emulator's disk image. They are not CLATTER 12's.

This log does not say how it sounded or whether touches and LEDs behaved: not yet recorded.

## The browser demo

The project page's sound is the same engine, so the tests above cover it, and `test_wasm.mjs` covers the build itself. In a real browser (Chromium, 2026-11-04, via the app's browser pane) the page was driven end to end: a real click starts the audio context at 32 kHz, the engine reports ready, the playhead follows the engine's sequencer through all 16 steps, GEN produced a new pattern at the current length, KIT, MUT, mute and STOP behaved, and the level meter showed peaks of 0.45 to 0.92 of full scale. Not tested: Firefox and Safari, and listening on real speakers.

## Untested

Whether it sounds and plays right in the emulator (not reported), and anything on a Plinky 12.
Specifically core-1 CPU load, linking beside
the firmware (static SRAM), PSRAM table speed, the IDE's source-size limit and compiler flags,
touch latency, LED update cost, `SAMPLE_FREQ`'s actual value, transport behaviour with external
clock, and audio level through Plinky's output stage.

## On a device (first session)

1. `clatter_12.cpp` in the IDE emulator: default beat plays; edit, GEN, KIT.
2. Flash; note link errors (then try `clatter_12_psram.cpp`).
3. DSP page and log: mean/max µs per block idle, playing all tracks, while loading a kit.
   Over ~1800 µs max means crackle; try `clatter_12_sram.cpp`.
4. Listen for crackle at block boundaries; compare a kit with CLATTER Web.
