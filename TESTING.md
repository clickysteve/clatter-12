# Testing

`make test` runs all of these. None involves a Plinky 12; none is hardware verification.

| Test | What it shows | Result (2026-11-03, macOS, Apple clang 21) |
|---|---|---|
| `test_resampler` | 44 140 → 32 000 Hz converter: gain 100 Hz-12 kHz, alias rejection, exact long-run rate, other output rates | 0.003 dB to 12 kHz; aliases -70.7 dB (19 kHz), -73.5 dB (20 kHz), -78.2 dB (22 kHz); no drift over 100 s; 12/12 |
| `test_engine` | each trigger converted vs an independent ideal conversion of the 44 140 Hz LXR (SNR below 11 kHz); pitch, level, decay vs the native rate; sequencer timing at 120 BPM; all triggers every 16th with three kits: underruns, clipping | SNR 50-79 dB; kick 49.00/49.00 Hz, 261/261 ms (LXR clocked at 32 kHz instead: 35.8 Hz, 356 ms); beats within one LXR block (1.45 ms) of 500 ms, no drift; 0 underruns, 0 clipped; 32/32 |
| `test_authentic` | CLATTER 12's C++ engine vs **CLATTER Pocket's own build** (C, double precision, CLATTER's libm), 20 s with six designed kits, all triggers, variations; and the generator, mutation and kit designer vs Pocket's | envelopes within 0.10 dB (95 %), spectra within 0.03 dB per third-octave band; 880/880 patterns, 880/880 mutations, 1440/1440 kits identical; 21/21 |
| `equiv_c` / `equiv_cxx` | the generated engine text compiled as C and as C++, same scenario | 95 % of samples identical, max 2 LSB, 84 dB: C++ picks the `float` overloads of `<math.h>` functions where C promotes to `double` |
| `test_panel_sim` | **the real panel source** in the simulator: boot, play, playhead, pressure velocity, toggle, hold, variations, pages, copy, length, GEN/MUT/UNDO/redo/CLR, kits while playing | 36/36; panel object 2 728 bytes; writes `build/sim/*.ppm`, `build/sim/session.wav` |
| `check_api.py` | every Plinky name the glue uses (via the simulator) exists in the published API bundle | 36 names found (`SAMPLE_FREQ` only in Plinky's own example panels) |
| `make m33` | GCC compile for Cortex-M33 + FPv5-SP, three variants, section sizes | compiles; see ARCHITECTURE.md "Memory" |

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

## Untested

On a Plinky 12 or in Plinky's emulator: everything. Specifically core-1 CPU load, linking beside
the firmware (static SRAM), PSRAM table speed, the IDE's source-size limit and compiler flags,
touch latency, LED update cost, `SAMPLE_FREQ`'s actual value, transport behaviour with external
clock, and audio level through Plinky's output stage.

## On a device (first session)

1. `clatter_12.cpp` in the IDE emulator: default beat plays; edit, GEN, KIT.
2. Flash; note link errors (then try `clatter_12_psram.cpp`).
3. DSP page and log: mean/max µs per block idle, playing all tracks, while loading a kit.
   Over ~1800 µs max means crackle; try `clatter_12_sram.cpp`.
4. Listen for crackle at block boundaries; compare a kit with CLATTER Web.
