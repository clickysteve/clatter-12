# CLATTER 12 interface

The grid is the instrument. A pattern can be built, heard and changed without leaving page 0 or
opening a menu.

```
      x: 0 ......................................... 15
y 0   DRUM 1   ▪ ▫ ▫ ▫ ▪ ▫ ▫ ▫ ▪ ▫ ▫ ▫ ▪ ▫ ▫ ▫     steps of the viewed page
y 1   DRUM 2                                           brightness = velocity
y 2   DRUM 3                                           dim marks on beats 1, 5, 9, 13
y 3   SNARE
y 4   CYMBAL
y 5   CL HAT
y 6   OP HAT
y 7   playhead ─────────▮──────────────────
y 8   DRUM 1 variations (under each lit step: dim = none, tinted = A, B; magenta = 2nd sound)
 …     …
y 14  OP HAT variations
y 15  P1 P2 P3 P4 | 16 32 48 64 | GEN MUT KIT STYLE UNDO CLR STOP PLAY
```

## Why this layout

- **Seven rows are the LXR's seven triggers**, nothing more: six voices, the hi-hat with its closed
  and open trigger. No invented voices.
- **Variations sit directly under their track**, in that track's colour. A variation is the same LXR
  voice with parameters moved for one hit (Pocket's model), so it belongs to a step, not to a new
  row of its own. Tapping under a step is one touch, and the relationship is visible.
- **Row 15 follows Plinky's convention** (play bottom right, stop next to it, clear and record
  beside: API docs "Global Transport") so it feels like the stock panels.
- **Pages are one touch** (left of row 15). Copy is a hold-and-tap between page pads: the only
  two-finger gesture, and only where it cannot be mistaken for anything else.
- **Tempo** stays on Plinky's own side buttons, with Plinky's BPM display; CLATTER 12 follows
  Plinky's transport so other panels and MIDI clock out agree.

## Pressure: one interaction, done reliably

Step entry. Pressing an empty step sets it on immediately (instant feedback) and its velocity
follows the peak pressure while the finger stays down, shown live as brightness. A light tap still
gives an audible step (velocity floor 32). Tapping a lit step clears it; holding a lit step longer
than 0.3 s instead re-takes its velocity, so editing accents needs no mode. Stopped, each entry
auditions the voice at that velocity.

Deferred: pressure on the playhead or variation rows for live triggering, timbre or pitch while held.

## Colour

Seven distinct hues (red, orange, yellow, green, cyan, blue, violet), brightness for velocity,
white for the playhead and the viewed page, amber for length, one fixed colour per control. The
only motion is the playhead and the CLR hold fill; no idle animation. Dim tiers were chosen above
the levels that vanish on the hardware (`DIMMESTEST(WHITE)` is 1 of 31, reported invisible).

## Not yet

Conditions (Pocket supports them: probability, 1:2 … 4:4), per-step pitch, swing, density and
complexity, per-track generate and lock, track mute, mixer, kit editing. The likely home is a
second page (`y = 16`) and holding a track's row-0 pad as a modifier.
