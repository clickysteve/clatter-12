# CLATTER 12 interface

The grid is the instrument. Five pages, each a whole 16 × 16 surface, and one row that is the same on
all of them. Nothing is more than one touch deep: no menus, no modes, no shift layers.

```
y 15 on every page:  SEQ SND GEN MIX FX SAV | (the page's own) | UNDO REDO STOP PLAY
                      x0  x1  x2  x3  x4 x5    x6 .......... x11   x12  x13  x14  x15
```

The pages are Plinky's own: the panel reports six (`get_num_pages`), they stack vertically in
Plinky's page canvas, and Plinky's right side buttons scroll through them as on any panel. Row 15's
first six pads jump straight to one (`scroll_to_page`). The page shown is the one lit white on row 15,
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
y 10  ROLL even accel decel random  ·  VELOCITY RUN decay cresc decresc human
y 11  VELOCITY 16 levels
y 12  MUTE x7  ·  SOLO x7
y 13  LOCK x7  ·  PLAY (audition, pressure) x7
y 14  VIEW P1-P4 (hold + tap: copy)  ·  LENGTH 16/32/48/64  ·  SWING 50 54 57 61 64 68 71 75
y 15  pages | GEN MUT STYLE CLR(hold) | UNDO REDO STOP PLAY
```

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

### Ratchets and rolls

A step strikes 1, 2, 3, 4, 6 or 8 times within its own length (swing included). How the strokes are
spaced: evenly, accelerating, decelerating, or each nudged by up to a quarter of a gap by chance (the
same on the same pass). How their velocities run: CLATTER Web's ratchet (each stroke 12 % softer), or
its ROLLS shapes (crescendo, decrescendo, human). Strokes are scheduled in the audio context at LXR
block resolution (1.45 ms, the sequencer's own), so they cost no UI time and cannot drift.

The data: Pocket's 32-bit step cell uses bits 0-19, and its sequencer reads nothing above them. Bits
20-26 hold the ratchet, so it is copied, undone and saved with its step, and published atomically.

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
the track's triggers back and cuts its voice, solo likewise, and the sends feed Plinky's own delay and
reverb (`mix_buffers_t` sends). Pan is the LXR's own pan parameter, applied as each hit starts, as on
the LXR-02. The hats are one LXR voice: its level and pan follow whichever hat track last played it.

## FX

```
y 0   ROUTE x7 (tracks into the chain) | LIMITER (x15)
y 1-5   DRIVE   wet, shape (TUBE FOLD RAT), gain, tone, level   the LXR firmware's master distortion
y 6-7   RING    wet, frequency                                   the LXR firmware's ring modulator
y 8-13  COMP    wet, threshold, ratio, attack, release, make-up  the LXR firmware's compressor
y 14  BITS 16 12 10 8 6 5 4 | SAMPLE RATE (the LXR's VOICE_DECIMATION_ALL)
```

The three effects are the LXR-02 firmware's `DSPAudio/FX` code, unchanged, run by CLATTER 12 in series
on the tracks routed to them. At wet 0 an effect is skipped: off costs nothing. Their measured cost is
in TESTING.md: the compressor (a log and an exp per sample) is the expensive one. The LXR's own FX bus
delay (64 KB of half-float memory) is not used: the sends reach Plinky's delay and reverb instead.

## Colour

Seven track hues (red, orange, yellow, green, cyan, blue, violet), brightness for velocity, white for
the page shown, the selected step and the playhead, one colour per page and per control family.
The faintest tier is `fade_col(c, 28)`, about 3 of 31 on a full channel: 1 of 31 is reported invisible
on the device. The only motion is the playhead, meters and the CLR hold fill.

## Not yet

Live recording from the audition pads, per-step pitch, parameter locks per step, MIDI and CV,
phase lock to Plinky's clock. See ROADMAP.md.
