# CLATTER 12 interface

The grid is the instrument. Seven pages, each a whole 16 × 16 surface, and one row that is the same on
all of them. Nothing is more than one touch deep: no menus, no shift layers. The one overlay is the
tempo's, and it lasts only while its pad is held.

```
y 15 on every page:  SEQ SND GEN MIX FX PRF RPT SAV | TEMPO | (page's own) | UNDO REDO STOP PLAY
                      x0  x1  x2  x3  x4 x5  x6  x7     x8      x9 .. x11      x12  x13  x14  x15
```

The pages are Plinky's own: the panel reports eight (`get_num_pages`), they stack vertically in
Plinky's page canvas, and Plinky's right side buttons scroll through them as on any panel. Row 15's
first eight pads jump straight to one (`scroll_to_page`). A pad held when the page changes is let
go: performance pads never stick because a finger moved to the navigation. The page shown is the one lit white on row 15,
and each page has its own colour on its pad. The panel settings page above SEQ (right side button up)
shows the audio core's load. Changing page never touches the audio: the engine runs on core 1, and
the transport, tempo, kit streaming and meters are serviced every UI frame whatever page is shown.

## SEQ

```
      x: 0 ......................................... 15
y 0   DRUM 1   steps of the viewed page; brightness = velocity
 …    …        tinted towards white: the step has a variation, condition or ratchet
y 6   OP HAT
y 7   playhead
y 8   VARIATION none A B 2nd 2nd-A 2nd-B  ·  RATCHET 1x 2x 3x 4x 6x 8x
y 9   CONDITION ALWAYS 1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4 25% 50% 75%
y 10  ROLL none flam drag double triple accel decel  ·  CURVE flat fade-in fade-out acc-first acc-last alternate human
y 11  VELOCITY 16 levels
y 12  MUTE x7  ·  SOLO x7
y 13  LOCK x7  ·  PLAY (audition, pressure) x7
y 14  VIEW P1-P4 (hold + tap: copy)  ·  LENGTH 16/32/48/64  ·  SWING 50 54 57 61 64 68 71 75
y 15  pages | TEMPO | GEN MUT CLR(hold) | UNDO REDO STOP PLAY
```

(STYLE left row 15 for the TEMPO pad: the style is on GEN, row 0, and GEN's readout names it.)

### Steps and variations: why hold, not repeated taps

| Gesture | Result |
|---|---|
| Tap an empty step | On, as loud as the press (velocity follows the peak while held). It becomes the **selected** step. Stopped, it plays. |
| Tap a lit step (released within 0.3 s) | Off, with everything it carried. Effortless removal is the first priority. |
| Hold a lit step (0.3 s) | It stays on and becomes the selected step. Rows 8-11 now show and change its variation, condition, ratchet, roll and velocity. |
| Hold a lit step, no editor pad touched | Pressure re-takes its velocity (as before). |
| Hold a lit step, tap an editor pad | The edit applies; the step keeps the velocity it had (a light hold to change a variation does not quieten an accent). |
| Hold several steps, tap an editor pad | All of them change together. |
| After release | The step stays selected: the editor rows keep acting on it without holding. |

Cycling variations by repeated taps was considered and rejected: it makes the tap on a lit step mean
two things (remove, or next variation), so removal would need a gesture of its own, and with six
variations on DRUM 3 and SNARE a cycle is up to six taps where a direct choice is one. The variation
lanes of the first version are gone: the seven rows they took are now the step editor and the track
controls. Every variation the lanes could reach is on row 8, directly, with the track's real number
of choices (A and B; DRUM 3 and SNARE also their second sound and its A and B).

### Conditions

Pocket's conditions, unchanged: the sequencer counts passes of the whole pattern from PLAY (modulo 12,
which every one divides), so 1:2 sounds on passes 1, 3, 5…, 2:2 on 2, 4, 6…, and the chances are drawn
per step. The tests play a 1:2 step through four passes and a 4:4 step through eight and count hits.

### Ratchets, curves and rolls

Three separate things, each one pad deep on the step editor:

- **Ratchet** (row 8): the step strikes 1, 2, 3, 4, 6 or 8 times, evenly, within its own length (swing
  included): 32nds, 32nd triplets, 64ths, 64th triplets, 128ths. Tempo-synchronised by construction:
  strokes are kept in Pocket's own phase units from the moment the step fires, so they follow tempo
  changes and land on the first LXR block (1.45 ms) at or after their time, as steps do, never
  accumulating error. **No two strokes closer than 12 ms** (`C12_MIN_STROKE_MS`): at 300 BPM an 8x
  plays 4 (12.5 ms apart), so every stroke's envelope opens; at 120 BPM all 8 play (15.6 ms).
- **Curve** (row 10, x8-14): how the strokes' velocities run: FLAT, FADE IN, FADE OUT, ACCENT FIRST,
  ACCENT LAST, ALTERNATING, HUMAN (CLATTER Web's roll shape).
- **Roll** (row 10, x0-6): an articulation, after CLATTER Web's FLAM (`rhythm/realize.ts`, a grace a
  64th before the hit at 55 %):

| Roll | What plays |
|---|---|
| FLAM | one grace a 64th before the hit (clamped 15-35 ms), 55 %; **played ahead of the step**, so the hit stays on the beat |
| DRAG | two graces a 128th apart (10-20 ms), 45 % and 50 %, ahead of the step |
| DOUBLE | two strokes in the step, 100 % and 82 % |
| TRIPLE | three strokes, 100 / 72 / 86 % |
| ACCEL | strokes over two steps (an eighth), gaps closing, velocity rising 45 → 100 % (the ratchet count sets how many; 6 if under 3) |
| DECEL | the same, gaps opening, velocity falling to 40 % |

A flam or drag is played ahead by looking at the next step while this one plays. When that cannot be
known in advance (the first step after PLAY, a chance condition, a roll played by hand) the grace is
played on the step and the hit follows it by the grace's gap. A curve other than FLAT replaces ACCEL's
and DECEL's own velocity runs.

**Auditioning**: stopped, every ratchet, roll and curve pad plays the selected step as it now sounds.
The REPEAT page's ROLL pads play any roll on any track by hand.

The data: Pocket's 32-bit step cell uses bits 0-19, and its sequencer reads nothing above them. Bits
20-22 hold the ratchet, 23-25 the curve, 26-28 the roll, so they are copied, undone and saved with the
step, and published atomically. Project version 1 (0.0.2) had a spacing and a velocity run there; it
loads converted (accelerating and decelerating spacing become those rolls; decay and decrescendo FADE
OUT, crescendo FADE IN, human HUMAN).

## SOUND

```
y 0   EDIT voice x7 | BANK SOURCE SHAPE MOD | AUDITION | NEW SOUND | MUTATE SOUND | LOCK
y 1 … y 13   one LXR parameter a row: x0-13 its value, x14 -1, x15 +1 (hold: repeats)
y 14  each voice's level as it plays
```

Each row is a real LXR parameter of the selected voice, taken from CLATTER Web's `lxrParams.ts`
(ported to `core/c12_params.c`): the firmware's own parameter numbers, ranges and menu names. Ranges
run across the row (bipolar ones from the centre); menus show one pad per option (six waveforms, eight
filter types, fourteen transients, …). Banks: **SOURCE** (oscillator, noise, modulators, FM, pitch
envelope), **SHAPE** (amplitude envelope, repeat, transient, filter, drive), **MOD** (LFO, velocity,
volume, pan, sample rate). 32 or 33 parameters a voice, at most 12 a bank. Not on the grid, and said so: the
two TARGET parameters (LFO target, velocity destination), whose values are parameter numbers.

A change is sent to the engine within the next UI frame (4 ms) and is heard at once; stopped, the
voice plays (at most every 150 ms) so editing can be heard without a pattern.

## GEN

```
y 0   STYLE x11 | GENERATE | MUTATE
y 1   DENSITY          y 2   COMPLEXITY
y 3   MUTATION small medium large | LOCK track x7
y 4   NEW RHYTHM for one track x7
y 6   KIT CHARACTER x16
y 7   WILDNESS         y 8   MUTATION AMOUNT (sounds)
y 9   LOCK SOUND x6
y 10  NEW SOUND x6 | MUTATE SOUND x6
y 12  NEW KIT (2 pads) | MUTATE KIT (2 pads)
y 13  the kit's seed, in binary
```

Kits are designed, never picked: `NEW KIT` draws a new seed for Pocket's kit designer (a port of
CLATTER Web's `kitgen.ts`: archetypes, recipes, a character's root, brightness and drive), so every
press is a new kit of the chosen character. Locked sounds keep every parameter. `NEW SOUND` designs one
voice of the same family. Mutation (CLATTER 12's, `core/c12_kitgen.c`) moves a voice towards another
sound the designer draws for it, by AMOUNT; menus switch with a chance that grows with AMOUNT and
bring their companions (a filter type its cutoff, a waveform its pulse width; pitch moves as a whole,
since the designer keeps drums on a scale), so a mutation stays inside what the designer makes.
Everything here is undoable.

## MIX

Eight channel strips of two pads: the seven tracks and the master (x14-15).

```
y 0   MUTE (master: LIMITER)      y 1   SOLO
y 2 … y 9   LEVEL fader: 115 100 90 78 65 50 30 off (gain (level/100)², so 50 is -12 dB)
y 10  level - | +                 y 11  PAN left | right (both: centre)
y 12  DELAY send - | +            y 13  REVERB send - | +
y 14  meter
```

All of it is in the audio path: CLATTER 12 mixes the six LXR voice blocks itself (with Pocket's pan
law, master gain and DC blocker, to the bit), so a level is a gain with a 64-sample ramp, a mute holds
the track's triggers back and cuts its voice, solo likewise. The sends (post-fader, 8 steps of 16)
feed CLATTER 12's own delay and reverb (FX page, SPACE bank), or, with FX ENGINE set to PLINKY,
Plinky's own (`mix_buffers_t` sends). Pan is the LXR's own pan parameter, applied as each hit starts, as on
the LXR-02. The hats are one LXR voice: its level and pan follow whichever hat track last played it.

## FX

Two banks, chosen on row 0 as SOUND's banks are. Row 0 is otherwise the same on both:

```
y 0   (x0-6: SPACE: each track's sends, shown; COLOUR: ROUTE) | x8 SPACE  x9 COLOUR | x11 FX ENGINE
      | x12 DELAY FREEZE  x13 REVERB FREEZE (momentary) | x15 LIMITER
SPACE                                        COLOUR
y 1   DELAY TIME (12 divisions)              DRIVE wet, shape, gain, tone, level (y1-5)
y 2   DELAY FEEDBACK                         RING wet, frequency (y6-7)
y 3   DELAY FILTER (low-pass | open | high)  COMP wet, threshold, ratio, attack, release, make-up (y8-13)
y 4   DELAY WET                              BITS | SAMPLE RATE (y14)
y 5   MONO | PING-PONG
y 7   REVERB SIZE
y 8   REVERB DECAY
y 9   REVERB DAMPING
y 10  REVERB WET
y 14  delay and reverb returns, as meters
```

### SPACE: CLATTER 12's delay and reverb

**What there was before.** The MIX page's per-track delay and reverb sends were real: CLATTER 12 built
them per voice and handed them to Plinky through `mix_buffers_t.delaysend` / `reverbsend`. But
CLATTER 12 had no delay or reverb of its own, nothing on the grid set Plinky's (their time, feedback and
tail are Plinky's mix settings, `MIX_PARAM_DELAY_TIME` and friends, which published panels edit with
Plinky's own slider widgets), and in the web demo and simulator the sends went nowhere: the send
pads did nothing audible there. The LXR-02's own FX bus delay is not built (64 KB of half floats).

**What there is now** (`core/c12_space.c`): a delay and a reverb of CLATTER 12's own, fed by those
sends, their returns joining the mix before the master gain and the limiter.

- **Delay**, at the LXR's 44 140 Hz (so hats keep their top in the echoes): TIME is one of twelve
  divisions (1/32, 1/16T, 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D, 1/4, 1/2T, 1/4D, 1/2) of the sequencer's
  tempo, following it as it changes (the read position glides, as tape does); longer than its 1.48 s line, it
  halves. FEEDBACK 0-100 (at most 0.95 a pass). FILTER is one bipolar row: left of centre a low-pass
  (16 kHz down to 400 Hz), right a high-pass (20 Hz up to 2.5 kHz), in the loop, so each repeat is
  filtered once more. WET is the return level. MONO or PING-PONG (echoes alternate sides).
- **Reverb**, at half rate (22 070 Hz, as Plinky's own reverb runs at half its rate): two allpass
  diffusers into a four-line feedback delay network with a Hadamard matrix. SIZE scales the lines,
  DECAY sets the time to fall 60 dB (0.3-8 s), DAMPING a low-pass in each line (10 kHz to 1.2 kHz),
  WET the return.
- **Freeze** (momentary; on FX row 0 and PERFORM row 1): the delay goes round unfiltered at a whole
  number of samples, taking no new input; the reverb's lines stop losing. Let go: they decay again.
- **FX ENGINE** (toggle): CLATTER 12 (default) or PLINKY. On PLINKY the sends go to Plinky's own
  delay and reverb as before and CLATTER 12's cost nothing; the SPACE rows dim. Projects from 0.0.2
  load on PLINKY, as they sounded.

Every control is checked to change the sound (`tests/test_perform.c`, `tests/test_panel_sim.cpp`
"FX SPACE"). Memory: 282 KB of 16-bit lines, bound by the panel in PSRAM (after the 128 KB the LXR
tables may take in the `_psram` variant). With nothing sent and the lines empty (their 16-bit samples
truncate to exactly zero as a tail ends) the effects do not run at all: off costs nothing.
CPU: TESTING.md.

### COLOUR

The three effects are the LXR-02 firmware's `DSPAudio/FX` code, unchanged, run by CLATTER 12 in series
on the tracks routed to them. At wet 0 an effect is skipped: off costs nothing. Their measured cost is
in TESTING.md: the compressor (a log and an exp per sample) is the expensive one.

## TEMPO (row 15, x8, every page)

| Gesture | Result |
|---|---|
| Tap in time | Tap tempo, from the second tap (the mean of the last up to three intervals); a 2 s pause starts a new count. A tap is timed from its press. |
| Hold (0.3 s) | The tempo overlay over rows 0-14, for as long as it is held (a two-handed gesture, like holding a step to edit it) |
| (pad) | Flashes on each beat: the sequencer's quarter notes, or the clock when stopped |

```
y 1-5  the BPM in digits (amber after a tap tempo)      x14 y3: the beat
y 7    -10 -5 -1 | ½× ½× 2× 2× | +1 +5 +10
y 9    presets: 70 80 90 100 110 115 120 124 128 132 136 140 150 160 170 174
y 11   NUDGE slower (x0-3) | NUDGE faster (x12-15): held, about 4 % off; let go, back
y 13   TAP: any pad of the row
y 14   where the tempo sits, 30 to 300
```

There is one clock: every change is Plinky's own tempo (`set_tempo_bpm`), which CLATTER 12's
sequencer follows each UI frame, as it follows the left side buttons. Pocket's sequencer keeps whole
BPM (integer phase arithmetic that cannot drift), so the finest step is 1 BPM. A tempo change applies
from the next LXR block and never moves the position; ratchet strokes, repeats and the delay follow
it. Tested: 8 s of a random tempo every 4 ms with ratchets everywhere, no underruns, no clipping.

## PERFORM

Everything here is **temporary**: it lasts while the pad is held, and the pattern, kit and mixer are
untouched. The engine changes what plays (`c12_perf_*`), not what is stored.

```
y 0   MUTE x7 (hold)              | SOLO x7 (hold)
y 1   KILL LOW MID HIGH ALL (2 pads each) | DELAY THROW all | REVERB THROW all | DELAY FREEZE | REVERB FREEZE
y 2   DELAY THROW x7              | REVERB THROW x7
y 4   FILL x7                     | FILL DRUMS (2 pads) | FILL ALL (2 pads)
y 5   MUTATE small medium large (2 pads each)
y 6   DENSITY: x0-6 thinner (-100 .. -14), x7-8 as written, x9-15 busier (+14 .. +100)
y 8   STUTTER 1/8 1/16 1/32 1/32T (2 pads each)
y 10  SCENES 1-8 (tap: recall; hold 1 s: store) | x9 QUANTISE (next bar / at once)
y 13  the step in the bar          y 14  meters: tracks, output, delay, reverb returns
y 15  pages | TEMPO | LATCH COMMIT(hold) RELEASE | UNDO REDO STOP PLAY
```

- **KILL** cuts the tracks' voices at once and holds their steps back; the delay and reverb tails ring on.
- **FILL** is CLATTER Web's fill (`rhythm/realize.ts`), live: the held tracks' empty steps sound with a
  chance rising through the bar (25 % to 100 %), velocities rising, the last two steps ratcheting.
- **MUTATE** plays a mutation of the pattern (Pocket's `pocket_gen_mutate`, locked tracks kept) from a
  second pattern buffer, a new take each press.
- **DENSITY** thins with CLATTER Web's density by strength (soft hits drop first: DOWNBEAT's weights),
  or adds ghost notes into the gaps of tracks that play in the bar, most between the eighths.
- **STUTTER** is a beat repeat of the step just played (REPEAT's row 10 sets its shape and tracks).
- **THROW** puts the tracks' sends at full while held: let go and the echoes trail off.
- **LATCH** (toggle): a tap holds a pad until tapped again; RELEASE lets go of everything; LATCH off too.
- **COMMIT** (hold 0.5 s; destructive, undoable): what the performance does to the pattern (mutation,
  fill, density, as they play on pass 1) is written into it, its mutes and solos into the mixer; then
  everything is released. Throws, freezes and stutters are moments, never written.

### Scenes

Eight snapshots of the mixer (levels, mutes, solos, sends, route, master) and every FX value. Tap:
recalled **at the next bar** (QUANTISE on, playing): the engine takes the mixer as the bar's first step
starts, before it sounds, so the downbeat is already in the new scene (`c12_mix_schedule`); the FX
values follow within a UI frame (4 ms). Stopped, or QUANTISE off, at once. Hold a second: store.
Saved with the project. CLATTER 12 has one pattern and one kit, so a scene does not change them:
pattern and kit selection would need pattern and kit banks first (ROADMAP).

## REPEAT

```
y 0-2  REPEAT pads: a track each, 2 x 3 pads (x0-13) | x14-15: the velocity the press gives
y 4    RATE  1/4 1/8 1/16 1/32 1/64 | 1/4T 1/8T 1/16T 1/32T | 1/8D 1/16D
y 5    PRESSURE: VELOCITY RATE CHANCE FIXED | CURVE x7 (the repeats' velocities)
y 7    ROLL: FLAM DRAG DOUBLE TRIPLE ACCEL DECEL (tap) | TARGET x7
y 9    BEAT REPEAT (hold): 1 BAR, 1/2, 1/4, 1/8, 1/16, 1/32, 1/16T, 1/32T (2 pads each)
y 10   SHAPE: FLAT FADE BUILD | the tracks beat repeat takes (toggles)
y 13   the step in the bar          y 14  meters
y 15   pages | TEMPO | LATCH . RELEASE | UNDO REDO STOP PLAY
```

- **Note repeat**: hold a track's pad and it strikes on the RATE's grid: the first at once, then in time
  with the sequence's own grid (swing followed), or the tempo when stopped. Never faster than one strike
  in 10 ms. Several tracks at once.
- **Pressure** (defaults chosen so a light touch always works): VELOCITY (default; 45 to 127, never
  silent), RATE (the rate, twice it past a firm press, four times past a hard one, with hysteresis so it
  does not flicker), CHANCE (a quarter of the strikes to all of them), FIXED (100).
- **Curve**: fades run over the first eight strikes; ACCENT LAST, for a held repeat that has no last,
  accents each beat; ALTERNATING loud and soft.
- **Rolls by hand**: a ROLL pad plays that articulation on the held REPEAT pads' tracks, else the TARGET:
  at once when stopped, on the next step when playing. As loud as you press. For hearing rolls before
  writing them into steps.
- **Beat repeat** is sequencer-level, not audio: the engine replays the steps of the last stretch of the
  chosen length (from the step playing, or the next if it is more than half gone), with their
  ratchets and rolls, for the tracks lit on row 10, while the sequencer runs on underneath and is heard
  again where it is when let go. Lengths under a step (1/32, triplets) retrigger the step's hits at
  that rate. Shape: each repeat as loud, softer (-18 % each), or building. Kits and edits stay live. An
  audio-buffer repeat (stutter of the sound itself) is not built: it would need a buffer and CPU the
  budget does not have.

## Control kinds

Every pad's description on the project page names its kind:

| Kind | Looks like | Examples |
|---|---|---|
| MOMENTARY · HOLD | dim at rest, its colour while held, white-tinted while LATCH holds it | PERFORM, beat repeat, note repeat, freezes, nudge |
| TOGGLE | lit when on | mute, solo, lock (SEQ, MIX), LATCH, FX ENGINE, ROUTE, QUANTISE |
| CHOICE | the chosen one bright, the others faint | variation, ratchet, roll, curve, rate, bank |
| CONTINUOUS | a bar with - and + at its end (hold to repeat) | SOUND rows, FX rows, GEN density |
| HOLD 0.5 S · DESTRUCTIVE | dark red, filling with red as it is held; undoable | CLR, COMMIT |
| DISPLAY | does nothing when touched | meters, playhead, the bar's step, the seed |

## MORPH (evaluated, not built)

Morphing between two kits on a continuous control was evaluated and deferred. Of the 197 parameters
of the six voices (`core/c12_params.c`, 32-33 a voice), 52 are menus (waveforms, filter types,
transients, on/off switches: 7-9 a voice) and cannot be interpolated and would have to
switch at a point. The continuous ones can, but each change reaches the LXR as a parameter event
through Pocket's ring (`c12_kit_pump`: 48 a UI frame, drained 32 a block) and some LXR setters
recompute coefficients: sweeping the 145 continuous parameters smoothly means hundreds of events a
second through a path budgeted for edits, on a core with no measured headroom yet. A dual-engine
crossfade (two LXRs) is out of the question at the present CPU estimate. A small version (one voice's
continuous parameters at a time, rate-limited) is possible once a device measurement shows room.

## Colour

Seven track hues (red, orange, yellow, green, cyan, blue, violet), brightness for velocity, white for
the page shown, the selected step and the playhead, one colour per page and per control family. On
the performance pages: kill red, delay amber, reverb violet, freeze ice blue, fill green, mutation
teal, stutter and beat repeat magenta, scenes gold, LATCH blue; density blue (thinner) to orange
(busier) with a white centre.
The faintest tier is `fade_col(c, 28)`, about 3 of 31 on a full channel: 1 of 31 is reported invisible
on the device. The only motion is the playhead, the bar's step, meters, the TEMPO pad's beat, a
scene waiting for its bar, and the CLR, COMMIT and scene-store hold fills.

## Not yet

Live recording from the audition pads, per-step pitch, parameter locks per step, MIDI and CV,
phase lock to Plinky's clock, pattern and kit banks (and so scenes that change them), MORPH, an
audio-buffer stutter. See ROADMAP.md.
