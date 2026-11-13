# Hardware testing

CLATTER 12 has **never run on a Plinky 12**. Nothing here flashes anything. This is the procedure for
the first device test: emulator first, then the device in stages, stopping at the first sign of
trouble. Figures not marked *measured on the device* are estimates (host, simulator, QEMU). What is
known and what is not: [docs/HARDWARE_AUDIT.md](docs/HARDWARE_AUDIT.md).

## Before you start

| | |
|---|---|
| Firmware | the current Plinky release. **Write down the firmware build code** the IDE shows |
| Panel source | `build/clatter_12.cpp` from `make` (477 KB). Optional: `clatter_12_cycles.cpp` (stage 8b) |
| IDE | [plinky12.com/codeed.html](https://plinky12.com/codeed.html) (closed beta), Chrome or Edge (WebUSB). Use plinky12.com, not the staging IDE: its device log does not work |
| Listening | headphones or monitors at **low volume**: a fault can be a full-scale burst |
| Backup | eject the SD card and copy all of it to a computer; note which panels are installed |
| Recovery | Plinky's [firmware page](https://plinky12.com/firmware.html) restores stock firmware and panels. The RP2350's boot ROM is mask ROM: a bad panel cannot remove the means of reflashing. **Find Plinky's instructions for entering update mode on your unit before you start** |

CLATTER 12 has no installer, never touches the bootloader, and writes only its own save slots.

## What it needs (from `make m33`: GCC 13 `-O3`, a panel-only link; not linked with Plinky's firmware)

| | |
|---|---|
| static SRAM | **1.7 KB**. Firmware 90bd leaves about 20 KB for a panel (less the heap); 0.0.4 needed 140 KB and could not have linked |
| panel object | 103.2 KB of Plinky's 128 KB arena (the 16-slot pattern bank 29 KB, the engine's 41 KB of working memory and the reverb's 26 KB included) |
| PSRAM | 100 KB LXR tables (from 0), 256 KB delay (from 128 KB), 33 KB undo (from 384 KB), up to 64 KB for a save or load (from 448 KB) |
| flash | 171 KB code + 27 KB constants at `-O3` |

## Stop conditions (any stage)

Stop, note what happened and the stage, and unplug USB at once on any of:

- crash, freeze, reboot, LEDs frozen or garbage, the grid not responding;
- loud noise, a full-scale burst, an unexpected continuous tone, or DC (a speaker cone held out);
- crackles or dropouts that recur (more than one isolated click), or the **LATE** or **OVER** count rising;
- **DSP** above 85 % or **PEAK** above 95 %;
- anything like memory corruption: patterns or kits changing by themselves, other panels' settings or
  saves changed, the save page listing nonsense;
- a flash failing part way, or the Plinky not coming back after one; heat, smell, USB dropping out.

Do not retry on the device until the cause is understood. Restore stock firmware if it behaves oddly.

## Reading the diagnostics

Right side button **up** from SEQ scrolls through seven settings pages (Plinky's own style). None
costs a musical page. The audio core's figures are this panel's `on_dsp` only, timed with the
RP2350's hardware microsecond timer; Plinky's own mix and FX run after it on the same core and are
not included, so treat 85 % as the limit, not 100 %.

| Page | Shows | Healthy |
|---|---|---|
| **DSP** | average load over the last half second (% of the 2000 us block) | under 70 %: green; 70-85 %: amber; over 85 %: stop |
| **PEAK** | the longest callback since reset. **Touch the value to reset every peak and count** | under 85 % |
| **OVER** | callbacks whose own work outlasted the block; the bar: how often one passed 85 % lately | 0 |
| **LATE** | callbacks that started over 1.25 blocks after the previous one (the nearest a panel can see to an output underrun; the firmware does not expose its own), plus the converter's underruns | 0 |
| **SPRD** | the most LXR stages one callback ran lately (DSP spreading: 12 = one LXR block; 24 = two) | about 12-19 |
| **VOX** | voices sounding now, then the effects running: D delay, R reverb, C colour (drive, ring, compressor), P sends to Plinky's effects | as expected |
| **MEM** | the panel object's size; `ERR` if the delay, undo or tables found no memory | `104K`, never `ERR` |

**Connect for logging** in the IDE prints every 2 s:
`c12 dsp avg A max M us of 2000 (peak P since reset); over O near N late L gap G us; lxr .. mix .. rs .. us; stages max S; ...; clip C limited .. under U`.
In Plinky's emulator the clock does not move inside a callback: DSP and PEAK read `n/a`.

**Signs of audio deadline failure**: OVER or LATE above 0; PEAK near or over 100 %; `gap` well over
2000 us in the log; crackle that follows load (more tracks, delay and reverb, the compressor) rather
than particular sounds. If DSP is under 70 % yet LATE rises, Plinky's own chain or another task is
the cause: report it, with the log.

## Stages

Record for each: pass or fail; DSP, PEAK, OVER, LATE (reset PEAK at the start of each stage); the
log line; what you heard.

**0. Emulator.** Paste `build/clatter_12.cpp` in a new tab, **Test**. No red pad at top left (red:
bring-up failed; see the log). PLAY (bottom right) plays a two-bar beat. Note compiler warnings or
errors. If the IDE refuses the size, stop (it accepted 483 KB for Flash before).

**1. Install and first boot.** **Flash**. If the build fails, copy the whole error and stop: a link
error naming RAM ("region RAM overflowed") means the static SRAM estimate is wrong. The panel appears
in Plinky's list and opens on SEQ; the log shows `clatter12: engine up, out rate 32000 Hz`. Nothing
plays until PLAY. Settings pages: **MEM** shows `104K`, not `ERR`. Idle **DSP**: note it.

**2. Navigation.** Row 15's first eight pads jump to SEQ, SOUND, GEN, MIX, FX, PERFORM, REPEAT, SAVE;
the right side buttons scroll the same, and above SEQ the seven diagnostic pages. Every page lights.

**3. Single voice.** Stopped: tap DRUM 1's audition pad (SEQ row 13, x8) softly, then hard: a clean
kick, louder when harder, no click at the end. Then each of the seven audition pads in turn.

**4. Seven-track playback.** Put a step on every track in the same column, PLAY: all seven together, no
crackle. Then GEN a pattern and let it play a minute. Note DSP and PEAK.

**5. Delay and reverb stress.** MIX: raise the delay and reverb sends of every track (rows 12-13). FX,
SPACE: delay feedback high, reverb decay long. All seven tracks dense (GEN, density up). Two minutes.
Freeze each; let the tails die. **Note DSP and PEAK with both effects running and all seven tracks:
the figure this build was optimised for** (QEMU: about 64 % average, 71 % busiest at one instruction
a cycle; real cycles are higher). Then FX ENGINE PLINKY (x11 row 0) and back.

**6. Full LXR effects.** FX, COLOUR: route every track (row 0), drive wet up (row 1), ring modulator wet
(row 6), compressor wet (row 8), bits (row 14), with stage 5's delay and reverb still on. The heaviest
DSP configuration (QEMU busiest block 89 %). Add 4x and 8x ratchets on several tracks, a FILL and a
BEAT REPEAT (PERFORM, REPEAT). Five minutes. Pass: OVER and LATE 0, PEAK under 95 %. If it fails only
here, note which addition started it.

**7. Tempo and transport.** Tap TEMPO four times; hold it for the overlay, +1/-10, half, double.
Change tempo quickly for ten seconds while playing: LATE stays 0.

**8. Diagnostics.** Touch PEAK's value: PEAK, OVER and LATE return to 0. Optional **8b**: flash
`clatter_12_cycles.cpp` (it enables the core's cycle counter, a debug register a debugger also
uses) and repeat stage 5: the log adds `cycles .. max .. (N MHz)`, the real core clock and cycles per
block, which turn QEMU's instruction counts into a real estimate. Then flash `clatter_12.cpp` again.

**9. Patterns.** Tap SEQ's lit pad (row 15, x0): the PATTERN overlay opens. Stopped, tap slot 2 and
program a beat; tap slot 1, slot 2: each keeps its own. PLAY. Tap DUP: no audible change, slot 3 now
lights white; edit a few steps on SEQ; open the overlay and tap slot 2: it blinks, and takes over
exactly on the next bar line (listen for the downbeat: no flam, no gap, no double hit). Then QUANT
NOW and END. With delay and reverb sends up, switch to an empty slot: the tails ring on. Tap VARY a few
times and BACK. Build a chain (hold an entry, tap a slot; repeats on row 8), CHAIN on, PLAY: it
follows the chain; ONCE stops at its end. Hold PERFORM's MUTE through a switch: it stays muted. Watch
**PEAK**, **OVER** and **LATE** throughout: a switch must not move them.

**10. Saving and reloading.** The SAVE page must show Plinky's slot picker on its own grid (before
0.0.5 it was drawn off-page: report it if the page is blank). With several pattern slots and a chain
set, save to a slot. Change everything. Load the slot: the sound fades out, there is a short silence,
and it fades back in with every pattern slot, the chain and the slot that was playing (the reverb tail
stops at a load, by design). Load a slot saved by 0.0.4 if you have one: it must load into pattern
slot 1. Power-cycle and load again.

**11. Recovery check.** Flash the stock panel set from the IDE; confirm the stock panels work and your
SD card's other saves are intact.

## If the load is too high

One change at a time, each reported with the diagnostics and the firmware code:

1. Report stage 5's and 6's figures and the `_cycles` log: they decide what to do next.
2. `clatter_12_sram.cpp` (LXR per-sample code in SRAM) only if Plinky confirms RAM to spare: it adds
   15 KB of static SRAM, near the whole budget.
3. Fewer voices or effects at once, to find the limit (a measurement, not a fix).

Never describe a build as hardware-verified from this checklist alone: record what was run, on which
firmware, with what result.
