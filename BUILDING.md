# Building

## Requirements

| | Version used | For |
|---|---|---|
| C/C++ compiler (clang or GCC) | Apple clang 21; any C11 / C++17 compiler | host build, tests, preprocessing |
| Python 3 | 3.9+ | generators, checks (no packages needed) |
| make, git (with submodules) | | |
| `arm-none-eabi-g++` + binutils | GCC 8.5.0 (Homebrew `arm-none-eabi-gcc@8`); Arm GNU Toolchain 13 in CI | optional: `make m33` |
| curl | | optional: `tools/fetch-plinky-headers.sh` (API check) |

No Plinky SDK is needed or available: Plinky compiles panels on its own server.

```bash
git clone --recurse-submodules https://github.com/clickysteve/clatter-12
```

The submodule is CLATTER Pocket at a pinned commit. Only its top level is needed (its own
`third_party/fm1_open` submodule is not).

## Targets

```bash
make
```

| Target | Output |
|---|---|
| `make panel` | `build/clatter_12.cpp` and its variants (below) |
| `make test` | every host test (TESTING.md) |
| `make m33` | Cortex-M33 compile of each variant, section sizes. `ARM_GXX=/path/to/arm-none-eabi-g++` if not on PATH |
| `make bench` | host timing (not predictive of the RP2350) |
| `make pocket-test` | CLATTER Pocket's own tests, unchanged |
| `sh tools/fetch-plinky-headers.sh` | the published Plinky API bundle into `build/`, sha256-pinned |

## Panel variants

| File | LXR tables (102 KB) | LXR per-sample code | Try it when |
|---|---|---|---|
| `clatter_12.cpp` | static SRAM | flash (XIP) | first |
| `clatter_12_psram.cpp` | PSRAM | flash | the firmware will not link the 130 KB of static data |
| `clatter_12_sram.cpp` | static SRAM | SRAM (`.time_critical`) | it links but the DSP page shows too much load |
| `clatter_12_full.cpp` | static SRAM | flash | reading; the simulator uses it |

All are generated: edit `core/`, `panel/` or the tools, never `build/`. Each file's header records
the CLATTER 12 commit and configuration it came from (`+modified` if built from uncommitted work).

## Installing (not yet done by anyone)

There is no command-line install: Plinky panels go through Plinky's IDE.

1. Open the Plinky 12 Custom Panels code editor (closed beta; access via Plinky's Discord,
   [plinky12.com/codeed.html](https://plinky12.com/codeed.html)).
2. New tab, paste `build/clatter_12.cpp`, press **Test** to run it in Plinky's in-browser emulator.
   Expect a red top-left pad if bring-up failed, otherwise the default beat; PLAY is bottom right.
3. Only then **Flash** to a Plinky 12 over WebUSB, which re-flashes the panel set using Plinky's own
   installer. Stock panels stay; Plinky's [firmware page](https://plinky12.com/firmware.html) restores
   stock firmware. CLATTER 12 never touches the bootloader and has no installer of its own.
4. Press **Connect for logging** in the IDE to see the `c12 dsp avg … max … us` line every 2 s; the
   right-up side button shows the DSP page (core-1 load in %). Record those numbers.

Every build is labelled **EXPERIMENTAL** in its library metadata. Do not submit to the community
library before the licensing question in FEASIBILITY.md §5 is answered.
