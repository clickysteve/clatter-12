// panel_help.h - what each pad of each page does, in words: the project page's readout.
// Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Not part of the panel (the firmware has no use for it). Included after the panel by the
// WebAssembly build (sim/wasm_panel.cpp); tests/test_wasm.mjs checks that every
// pad a page lights has an entry here. Names that depend on state (a step's track and number, a
// SOUND row's LXR parameter and value) are read from the running panel, so they cannot drift from
// it. When the panel's layout changes, change this too: the test fails until it is.
#pragma once

struct c12_help_t {
  char name[48], kind[48], does[480];
};

static void c12_help_set(c12_help_t &h, const char *name, const char *kind, const char *does) {
  snprintf(h.name, sizeof h.name, "%s", name);
  snprintf(h.kind, sizeof h.kind, "%s", kind);
  snprintf(h.does, sizeof h.does, "%s", does);
}

static const char *const c12_track_names[7] = {"DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "CL HAT", "OP HAT"};
static const char *const c12_voice_names[6] = {"DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "HI-HAT"};
static const char *const c12_page_names[8] = {"SEQ", "SOUND", "GEN", "MIX", "FX", "PERFORM", "REPEAT", "SAVE"};
static const char *const c12_roll_names[7] = {"NONE", "FLAM", "DRAG", "DOUBLE STROKE", "TRIPLE STROKE", "ACCELERATING", "DECELERATING"};
static const char *const c12_curve_names[7] = {"FLAT", "FADE IN", "FADE OUT", "ACCENT FIRST", "ACCENT LAST", "ALTERNATING", "HUMAN"};
static const char *const c12_delay_names[12] = {"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2"};
static const char *c12_div_name(int d) {
  static const char *const n[14] = {"OFF", "1 BAR", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/4T", "1/8T", "1/16T", "1/32T", "1/8D", "1/16D"};
  return d >= 0 && d < 14 ? n[d] : "?";
}
// the kinds of control, as the site and DESIGN.md name them
#define K_MOM "MOMENTARY · HOLD"
#define K_TOG "TOGGLE"
#define K_SEL "CHOICE"
#define K_CONT "CONTINUOUS"
#define K_DESTR "HOLD 0.5 S · DESTRUCTIVE (UNDOABLE)"
#define K_SHOW "DISPLAY"
static const char *const c12_condition_names[13] = {"ALWAYS", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "25 %", "50 %", "75 %"};
static const char *const c12_variation_names[6] = {"NONE", "A", "B", "SECOND SOUND", "SECOND SOUND A", "SECOND SOUND B"};
static const char *const c12_style_names[11] = {"MACHINE", "ELECTRO", "BREAKS", "JUNGLE", "HALFTIME", "BROKEN", "GARAGE", "INDUSTRIAL", "GLITCH", "POLYMETER", "DUB"};
static const char *const c12_kit_names[16] = {"CLASSIC", "ANALOG", "DIGITAL", "INDUSTRIAL", "MINIMAL", "GLITCH", "TECHNO", "ELECTRO", "DUB", "NOISE", "METALLIC", "SOFT", "BROKEN", "ACID", "LO-FI", "CHAOS"};

// returns false for a pad that does nothing on that page
static bool c12_help(clatter12 *p, int page, int x, int y, c12_help_t &h) {
  char a[96], b[400];
  h.name[0] = h.kind[0] = h.does[0] = 0;
  if (y == 15) {
    if (x < 8) {
      snprintf(a, sizeof a, "%s PAGE", c12_page_names[x]);
      c12_help_set(h, a, "NAVIGATION · ROW 15, EVERY PAGE",
                   x == page ? "The page you are on (lit white). Row 15's first eight pads jump between pages; Plinky's right side buttons step through them too. Audio keeps playing, and what LATCH holds keeps going." :
                   x == 7 ? "Plinky's own save and load page: CLATTER 12 saves the whole project (steps, kit, mixer, FX, tempo, swing, generator, REPEAT settings, scenes) to a song slot. Leave it with the side buttons." :
                   "Go to this page. Switching pages never interrupts the sound; a pad held on the page you leave is let go.");
      return true;
    }
    if (x == 8) {
      snprintf(a, sizeof a, "TEMPO %d BPM", (int)(get_tempo_bpm() + 0.5f));
      c12_help_set(h, a, "TAP · HOLD · EVERY PAGE",
                   "Tap it in time: tap tempo, from the second tap (a 2 s pause starts a new count). Hold it: the tempo overlay, for as long as you hold, with the BPM in digits, -10 -5 -1 / +1 +5 +10, half and double time, sixteen preset tempos, a held nudge (about 4 % slower or faster, back when let go) and a row to tap on. It flashes on the beat. It sets Plinky's own tempo: one clock, as the left side buttons.");
      return true;
    }
    if (x == 12) { c12_help_set(h, "UNDO", "ROW 15, EVERY PAGE", "Undo the last change: steps, generated or mutated patterns, kits and sounds, parameter edits, clears, length, copies and COMMITs. Edits of one kind close together undo as one. Sixteen levels."); return true; }
    if (x == 13) { c12_help_set(h, "REDO", "ROW 15, EVERY PAGE", "Redo what UNDO took back."); return true; }
    if (x == 14) { c12_help_set(h, "STOP", "TRANSPORT", "Stop Plinky's transport (and the sequencer with it)."); return true; }
    if (x == 15) { c12_help_set(h, "PLAY", "TRANSPORT", "Start or stop. Tempo: the TEMPO pad on this row, or Plinky's left side buttons."); return true; }
    if ((page == 5 || page == 6) && x == 9) { c12_help_set(h, p->latch ? "LATCH: ON" : "LATCH: OFF", K_TOG, "Off, a performance pad lasts while you hold it. On, a tap holds it until you tap it again (or RELEASE); turning LATCH off lets everything go. Shared by PERFORM and REPEAT."); return true; }
    if (page == 5 && x == 10) { c12_help_set(h, "COMMIT", K_DESTR, "Hold half a second: what the performance is doing to the pattern (a mutation, a fill, a density) is written into the pattern, and its mutes and solos into the mixer. Throws, freezes and stutters are moments and are not written. Then everything is released. UNDO takes it back."); return true; }
    if ((page == 5 || page == 6) && x == 11) { c12_help_set(h, "RELEASE ALL", "TAP", "Let go of everything LATCH holds: every performance change returns to the pattern and mixer as they are."); return true; }
  }
  if (p->tempo_overlay && y < 15) {
    if (y >= 1 && y <= 5 && x < 13) { snprintf(a, sizeof a, "%d BPM", (int)(get_tempo_bpm() + 0.5f)); c12_help_set(h, a, K_SHOW, "The tempo. Amber after a tap tempo."); return true; }
    if (y == 3 && x == 14) { c12_help_set(h, "BEAT", K_SHOW, "Flashes on each beat."); return true; }
    if (y == 7) {
      static const int st[16] = {-10, -5, -1, 0, 0, 0, -100, -100, 100, 100, 0, 0, 0, 1, 5, 10};
      if (st[x] == -100) { c12_help_set(h, "HALF TIME", "TAP", "Halve the tempo."); return true; }
      if (st[x] == 100) { c12_help_set(h, "DOUBLE TIME", "TAP", "Double the tempo (at most 300)."); return true; }
      if (st[x]) { snprintf(a, sizeof a, "TEMPO %+d", st[x]); c12_help_set(h, a, "TAP", "Coarse and fine: the whole BPM steps CLATTER 12's sequencer keeps."); return true; }
      return false;
    }
    if (y == 9) { snprintf(a, sizeof a, "%d BPM", (int)c12glue::tempo_presets[x]); c12_help_set(h, a, "TAP", "Go straight to this tempo."); return true; }
    if (y == 11 && (x < 4 || x > 11)) { c12_help_set(h, x < 4 ? "NUDGE SLOWER" : "NUDGE FASTER", K_MOM, "While held, about 4 % slower or faster, to bring the beat into line with something else; let go, back to the tempo."); return true; }
    if (y == 13) { c12_help_set(h, "TAP", "TAP", "Tap the beat here, any pad of the row."); return true; }
    if (y == 14) { c12_help_set(h, "TEMPO RANGE", K_SHOW, "Where the tempo sits between 30 and 300 BPM."); return true; }
    return false;
  }
  switch (page) {
  case 0: // SEQ
    if (y < 7) {
      snprintf(a, sizeof a, "%s · STEP %d", c12_track_names[y], p->view_page * 16 + x + 1);
      c12_help_set(h, a, "STEP PAD",
                   "Tap an empty pad: the step is on, as loud as you press, and selected. Tap a lit pad: it goes (with everything it carried). Hold a lit pad: it stays, is selected, and the editor rows below change it; pressure re-takes its velocity until you use them. Hold several to edit them together.");
      return true;
    }
    if (y == 7) { c12_help_set(h, "PLAYHEAD", "DISPLAY", "The step playing now."); return true; }
    if (y == 8 && x <= 5) {
      snprintf(a, sizeof a, "VARIATION %s", c12_variation_names[x]);
      c12_help_set(h, a, "STEP EDITOR", "The selected step's variation: the same LXR voice with its parameters moved for that hit. Every track has A and B; DRUM 3 and SNARE also have a second sound (percussion, clap) with its A and B.");
      return true;
    }
    if (y == 8 && x >= 8 && x <= 13) {
      static const char *const n[6] = {"1x", "2x", "3x", "4x", "6x", "8x"};
      snprintf(a, sizeof a, "RATCHET %s", n[x - 8]);
      c12_help_set(h, a, "STEP EDITOR", "How many times the selected step strikes within its length, evenly (2x: 32nds, 3x: 32nd triplets, 4x: 64ths, 6x: 64th triplets, 8x: 128ths). No two strokes closer than 12 ms: at a fast tempo 8x plays 6 or 4 so each hit can sound. The strokes' velocities follow the curve on row 10. Stopped, it is played as you choose.");
      return true;
    }
    if (y == 9 && x < 13) {
      snprintf(a, sizeof a, "CONDITION %s", c12_condition_names[x]);
      c12_help_set(h, a, "STEP EDITOR", "When the selected step sounds: always; on one pass of every 2, 3 or 4 (counted in whole passes of the pattern from PLAY: 1:2 plays on the first, 2:2 on the second); or by chance.");
      return true;
    }
    if (y == 10 && x < 7) {
      static const char *const d[7] = {
          "No roll: the step (and its ratchet) as it is.",
          "A grace note about a 64th before the hit (15-35 ms), soft, played ahead of the step so the hit stays on the beat.",
          "Two grace notes, a 128th apart, before the hit: a ruff.",
          "Two strokes in the step, the second a little softer.",
          "Three strokes in the step, 100 / 72 / 86 %.",
          "Strokes closing in over two steps (an eighth), rising: the ratchet count sets how many (6 if under 3).",
          "Strokes spreading out over two steps, falling away."};
      snprintf(a, sizeof a, "ROLL %s", c12_roll_names[x]);
      c12_help_set(h, a, "STEP EDITOR", d[x]);
      return true;
    }
    if (y == 10 && x >= 8 && x < 15) {
      static const char *const d[7] = {"Every stroke at the step's velocity.", "Strokes rising to the step's velocity.", "Strokes falling from it.",
                                       "The first stroke at full, the rest softer.", "The last stroke at full, the rest softer.",
                                       "Loud, soft, loud, soft.", "Varied by chance (CLATTER's roll shape)."};
      snprintf(a, sizeof a, "CURVE %s", c12_curve_names[x - 8]);
      c12_help_set(h, a, "STEP EDITOR", d[x - 8]);
      return true;
    }
    if (y == 11) {
      snprintf(a, sizeof a, "VELOCITY %d", 8 + x * 119 / 15);
      c12_help_set(h, a, "STEP EDITOR", "Set the selected step's velocity exactly.");
      return true;
    }
    if (y == 12 && x < 7) { snprintf(a, sizeof a, "MUTE %s", c12_track_names[x]); c12_help_set(h, a, "TRACK", "Mute or unmute the track. Muted, its steps do not reach the voice and its sound is cut. Dark when muted."); return true; }
    if (y == 12 && x >= 8 && x < 15) { snprintf(a, sizeof a, "SOLO %s", c12_track_names[x - 8]); c12_help_set(h, a, "TRACK", "Solo: only soloed tracks are heard."); return true; }
    if (y == 13 && x < 7) { snprintf(a, sizeof a, "LOCK %s", c12_track_names[x]); c12_help_set(h, a, "TRACK", "Lock the track's rhythm: GEN and MUT leave it alone."); return true; }
    if (y == 13 && x >= 8 && x < 15) { snprintf(a, sizeof a, "PLAY %s", c12_track_names[x - 8]); c12_help_set(h, a, "AUDITION", "Play the voice now, as loud as you press."); return true; }
    if (y == 14 && x < 4) { snprintf(a, sizeof a, "VIEW STEPS %d-%d", x * 16 + 1, x * 16 + 16); c12_help_set(h, a, "PATTERN PAGE", "Show this page of steps. Hold one and tap another to copy it there."); return true; }
    if (y == 14 && x < 8) { snprintf(a, sizeof a, "LENGTH %d", (x - 3) * 16); c12_help_set(h, a, "PATTERN", "How many steps play."); return true; }
    if (y == 14) { static const int sw[8] = {50, 54, 57, 61, 64, 68, 71, 75}; snprintf(a, sizeof a, "SWING %d %%", sw[x - 8]); c12_help_set(h, a, "PATTERN", "Swing: 50 % is straight, 75 % dotted."); return true; }
    if (y == 15 && x == 9) { snprintf(a, sizeof a, "GENERATE (%s)", c12_style_names[p->style % 11]); c12_help_set(h, a, "PATTERN", "A new pattern from CLATTER's generator, in the GEN page's style, density and complexity. Locked tracks stay."); return true; }
    if (y == 15 && x == 10) { c12_help_set(h, "MUTATE", "PATTERN", "Musical edits to the unlocked tracks, at the GEN page's strength."); return true; }
    if (y == 15 && x == 11) { c12_help_set(h, "CLEAR", K_DESTR, "Hold for half a second to clear every step. UNDO brings them back."); return true; }
    return false;
  case 1: { // SOUND
    const int voice = c12::c12_voice_of_track(p->sound_track);
    if (y == 0) {
      if (x < 7) { snprintf(a, sizeof a, "EDIT %s", c12_track_names[x]); c12_help_set(h, a, "VOICE", "Choose the voice to edit (CL HAT and OP HAT are one voice)."); return true; }
      if (x >= 8 && x <= 10) { snprintf(a, sizeof a, "BANK %s", c12::c12_bank_name(x - 8)); c12_help_set(h, a, "PARAMETER BANK", "SOURCE: oscillators, noise, FM, pitch envelope. SHAPE: amplitude envelope, transient, filter, drive. MOD: LFO, velocity, volume, pan, sample rate."); return true; }
      if (x == 12) { c12_help_set(h, "AUDITION", "VOICE", "Play the voice, as loud as you press, without leaving the page."); return true; }
      if (x == 13) { c12_help_set(h, "NEW SOUND", "GENERATE", "Design a new sound for this voice, of the same family as the kit (CLATTER's kit designer)."); return true; }
      if (x == 14) { c12_help_set(h, "MUTATE SOUND", "GENERATE", "Move this voice towards another sound the designer draws for it, by the GEN page's AMOUNT."); return true; }
      if (x == 15) { c12_help_set(h, "LOCK SOUND", "GENERATE", "Locked, the voice keeps every parameter through NEW KIT and MUTATE KIT."); return true; }
      return false;
    }
    const c12::c12_param *rows[13];
    int n = c12::c12_bank_params(voice, p->sound_bank, rows);
    if (y >= 1 && y - 1 < n) {
      const c12::c12_param *q = rows[y - 1];
      const int v = p->kit[q->par];
      char val[24];
      if (q->kind == c12::c12x_kind_menu) snprintf(val, sizeof val, "%s", c12::c12_menu_option(q->menu, v));
      else if (q->kind == c12::c12x_kind_bipolar) snprintf(val, sizeof val, "%s%d", v > 63 ? "+" : "", v - 63);
      else snprintf(val, sizeof val, "%d", v);
      snprintf(a, sizeof a, "%s %s: %s", c12_voice_names[voice], q->label, val);
      if (x == 14) snprintf(b, sizeof b, "One step down (hold to repeat). The LXR parameter %d, 0-%d.", q->par, q->max);
      else if (x == 15) snprintf(b, sizeof b, "One step up (hold to repeat). The LXR parameter %d, 0-%d.", q->par, q->max);
      else if (q->kind == c12::c12x_kind_menu) snprintf(b, sizeof b, "%s", x <= q->max ? c12::c12_menu_option(q->menu, x) : "(not an option)");
      else snprintf(b, sizeof b, "Tap to set the value from left (0) to right (%d); - and + at the end of the row for single steps. It is heard at once.", q->max);
      c12_help_set(h, a, "LXR PARAMETER", b);
      return x <= 13 || x >= 14;
    }
    if (y == 14 && x < 7) { snprintf(a, sizeof a, "%s LEVEL", c12_track_names[x]); c12_help_set(h, a, "METER", "Each voice's level as it plays."); return true; }
    return false;
  }
  case 2: // GEN
    if (y == 0 && x < 11) { snprintf(a, sizeof a, "STYLE %s", c12_style_names[x]); c12_help_set(h, a, "PATTERN GENERATOR", "The rhythm style GENERATE writes in."); return true; }
    if (y == 0 && x == 12) { c12_help_set(h, "GENERATE PATTERN", "PATTERN GENERATOR", "A new pattern: style, density and complexity as set, locked tracks kept."); return true; }
    if (y == 0 && x == 13) { c12_help_set(h, "MUTATE PATTERN", "PATTERN GENERATOR", "Edit the pattern musically, at the mutation strength."); return true; }
    if (y == 1) { snprintf(a, sizeof a, "DENSITY %d", p->density); c12_help_set(h, a, "PATTERN GENERATOR", "How busy a new pattern is (50 is the style as written). Raising it adds hits to the same rhythm."); return true; }
    if (y == 2) { snprintf(a, sizeof a, "COMPLEXITY %d", p->complexity); c12_help_set(h, a, "PATTERN GENERATOR", "How unpredictable a new pattern is."); return true; }
    if (y == 3 && x < 3) { static const char *const n[3] = {"SMALL", "MEDIUM", "LARGE"}; snprintf(a, sizeof a, "MUTATION %s", n[x]); c12_help_set(h, a, "PATTERN GENERATOR", "How much MUTATE changes."); return true; }
    if (y == 3 && x >= 4 && x < 11) { snprintf(a, sizeof a, "LOCK %s", c12_track_names[x - 4]); c12_help_set(h, a, "PATTERN GENERATOR", "A locked track keeps its rhythm through GENERATE and MUTATE."); return true; }
    if (y == 4 && x >= 4 && x < 11) { snprintf(a, sizeof a, "NEW %s RHYTHM", c12_track_names[x - 4]); c12_help_set(h, a, "PATTERN GENERATOR", "Generate this track alone (again: another take)."); return true; }
    if (y == 6) { snprintf(a, sizeof a, "CHARACTER %s", c12_kit_names[x]); c12_help_set(h, a, "KIT GENERATOR", "The kind of kit NEW KIT designs. Every NEW KIT is a new design of it (a new seed), not a preset."); return true; }
    if (y == 7) { snprintf(a, sizeof a, "WILDNESS %d", p->wildness); c12_help_set(h, a, "KIT GENERATOR", "How far new and mutated sounds stray from the character: 50 is as written; higher lets voices become other archetypes."); return true; }
    if (y == 8) { snprintf(a, sizeof a, "MUTATION AMOUNT %d", p->kit_amount); c12_help_set(h, a, "KIT GENERATOR", "How far MUTATE KIT and MUTATE SOUND move a sound."); return true; }
    if (y == 9 && x < 6) { snprintf(a, sizeof a, "LOCK %s SOUND", c12_voice_names[x]); c12_help_set(h, a, "KIT GENERATOR", "Locked, the sound keeps every parameter through NEW KIT and MUTATE KIT."); return true; }
    if (y == 10 && x < 6) { snprintf(a, sizeof a, "NEW %s SOUND", c12_voice_names[x]); c12_help_set(h, a, "KIT GENERATOR", "Design a new sound for this voice alone."); return true; }
    if (y == 10 && x >= 8 && x < 14) { snprintf(a, sizeof a, "MUTATE %s SOUND", c12_voice_names[x - 8]); c12_help_set(h, a, "KIT GENERATOR", "Move this voice by the mutation amount."); return true; }
    if (y == 12 && x < 2) { c12_help_set(h, "NEW KIT", "KIT GENERATOR", "Design a whole new kit of the chosen character: every unlocked voice. UNDO returns to the last one."); return true; }
    if (y == 12 && x >= 3 && x < 5) { c12_help_set(h, "MUTATE KIT", "KIT GENERATOR", "Move every unlocked voice by the mutation amount."); return true; }
    if (y == 13) { c12_help_set(h, "KIT SEED", "DISPLAY", "The current kit's seed, in binary: each NEW KIT draws a new one."); return true; }
    return false;
  case 3: { // MIX
    const int s = x / 2;
    const bool master = s == 7;
    const char *who = master ? "MASTER" : c12_track_names[s];
    if (y == 0) { if (master) { c12_help_set(h, "LIMITER", "MASTER", "The output limiter on or off. On, nothing reaches full scale: no clipping in the 16-bit output."); return true; } snprintf(a, sizeof a, "MUTE %s", who); c12_help_set(h, a, "CHANNEL STRIP", "Mute the track."); return true; }
    if (y == 1 && !master) { snprintf(a, sizeof a, "SOLO %s", who); c12_help_set(h, a, "CHANNEL STRIP", "Solo the track."); return true; }
    if (y >= 2 && y <= 9) { snprintf(a, sizeof a, "%s LEVEL", who); c12_help_set(h, a, "FADER", "Tap a row to set the level: the top row is 115 (+2.4 dB), the second 100 (unity), the bottom off. Level 50 is -12 dB."); return true; }
    if (y == 10) { snprintf(a, sizeof a, "%s LEVEL %s", who, x & 1 ? "+" : "-"); c12_help_set(h, a, "FADER", "Fine level adjustment."); return true; }
    if (y == 11 && !master) { snprintf(a, sizeof a, "%s PAN %s", who, x & 1 ? "RIGHT" : "LEFT"); c12_help_set(h, a, "CHANNEL STRIP", "Move the voice's pan (the LXR's own pan, applied as each hit starts). Both pads at once: centre."); return true; }
    if (y == 12 && !master) { snprintf(a, sizeof a, "%s DELAY SEND %d %s", who, p->dsend[s], x & 1 ? "+" : "-"); c12_help_set(h, a, "SEND", p->plinky_fx() ? "How much of the track goes to the delay: Plinky's own (FX page, row 0: FX ENGINE is set to PLINKY)." : "How much of the track goes to the delay: CLATTER 12's, set on the FX page's SPACE bank (time, feedback, filter, wet)."); return true; }
    if (y == 13 && !master) { snprintf(a, sizeof a, "%s REVERB SEND %d %s", who, p->rsend[s], x & 1 ? "+" : "-"); c12_help_set(h, a, "SEND", p->plinky_fx() ? "How much of the track goes to the reverb: Plinky's own (FX ENGINE: PLINKY)." : "How much of the track goes to the reverb: CLATTER 12's, set on the FX page's SPACE bank (size, decay, damping, wet)."); return true; }
    if (y == 14) { snprintf(a, sizeof a, "%s METER", who); c12_help_set(h, a, "METER", "The track's level as it plays."); return true; }
    return false;
  }
  case 4: { // FX
    const bool colour = p->fx_bank == 1;
    if (y == 0) {
      if (x == 8) { c12_help_set(h, "BANK SPACE", K_SEL, "CLATTER 12's delay and reverb: rows 1-5 the delay, 7-10 the reverb. The tracks reach them through the MIX page's DELAY and REVERB sends."); return true; }
      if (x == 9) { c12_help_set(h, "BANK COLOUR", K_SEL, "The LXR-02 firmware's drive, ring modulator and compressor on the routed tracks; bit and sample-rate reduction."); return true; }
      if (x == 11) { c12_help_set(h, p->plinky_fx() ? "FX ENGINE: PLINKY" : "FX ENGINE: CLATTER 12", K_TOG, "Where the sends go. CLATTER 12 (green): its own delay and reverb, set here, heard everywhere, this page included. PLINKY (yellow): Plinky's own delay and reverb, set in Plinky's mix settings, not here; CLATTER 12's then cost nothing. Projects from version 0.0.2 load as PLINKY, as they were."); return true; }
      if (x == 12) { c12_help_set(h, "DELAY FREEZE", K_MOM, "While held, the delay keeps what it holds, round and round, and takes nothing new: a loop of the last echoes. Let go: it decays again. Also on PERFORM, where LATCH can hold it."); return true; }
      if (x == 13) { c12_help_set(h, "REVERB FREEZE", K_MOM, "While held, the reverb's tail stops decaying and takes nothing new: a pad of the room. Let go: it fades."); return true; }
      if (x == 15) { c12_help_set(h, "LIMITER", K_TOG, "The output limiter on or off. On, nothing reaches full scale."); return true; }
      if (x < 7 && colour) { snprintf(a, sizeof a, "ROUTE %s", c12_track_names[x]); c12_help_set(h, a, K_TOG, "Whether the track goes through the COLOUR chain (drive, ring modulator, compressor)."); return true; }
      if (x < 7) { snprintf(a, sizeof a, "%s SENDS %d / %d", c12_track_names[x], p->dsend[x], p->rsend[x]); c12_help_set(h, a, K_SHOW, "The track's delay (amber) and reverb (violet) sends, set on the MIX page, rows 12 and 13."); return true; }
      return false;
    }
    if (!colour) {
      const char *what = nullptr, *does = nullptr;
      char nm[64];
      switch (y) {
      case 1: snprintf(nm, sizeof nm, "DELAY TIME %s", c12_delay_names[c12::c12_fx_get(c12::c12x_fx_dly_time) % 12]); what = nm;
        does = x < 12 ? "Tap a division: 1/32, 1/16T, 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D, 1/4, 1/2T, 1/4D, 1/2, at the tempo (it follows tempo changes, gliding). Longer than 1.48 s, it halves." : x >= 14 ? "One division shorter or longer." : nullptr; break;
      case 2: snprintf(nm, sizeof nm, "DELAY FEEDBACK %d", c12::c12_fx_get(c12::c12x_fx_dly_feedback)); what = nm; does = "How much of each echo comes round again: 0 one echo, 100 a long trail (never runaway)."; break;
      case 3: { const int f = c12::c12_fx_get(c12::c12x_fx_dly_filter); snprintf(nm, sizeof nm, "DELAY FILTER %s%d", f < 64 ? "LOW-PASS " : f > 64 ? "HIGH-PASS " : "OPEN ", f); what = nm;
        does = "Each echo through it, once more each repeat: left of centre darker, right of centre thinner, the middle open."; break; }
      case 4: snprintf(nm, sizeof nm, "DELAY WET %d", c12::c12_fx_get(c12::c12x_fx_dly_wet)); what = nm; does = "The delay's return level (the dry sound is set by the MIX faders)."; break;
      case 5: snprintf(nm, sizeof nm, "DELAY %s", c12::c12_fx_get(c12::c12x_fx_dly_pingpong) ? "PING-PONG" : "MONO"); what = nm; does = x < 2 ? (x ? "PING-PONG: echoes alternate left and right." : "MONO: every echo in the middle.") : nullptr; break;
      case 7: snprintf(nm, sizeof nm, "REVERB SIZE %d", c12::c12_fx_get(c12::c12x_fx_verb_size)); what = nm; does = "The room's size: the lengths of the reverb's four lines."; break;
      case 8: snprintf(nm, sizeof nm, "REVERB DECAY %d", c12::c12_fx_get(c12::c12x_fx_verb_decay)); what = nm; does = "How long the tail rings: 0.3 s at 0 to 8 s at 100 (to 60 dB down)."; break;
      case 9: snprintf(nm, sizeof nm, "REVERB DAMPING %d", c12::c12_fx_get(c12::c12x_fx_verb_damp)); what = nm; does = "How fast the tail loses its top: bright at 0, dark at 100."; break;
      case 10: snprintf(nm, sizeof nm, "REVERB WET %d", c12::c12_fx_get(c12::c12x_fx_verb_wet)); what = nm; does = "The reverb's return level."; break;
      case 14: if (x < 7 || (x >= 8 && x < 15)) { c12_help_set(h, x < 7 ? "DELAY RETURN" : "REVERB RETURN", K_SHOW, "The delay's and the reverb's levels as they sound."); return true; } return false;
      }
      if (!what) return false;
      if (x >= 14) does = x == 14 ? "One step down (hold to repeat)." : "One step up (hold to repeat).";
      if (!does) return false;
      snprintf(b, sizeof b, "%s%s", does, p->plinky_fx() ? " (FX ENGINE is PLINKY: this does nothing until it is CLATTER 12 again.)" : "");
      c12_help_set(h, what, y == 1 || y == 5 ? K_SEL : K_CONT, b);
      return true;
    }
    static const char *const names[14] = {"", "DRIVE AMOUNT (WET)", "DRIVE SHAPE: TUBE, FOLD, RAT", "DRIVE GAIN", "DRIVE TONE", "DRIVE LEVEL", "RING MOD AMOUNT (WET)", "RING MOD FREQUENCY", "COMPRESSOR AMOUNT (WET)", "COMPRESSOR THRESHOLD", "COMPRESSOR RATIO 1:1 2:1 3:1 4:1 6:1 8:1", "COMPRESSOR ATTACK", "COMPRESSOR RELEASE", "COMPRESSOR MAKE-UP GAIN"};
    if (y >= 1 && y <= 13) {
      c12_help_set(h, names[y], "LXR FX (DSPAudio/FX) · " K_CONT, "The LXR-02 firmware's own effect, unchanged, run by CLATTER 12 on the routed tracks. At wet 0 it is off and costs nothing. Tap to set; - and + at the end of the row.");
      return true;
    }
    if (y == 14 && x < 7) { static const int bits[7] = {16, 12, 10, 8, 6, 5, 4}; snprintf(a, sizeof a, "BITS %d", bits[x]); c12_help_set(h, a, K_SEL, "Bit reduction of the whole mix (16: off). CLATTER 12's, not the LXR's."); return true; }
    if (y == 14 && x >= 8) { snprintf(a, sizeof a, "SAMPLE RATE %d", (int)c12glue::rate_levels[x - 8]); c12_help_set(h, a, K_SEL, "The LXR's own sample-rate reduction of every voice (VOICE_DECIMATION_ALL): 127 is full rate."); return true; }
    return false;
  }
  case 5: { // PERFORM
    const char *t7 = x < 7 ? c12_track_names[x] : x >= 8 && x < 15 ? c12_track_names[x - 8] : "";
    if (y == 0 && x < 7) { snprintf(a, sizeof a, "MUTE %s", t7); c12_help_set(h, a, K_MOM, "Silent while held (with LATCH, until tapped again). The mixer's own mute is untouched: COMMIT makes it one."); return true; }
    if (y == 0 && x >= 8 && x < 15) { snprintf(a, sizeof a, "SOLO %s", t7); c12_help_set(h, a, K_MOM, "Only this track (and other held solos) while held."); return true; }
    if (y == 1) {
      static const char *const k[4] = {"KILL LOW (DRUM 1, DRUM 2)", "KILL MID (DRUM 3, SNARE)", "KILL HIGH (CYMBAL, HATS)", "KILL ALL"};
      if (x < 8) { c12_help_set(h, k[x / 2], K_MOM, "Those tracks cut at once while held, their steps held back; the delay and reverb tails ring on. Let go: back on the next hit."); return true; }
      if (x < 10) { c12_help_set(h, "DELAY THROW (ALL)", K_MOM, "Every track's delay send at full while held: throw the beat into the echoes; let go and they trail off."); return true; }
      if (x < 12) { c12_help_set(h, "REVERB THROW (ALL)", K_MOM, "Every track's reverb send at full while held."); return true; }
      if (x < 14) { c12_help_set(h, "DELAY FREEZE", K_MOM, "The delay loops what it holds while held, taking nothing new."); return true; }
      c12_help_set(h, "REVERB FREEZE", K_MOM, "The reverb tail holds still while held.");
      return true;
    }
    if (y == 2 && x < 7) { snprintf(a, sizeof a, "DELAY THROW %s", t7); c12_help_set(h, a, K_MOM, "This track's delay send at full while held."); return true; }
    if (y == 2 && x >= 8 && x < 15) { snprintf(a, sizeof a, "REVERB THROW %s", t7); c12_help_set(h, a, K_MOM, "This track's reverb send at full while held."); return true; }
    if (y == 4 && x < 7) { snprintf(a, sizeof a, "FILL %s", t7); c12_help_set(h, a, K_MOM, "While held the track fills its gaps, more and louder towards the end of the bar, the last steps ratcheting (CLATTER's fill). The pattern is untouched unless you COMMIT."); return true; }
    if (y == 4 && x >= 8 && x < 10) { c12_help_set(h, "FILL DRUMS (DRUM 2, DRUM 3, SNARE)", K_MOM, "The toms and the snare fill while held."); return true; }
    if (y == 4 && x >= 10 && x < 12) { c12_help_set(h, "FILL ALL", K_MOM, "Every track fills while held."); return true; }
    if (y == 5 && x < 6) { static const char *const m[3] = {"MUTATE SMALL", "MUTATE MEDIUM", "MUTATE LARGE"}; c12_help_set(h, m[x / 2], K_MOM, "While held, a mutation of the pattern plays (CLATTER's mutation; locked tracks kept); a new one each press. Let go: the pattern as written. COMMIT keeps it."); return true; }
    if (y == 6) {
      const int d = clatter12::density_of(x);
      snprintf(a, sizeof a, d ? "DENSITY %+d" : "DENSITY AS WRITTEN", d);
      c12_help_set(h, a, K_MOM, d < 0 ? "While held, hits drop out, soft ones first (CLATTER's density by strength)." : d > 0 ? "While held, ghost notes go into the gaps of the tracks that play, most between the eighths." : "The middle: as written (with LATCH, tap it to let a latched density go).");
      return true;
    }
    if (y == 8 && x < 8) { snprintf(a, sizeof a, "STUTTER %s", c12_div_name(c12glue::stutter_lengths[x / 2])); c12_help_set(h, a, K_MOM, "While held, the step just played repeats at this rate (a beat repeat on the sequence, not the audio). Let go: the sequence is where it would have been. Shape and tracks: the REPEAT page, row 10."); return true; }
    if (y == 10 && x < 8) {
      snprintf(a, sizeof a, "SCENE %d%s", x + 1, p->scenes[x].used ? "" : " (EMPTY)");
      c12_help_set(h, a, "TAP: RECALL · HOLD 1 S: STORE", "A snapshot of the mixer (levels, mutes, solos, sends, route, master) and every FX setting. Tap: recalled at the next bar (QUANTISE on, playing), the bar's first hit already in it; at once when stopped. Hold a second: store the present into it. CLATTER 12 has one pattern and one kit: a scene does not change them.");
      return true;
    }
    if (y == 10 && x == 9) { c12_help_set(h, p->scene_quant ? "QUANTISE: NEXT BAR" : "QUANTISE: AT ONCE", K_TOG, "Whether a scene waits for the next bar."); return true; }
    if (y == 13) { c12_help_set(h, "STEP IN THE BAR", K_SHOW, "Where the sequence is in its bar: for timing a throw, a fill or a scene."); return true; }
    if (y == 14 && (x < 7 || x == 8 || x == 10 || x == 11)) { c12_help_set(h, x < 7 ? c12_track_names[x] : x == 8 ? "OUTPUT" : x == 10 ? "DELAY RETURN" : "REVERB RETURN", K_SHOW, "Levels as they sound."); return true; }
    return false;
  }
  case 6: { // REPEAT
    if (y <= 2 && x < 14) {
      snprintf(a, sizeof a, "REPEAT %s", c12_track_names[x / 2]);
      c12_help_set(h, a, K_MOM, "Hold to strike the voice on the RATE's grid: the first at once, then in time with the sequence (or the tempo, stopped). PRESSURE (row 5) decides what pressing harder does; a light touch always sounds. With LATCH a tap keeps it going. It also makes this track the roll target.");
      return true;
    }
    if (y <= 2) { c12_help_set(h, "PRESS VELOCITY", K_SHOW, "The velocity your press gives the repeats."); return true; }
    if (y == 4 && c12glue::rep_rates[x]) { snprintf(a, sizeof a, "RATE %s", c12_div_name(c12glue::rep_rates[x])); c12_help_set(h, a, K_SEL, "The note-repeat rate: straight (violet), triplet (cyan), dotted (amber). Never faster than one strike in 10 ms."); return true; }
    if (y == 5 && x < 4) {
      static const char *const n[4] = {"PRESSURE: VELOCITY", "PRESSURE: RATE", "PRESSURE: CHANCE", "PRESSURE: FIXED"};
      static const char *const d[4] = {"Harder is louder (45 to 127). The default.", "Harder is faster: the rate, twice it past a firm press, four times past a hard one (with some give, so it does not flicker).", "Harder is more: from a quarter of the strikes to all of them.", "Pressure does nothing: every strike at 100."};
      c12_help_set(h, n[x], K_SEL, d[x]);
      return true;
    }
    if (y == 5 && x >= 8 && x < 15) { snprintf(a, sizeof a, "REPEAT CURVE %s", c12_curve_names[x - 8]); c12_help_set(h, a, K_SEL, "How the repeats' velocities run while held: fades over the first eight strikes; ACCENT LAST accents each beat; ALTERNATING loud and soft."); return true; }
    if (y == 7 && x < 6) { snprintf(a, sizeof a, "ROLL %s", c12_roll_names[x + 1]); c12_help_set(h, a, "TAP", "Play this roll now on the held REPEAT pads' tracks, or else the target (x8-14): at once when stopped, on the next step when playing. As loud as you press. To audition rolls before writing them into steps (SEQ row 10)."); return true; }
    if (y == 7 && x >= 8 && x < 15) { snprintf(a, sizeof a, "ROLL TARGET %s", c12_track_names[x - 8]); c12_help_set(h, a, K_SEL, "The track a roll pad plays."); return true; }
    if (y == 9) { snprintf(a, sizeof a, "BEAT REPEAT %s", c12_div_name(c12glue::br_lengths[x / 2])); c12_help_set(h, a, K_MOM, "While held, the sequence's last stretch of this length repeats, from the step playing (or the next if it is half gone), for the tracks lit on row 10. It is the sequencer that repeats, not a recording: kits and edits stay live. Let go: the sequence is where it would have been."); return true; }
    if (y == 10 && x < 3) { static const char *const n[3] = {"REPEAT SHAPE FLAT", "REPEAT SHAPE FADE", "REPEAT SHAPE BUILD"}; c12_help_set(h, n[x], K_SEL, "Each beat repeat as loud as the first, or each softer, or building up."); return true; }
    if (y == 10 && x >= 8 && x < 15) { snprintf(a, sizeof a, "BEAT REPEAT %s", c12_track_names[x - 8]); c12_help_set(h, a, K_TOG, "Whether this track repeats in BEAT REPEAT and STUTTER (unlit: it plays on as written)."); return true; }
    if (y == 13) { c12_help_set(h, "STEP IN THE BAR", K_SHOW, "Where the sequence is in its bar."); return true; }
    if (y == 14 && (x < 7 || x == 8 || x == 10 || x == 11)) { c12_help_set(h, x < 7 ? c12_track_names[x] : x == 8 ? "OUTPUT" : x == 10 ? "DELAY RETURN" : "REVERB RETURN", K_SHOW, "Levels as they sound."); return true; }
    return false;
  }
  }
  return false;
}
