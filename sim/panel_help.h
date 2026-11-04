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
  char name[48], kind[40], does[400];
};

static void c12_help_set(c12_help_t &h, const char *name, const char *kind, const char *does) {
  snprintf(h.name, sizeof h.name, "%s", name);
  snprintf(h.kind, sizeof h.kind, "%s", kind);
  snprintf(h.does, sizeof h.does, "%s", does);
}

static const char *const c12_track_names[7] = {"DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "CL HAT", "OP HAT"};
static const char *const c12_voice_names[6] = {"DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "HI-HAT"};
static const char *const c12_page_names[6] = {"SEQ", "SOUND", "GEN", "MIX", "FX", "SAVE"};
static const char *const c12_condition_names[13] = {"ALWAYS", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "25 %", "50 %", "75 %"};
static const char *const c12_variation_names[6] = {"NONE", "A", "B", "SECOND SOUND", "SECOND SOUND A", "SECOND SOUND B"};
static const char *const c12_style_names[11] = {"MACHINE", "ELECTRO", "BREAKS", "JUNGLE", "HALFTIME", "BROKEN", "GARAGE", "INDUSTRIAL", "GLITCH", "POLYMETER", "DUB"};
static const char *const c12_kit_names[16] = {"CLASSIC", "ANALOG", "DIGITAL", "INDUSTRIAL", "MINIMAL", "GLITCH", "TECHNO", "ELECTRO", "DUB", "NOISE", "METALLIC", "SOFT", "BROKEN", "ACID", "LO-FI", "CHAOS"};

// returns false for a pad that does nothing on that page
static bool c12_help(clatter12 *p, int page, int x, int y, c12_help_t &h) {
  char a[96], b[400];
  h.name[0] = h.kind[0] = h.does[0] = 0;
  if (y == 15) {
    if (x < 6) {
      snprintf(a, sizeof a, "%s PAGE", c12_page_names[x]);
      c12_help_set(h, a, "NAVIGATION · ROW 15, EVERY PAGE",
                   x == page ? "The page you are on (lit white). Row 15's first six pads jump between pages; Plinky's right side buttons step through them too. Audio keeps playing." :
                   x == 5 ? "Plinky's own save and load page: CLATTER 12 saves the whole project (steps, kit, mixer, FX, tempo, swing, generator) to a song slot. Leave it with the side buttons." :
                   "Go to this page. Switching pages never interrupts the sound.");
      return true;
    }
    if (x == 12) { c12_help_set(h, "UNDO", "ROW 15, EVERY PAGE", "Undo the last change: steps, generated or mutated patterns, kits and sounds, parameter edits, clears, length and copies. Edits of one kind close together undo as one. Sixteen levels."); return true; }
    if (x == 13) { c12_help_set(h, "REDO", "ROW 15, EVERY PAGE", "Redo what UNDO took back."); return true; }
    if (x == 14) { c12_help_set(h, "STOP", "TRANSPORT", "Stop Plinky's transport (and the sequencer with it)."); return true; }
    if (x == 15) { c12_help_set(h, "PLAY", "TRANSPORT", "Start or stop. Tempo is Plinky's: its left side buttons."); return true; }
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
      c12_help_set(h, a, "STEP EDITOR", "How many times the selected step strikes within its length. 1x is a plain hit. The spacing and velocity of the strokes are on row 10.");
      return true;
    }
    if (y == 9 && x < 13) {
      snprintf(a, sizeof a, "CONDITION %s", c12_condition_names[x]);
      c12_help_set(h, a, "STEP EDITOR", "When the selected step sounds: always; on one pass of every 2, 3 or 4 (counted in whole passes of the pattern from PLAY: 1:2 plays on the first, 2:2 on the second); or by chance.");
      return true;
    }
    if (y == 10 && x <= 3) {
      static const char *const n[4] = {"EVEN", "ACCELERATING", "DECELERATING", "RANDOM"};
      snprintf(a, sizeof a, "ROLL %s", n[x]);
      c12_help_set(h, a, "STEP EDITOR", "How a ratchet's strokes are spaced: evenly, closing in, spreading out, or each nudged by chance (the same on the same pass).");
      return true;
    }
    if (y == 10 && x >= 5 && x <= 8) {
      static const char *const n[4] = {"DECAY", "CRESCENDO", "DECRESCENDO", "HUMAN"};
      snprintf(a, sizeof a, "VELOCITY %s", n[x - 5]);
      c12_help_set(h, a, "STEP EDITOR", "How a ratchet's strokes' velocities run: each 12 % softer (CLATTER's ratchet), rising, falling, or varied (CLATTER's roll shapes).");
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
    if (y == 15 && x == 6) { c12_help_set(h, "GENERATE", "PATTERN", "A new pattern from CLATTER's generator, in the GEN page's style, density and complexity. Locked tracks stay."); return true; }
    if (y == 15 && x == 7) { c12_help_set(h, "MUTATE", "PATTERN", "Musical edits to the unlocked tracks, at the GEN page's strength."); return true; }
    if (y == 15 && x == 8) { snprintf(a, sizeof a, "STYLE: %s", c12_style_names[p->style % 11]); c12_help_set(h, a, "PATTERN", "Step to the next of the generator's eleven styles."); return true; }
    if (y == 15 && x == 9) { c12_help_set(h, "CLEAR", "PATTERN", "Hold for half a second to clear every step. UNDO brings them back."); return true; }
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
    if (y == 12 && !master) { snprintf(a, sizeof a, "%s DELAY SEND %s", who, x & 1 ? "+" : "-"); c12_help_set(h, a, "SEND", "How much of the track goes to Plinky's own delay."); return true; }
    if (y == 13 && !master) { snprintf(a, sizeof a, "%s REVERB SEND %s", who, x & 1 ? "+" : "-"); c12_help_set(h, a, "SEND", "How much of the track goes to Plinky's own reverb."); return true; }
    if (y == 14) { snprintf(a, sizeof a, "%s METER", who); c12_help_set(h, a, "METER", "The track's level as it plays."); return true; }
    return false;
  }
  case 4: { // FX
    if (y == 0 && x < 7) { snprintf(a, sizeof a, "ROUTE %s", c12_track_names[x]); c12_help_set(h, a, "FX CHAIN", "Whether the track goes through the FX chain (drive, ring modulator, compressor)."); return true; }
    if (y == 0 && x == 15) { c12_help_set(h, "LIMITER", "OUTPUT", "The output limiter on or off."); return true; }
    static const char *const names[14] = {"", "DRIVE AMOUNT (WET)", "DRIVE SHAPE: TUBE, FOLD, RAT", "DRIVE GAIN", "DRIVE TONE", "DRIVE LEVEL", "RING MOD AMOUNT (WET)", "RING MOD FREQUENCY", "COMPRESSOR AMOUNT (WET)", "COMPRESSOR THRESHOLD", "COMPRESSOR RATIO 1:1 2:1 3:1 4:1 6:1 8:1", "COMPRESSOR ATTACK", "COMPRESSOR RELEASE", "COMPRESSOR MAKE-UP GAIN"};
    if (y >= 1 && y <= 13) {
      c12_help_set(h, names[y], "LXR FX (DSPAudio/FX)", "The LXR-02 firmware's own effect, unchanged, run by CLATTER 12 on the routed tracks. At wet 0 it is off and costs nothing. Tap to set; - and + at the end of the row.");
      return true;
    }
    if (y == 14 && x < 7) { static const int bits[7] = {16, 12, 10, 8, 6, 5, 4}; snprintf(a, sizeof a, "BITS %d", bits[x]); c12_help_set(h, a, "OUTPUT", "Bit reduction of the whole mix (16: off). CLATTER 12's, not the LXR's."); return true; }
    if (y == 14 && x >= 8) { snprintf(a, sizeof a, "SAMPLE RATE %d", (int)c12glue::rate_levels[x - 8]); c12_help_set(h, a, "LXR", "The LXR's own sample-rate reduction of every voice (VOICE_DECIMATION_ALL): 127 is full rate."); return true; }
    return false;
  }
  }
  return false;
}
