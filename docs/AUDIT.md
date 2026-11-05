# Audit before the performance overhaul (2026-11-05, version 0.0.2-exp)

What the interface and the audio path were before 0.0.3, read from the code (`panel/clatter12.cpp`,
`core/c12_engine.c`), the simulator and the project page, not from the older docs.

## Pages and controls

| Page | What it did | Notes |
|---|---|---|
| SEQ | steps (rows 0-6), playhead (7), step editor: variation and ratchet count (8), condition (9), ratchet spacing (even, accel, decel, random) and velocity run (decay, cresc, decresc, human) (10), velocity (11); mute and solo toggles (12); lock and audition (13); view page, length, swing (14); row 15 GEN, MUT, STYLE, CLR (hold) | all implemented. Hard to find: hold-to-edit, hold-a-page-and-tap-another to copy, CLR's hold. Ratchet spacing and velocity applied to ratchets only; no flam, drag or stroke rolls. STYLE duplicated GEN row 0 |
| SOUND | one voice's LXR parameters, three banks, audition, new/mutate/lock sound | implemented; the two TARGET parameters deliberately left off |
| GEN | pattern and kit generators, locks, mutation | implemented |
| MIX | mute, solo, fader, fine level, pan, delay and reverb sends (- +), meters, master, limiter | the sends were real but went only to Plinky's effects (below) |
| FX | route; LXR drive, ring modulator, compressor; bits; the LXR's sample rate; limiter | implemented, all audible |
| SAVE | Plinky's save page | implemented |
| above SEQ | DSP load (settings page) | implemented |

- **Placeholders**: none drawn as working that did nothing in the firmware; but in the web demo and the
  simulator the delay and reverb sends did nothing audible.
- **Page slots**: `get_num_pages` was 6; the simulator's canvas held 8, so two pages were free. Row 15
  had x6-11 for a page's own pads (SEQ used x6-9).
- **Navigation conventions**: row 15 identical on every page (pages, page's own, UNDO REDO STOP PLAY);
  Plinky's right side buttons scroll pages; the page shown is lit white; one colour per page.
- **Performance already there**: mute and solo toggles (SEQ, MIX), audition pads with pressure, GEN and
  MUT live, swing, undo. Tempo only from Plinky's left side buttons (or MIDI clock): nothing on the grid.
- **Ratchets**: 1-8 strokes evenly or shaped within the step, at LXR block resolution, following tempo;
  no minimum gap (8 strokes at 300 BPM were 6 ms apart: the voice restarted before its envelope opened).

## The delay and reverb path

- **Effect implementations**: none in CLATTER 12. The LXR-02 firmware's FX bus (its delay, 64 KB of half
  floats) is not built into the amalgamation; the LXR release has no reverb (`USE_REVERB 0`).
- **Input routing and per-track sends**: `mix_block` built a delay and a reverb send per voice from the
  MIX page's levels (post-fader, mute-aware), converted linearly to 32 kHz (`sends_out`), and `on_dsp`
  copied them to `mix_buffers_t.delaysend` / `reverbsend`, returning `false` so Plinky's standard
  chain (with its delay and reverb) would follow.
- **Global wet/dry, effect parameters, bypass**: none in CLATTER 12. Plinky's delay time and feedback and
  reverb tail and shimmer live in Plinky's mix preset (`MIX_PARAM_DELAY_TIME`, `_DELAY_FEEDBACK`,
  `_REVERB_FEEDBACK`, `_REVERB_SHIMMER`, seen in the published community panels, which edit them with
  Plinky's slider widgets); CLATTER 12 never touched them.
- **Audible**: on a Plinky, presumably (untested: whether the sends reach Plinky's effects as the API
  describes is on the "Untested" list); in Plinky's emulator, not recorded; in the simulator and on the
  project page, **no**: the stand-in has no Plinky effects, so the send pads changed nothing heard.

## What was done about it

CLATTER 12's own delay and reverb (`core/c12_space.c`), on the FX page's SPACE bank, fed by the same
sends, with an FX ENGINE switch to send to Plinky's instead. See docs/DESIGN.md "FX".
