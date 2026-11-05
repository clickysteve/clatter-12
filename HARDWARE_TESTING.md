# Hardware testing

CLATTER 12 has **never run on a Plinky 12**. Nothing in this repository has been flashed, and nothing
here flashes anything. This is the plan for the first test: the emulator first, then a device, in
stages, stopping at the first sign of trouble. Every figure below that is not marked *measured on
the device* comes from the host, the simulator or QEMU, and is an estimate.

## What you need

| | |
|---|---|
| Plinky 12 firmware | the current release from Plinky (no particular build is required by anything CLATTER 12 uses that we know of). **Write down the firmware build code the IDE shows** before you start: it goes in the report |
| Panel source | `build/clatter_12.cpp` from `make` (483 KB). The variants are below |
| Plinky 12 IDE | [plinky12.com/codeed.html](https://plinky12.com/codeed.html) (closed beta, access via Plinky's Discord), Chrome or Edge for WebUSB |
| A computer with an SD card reader | for the backup |
| Headphones or monitors at **low volume** | a fault can be a full-scale burst |

## Installation, backup and recovery

- **Installation** is only through Plinky's IDE: paste the file into a new tab, **Test** (emulator),
  then **Flash** (WebUSB). The IDE re-flashes the panel set with Plinky's own installer. CLATTER 12 has
  no installer and never touches the bootloader.
- **Backup first.** Eject the SD card and copy all of it to the computer (projects, saves, presets).
  Note which panels are installed. CLATTER 12 writes only its own save slots, but a crash during a save
  could leave a damaged file.
- **Recovery.** Plinky's [firmware page](https://plinky12.com/firmware.html) restores stock firmware
  and panels. The RP2350's boot ROM is in mask ROM, so a bad panel cannot remove the means of
  reinstalling firmware; check Plinky's own instructions for entering the update mode on your unit
  *before* you start, so you are not looking for them with a stuck device.
- To go back to stock panels without a full restore, flash the stock panel set from the IDE.

## Memory it needs (from the Cortex-M33 compile, GCC 13, `-O2`; not linked against Plinky's firmware)

| | `clatter_12.cpp` | `clatter_12_psram.cpp` |
|---|---|---|
| flash code + constants | 120 KB + 27 KB | 120 KB + 27 KB |
| static SRAM (outside the panel arena) | **140 KB** (40 KB + the LXR's 100 KB of tables) | 40 KB |
| panel object (Plinky's 128 KB arena) | 72 KB (of which 26 KB the reverb's lines) | 72 KB |
| PSRAM | 256 KB (the delay's lines, from 128 KB in) | 128 KB tables + 256 KB delay |
| stack | small: nothing large on it in CLATTER 12's code; the LXR FX run 32 frames at a time to keep their buffers at 128 bytes | |

Whether Plinky's firmware has 140 KB of static SRAM free for one panel is **unknown**: that is the
first link risk, and the reason the `_psram` variant exists.

## Known limitations and risks

- **CPU.** QEMU counts instructions, not cycles. With the delay and reverb on, playing all seven tracks,
  the busiest block is about 218 000 instructions (73 % of a 150 MHz core 1 at one cycle each) and the
  average 195 000 (65 %); the worst case tried, 252 000 (84 %). Real cycles are higher (flash and PSRAM
  waits, multi-cycle loads, branches): a factor of 1.2 to 1.6 would put the busiest blocks near or over
  budget. Unknown: Plinky's real core-1 clock, how much its own output chain costs after `on_dsp`
  (the panel returns `false`, so Plinky's standard mix and FX run after it on the same core; the DSP
  page does not include them), and the compiler flags Plinky's server uses (`-Os` would need about 20 %
  more instructions than the figures here).
- **PSRAM.** The delay's 256 KB are in PSRAM, read and written as one sequential stream; other panels
  report scattered PSRAM access as their main cost, which is why the reverb's lines are now in SRAM.
  Never measured.
- **Source size.** 483 KB. Plinky's IDE **Test** has been shown to take 306 KB; another panel author
  reports a ~200 KB limit for **Flash**. Plinky describe the limit as adjustable. If the IDE refuses
  the file, stop and ask Plinky; lossless compaction cannot get under 200 KB (TESTING.md).
- **Eight pages** (seven and Plinky's save page). The published panels use two to five; Plinky
  publishes no maximum. The simulator allows eight.
- **Loads clear the reverb.** Loading a project replaces the panel object, and with it the reverb's
  lines: the reverb tail stops at the load (by design, rather than replaying an older tail).
- **Emulator timing.** `time_us()` does not advance within an audio block there, so the DSP page
  reads `n/a`: the emulator says nothing about CPU.

## Stop conditions

Stop the test, note what happened, and power the Plinky off (pull USB) at once if you see any of:

- a crash, freeze, reboot or watchdog reset; LEDs frozen or garbage; the grid no longer responding;
- loud noise, a full-scale burst, a continuous tone that should not be there, or DC (a speaker cone held out);
- crackling, dropouts or gaps that come back (more than one isolated click), or the log's `under`
  count rising;
- the DSP page's average above 85 % or its maximum above 95 %;
- anything that looks like memory corruption: a pattern or kit changing on its own, other panels'
  settings or saves changed, corrupted save files, the save page listing nonsense;
- the IDE failing to flash part way, or the Plinky not coming back after a flash;
- anything else unexpected from the hardware (heat, smell, USB disconnecting repeatedly).

Do not try again on the device before the cause is understood. Restore stock firmware if the device
behaves oddly after a stop.

## Staged first test

Record for each stage: pass or fail, the DSP page's average and maximum, the log line
(`c12 dsp avg … max … us (lxr … mix … rs …); … clip … limited … under …`), and what you heard.

**0. Emulator (no device).** Paste `build/clatter_12.cpp`, press **Test**. Expect a 2-bar default
beat after pressing PLAY (bottom right) and no red pad at top left (red: bring-up failed, see the
log). Go through every stage below that does not need sound quality or timing. Note warnings and
errors from the compiler. If the IDE refuses the file's size, stop (see "Source size").

**1. Boot and panel loading.** Flash. The panel appears in Plinky's panel list and opens on SEQ; the
log shows `clatter12: engine up, out rate 32000 Hz`. Nothing plays until PLAY. *Stop* on a red pad,
a reboot loop, or a link error in the IDE (then try `clatter_12_psram.cpp`, and report the error).

**2. Basic navigation.** Row 15's first eight pads jump to SEQ, SOUND, GEN, MIX, FX, PERFORM, REPEAT,
SAVE; the right-hand side buttons scroll through the same eight and up to the DSP page. Every page
lights. Touches on one page do nothing on the others.

**3. One voice.** Stopped: tap DRUM 1's audition pad (SEQ, row 13, the first of the seven PLAY pads
from x8; pressure is velocity) a few times, soft and hard. A clean kick,
louder when pressed harder; no clicks at the end of the decay. DSP page: note the idle load.

**4. Seven voices.** Audition each of the seven tracks in turn. Then program a step on every track in
the same column and press PLAY: all seven sound together, no crackle. Note the DSP average and maximum.

**5. Pattern sequencing.** Clear, then program a two-bar beat; GEN a pattern; MUT; UNDO and REDO;
change a step's velocity and condition. Steps light in time with what you hear. Save to a slot on the
SAVE page, change the pattern, load the slot back: the saved pattern returns (the reverb tail stops at
the load; that is expected).

**6. Tempo.** Tap TEMPO four times at a steady rate; hold TEMPO for the overlay, nudge +1/-10, half,
double. The beat follows at once without a glitch. Change the tempo quickly for ten seconds while
playing: no crackle, `under` stays 0.

**7. Delay and reverb.** FX page, SPACE: raise the delay and reverb sends of two tracks (MIX), then of
all tracks. Echoes in time with the tempo, a smooth reverb tail; ping-pong and mono; freeze each; turn
the sends down and let the tails die. **Note the DSP average and maximum with both on and all seven
tracks playing: this is the number this release was optimised for.** Then FX ENGINE PLINKY and
back.

**8. Ratchets and rolls.** Give a hat step a 4x ratchet, an 8x at 300 BPM; flam and drag a snare;
an accelerating roll. They play evenly, no doubled or missing strokes, no crackle.

**9. Performance controls.** PERFORM: hold MUTE, KILL ALL, FILL, MUTATE, DENSITY, STUTTER, THROW; LATCH
one and release it; COMMIT. Store a scene and recall it at the bar. REPEAT: note repeat by pressure on
a few tracks at once, a roll pad, BEAT REPEAT. Everything returns to the pattern when released.

**10. Stress.** All seven tracks dense, 8x ratchets on several, delay and reverb on, the LXR drive,
ring modulator and compressor on (FX, COLOUR), and at the same time: FILL, BEAT REPEAT, two note
repeats, a tempo change every second, GEN and NEW KIT while playing. Run five minutes. Then the same
with FX ENGINE PLINKY. Pass: no crackle, `under` 0, DSP maximum under 95 %. If it fails only in stage
10, note at which addition it started.

## If the load is too high

In order, one change at a time, each reported with the DSP page's figures:

1. `clatter_12_sram.cpp`: the LXR's per-sample code (15 KB) in SRAM instead of flash.
2. Fewer voices or effects playing at once, to find where the limit is (not a fix: a measurement).
3. Report the figures with the build code and the variant; the next steps (converter in SRAM, the LXR
   tables' placement, a voice skipped while silent) need those numbers to choose between.

Never describe a build as hardware-verified on the strength of this checklist alone; record what was
run, on which firmware, with what result.
