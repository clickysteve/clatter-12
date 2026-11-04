# CLATTER 12: technical feasibility

Investigated 2026-11-03. Sources are linked; anything not from an authoritative source is marked.
**Nothing here has run on a Plinky 12.** "Measured" below means on a desktop, in a cross-compile,
or in CLATTER 12's own simulator, and says which.

## Verdict

**Feasible, and started.** Plinky 12 custom panels are native C++ compiled into the firmware for the
RP2350, with a documented audio callback on core 1 that may write its own samples. The authentic
LXR-02 engine, through CLATTER Pocket's code, compiles into a single Plinky panel file unchanged
at source level, builds for the Cortex-M33 with GCC, and in host tests sounds the same as CLATTER
Pocket's reference build. No firmware replacement and no extension of Plinky's firmware is needed.

What cannot be known without hardware or Plinky's developers: whether the LXR fits the core-1 time
budget (the deciding question), where ~100 KB of tables may live, and whether the IDE accepts a
306 KB source file. Each has a prepared variant or a fallback (below).

## 1. How custom panels work

| Question | Answer | Source |
|---|---|---|
| How panels are compiled | One C++ file per panel, a class derived from `panel_t`. The IDE's **Test** compiles it to WebAssembly for the in-browser emulator; **Flash** builds native firmware on Plinky's server and sends it over WebUSB. `#include` is banned: headers are injected. | [Custom Panels](https://plinky12.com/codeed.html) (raw: `docs/ide/codeed.md`), [community-panels README](https://github.com/plinkysynth/community-panels) |
| Native or restricted? | **Native.** Panels are linked into the firmware image (community panels are distributed as prebuilt UF2 firmware; a "pack" builds up to four panels into one firmware). No VM, no sandbox. | community-panels README ("prebuilt UF2", "firmware"), [API docs](https://plinky12.com/api_docs.html) |
| Arbitrary real-time DSP? | **Yes.** `bool on_dsp(const int16_t* in, int16_t* out, mix_buffers_t* mix)` runs on **core 1** per **64-sample block**. Return `false` after filling `mix->dry` (and delay/reverb sends) to use Plinky's mix/FX chain, or `true` after writing final stereo to `out`. | API docs, `blocks.h` "Lifecycle Hooks" |
| Entirely custom synthesis engine? | **Yes.** The built-in synth renders only if the panel calls the default `on_dsp`. Plinky's `worm.cpp` replaces it with a granular sampler; the community *Ambiotica* panel runs a full reverb/granular chain. | API docs (worm), [plinky-ambiotica](https://github.com/charlesvestal/plinky-ambiotica) |
| Audio output | 64-frame interleaved int16 stereo blocks. Rate: not stated as a number in the API; Plinky's reverb/delay run "at 16 kHz, half the normal audio sample rate" → **32 kHz**; Ambiotica's author reports 32 kHz, 2000 µs per block. `SAMPLE_FREQ` exists (used by the official `looper.cpp`). | API docs (`do_reverb`), Ambiotica `PORT_NOTES.md` (community) |
| Touch surface | `get_touch_pressure_xy`, `get_touch_down/pressed/released`, touch origin (drag detection), all 256 pads, multitouch. | `blocks_ll.h` "Low-Level Touch Access" |
| RGB LEDs | `set_led(x, y, LED_RGB(r,g,b))` on the same 16×16 grid, 5-bit nominal channels, palette and fade helpers, system overlays (ripple, sparkles). The grid **is** the display. | `blocks_ll.h` "LED And Colour Helpers" |
| Pressure | Continuous per pad, ~0-255 (≈127 a firm press after the user's Sens setting), `touch_pressure_curve_q7` to linearise. | `blocks_ll.h` |
| MIDI, CV | `on_midi` (packed, with port), `midi_write` to USB1/2 and TRS1/2, MIDI clock/transport routing; CV in/out in millivolts, clock/reset in, clock out, MIDI out 2 as dual triggers. | `blocks_ll.h` "Low-Level MIDI I/O", "Low-Level CV" |
| Persistent storage | System-managed: `on_serialise` (song slots, JSON on SD) and `on_serialise_settings` (panel preferences); PSRAM ranges can be appended as a binary footer; preset banks. | `save_and_load.h`, `blocks.h` |
| SD card | Yes, through those helpers (and `read_rgb_ppm`). Panels do not get a general file API in the docs. | API docs |
| Install / update | Flash from the IDE (WebUSB) or from a community page; "re-flashing your Plinky" installs the panel set, stock panels stay; restore via Plinky's [firmware page](https://plinky12.com/firmware.html). | [Community](https://plinky12.com/community.html), codeed.md |

**Can we run the actual LXR engine in a panel? Yes**: see §3. Plinky's firmware does not need to be
extended. (Its source is not public, so extending it was never an option; the Plinky 12 firmware
is not in [plinkysynth's repositories](https://github.com/plinkysynth); `plinky_public` is the
original STM32 Plinky.)

Execution contexts (API docs): `on_ui` ~250 fps on core 0 (stalls during SD work), `on_sequence`
500-1000 fps in an interrupt on core 0, `on_dsp` core 1. CLATTER 12 uses `on_ui` and `on_dsp` only.

## 2. Hardware resources

| | Physical | Available to a panel | Source |
|---|---|---|---|
| CPU | RP2350: 2 × Arm Cortex-M33 (FPv5 **single-precision** FPU, DSP extension), 150 MHz nominal | **Core 1 per 64-frame block: 2000 µs at 32 kHz.** Plinky's clock frequency is not published. Core 1 also runs Plinky's synth if used (CLATTER 12 does not). Whether Plinky's FX chain (used when `on_dsp` returns false) runs on core 1 is not documented (a hook suggests `do_fx` runs on core 0). | [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf), API docs, Ambiotica notes |
| RAM | 512 KB (RP2350: 520 KB SRAM) | **128 KB panel arena** (the panel object), plus a second 128 KB "shadow" arena that the system may reclaim; 4 KB core-0 temp scratch; stack: keep objects < 128 bytes, 256 bytes hard. **Free SRAM for a panel's static (global) data is not documented.** | [plinky12.com](https://plinky12.com/), `blocks.h` "Coding Constraints" |
| PSRAM | 8 MB QSPI | ~7.5 MB (`get_psram_size()`; the end holds the FX delay). Slow, behind the XIP cache; sequential access good, scattered access bad. | plinky12.com, `blocks_ll.h`, Ambiotica notes (mmalex quoted) |
| Flash | 2 MB firmware + 14 MB sample flash | Code/constants link into the firmware image; free space not documented. Hot code can go to SRAM with `PLINKY_DSP_RAM_FUNC` ("the RAM code region is small"). | plinky12.com, `blocks_ll.h` |
| Audio | stereo in/out, USB audio | 32 kHz (see §1), 64-frame blocks | as §1 |
| Floating point | single precision in hardware | `double` is software: the LXR's double-precision envelopes must run in single precision, as on CLATTER Pocket's FM-1 | Arm FPv5-SP; Pocket `ARCHITECTURE.md` |
| Allocation | | no heap (`new`/`malloc` unavailable in practice) | `blocks.h` |
| Existing DSP load | | none if the panel replaces the synth; unknown for Plinky's FX chain | |

## 3. LXR engine portability

**Starting point: CLATTER Pocket** (pinned submodule, `f3ec3ac`). It already isolates the LXR-02
firmware's DSP as freestanding C, runs it natively with no allocation, has a lock-free event ring
between a control context and an audio context, a 64-step sequencer with conditions and
variations, CLATTER's generator, mutation and kit designer, and a single-precision mode built for
an FPU without doubles. CLATTER Web adds only the WebAssembly/browser layer around the same LXR
files (Pocket's `lxr/upstream` is byte-identical to CLATTER's import). Nothing in Pocket is
modified; CLATTER 12 reuses it as is.

| Concern | Finding | How CLATTER 12 handles it |
|---|---|---|
| DSP dependencies | libm (exp, sin, tan, pow), mem*; CLATTER's WebAssembly libm shim | uses the platform libm (the stock panels call `sqrtf`/`powf`); shim headers disabled by their guards |
| Floating point | envelopes and smoothing in `double` | Pocket's `single_precision.h` (`double` → `float`), as on the FM-1 |
| **Sample rate** | coefficients written for `REAL_FS` = 44 140 Hz; only oscillators, LFOs and one-pole filters derive from it. Envelopes, filter cutoffs, decimator and the recorded transients are per-sample: at 32 kHz the kick would drop from 49 Hz to 36 Hz and decay 36 % slower (measured) | **the LXR runs at 44 140 Hz, unchanged, and a 32-tap windowed-sinc converter (`core/c12_resampler.c`) produces 32 kHz**: 1 or 2 LXR blocks per Plinky block (mean 1.379) |
| Buffers | fixed 64-sample LXR blocks (`OUTPUT_DMA_SIZE`) | kept; sequencer and triggers keep the LXR's 1.45 ms block timing |
| Memory | 102 KB of constant tables (wavetables, transients) | kept packed in the source (Pocket's packed tables) and unpacked at start-up into memory the panel chooses: static SRAM or PSRAM |
| Platform code | STM32 HAL shim, TCM section attributes | shim kept; sections mapped to flash or Plinky's `.time_critical` SRAM section |
| Compiler | C99/C11; Plinky compiles C++ | **`tools/amalgamate.py`**: one preprocessed unity, wrapped in `namespace c12`, with a short list of mechanical, count-checked rewrites for C-only constructs (designated initialisers, a K&R prototype, `const` tables written at start-up, one `void*` conversion, an unused `__fp16` declaration, two colliding file-local names). Upstream files are untouched. |
| Voice management | all six voices (seven triggers) computed every block: a fixed cost | unchanged |
| Parameters | LXR parameter handler via Pocket's event ring | unchanged; kits are streamed 48 parameters per UI frame |
| CPU cost | see below | |

### Seven voices on the RP2350?

**Not measured, and this is the deciding question.** Evidence available:

- The same code (with FX and the LXR's own sequencer and UI) runs on the LXR-02's Cortex-M7 at
  216 MHz with a double-precision FPU. The M33 is a simpler single-issue core.
- Desktop (Apple silicon): **12-13 µs per 2000 µs Plinky block, 0.65 %**, for all voices including
  the 44.1→32 kHz conversion (`make bench`). The engine's cost is the same idle or busy.
- A desktop-to-M33 factor is not knowable without hardware. Ambiotica's author reports desktop
  timing "239× realtime" was "not predictive", with PSRAM/XIP-cache misses dominating on device.
  As an **unverified guess** only: a factor of 40-100 would put CLATTER 12 at roughly 25-65 % of
  core 1.
- The panel measures itself: the **DSP settings page** (right-up button, above page 0) shows the
  core-1 load, and the log prints mean/max µs per block every 2 s. The first hardware run answers it.

If it does not fit, in order (all keep the LXR's synthesis): LXR per-sample code in SRAM (prepared
variant `clatter_12_sram.cpp`), tables in SRAM rather than PSRAM, skip silent voices (Pocket notes
the filter/LFO state care needed), a shorter converter, then a measured choice between the LXR at a
lower internal rate and fewer simultaneous voices. Never Plinky's own synth renamed.

## 4. Building and testing without hardware

| Plinky provides | Status |
|---|---|
| Browser emulator | **Yes**, in the IDE (WebAssembly build of the firmware + panel). Closed beta at the time of writing; access via Plinky's Discord. Not used here (no access in this environment). |
| Desktop simulator / firmware emulator / SDK | **No public one.** The IDE compiles on Plinky's server ("compiling edited sources needs the development backend"). The header bundle is published but is documentation, not a compilable SDK. |
| Unit-test support, HAL | No. |
| Automated firmware build | Only Plinky's own pipeline for the community library. |

What CLATTER 12 tests without a Plinky (`make test`, `make m33`): the engine against the LXR at its
native rate and against CLATTER Pocket's reference build; the converter; the C++ amalgamation
against the same code as C; the generator and kit designer against Pocket's; the **real panel source
driven in a host stand-in for the Plinky API** (scripted touches, pressure, time, audio and LEDs);
API names against the published headers; and a **GCC cross-compile for Cortex-M33** with section
sizes. See TESTING.md. None of this is hardware verification.

## 5. Principal obstacles (ranked)

1. **Core-1 CPU.** Unmeasured; see §3. Mitigations prepared, decision needs one device run.
2. **SRAM for the LXR's tables.** The default build needs ~130 KB of static SRAM (102 KB tables)
   outside the 128 KB panel arena; whether the firmware has it is undocumented. `clatter_12_psram.cpp`
   needs 31 KB and puts the tables in PSRAM (cost: XIP-cache misses on oscillator reads). A third
   option, the tables inside the panel arena (the object is 2.7 KB), needs a custom staged-load
   commit and is on the roadmap.
3. **Source size.** The generated panel is ~306 KB. Another panel author reports the flash endpoint
   rejecting uploads over ~200 KB; Plinky's developer describes the limit as arbitrary and raisable.
   Ask Plinky first; otherwise shrink (identifier compaction, dropping unused LXR code paths).
4. **Compiler flags unknown.** Plinky's flags are not published. The amalgamation compiles with
   clang (C++17) and GCC 8.5 (Cortex-M33); a different GCC or `-std` may need further rewrites, which
   the amalgamator's checked-rewrite mechanism is made for. Without `-ffast-math` (likely), noise
   voices decorrelate from CLATTER sample-by-sample but measure the same (TESTING.md, "Authenticity").
5. **Licensing for distribution.** CLATTER 12 is GPL-3.0-only (the LXR firmware's licence, with its
   section 7 terms). Sharing the **source** (`clatter_12.cpp`) is fine. A **compiled UF2** combines it
   with Plinky's firmware, whose licence is not published: distributing that binary (e.g. through the
   community library, which publishes prebuilt UF2s) needs Plinky's firmware licence to be
   GPL-compatible or an arrangement with the LXR copyright holder. Personal builds are unaffected.
   This needs an answer from Plinky before a community-library submission; it is not legal advice.
6. **No device, no emulator access here.** Everything Plinky-side is untested.
