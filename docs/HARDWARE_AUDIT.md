# Hardware integration audit (2026-11-13, 0.0.5-exp)

CLATTER 12 against Plinky 12's public panel interface and firmware, before any device test. Every
finding says what it rests on:

- **Source**: read in code, CLATTER 12's or Plinky's public material (the community panels Plinky
  builds, their notes from Plinky's developers, and the firmware images Plinky's pipeline publishes).
- **Test**: shown by a test here (host, simulator, cross-compile, QEMU, WebAssembly).
- **Unknown**: only a Plinky 12 (or Plinky) can answer it.

A successful standalone ARM compile is **not** evidence that the complete firmware links or runs:
nothing here has been linked with Plinky's firmware or run on a device.

Public material used (not vendored; downloaded for the audit): `plinkysynth/community-panels` (source,
`aaaf0a9`), `plinkysynth/community-panels-built` (`538282171`, firmware images built by Plinky's
pipeline against firmware 90bd, `90bd729a`), `charlesvestal/plinky-ambiotica` (`SYSTEM_NOTES.md`:
answers from Plinky's developers). plinky12.com (the API bundle) was not reachable from this session;
CI fetches it for `tools/check_api.py`.

## 1. The hardware compiler errors

| | Finding | Evidence |
|---|---|---|
| Reported failures | `-Werror=missing-field-initializers` in `pocket_lxr_set_param`, `_trigger_locked`, `_trigger_variation`, `_set_master`, `_set_bpm`, `_set_dc_block`; `-Werror=sign-compare` in `mix_voices`, `mix_fx`, `mix_master` | Reproduced exactly at `b0e38be` with GCC 13.2 `-Wall -Wextra -Werror`: 72 + 10 errors, in exactly those nine functions (**test**) |
| Field initialisers | Pocket's six event builders write `struct event e = { 0 };`. In C that zero-fills; GCC's C++ `-Wextra` reports each later member. `tools/amalgamate.py` rewrites it to `= {}` in the generated text only (value-initialisation: every member zero, as before); count-checked (6). Pocket's source is untouched | **source**; output bit-identical (**test**) |
| Sign compare | `C12_LXR_BLOCK` is `64u`; ten loops compared it with an `int`. They index with `unsigned` now (0..63, used as indices or cast to `uint32_t`) | **source**; bit-identical (**test**) |
| Found after those | four unused MIDI-parser statics (`-Wunused-variable`, in `-Wall`): removed in the generated text, count-checked | **test** |
| | `-O3` clones in the `_sram` variant gave a section type conflict, and its prototypes' section attributes conflicted with their definitions' (`-Wattributes`): `noclone`, and prototypes no longer carry the attribute | **test** |
| | the engine-memory change (§2) would have tripped `-Wparentheses` on scalar pointers: written as plain pointers | **test** |
| Production flags | **`-O3`, likely.** zone_plate's `on_ui` in Plinky's own firmware image is instruction for instruction GCC 13's `-O3` output (35 of 54 instructions; the rest are its prologue and call relocations); no `-O2` or `-Os` sequence of it appears. Warnings: `-Wextra`-level with `-Werror` (the reported log), `-Wunused-parameter` not fatal (zone_plate's `on_ui(int dt)` builds) | **source** (binary comparison); exact flags and GCC version **unknown** |
| Now | every variant compiles at `-O3`, `-O2` and `-Os` with `-Wall -Wextra -Wno-unused-parameter -Werror` (`make m33`, `tools/hw_check.py`, in CI) | **test**; with Plinky's real headers and server: **unknown** |

## 2. Memory

| | Finding | Evidence |
|---|---|---|
| **Static SRAM** | **The deciding finding.** Firmware 90bd with Grid and a panel with no static data (zone_plate, arcade_12) ends `.bss` at 0x2007b0bc: **20 292 bytes** before the end of main SRAM, from which the pico-sdk reserves the heap (2 KB by default) and fails the link if it does not fit. Ambiotica leaves 2 808 bytes. CLATTER 12 0.0.4 needed **140 KB** (`clatter_12.cpp`) or **40 KB** (`_psram`): neither could have linked | **source**: `tools/firmware_ram.py` reads crt0's data copy table and `.bss` bounds from the images |
| Now | **1.7 KB** of static SRAM (`.bss` 1 656, `.data` 56), gc-linked, at every optimisation level. The LXR tables went to PSRAM (default); the engine's 39 KB of working memory to the panel object (`tools/amalgamate.py` `RELOCATE`, bit-identical); the 33 KB undo history to PSRAM. `make m33` fails above 8 KB | **test** (compile and panel-only link); the real link: **unknown** |
| `_sram` variant | puts 15 KB of LXR code in SRAM: 17.2 KB static at `-O3`, beside a budget of about 18 KB less the heap. Kept for a measured experiment only | **test**; whether it links: **unknown**, likely not |
| Panel arena | 128 KB, "always yours", the whole object. CLATTER 12's object: **75.2 KB** (UI state, project, scenes, engine memory 39 KB, reverb lines 26 KB). A `static_assert` stops a build over 128 KB | arena size: **source** (Plinky developers via SYSTEM_NOTES); size: **test** |
| PSRAM | ~7.5 MB usable (`get_psram_size()`, the system FX delay's last 128 KB excluded). CLATTER 12: tables at 0 (100 KB), delay 128 KB in (256 KB), undo 384 KB in (33 KB of 64). Slow, "an order of magnitude", behind a small XIP cache shared with flash code; sequential good, scattered bad | **source**; layout: **test** (simulator) |
| PSRAM access | The delay: one sequential stream each way. The tables: oscillators read a 2 KB band-limited row each (six voices), transients sequentially; the sine table (8 KB) is shared. Undo: 2 KB copies on core 0 when a change is made. **The XIP cache's behaviour with 144 KB of `-O3` code plus table reads is the largest performance unknown** | access patterns: **source**; cost: **unknown** |
| Stack | Deepest chains (GCC's call graph, with the engine's two indirect calls followed): core 1 from `on_dsp` 760 B, core 0 from `on_ui` 664 B at `-O3`. Largest frames on them: the LXR's snare, hi-hat and cymbal block functions, 280-296 B (two 64-sample buffers). Guidance: stack objects under 128 B, 256 at most; the core-1 stack size is not published (pico-sdk default 2 KB) | **test**; stack sizes: **unknown** |
| Allocation | none (no `new`, `malloc`, containers); the panel object is placement-constructed by the firmware | **source** |

## 3. The eight-page interface and the settings pages

| | Finding | Evidence |
|---|---|---|
| Pages | `get_num_pages` = 8 (seven pages and Plinky's save page); each page is drawn at y = page × 16, as the community panels do | **source**; scrolling eight on a device: **unknown** (published panels use 2-5; no maximum published) |
| Settings pages | `get_num_panel_settings_pages` = 7 (the diagnostics); drawn with Plinky's own `draw_system_style_settings_page`, as Life (17 settings pages), Ambiotica and Grid do; its return value (a touch's change) resets the counters | **source**; on a device: **unknown** |
| **Save page** | **Bug found and fixed.** `panel_page_t::saveload`'s first argument is the absolute row of the page it draws on: every published panel passes `page × 16` (Spiegel and mpe_test 16 for page 1; Pendulum, Kristallwuchs, Arcade 32 for page 2). CLATTER 12 passed 16 from page 7, which would have drawn Plinky's save picker on page 1's rows: invisible and untouchable on the SAVE page, so saving from the device would not have worked. It passes `PAGE_SAVE * 16` now; the simulator records the argument and the test checks it | **source**; on a device: **unknown** |
| Touch | raw reads (`get_touch_pressed`/`down`, origin), not Plinky's widgets: Plinky's developers recommend widgets for slide and debounce handling | **source**; whether it matters for CLATTER 12's pads: **unknown** |

## 4. Audio callback

| | Finding | Evidence |
|---|---|---|
| Contract | `on_dsp` on core 1 per 64-frame block; CLATTER 12 fills `mix_buffers_t.dry` and the two sends and returns `false` (Plinky's mix and FX follow on the same core). `SAMPLE_FREQ` 32 kHz (Plinky's emulator log) | **source**; the cost of Plinky's chain after it: **unknown** |
| Real-time safety | no allocation, no locks, no printf or formatting on core 1; core 0 ↔ core 1 by single aligned words and Pocket's lock-free ring. Diagnostics add two `time_us()` readings (the RP2350's hardware timer) and a few adds and compares per callback | **source**, **test** |
| CPU | QEMU: 191 000 instructions per block mean, 213 000 busiest (all tracks, delay and reverb, `-O3`); 64-71 % of a 150 MHz core at one instruction a cycle; the whole LXR FX chain 89 %. Instructions are a lower bound on cycles | **test** (QEMU); cycles, clock, Plinky's chain: **unknown** (the `_cycles` build measures cycles and the clock on a device) |
| Loads | the engine's memory is in the panel object, so a load holds core 1 (one faded block, silence, nothing touched) while the object is copied; released on apply, `on_load_finished`, or 10 s | **test** (simulator, Plinky's documented sequence); the real firmware's timing: **unknown** |

## 5. Clock and transport

| | Finding | Evidence |
|---|---|---|
| Tempo | each UI frame, `get_tempo_bpm()` → the sequencer and the delay, rounded to whole BPM (Pocket's integer phase). TEMPO on the grid sets Plinky's own tempo (`set_tempo_bpm`) | **source**, **test** |
| Transport | each UI frame, `is_transport_playing()` → the sequencer's play state; STOP and PLAY call Plinky's transport | **source**, **test** |
| Phase | CLATTER 12's sequencer keeps its own phase, counted in LXR samples, not locked to Plinky's clock. On Plinky's internal clock at a tempo CLATTER 12 set, the two agree in rate; with an external clock at a fractional tempo, or a codec clock that is not the timer's, they drift. Plinky exposes `get_clock_phase()` (a Q16 quarter-note phase, used by Ambiotica and Life): the basis for a phase lock, not done | **source**; drift on a device: **unknown** |

## 6. Persistence and project compatibility

| | Finding | Evidence |
|---|---|---|
| Format | one `FIELD_BASE64("clatter12", ...)` in Plinky's JSON serialiser (`on_serialise`), versioned (magic, version, size, CRC-32); version 1 projects convert; a file that fails any check changes nothing | **source**, **test** (simulator's serialiser stand-in) |
| Staged loads | the field is cleared before reading (a missing field cannot keep an old value); the live state is copied into the staged object first, so a refused file leaves everything as it was | **source**, **test** |
| Real serialiser | the size Plinky accepts in one base64 field, and whether `on_serialise` returning `true` after a refused field is right | **unknown** |

## 7. Source size

473 KB (`--compact`, now with the glue's comments stripped too). The reported hardware failure was a
compile error, so Plinky's server accepted the 483 KB file of 0.0.4 for **Flash**: at least that much
is taken (**source**: the report). The limit itself is not published ("arbitrary", raisable); about
200 KB was reported by another author for an earlier IDE (**unknown**).

## What only a device can answer

Linking beside the firmware (the 1.7 KB is a panel-only link); cycles per block and the core clock
(`clatter_12_cycles.cpp` reads them); PSRAM and XIP cache cost of the tables at `-O3` code size;
Plinky's own chain after `on_dsp`; the core-1 stack size; eight pages and seven settings pages
scrolling; the load hold against Plinky's real staged-load timing; external clock drift.
