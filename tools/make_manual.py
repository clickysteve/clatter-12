#!/usr/bin/env python3
"""make_manual.py - writes docs/manual/index.html, CLATTER 12's manual, in the project page's style.

The content follows the panel's own pad descriptions (sim/panel_help.h, the page's readout) and
docs/DESIGN.md. When the panel's layout changes, change this too, run it, and rebuild the PDFs
(`make manual`). The screenshots in docs/manual/img are taken from the running panel by
tools/make_manual_pdf.mjs --shots.

Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "manual", "index.html")
VERSION = open(os.path.join(ROOT, "VERSION")).read().strip()

# ---------------------------------------------------------------- colours of the pad maps
C = dict(
    red="#ff3b1f", orange="#ff8a00", yellow="#e8d800", green="#35d435", cyan="#00c8d8", blue="#3a6cff",
    violet="#b040ff", grey="#77776f", white="#e8e8e2", amber="#ff9a1f", teal="#00c8a8", magenta="#d0309a",
    gold="#b8a040", ice="#80d0ff", kill="#c01818", lime="#9be000", pink="#ff6080", sky="#0090ff",
    olive="#a09a20", dim="#3a3a36", rose="#c86a7a",
)
TRACKS = ["DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "CL HAT", "OP HAT"]
TRACK_COL = ["red", "orange", "yellow", "green", "cyan", "blue", "violet"]


def ink_for(hexcol):
    r, g, b = (int(hexcol[i:i + 2], 16) for i in (1, 3, 5))
    return "#000" if (r * .3 + g * .59 + b * .11) > 118 else "#fff"


def row15(own=()):
    z = [(15, 0, 7, "PAGES", "grey"), (15, 8, 8, "TMP", "rose"), (15, 12, 12, "UND", "white"),
         (15, 13, 13, "RED", "grey"), (15, 14, 14, "STP", "kill"), (15, 15, 15, "PLY", "green")]
    return z + [(15, x, x, lab, col) for x, lab, col in own]


def tracks_row(y, x0, prefix=""):
    return [(y, x0 + i, x0 + i, (prefix + TRACKS[i][:2]) if prefix else TRACKS[i][:2], TRACK_COL[i]) for i in range(7)]


def pad_map(zones, caption):
    cells = "".join(f'<i style="grid-area:{y + 1}/{x + 1}"></i>' for y in range(16) for x in range(16))
    out = []
    for (y, x0, x1, label, col) in zones:
        y0, y1 = y if isinstance(y, tuple) else (y, y)
        hx = C[col]
        out.append(f'<b style="grid-area:{y0 + 1}/{x0 + 1}/{y1 + 2}/{x1 + 2};background:{hx};color:{ink_for(hx)}">{label}</b>')
    return f'<figure class="padmap" aria-label="{caption}"><div class="map">{cells}{"".join(out)}</div><figcaption>{caption}</figcaption></figure>'


# ---------------------------------------------------------------- the chapters
# rows: (row, pads, control, kind, does)
MOM, TOG, SEL, CONT, DESTR, SHOW, TAP = "MOMENTARY", "TOGGLE", "CHOICE", "CONTINUOUS", "HOLD · DESTRUCTIVE", "DISPLAY", "TAP"

CHAPTERS = []

CHAPTERS.append(dict(
    id="tempo", name="TEMPO", tag="ROW 15 · PAD 8 · EVERY PAGE", img="tempo.png",
    alt="The tempo overlay: 120 in pad digits, the -10 -5 -1, half, double and +1 +5 +10 row, sixteen presets, nudge pads, a tap row and the tempo range.",
    lede="""<p>The <b>TMP</b> pad sits on row 15 of every page. <b>Tap</b> it in time to set the tempo; <b>hold</b> it to open the
tempo overlay over rows 0 to 14, for as long as you hold it. It flashes on each beat. There is one clock: every change
is Plinky's own tempo, the same one the left side buttons move, and the sequencer follows it.</p>""",
    rows=[
        ("15", "8", "TMP: tap", TAP, "Tap tempo from the second tap (the average of the last up to three intervals). A 2 second pause starts a new count."),
        ("15", "8", "TMP: hold", MOM, "Hold for 0.3 s: the tempo overlay replaces rows 0-14 while held. Let go and the page comes back."),
        ("1-5", "0-12", "BPM", SHOW, "The tempo in pad digits. Amber after a tap tempo."),
        ("3", "14", "BEAT", SHOW, "Flashes on each beat."),
        ("7", "0-2", "-10 -5 -1", TAP, "Coarse and fine steps down. The sequencer keeps whole BPM, so 1 is the finest step."),
        ("7", "6-7", "HALF TIME", TAP, "Halve the tempo."),
        ("7", "8-9", "DOUBLE TIME", TAP, "Double the tempo (at most 300)."),
        ("7", "13-15", "+1 +5 +10", TAP, "Fine and coarse steps up."),
        ("9", "0-15", "PRESETS", TAP, "Go straight to 70 80 90 100 110 115 120 124 128 132 136 140 150 160 170 174 BPM."),
        ("11", "0-3", "NUDGE SLOWER", MOM, "While held, about 4 % slower, to pull the beat into line with something else. Let go: back to the tempo."),
        ("11", "12-15", "NUDGE FASTER", MOM, "While held, about 4 % faster."),
        ("13", "0-15", "TAP", TAP, "Tap the beat on any pad of the row."),
        ("14", "0-15", "TEMPO RANGE", SHOW, "Where the tempo sits between 30 and 300 BPM."),
    ],
    notes="""<p>A tempo change applies from the next LXR block and never moves the play position. Ratchets, repeats and the
delay's time all follow it. Hold TMP with one hand and tap the overlay with the other: it is a two-handed gesture,
like holding a step to edit it.</p>""",
    crib=dict(
        zones=[((1, 5), 0, 12, "BPM IN DIGITS", "white"), (3, 14, 14, "♩", "white"),
               (7, 0, 2, "-10 -5 -1", "sky"), (7, 6, 7, "½×", "ice"), (7, 8, 9, "2×", "amber"), (7, 13, 15, "+1+5+10", "orange"),
               (9, 0, 15, "PRESETS 70 … 174", "olive"),
               (11, 0, 3, "NUDGE -", "sky"), (11, 12, 15, "NUDGE +", "orange"),
               (13, 0, 15, "TAP THE BEAT HERE", "grey"), (14, 0, 15, "30 … RANGE … 300", "dim")] + row15(),
        moves=[("Tap TMP in time", "tap tempo (from the 2nd tap)"),
               ("Hold TMP", "overlay, while held"),
               ("Hold TMP + row 7", "±1 / ±5 / ±10, ½×, 2×"),
               ("Hold TMP + row 9", "jump to a preset BPM"),
               ("Hold TMP + hold row 11", "nudge ±4 % while held"),
               ("Left side buttons", "tempo up / down (hold repeats)")],
    ),
))

CHAPTERS.append(dict(
    id="seq", name="SEQ", tag="PAGE 1 · ROW 15 PAD 0", img="seq.png",
    alt="The SEQ page: seven track rows of steps, the playhead, and the step editor with the selected snare step's variation, condition, roll, curve and velocity.",
    lede="""<p>The sequencer: seven LXR triggers by 16 steps, up to 64 steps in four views. Tap to put a step in, tap to
take it out, <b>hold</b> a step to edit it with the rows below. Brightness is velocity; a step tinted towards white
carries a variation, condition or ratchet.</p>""",
    rows=[
        ("0-6", "0-15", "STEPS", "STEP PAD", "One row per track: DRUM 1, DRUM 2, DRUM 3, SNARE, CYMBAL, CL HAT, OP HAT. The 16 steps of the view shown on row 14."),
        ("7", "0-15", "PLAYHEAD", SHOW, "The step playing now."),
        ("8", "0-5", "VARIATION", SEL, "NONE, A, B, SECOND SOUND, SECOND SOUND A, SECOND SOUND B: the same LXR voice with its parameters moved for that hit. Every track has A and B; DRUM 3 and SNARE also have a second sound (percussion, clap)."),
        ("8", "8-13", "RATCHET", SEL, "1x 2x 3x 4x 6x 8x: strokes within the step (32nds, 32nd triplets, 64ths, 64th triplets, 128ths). Never closer than 12 ms, so at fast tempos 8x plays 6 or 4."),
        ("9", "0-12", "CONDITION", SEL, "ALWAYS · 1:2 2:2 · 1:3 2:3 3:3 · 1:4 2:4 3:4 4:4 · 25 % 50 % 75 %. Passes are counted from PLAY: 1:2 plays on the first of every two, 2:2 on the second."),
        ("10", "0-6", "ROLL", SEL, "NONE, FLAM, DRAG, DOUBLE, TRIPLE, ACCELERATING, DECELERATING. Flams and drags play ahead of the step, so the hit stays on the beat."),
        ("10", "8-14", "CURVE", SEL, "How a ratchet's or roll's strokes run: FLAT, FADE IN, FADE OUT, ACCENT FIRST, ACCENT LAST, ALTERNATING, HUMAN."),
        ("11", "0-15", "VELOCITY", SEL, "Sixteen levels, 8 to 127: set the selected step's velocity exactly."),
        ("12", "0-6", "MUTE", TOG, "Mute a track: its steps do not reach the voice and its sound is cut. Dark when muted."),
        ("12", "8-14", "SOLO", TOG, "Only soloed tracks are heard."),
        ("13", "0-6", "LOCK", TOG, "Lock a track's rhythm: GENERATE and MUTATE leave it alone."),
        ("13", "8-14", "PLAY", "AUDITION", "Play the voice now, as loud as you press."),
        ("14", "0-3", "VIEW 1-16 … 49-64", SEL, "Show that page of steps. Hold one and tap another to copy it there."),
        ("14", "4-7", "LENGTH", SEL, "16, 32, 48 or 64 steps play."),
        ("14", "8-15", "SWING", SEL, "50 54 57 61 64 68 71 75 %: 50 is straight, 75 dotted."),
        ("15", "9", "GEN", TAP, "A new pattern in the GEN page's style, density and complexity. Locked tracks stay."),
        ("15", "10", "MUT", TAP, "Musical edits to the unlocked tracks, at the GEN page's mutation strength."),
        ("15", "11", "CLR", DESTR, "Hold half a second to clear every step. UNDO brings them back."),
    ],
    notes="""<h4>Steps: tap, hold, edit</h4>
<div class="scroll"><table class="gest"><thead><tr><th>Gesture</th><th>Result</th></tr></thead><tbody>
<tr><th>Tap an empty step</th><td>On, as loud as you press, and selected. Stopped, it plays.</td></tr>
<tr><th>Tap a lit step</th><td>Off (released within 0.3 s), with everything it carried.</td></tr>
<tr><th>Hold a lit step</th><td>It stays on and is selected; rows 8-11 show and change it. Pressure re-takes its velocity until you touch an editor pad.</td></tr>
<tr><th>Hold a step, tap an editor pad</th><td>The edit applies; the step keeps its velocity, so a light hold does not quieten an accent.</td></tr>
<tr><th>Hold several steps, tap an editor pad</th><td>All of them change together.</td></tr>
<tr><th>After letting go</th><td>The step stays selected: the editor rows keep acting on it.</td></tr>
<tr><th>Hold a VIEW pad, tap another</th><td>Copy that page of steps onto the other.</td></tr>
</tbody></table></div>
<h4>Rolls</h4>
<div class="scroll"><table class="gest"><thead><tr><th>Roll</th><th>What plays</th></tr></thead><tbody>
<tr><th>FLAM</th><td>One soft grace note about a 64th before the hit (15-35 ms), played ahead of the step.</td></tr>
<tr><th>DRAG</th><td>Two grace notes a 128th apart before the hit: a ruff.</td></tr>
<tr><th>DOUBLE</th><td>Two strokes in the step, the second a little softer.</td></tr>
<tr><th>TRIPLE</th><td>Three strokes, 100 / 72 / 86 %.</td></tr>
<tr><th>ACCELERATING</th><td>Strokes closing in over two steps, rising. The ratchet count sets how many (6 if under 3).</td></tr>
<tr><th>DECELERATING</th><td>Strokes spreading out over two steps, falling away.</td></tr>
</tbody></table></div>
<p>Stopped, every ratchet, roll and curve pad plays the selected step as it now sounds, so you can hear a choice as you make it.</p>""",
    crib=dict(
        zones=[(y, 0, 15, f"{TRACKS[y]} STEPS", TRACK_COL[y]) for y in range(7)] +
              [(7, 0, 15, "PLAYHEAD", "dim"),
               (8, 0, 5, "VARIATION", "magenta"), (8, 8, 13, "RATCHET 1-8x", "amber"),
               (9, 0, 12, "CONDITION", "sky"),
               (10, 0, 6, "ROLL", "olive"), (10, 8, 14, "CURVE", "teal"),
               (11, 0, 15, "VELOCITY 8 … 127", "green"),
               (12, 0, 6, "MUTE ×7", "red"), (12, 8, 14, "SOLO ×7", "gold"),
               (13, 0, 6, "LOCK ×7", "grey"), (13, 8, 14, "PLAY ×7", "violet"),
               (14, 0, 3, "VIEW", "white"), (14, 4, 7, "LENGTH", "amber"), (14, 8, 15, "SWING 50 … 75", "sky")] +
              row15([(9, "GEN", "lime"), (10, "MUT", "teal"), (11, "CLR", "kill")]),
        moves=[("Tap empty step", "on, velocity = pressure"),
               ("Tap lit step", "off"),
               ("Hold lit step + rows 8-11", "edit it (several at once)"),
               ("Row 14 VIEW hold + tap", "copy a page of steps"),
               ("GEN / MUT (row 15)", "new pattern / mutate"),
               ("Hold CLR 0.5 s", "clear all (UNDO restores)"),
               ("PLY", "start / stop")],
    ),
))

CHAPTERS.append(dict(
    id="sound", name="SOUND", tag="PAGE 2 · ROW 15 PAD 1", img="sound.png",
    alt="The SOUND page with DRUM 1 and the SOURCE bank chosen: oscillator wave, pitch, fine tune, pulse width, FM and pitch envelope rows.",
    lede="""<p>Every LXR parameter of one voice, a row each, its value across the row. Three banks, SOURCE, SHAPE and MOD.
Changes are heard at once; stopped, the voice plays as you edit (at most every 150 ms), so you can shape a sound
without a pattern running.</p>""",
    rows=[
        ("0", "0-6", "EDIT", SEL, "Choose the voice to edit: DRUM 1, DRUM 2, DRUM 3, SNARE, CYMBAL, CL HAT, OP HAT. The two hats are one LXR voice."),
        ("0", "8-10", "BANK", SEL, "SOURCE: oscillator, noise, modulators, FM, pitch envelope. SHAPE: amplitude envelope, transient, filter, drive. MOD: LFO, velocity, volume, pan, sample rate."),
        ("0", "12", "AUDITION", "AUDITION", "Play the voice, as loud as you press, without leaving the page."),
        ("0", "13", "NEW SOUND", TAP, "Design a new sound for this voice, of the same family as the kit (CLATTER's kit designer)."),
        ("0", "14", "MUTATE SOUND", TAP, "Move this voice towards another sound the designer draws for it, by the GEN page's MUTATION AMOUNT."),
        ("0", "15", "LOCK SOUND", TOG, "Locked, the voice keeps every parameter through NEW KIT and MUTATE KIT."),
        ("1-13", "0-13", "PARAMETER", CONT, "Tap to set the value, left (0) to right (max). Bipolar values (pitch, pan) run from the centre. Menus (waves, filter types, transients) show one pad per option."),
        ("1-13", "14", "−", CONT, "One step down; hold to repeat."),
        ("1-13", "15", "+", CONT, "One step up; hold to repeat."),
        ("14", "0-6", "LEVELS", SHOW, "Each voice's level as it plays."),
    ],
    notes="""<h4>What each bank holds</h4>
<div class="scroll"><table class="gest"><thead><tr><th>Voice</th><th>SOURCE</th><th>SHAPE</th></tr></thead><tbody>
<tr><th>DRUM 1-3</th><td>OSC WAVE, PITCH, FINE TUNE, PULSEWIDTH, FM AMOUNT, FM FREQ, FM WAVE, FM MODE, PITCH DECAY, PITCH SLOPE, PITCH AMOUNT</td><td rowspan="4">ATTACK, DECAY (hats: CLOSED and OPEN DECAY), AMP SLOPE, REPEAT (snare, cymbal), DRIVE, TRANSIENT WAVE, LEVEL and PITCH, CUTOFF, RESONANCE, FILTER TYPE, FILTER DRIVE</td></tr>
<tr><th>SNARE</th><td>OSC WAVE, PITCH, FINE TUNE, PULSEWIDTH, NOISE FREQ, NOISE MIX, PITCH DECAY, PITCH SLOPE, PITCH AMOUNT</td></tr>
<tr><th>CYMBAL</th><td rowspan="2">OSC WAVE, PITCH, FINE TUNE, PULSEWIDTH, MOD1 WAVE, FREQ and AMOUNT, MOD2 WAVE, FREQ and AMOUNT</td></tr>
<tr><th>HI-HAT</th></tr>
</tbody></table></div>
<p><b>MOD</b> is the same for every voice: LFO RATE, SYNC, DEPTH, WAVE, RETRIGGER and PHASE, VELOCITY AMOUNT, VELOCITY TO VOLUME,
VOLUME, PAN, SAMPLE RATE. The two TARGET parameters (LFO target, velocity destination) are not on the grid: their values
name other parameters. Hover a row on the project page to read the parameter's name and value.</p>""",
    crib=dict(
        zones=tracks_row(0, 0) + [(0, 8, 10, "BANK", "amber"), (0, 12, 12, "AUD", "white"), (0, 13, 13, "NEW", "lime"),
                                  (0, 14, 14, "MUT", "teal"), (0, 15, 15, "LCK", "grey")] +
              [((1, 13), 0, 13, "ONE LXR PARAMETER A ROW · TAP TO SET", "orange"),
               ((1, 13), 14, 14, "−", "dim"), ((1, 13), 15, 15, "+", "grey")] +
              [(14, 0, 6, "LEVELS", "dim")] + row15(),
        moves=[("Row 0 x0-6", "pick the voice"),
               ("Row 0 x8-10", "SOURCE / SHAPE / MOD"),
               ("Tap a row", "set that parameter"),
               ("− / + (x14, x15)", "fine step, hold repeats"),
               ("AUD (x12)", "hear it, pressure = velocity"),
               ("NEW / MUT (x13, x14)", "new or mutated sound"),
               ("LCK (x15)", "keep it through new kits")],
    ),
))

CHAPTERS.append(dict(
    id="gen", name="GEN", tag="PAGE 3 · ROW 15 PAD 2", img="gen.png",
    alt="The GEN page: eleven styles, density and complexity bars, mutation strength and locks, sixteen kit characters, wildness, mutation amount, sound locks, NEW KIT and MUTATE KIT, and the kit seed.",
    lede="""<p>CLATTER's pattern generator on the top half, its kit designer on the bottom. Every result is a new design,
not a preset: NEW KIT draws a new seed each time. Locks keep what you like. Everything here is undoable.</p>""",
    rows=[
        ("0", "0-10", "STYLE", SEL, "MACHINE, ELECTRO, BREAKS, JUNGLE, HALFTIME, BROKEN, GARAGE, INDUSTRIAL, GLITCH, POLYMETER, DUB: the rhythm style GENERATE writes in."),
        ("0", "12", "GENERATE", TAP, "A new pattern: style, density and complexity as set, locked tracks kept."),
        ("0", "13", "MUTATE", TAP, "Edit the pattern musically, at the mutation strength."),
        ("1", "0-15", "DENSITY", CONT, "How busy a new pattern is (50 is the style as written). Raising it adds hits to the same rhythm. − and + at x14-15."),
        ("2", "0-15", "COMPLEXITY", CONT, "How unpredictable a new pattern is."),
        ("3", "0-2", "MUTATION", SEL, "SMALL, MEDIUM, LARGE: how much MUTATE changes."),
        ("3", "4-10", "LOCK", TOG, "A locked track keeps its rhythm through GENERATE and MUTATE (the same locks as SEQ row 13)."),
        ("4", "4-10", "NEW RHYTHM", TAP, "Generate this one track alone; again for another take."),
        ("6", "0-15", "KIT CHARACTER", SEL, "CLASSIC, ANALOG, DIGITAL, INDUSTRIAL, MINIMAL, GLITCH, TECHNO, ELECTRO, DUB, NOISE, METALLIC, SOFT, BROKEN, ACID, LO-FI, CHAOS."),
        ("7", "0-15", "WILDNESS", CONT, "How far new and mutated sounds stray from the character: 50 as written; higher lets voices become other archetypes."),
        ("8", "0-15", "MUTATION AMOUNT", CONT, "How far MUTATE KIT and MUTATE SOUND move a sound."),
        ("9", "0-5", "LOCK SOUND", TOG, "DRUM 1, DRUM 2, DRUM 3, SNARE, CYMBAL, HI-HAT: a locked sound keeps every parameter through NEW KIT and MUTATE KIT."),
        ("10", "0-5", "NEW SOUND", TAP, "Design a new sound for this voice alone."),
        ("10", "8-13", "MUTATE SOUND", TAP, "Move this voice by the mutation amount."),
        ("12", "0-1", "NEW KIT", TAP, "A whole new kit of the chosen character: every unlocked voice. UNDO returns to the last one."),
        ("12", "3-4", "MUTATE KIT", TAP, "Move every unlocked voice by the mutation amount."),
        ("13", "0-15", "KIT SEED", SHOW, "The current kit's seed, in binary."),
    ],
    notes="""<p>Mutation moves a voice towards another sound the designer draws for it. Menus (waveforms, filter types) switch
with a chance that grows with the amount, and bring their companions with them, so a mutation stays inside what the
designer makes. The SEQ page's GEN and MUT pads (row 15) use the style, density, complexity and strength set here.</p>""",
    crib=dict(
        zones=[(0, 0, 10, "STYLE ×11", "lime"), (0, 12, 12, "GEN", "lime"), (0, 13, 13, "MUT", "teal"),
               (1, 0, 13, "DENSITY", "green"), (1, 14, 14, "−", "dim"), (1, 15, 15, "+", "grey"),
               (2, 0, 13, "COMPLEXITY", "green"), (2, 14, 14, "−", "dim"), (2, 15, 15, "+", "grey"),
               (3, 0, 2, "S M L", "teal")] + [(3, 4 + i, 4 + i, TRACKS[i][:2], TRACK_COL[i]) for i in range(7)] +
              [(4, 4 + i, 4 + i, "N", TRACK_COL[i]) for i in range(7)] +
              [(6, 0, 15, "KIT CHARACTER ×16", "orange"),
               (7, 0, 13, "WILDNESS", "amber"), (7, 14, 14, "−", "dim"), (7, 15, 15, "+", "grey"),
               (8, 0, 13, "MUTATION AMOUNT", "amber"), (8, 14, 14, "−", "dim"), (8, 15, 15, "+", "grey"),
               (9, 0, 5, "LOCK SOUND", "grey"), (10, 0, 5, "NEW SOUND", "lime"), (10, 8, 13, "MUTATE SOUND", "teal"),
               (12, 0, 1, "NEW KIT", "lime"), (12, 3, 4, "MUT KIT", "teal"), (13, 0, 15, "SEED", "dim")] + row15(),
        moves=[("Row 0", "style, then GEN or MUT"),
               ("Rows 1-2", "density, complexity"),
               ("Row 3 / 4", "lock a track / new rhythm for one"),
               ("Row 6", "kit character"),
               ("Row 12 NEW KIT", "a brand-new kit"),
               ("Row 9", "lock sounds you like"),
               ("UND (row 15)", "back to the last kit or pattern")],
    ),
))

CHAPTERS.append(dict(
    id="mix", name="MIX", tag="PAGE 4 · ROW 15 PAD 3", img="mix.png",
    alt="The MIX page: eight two-pad channel strips (seven tracks and the master) with mute, solo, an eight-row fader, fine level, pan, delay and reverb sends and meters.",
    lede="""<p>Eight channel strips, two pads wide: the seven tracks (pads 0-1 DRUM 1, 2-3 DRUM 2 … 12-13 OP HAT) and the
master at pads 14-15. All of it is real audio: CLATTER 12 mixes the LXR's voices itself.</p>""",
    rows=[
        ("0", "both", "MUTE", TOG, "Mute the track. Master strip: the LIMITER, on or off (on, nothing reaches full scale: no clipping)."),
        ("1", "both", "SOLO", TOG, "Solo the track (not on the master)."),
        ("2-9", "both", "LEVEL", SEL, "Tap a row: 115 (+2.4 dB), 100 (unity), 90, 78, 65, 50 (−12 dB), 30, off."),
        ("10", "left / right", "LEVEL − / +", CONT, "Fine level adjustment."),
        ("11", "left / right", "PAN", CONT, "Move the voice left or right (the LXR's own pan, applied as each hit starts). Both pads at once: centre."),
        ("12", "left / right", "DELAY SEND − / +", CONT, "How much of the track goes to the delay: CLATTER 12's (FX page, SPACE), or Plinky's own with FX ENGINE set to PLINKY. Post-fader, 8 steps of 16."),
        ("13", "left / right", "REVERB SEND − / +", CONT, "How much of the track goes to the reverb."),
        ("14", "both", "METER", SHOW, "The track's level as it plays."),
    ],
    notes="""<p>A level is a gain with a short ramp; a mute holds the track's triggers back and cuts its voice. The two hats
are one LXR voice: its level and pan follow whichever hat track last played it.</p>""",
    crib=dict(
        zones=[(y, 2 * s, 2 * s + 1, lab, col) for s in range(7) for (y, lab, col) in
               [(0, "MUTE", TRACK_COL[s]), (1, "SOLO", "gold"), ((2, 9), TRACKS[s].replace(" ", "<br>"), TRACK_COL[s]), (10, "− +", "grey"),
                (11, "L R", "sky"), (12, "DLY", "amber"), (13, "VRB", "violet"), (14, "MTR", "dim")]] +
              [(0, 14, 15, "LIMIT", "white"), ((2, 9), 14, 15, "MASTER", "white"), (10, 14, 15, "− +", "grey"), (14, 14, 15, "MTR", "dim")] +
              row15(),
        moves=[("Row 0 / 1", "mute / solo (master: limiter)"),
               ("Rows 2-9", "fader: 115 100 90 78 65 50 30 off"),
               ("Row 10", "fine level − +"),
               ("Row 11", "pan L / R, both = centre"),
               ("Rows 12 / 13", "delay / reverb send − +"),
               ("Pads 14-15", "the master strip")],
    ),
))

CHAPTERS.append(dict(
    id="fx", name="FX", tag="PAGE 5 · ROW 15 PAD 4", img="fx-space.png", img2="fx-colour.png",
    alt="The FX page, SPACE bank: delay time, feedback, filter, wet and ping-pong; reverb size, decay, damping and wet; returns.",
    alt2="The FX page, COLOUR bank: routing, the LXR-02's drive, ring modulator and compressor, bits and sample rate.",
    lede="""<p>Two banks, chosen on row 0. <b>SPACE</b> is CLATTER 12's own delay and reverb, fed by the MIX page's sends.
<b>COLOUR</b> is the LXR-02 firmware's own drive, ring modulator and compressor on the tracks you route, plus bit and
sample-rate reduction. At wet 0 an effect is off and costs nothing.</p>""",
    rows=[
        ("0", "0-6", "SENDS / ROUTE", "DISPLAY / TOGGLE", "SPACE: each track's delay (amber) and reverb (violet) sends, as set on MIX. COLOUR: ROUTE, whether the track goes through the COLOUR chain."),
        ("0", "8", "SPACE", SEL, "Show the delay and reverb bank."),
        ("0", "9", "COLOUR", SEL, "Show the drive, ring modulator and compressor bank."),
        ("0", "11", "FX ENGINE", TOG, "Where the sends go. CLATTER 12 (green): its own delay and reverb, set here. PLINKY (yellow): Plinky's own, set in Plinky's mix settings; the SPACE rows then do nothing."),
        ("0", "12", "DELAY FREEZE", MOM, "While held, the delay loops what it holds and takes nothing new. Let go: it decays."),
        ("0", "13", "REVERB FREEZE", MOM, "While held, the reverb tail stops decaying. Let go: it fades."),
        ("0", "15", "LIMITER", TOG, "The output limiter on or off."),
    ],
    rows_title_extra=[
        ("SPACE", [
            ("1", "0-11", "DELAY TIME", SEL, "1/32, 1/16T, 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D, 1/4, 1/2T, 1/4D, 1/2, at the tempo (it follows tempo changes, gliding). Longer than 1.48 s, it halves. − + at x14-15."),
            ("2", "0-15", "DELAY FEEDBACK", CONT, "How much of each echo comes round again: 0 one echo, 100 a long trail (never runaway)."),
            ("3", "0-15", "DELAY FILTER", CONT, "In the loop, so each repeat is filtered again: left of centre low-pass (darker), right high-pass (thinner), centre open."),
            ("4", "0-15", "DELAY WET", CONT, "The delay's return level."),
            ("5", "0-1", "MONO / PING-PONG", SEL, "Every echo in the middle, or alternating left and right."),
            ("7", "0-15", "REVERB SIZE", CONT, "The room's size."),
            ("8", "0-15", "REVERB DECAY", CONT, "How long the tail rings: 0.3 s at 0 to 8 s at 100."),
            ("9", "0-15", "REVERB DAMPING", CONT, "How fast the tail loses its top: bright at 0, dark at 100."),
            ("10", "0-15", "REVERB WET", CONT, "The reverb's return level."),
            ("14", "0-6 / 8-14", "RETURNS", SHOW, "The delay's and the reverb's levels as they sound."),
        ]),
        ("COLOUR", [
            ("1-5", "0-15", "DRIVE", CONT, "AMOUNT (wet), SHAPE (TUBE, FOLD, RAT), GAIN, TONE, LEVEL."),
            ("6-7", "0-15", "RING MOD", CONT, "AMOUNT (wet), FREQUENCY."),
            ("8-13", "0-15", "COMPRESSOR", CONT, "AMOUNT (wet), THRESHOLD, RATIO (1:1 2:1 3:1 4:1 6:1 8:1), ATTACK, RELEASE, MAKE-UP GAIN."),
            ("14", "0-6", "BITS", SEL, "16 (off), 12, 10, 8, 6, 5, 4: bit reduction of the whole mix."),
            ("14", "8-15", "SAMPLE RATE", SEL, "127 (full), 100, 80, 60, 45, 32, 20, 10: the LXR's own sample-rate reduction of every voice."),
        ]),
    ],
    notes="""<p>Every continuous row is a bar: tap a pad to set it, − and + at pads 14 and 15 for single steps (hold to
repeat). The freezes are also on the PERFORM page, where LATCH can hold them. Projects saved by version 0.0.2 load
with FX ENGINE on PLINKY, as they sounded then.</p>""",
    crib=dict(
        zones=[(0, 0, 6, "SENDS / ROUTE", "amber"), (0, 8, 8, "SPC", "white"), (0, 9, 9, "COL", "grey"),
               (0, 11, 11, "ENG", "lime"), (0, 12, 12, "DFZ", "ice"), (0, 13, 13, "RFZ", "ice"), (0, 15, 15, "LIM", "white"),
               (1, 0, 11, "DELAY TIME ×12", "amber"), (2, 0, 13, "FEEDBACK", "amber"), (3, 0, 13, "LP · FILTER · HP", "amber"),
               (4, 0, 13, "DELAY WET", "amber"), (5, 0, 0, "M", "amber"), (5, 1, 1, "PP", "amber"),
               (7, 0, 13, "REVERB SIZE", "violet"), (8, 0, 13, "DECAY", "violet"), (9, 0, 13, "DAMPING", "violet"),
               (10, 0, 13, "REVERB WET", "violet"),
               (14, 0, 6, "DLY RET", "dim"), (14, 8, 14, "VRB RET", "dim")] +
              [(y, 14, 14, "−", "dim") for y in (1, 2, 3, 4, 7, 8, 9, 10)] + [(y, 15, 15, "+", "grey") for y in (1, 2, 3, 4, 7, 8, 9, 10)] +
              row15(),
        caption="SPACE bank. COLOUR: rows 1-5 drive, 6-7 ring mod, 8-13 compressor, 14 bits | sample rate.",
        moves=[("Row 0 x8 / x9", "SPACE / COLOUR bank"),
               ("Row 0 x11", "FX ENGINE: CLATTER 12 or PLINKY"),
               ("Hold x12 / x13", "freeze delay / reverb"),
               ("SPACE rows 1-5", "delay: time, fdbk, filter, wet, ping"),
               ("SPACE rows 7-10", "reverb: size, decay, damp, wet"),
               ("COLOUR row 0", "route tracks to drive/ring/comp"),
               ("MIX rows 12-13", "how much each track sends")],
    ),
))

CHAPTERS.append(dict(
    id="perform", name="PERFORM", tag="PAGE 6 · ROW 15 PAD 5", img="perform.png",
    alt="The PERFORM page: momentary mutes and solos, kills, throws and freezes, fills, mutation, density, stutter, scenes and QUANTISE, and the bar position.",
    lede="""<p>Everything here is <b>temporary</b>: it lasts while the pad is held and leaves the pattern, kit and mixer
alone. <b>LATCH</b> makes a tap hold instead; <b>COMMIT</b> writes what you are hearing into the pattern; <b>UNDO</b>
takes it back.</p>""",
    rows=[
        ("0", "0-6", "MUTE", MOM, "Silent while held. The mixer's own mute is untouched (COMMIT makes it one)."),
        ("0", "8-14", "SOLO", MOM, "Only this track (and other held solos) while held."),
        ("1", "0-7", "KILL LOW · MID · HIGH · ALL", MOM, "Two pads each: DRUM 1-2, DRUM 3 and SNARE, CYMBAL and hats, everything. Cut at once while held; delay and reverb tails ring on."),
        ("1", "8-9", "DELAY THROW (ALL)", MOM, "Every track's delay send at full while held: let go and the echoes trail off."),
        ("1", "10-11", "REVERB THROW (ALL)", MOM, "Every track's reverb send at full while held."),
        ("1", "12-13", "DELAY FREEZE", MOM, "The delay loops what it holds."),
        ("1", "14-15", "REVERB FREEZE", MOM, "The reverb tail holds still."),
        ("2", "0-6", "DELAY THROW", MOM, "One track's delay send at full while held."),
        ("2", "8-14", "REVERB THROW", MOM, "One track's reverb send at full while held."),
        ("4", "0-6", "FILL", MOM, "The track fills its gaps, more and louder towards the end of the bar, the last steps ratcheting."),
        ("4", "8-9", "FILL DRUMS", MOM, "DRUM 2, DRUM 3 and SNARE fill."),
        ("4", "10-11", "FILL ALL", MOM, "Every track fills."),
        ("5", "0-5", "MUTATE S · M · L", MOM, "Two pads each: a mutation of the pattern plays while held (locked tracks kept), a new one each press."),
        ("6", "0-15", "DENSITY", MOM, "Pads 0-6 thinner (soft hits drop first), 7-8 as written, 9-15 busier (ghost notes in the gaps)."),
        ("8", "0-7", "STUTTER", MOM, "Two pads each: 1/8, 1/16, 1/32, 1/32T. The step just played repeats at this rate; let go and the sequence is where it would have been. Shape and tracks: REPEAT row 10."),
        ("10", "0-7", "SCENES 1-8", "TAP · HOLD 1 S", "Tap: recall the scene at the next bar (at once when stopped). Hold one second: store the present into it."),
        ("10", "9", "QUANTISE", TOG, "Whether a scene waits for the next bar or changes at once."),
        ("13", "0-15", "STEP IN THE BAR", SHOW, "Where the sequence is in its bar: for timing a throw, a fill or a scene."),
        ("14", "0-6, 8, 10-11", "METERS", SHOW, "Tracks, output, delay and reverb returns."),
        ("15", "9", "LAT", TOG, "LATCH. Off: a pad lasts while held. On: a tap holds it until tapped again. Turning it off lets everything go. Shared with REPEAT."),
        ("15", "10", "CMT", DESTR, "COMMIT. Hold half a second: the mutation, fill or density is written into the pattern, mutes and solos into the mixer; then everything is released. Throws, freezes and stutters are moments and are not written."),
        ("15", "11", "REL", TAP, "RELEASE ALL: let go of everything LATCH holds."),
    ],
    notes="""<p>A <b>scene</b> is a snapshot of the mixer (levels, mutes, solos, sends, routing, master) and every FX setting.
Recalled on the bar, the bar's first hit is already in the new scene. CLATTER 12 has one pattern and one kit, so a scene
does not change them. Scenes are saved with the project.</p>""",
    crib=dict(
        zones=tracks_row(0, 0) + [(0, 8, 14, "SOLO ×7", "gold"),
               (1, 0, 1, "K LO", "kill"), (1, 2, 3, "K MID", "kill"), (1, 4, 5, "K HI", "kill"), (1, 6, 7, "K ALL", "kill"),
               (1, 8, 9, "D THR", "amber"), (1, 10, 11, "R THR", "violet"), (1, 12, 13, "D FRZ", "ice"), (1, 14, 15, "R FRZ", "ice"),
               (2, 0, 6, "DELAY THROW ×7", "amber"), (2, 8, 14, "REVERB THROW ×7", "violet"),
               (4, 0, 6, "FILL ×7", "green"), (4, 8, 9, "F DRM", "green"), (4, 10, 11, "F ALL", "green"),
               (5, 0, 1, "MUT S", "teal"), (5, 2, 3, "MUT M", "teal"), (5, 4, 5, "MUT L", "teal"),
               (6, 0, 6, "THINNER", "blue"), (6, 7, 8, "=", "white"), (6, 9, 15, "BUSIER", "orange"),
               (8, 0, 1, "1/8", "magenta"), (8, 2, 3, "1/16", "magenta"), (8, 4, 5, "1/32", "magenta"), (8, 6, 7, "1/32T", "magenta"),
               (10, 0, 7, "SCENES 1-8", "gold"), (10, 9, 9, "Q", "white"),
               (13, 0, 15, "STEP IN THE BAR", "dim"), (14, 0, 6, "METERS", "dim"), (14, 8, 8, "O", "dim"), (14, 10, 11, "D R", "dim")] +
              row15([(9, "LAT", "sky"), (10, "CMT", "kill"), (11, "REL", "grey")]),
        moves=[("Hold any pad", "it lasts while held"),
               ("LAT, then tap", "it stays on; tap again to end"),
               ("REL", "let go of everything latched"),
               ("Hold CMT 0.5 s", "write it into the pattern"),
               ("Tap a scene", "recall at the next bar"),
               ("Hold a scene 1 s", "store the mixer and FX"),
               ("UND", "undo a COMMIT")],
    ),
))

CHAPTERS.append(dict(
    id="repeat", name="REPEAT", tag="PAGE 7 · ROW 15 PAD 6", img="repeat.png",
    alt="The REPEAT page: note repeat pads for seven tracks, rates, pressure modes and curves, roll pads and target, beat repeat lengths, shape and tracks.",
    lede="""<p>Note repeat, rolls by hand and beat repeat. Hold a track's pad and it strikes in time; press harder for louder,
faster or more often. Like PERFORM, its pads are momentary and LATCH holds them.</p>""",
    rows=[
        ("0-2", "0-13", "REPEAT", MOM, "A track each, two pads wide (DRUM 1 at 0-1 … OP HAT at 12-13). Hold to strike the voice on the RATE's grid: the first at once, then in time with the sequence. Several at once. It also makes this track the roll target."),
        ("0-2", "14-15", "PRESS VELOCITY", SHOW, "The velocity your press gives the repeats."),
        ("4", "0-4", "RATE straight", SEL, "1/4, 1/8, 1/16, 1/32, 1/64 (violet)."),
        ("4", "6-9", "RATE triplet", SEL, "1/4T, 1/8T, 1/16T, 1/32T (cyan)."),
        ("4", "11-12", "RATE dotted", SEL, "1/8D, 1/16D (amber). Never faster than one strike in 10 ms."),
        ("5", "0-3", "PRESSURE", SEL, "VELOCITY: harder is louder (45-127, the default). RATE: harder is faster (2× past a firm press, 4× past a hard one). CHANCE: harder plays more of the strikes. FIXED: every strike at 100."),
        ("5", "8-14", "CURVE", SEL, "FLAT, FADE IN, FADE OUT, ACCENT FIRST, ACCENT LAST (accents each beat), ALTERNATING, HUMAN. Fades run over the first eight strikes."),
        ("7", "0-5", "ROLL", TAP, "FLAM, DRAG, DOUBLE, TRIPLE, ACCELERATING, DECELERATING, played now on the held REPEAT tracks, or else the target. On the next step when playing. As loud as you press."),
        ("7", "8-14", "ROLL TARGET", SEL, "The track a roll pad plays."),
        ("9", "0-15", "BEAT REPEAT", MOM, "Two pads each: 1 BAR, 1/2, 1/4, 1/8, 1/16, 1/32, 1/16T, 1/32T. While held, the last stretch of the sequence of that length repeats, for the tracks lit on row 10. Let go: the sequence is where it would have been."),
        ("10", "0-2", "SHAPE", SEL, "FLAT (each repeat as loud), FADE (each softer), BUILD (building up). Also shapes PERFORM's STUTTER."),
        ("10", "8-14", "TRACKS", TOG, "Whether each track takes part in BEAT REPEAT and STUTTER. Unlit: it plays on as written."),
        ("13", "0-15", "STEP IN THE BAR", SHOW, "Where the sequence is in its bar."),
        ("14", "0-6, 8, 10-11", "METERS", SHOW, "Tracks, output, delay and reverb returns."),
        ("15", "9", "LAT", TOG, "LATCH, shared with PERFORM: a tap keeps a repeat going."),
        ("15", "11", "REL", TAP, "RELEASE ALL: let go of everything LATCH holds."),
    ],
    notes="""<p>Beat repeat is the sequencer repeating, not a recording: kits and edits stay live while it loops. Lengths shorter
than a step (1/32, the triplets) retrigger the step's hits at that rate. The ROLL pads are the way to audition a roll on
any track before writing it into a step on SEQ (row 10).</p>""",
    crib=dict(
        zones=[((0, 2), 2 * i, 2 * i + 1, TRACKS[i].replace(" ", "<br>"), TRACK_COL[i]) for i in range(7)] +
              [((0, 2), 14, 15, "VEL", "dim"),
               (4, 0, 4, "1/4 … 1/64", "violet"), (4, 6, 9, "TRIPLETS", "cyan"), (4, 11, 12, "DOT", "amber"),
               (5, 0, 3, "V R C F", "white"), (5, 8, 14, "CURVE ×7", "teal"),
               (7, 0, 5, "ROLL ×6", "olive"), (7, 8, 14, "ROLL TARGET ×7", "grey"),
               (9, 0, 15, "BEAT REPEAT  1 BAR … 1/32T", "magenta"),
               (10, 0, 2, "SHAPE", "pink"), (10, 8, 14, "TRACKS ×7", "magenta"),
               (13, 0, 15, "STEP IN THE BAR", "dim"), (14, 0, 6, "METERS", "dim"), (14, 8, 8, "O", "dim"), (14, 10, 11, "D R", "dim")] +
              row15([(9, "LAT", "sky"), (11, "REL", "grey")]),
        moves=[("Hold a track (rows 0-2)", "note repeat, harder = louder"),
               ("Row 4", "rate: straight, triplet, dotted"),
               ("Row 5 x0-3", "what pressure does"),
               ("Row 7", "tap a roll on the target"),
               ("Hold row 9", "beat repeat 1 bar … 1/32T"),
               ("Row 10", "shape and which tracks repeat"),
               ("LAT", "a tap keeps it going")],
    ),
))

CHAPTERS.append(dict(
    id="save", name="SAVE", tag="PAGE 8 · ROW 15 PAD 7", img="save.png",
    alt="The SAVE page placeholder on the project page: on a Plinky this is Plinky's own save and load page.",
    lede="""<p>Plinky's own save and load page. CLATTER 12 saves the <b>whole project</b> into a song slot: steps, kit, mixer,
FX, tempo, swing, generator settings, REPEAT settings and scenes. Leave it with the right side buttons or row 15.</p>""",
    rows=[
        ("all", "", "SAVE / LOAD", "PLINKY", "Plinky draws this page itself; use it as on any Plinky panel to choose a slot and save or load."),
    ],
    notes="""<p>Projects from older versions load converted: version-1 projects (0.0.2) bring their ratchet spacing and velocity
runs across as rolls and curves, and their sends to Plinky's effects. On the project page there is no Plinky save page:
<b>SAVE PROJECT</b> downloads the very JSON the panel gives Plinky's save page, and <b>LOAD PROJECT</b> reads one back.</p>""",
    crib=None,
))


# ---------------------------------------------------------------- HTML
def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def kind_tag(kind):
    return f'<span class="kind">{esc(kind)}</span>'


def ref_table(rows, caption=None):
    body = "".join(
        f'<tr><td class="rc">{r}</td><td class="rc">{x}</td><th>{esc(c)}{kind_tag(k)}</th><td>{esc(d)}</td></tr>'
        for (r, x, c, k, d) in rows)
    cap = f"<caption>{caption}</caption>" if caption else ""
    return (f'<div class="scroll"><table class="ref">{cap}<thead><tr><th>Row</th><th>Pads</th><th>Control</th>'
            f'<th>What it does</th></tr></thead><tbody>{body}</tbody></table></div>')


def crib_html(ch):
    cr = ch["crib"]
    moves = "".join(f"<li><b>{esc(g)}</b><span>{esc(r)}</span></li>" for g, r in cr["moves"])
    cap = cr.get("caption", f"{ch['name']}: the pads, row 0 at the top")
    return f'''<aside class="crib" id="crib-{ch['id']}" aria-label="{ch['name']} crib sheet">
  <p class="crib-head"><span>CRIB SHEET</span> {ch['name']}</p>
  <div class="crib-body">
    {pad_map(cr["zones"], cap)}
    <div class="crib-moves"><p class="crib-sub">KEY MOVES</p><ul>{moves}</ul>
    <p class="crib-sub">ALWAYS</p><p class="crib-always">Row 15: pages 0-7 · TMP 8 · UND 12 · RED 13 · STP 14 · PLY 15</p></div>
  </div>
</aside>'''


def chapter_html(i, ch):
    num = f"{i:02d}"
    figs = f'<figure class="shot"><img src="img/{ch["img"]}" alt="{esc(ch["alt"])}" loading="lazy" width="1305" height="1064"><figcaption>{ch["name"]}{" · SPACE BANK" if ch.get("img2") else ""}</figcaption></figure>'
    if ch.get("img2"):
        figs += f'<figure class="shot"><img src="img/{ch["img2"]}" alt="{esc(ch["alt2"])}" loading="lazy" width="1305" height="1064"><figcaption>{ch["name"]} · COLOUR BANK</figcaption></figure>'
    tables = ref_table(ch["rows"])
    for title, rows in ch.get("rows_title_extra", []):
        tables += f'<h4>{title} bank</h4>' + ref_table(rows)
    crib = crib_html(ch) if ch.get("crib") else ""
    return f'''
<section id="{ch['id']}" class="chapter" aria-labelledby="h-{ch['id']}">
  <h2 id="h-{ch['id']}" class="section"><span>{num}</span> {ch['name']}</h2>
  <p class="chip-tag">{ch['tag']}</p>
  <div class="lead-grid">
    <div class="lead-text">{ch['lede']}{'<p class="jump"><a href="#crib-' + ch['id'] + '">CRIB SHEET ↓</a></p>' if crib else ''}</div>
    <div class="figs{' two' if ch.get('img2') else ''}">{figs}</div>
  </div>
  <h3 class="tab sub-tab">The pads</h3>
  {tables}
  <div class="notes">{ch['notes']}</div>
  {crib}
</section>'''


def contents():
    items = [("start", "Quick start", "Your first beat in a minute."),
             ("grid", "The grid", "Rows, row 15, side buttons, kinds of control, colour.")]
    items += [(c["id"], c["name"], c["tag"].title().replace("·", "·")) for c in CHAPTERS]
    out = []
    for n, (id_, name, sub) in enumerate(items, 1):
        out.append(f'<li><a href="#{id_}"><span>{n:02d}</span><b>{esc(name)}</b><i>{esc(sub)}</i></a></li>')
    return '<ol class="toc">' + "".join(out) + "</ol>"


KINDS = [
    ("MOMENTARY · HOLD", "mom", "Dim at rest, its colour while held, white-tinted while LATCH holds it.", "PERFORM, beat repeat, note repeat, freezes, nudge"),
    ("TOGGLE", "tog", "Lit when on.", "Mute, solo, lock, LATCH, FX ENGINE, ROUTE, QUANTISE"),
    ("CHOICE", "sel", "The chosen one bright, the others faint.", "Variation, ratchet, roll, curve, rate, bank"),
    ("CONTINUOUS", "cont", "A bar with − and + at its end (hold to repeat).", "SOUND rows, FX rows, GEN density"),
    ("HOLD 0.5 S · DESTRUCTIVE", "destr", "Dark red, filling with red as it is held. Undoable.", "CLR, COMMIT"),
    ("DISPLAY", "show", "Does nothing when touched.", "Meters, playhead, the bar's step, the seed"),
]


def page():
    kinds = "".join(f'<tr><th><span class="sw {c}" aria-hidden="true"><i></i><i></i><i></i><i></i></span>{k}</th><td>{d}</td><td>{e}</td></tr>' for k, c, d, e in KINDS)
    chapters = "".join(chapter_html(i + 3, ch) for i, ch in enumerate(CHAPTERS))
    return f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CLATTER 12 Manual</title>
<meta name="description" content="The CLATTER 12 manual: every page of the Plinky 12 panel (TEMPO, SEQ, SOUND, GEN, MIX, FX, PERFORM, REPEAT, SAVE), every pad and what it does, with a crib sheet for each page. Also as a PDF.">
<meta property="og:title" content="CLATTER 12 Manual">
<meta property="og:description" content="Every page, every pad, and a crib sheet for each. Experimental: emulator only.">
<meta property="og:type" content="article">
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16' shape-rendering='crispEdges'%3E%3Crect width='16' height='16' fill='%23000'/%3E%3Cg fill='%23ff9a1f'%3E%3Crect x='2' y='2' width='4' height='4'/%3E%3Crect x='10' y='2' width='4' height='4'/%3E%3Crect x='2' y='10' width='4' height='4'/%3E%3C/g%3E%3Crect x='10' y='10' width='4' height='4' fill='%23f4f4f0'/%3E%3C/svg%3E">
<link rel="preload" href="../assets/fonts/silkscreen-latin-400-normal.woff2" as="font" type="font/woff2" crossorigin>
<link rel="preload" href="../assets/fonts/ibm-plex-sans-latin-400-normal.woff2" as="font" type="font/woff2" crossorigin>
<link rel="stylesheet" href="../assets/site.css">
<link rel="stylesheet" href="../assets/manual.css">
</head>
<body>
<!-- Generated by tools/make_manual.py: edit that, not this file. -->
<a class="skip" href="#main">Skip to the content</a>
<p class="strip" role="status"><b>EXPERIMENTAL</b> <span>CLATTER 12 compiles, links and runs in Plinky's emulator. It has not run on a Plinky 12. This manual describes the panel as designed and simulated.</span> <a href="https://github.com/clickysteve/clatter-12">GITHUB ↗</a></p>

<header class="top">
  <a class="brand" href="../">CLATTER 12</a>
  <nav aria-label="Manual">
    <a href="#contents">Contents</a><a href="#start">Quick start</a><a href="#grid">The grid</a><a href="#seq">Pages</a><a href="clatter12-manual.pdf" download>PDF</a><a href="clatter12-crib-sheets.pdf" download>Crib sheets</a><a href="../#grid">Play it</a>
  </nav>
</header>

<main id="main">
<section class="hero" aria-labelledby="h-title">
  <h1 id="h-title" class="title">MANUAL</h1>
  <p class="sub">CLATTER 12, page by page and pad by pad.</p>
  <p class="lede">Every page of the panel, every pad on it and what it does, the gestures that hold it together, and a crib sheet for each page to keep beside the Plinky. Version {VERSION}. The screenshots are the panel itself, running on the <a href="../#grid">project page</a>.</p>
  <div class="cta">
    <a class="button primary" href="clatter12-manual.pdf" download>DOWNLOAD PDF</a>
    <a class="button" href="clatter12-crib-sheets.pdf" download>CRIB SHEETS PDF</a>
    <a class="link" href="../#grid">PLAY IT IN THE BROWSER ↗</a>
  </div>
</section>

<section id="contents" aria-labelledby="h-contents">
  <h2 id="h-contents" class="section"><span>00</span> Contents</h2>
  {contents()}
</section>

<section id="start" class="chapter" aria-labelledby="h-start">
  <h2 id="h-start" class="section"><span>01</span> Quick start</h2>
  <ol class="steps">
    <li><b>Press PLY</b> (row 15, the last pad). A pattern plays. <b>STP</b> next to it stops.</li>
    <li><b>Tap a dark pad</b> on SEQ's rows 0-6 to put a step in: the harder you press, the louder it is. Tap it again to take it out.</li>
    <li><b>Hold a lit step</b> and tap the rows under the steps: a ratchet on row 8, a condition on row 9, a flam or roll on row 10.</li>
    <li><b>Tap TMP</b> (row 15, pad 8) in time to set the tempo, or <b>hold</b> it for the tempo overlay.</li>
    <li><b>Change page</b> with row 15's first eight pads: SEQ, SND, GEN, MIX, FX, PRF, RPT, SAV. On GEN, <b>NEW KIT</b> (row 12) designs a whole new kit.</li>
    <li>Go to <b>PRF</b> and hold pads: kills, throws, fills, stutters. Let go and the beat is back. <b>UND</b> undoes almost anything.</li>
  </ol>
  <div class="notice" style="margin-top:18px">
    <span class="notice-head">UNVERIFIED</span>
    <p class="notice-body">Nothing here has run on a Plinky 12. The manual describes the firmware as it behaves in its simulator and on the project page.</p>
  </div>
</section>

<section id="grid" class="chapter" aria-labelledby="h-grid">
  <h2 id="h-grid" class="section"><span>02</span> The grid</h2>
  <p class="intro">The grid is the instrument: a 16 × 16 field of pressure-sensitive pads, eight pages, and nothing more than one touch deep. No menus and no shift layers; the one overlay is the tempo's, and it lasts only while its pad is held. Throughout this manual <b>rows</b> are numbered 0 to 15 from the top and <b>pads</b> 0 to 15 from the left, as the firmware counts them.</p>

  <h3 class="tab sub-tab">Row 15: the same on every page</h3>
  <div class="row15" aria-label="Row 15">
    <span class="r-pg">SEQ</span><span class="r-pg">SND</span><span class="r-pg">GEN</span><span class="r-pg">MIX</span><span class="r-pg">FX</span><span class="r-pg">PRF</span><span class="r-pg">RPT</span><span class="r-pg">SAV</span><span class="r-tmp">TMP</span><span class="r-own">·</span><span class="r-own">·</span><span class="r-own">·</span><span class="r-und">UND</span><span class="r-red">RED</span><span class="r-stp">STP</span><span class="r-ply">PLY</span>
  </div>
  {ref_table([
      ("15", "0-7", "PAGES", "NAVIGATION", "SEQ, SOUND, GEN, MIX, FX, PERFORM, REPEAT, SAVE. The page shown is lit white. Audio never stops; a pad held on the page you leave is let go, and what LATCH holds keeps going."),
      ("15", "8", "TMP", "TAP · HOLD", "Tap tempo; hold for the tempo overlay. Flashes on the beat. See TEMPO."),
      ("15", "9-11", "(the page's own)", "", "SEQ: GEN, MUT, CLR. PERFORM: LAT, CMT, REL. REPEAT: LAT, REL."),
      ("15", "12", "UND", "TAP", "Undo: steps, generated or mutated patterns, kits and sounds, parameter edits, clears, length, copies and COMMITs. Edits of one kind close together undo as one. Sixteen levels."),
      ("15", "13", "RED", "TAP", "Redo what UNDO took back."),
      ("15", "14", "STP", "TRANSPORT", "Stop Plinky's transport, and the sequencer with it."),
      ("15", "15", "PLY", "TRANSPORT", "Start or stop."),
  ])}

  <h3 class="tab sub-tab">Plinky's side buttons</h3>
  <div class="scroll"><table class="gest"><thead><tr><th>Button</th><th>What it does</th></tr></thead><tbody>
    <tr><th>Left, top / bottom</th><td>Tempo up / down; hold to repeat. The same clock as TMP.</td></tr>
    <tr><th>Right, top / bottom</th><td>Previous / next page. Up from SEQ is the panel settings page, which shows the audio core's load (DSP).</td></tr>
  </tbody></table></div>

  <h3 class="tab sub-tab">Kinds of control</h3>
  <p class="intro" style="margin-bottom:14px">Every pad is one of six kinds, and it looks like its kind. The project page's readout names the kind of any pad you hover.</p>
  <div class="scroll"><table class="kinds"><thead><tr><th>Kind</th><th>Looks like</th><th>Examples</th></tr></thead><tbody>{kinds}</tbody></table></div>

  <h3 class="tab sub-tab">Colour</h3>
  <p class="intro">Seven track hues, always in the same order: <span class="hue" style="--h:{C['red']}">DRUM 1</span> <span class="hue" style="--h:{C['orange']}">DRUM 2</span> <span class="hue" style="--h:{C['yellow']}">DRUM 3</span> <span class="hue" style="--h:{C['green']}">SNARE</span> <span class="hue" style="--h:{C['cyan']}">CYMBAL</span> <span class="hue" style="--h:{C['blue']}">CL HAT</span> <span class="hue" style="--h:{C['violet']}">OP HAT</span>. Brightness is velocity or level; white is the page shown, the selected step and the playhead. On the performance pages: kill red, delay amber, reverb violet, freeze ice blue, fill green, mutation teal, stutter and beat repeat magenta, scenes gold, LATCH blue, density blue (thinner) to orange (busier).</p>
</section>
{chapters}
</main>

<footer class="foot">
  <p>CLATTER 12 manual, version {VERSION}. CLATTER 12 is free software under the GNU GPL version 3 only, with the LXR-02 firmware's additional terms. It contains a modified version of the Erica Synths LXR-02 firmware's sound engine. "Erica Synths", "LXR" and "LXR-02" are trademarks of Erica Synths; "Plinky" and "Plinky 12" name the hardware it targets. CLATTER 12 is an independent project, not made, approved or endorsed by either. <a href="../">Project page</a> · <a href="https://github.com/clickysteve/clatter-12/blob/main/docs/DESIGN.md">Interface design</a> · <a href="https://github.com/clickysteve/clatter-12">Source</a> · Fonts: Silkscreen and IBM Plex Sans, SIL OFL (<a href="../assets/fonts/OFL-Silkscreen.txt">1</a>, <a href="../assets/fonts/OFL-IBM-Plex.txt">2</a>).</p>
</footer>
</body>
</html>
'''


if __name__ == "__main__":
    html = page()
    if "—" in html:
        sys.exit("make_manual.py: an em dash crept in")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write(html)
    print("wrote", os.path.relpath(OUT, ROOT))
