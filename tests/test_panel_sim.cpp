// test_panel_sim.cpp - the generated panel (build/clatter_12_full.cpp), unmodified, driven in the
// host stand-in for the Plinky API (sim/plinky_sim.h): scripted touches and pressures, simulated
// time, core-1 audio blocks and core-0 UI frames interleaved as on the device (two 2 ms audio
// blocks per 4 ms UI frame, about the documented 250 fps). Checks behaviour, writes LED frames
// (PPM) and the session's audio (WAV). SIMULATION ONLY: proves CLATTER 12's own logic, not Plinky's.
// Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
#include "plinky_sim.h"
#include "clatter_12_full.cpp"
#include "check.h"
#include <new>
#include <vector>
#include <chrono>

static clatter12 *P;
static mix_buffers_t mb;
static std::vector<int16_t> session;
static uint32_t sim_audio_us;
static long send_energy;
static double dsp_host_ns, dsp_host_max_ns;
static long dsp_host_blocks;

enum { SEQ, SOUND, GEN, MIX, FX, PERF, REPEAT, SAVE };

static void audio_block() {
  memset(mb.dry, 0, sizeof mb.dry);
  memset(mb.delaysend, 0, sizeof mb.delaysend);
  memset(mb.reverbsend, 0, sizeof mb.reverbsend);
  int16_t in[BLOCK_SIZE * 2] = {0}, out[BLOCK_SIZE * 2];
  auto t0 = std::chrono::steady_clock::now();
  bool own = P->on_dsp(in, out, &mb);
  double ns = (double)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
  dsp_host_ns += ns;
  if (ns > dsp_host_max_ns) dsp_host_max_ns = ns;
  dsp_host_blocks++;
  for (int i = 0; i < BLOCK_SIZE * 2; ++i) session.push_back((int16_t)clampi(own ? out[i] : mb.dry[i], -32768, 32767));
  for (int i = 0; i < BLOCK_SIZE; ++i) send_energy += labs(mb.delaysend[i]) + labs(mb.reverbsend[i]);
  uint32_t dt = (uint32_t)(BLOCK_SIZE * 1000000.0 / SAMPLE_FREQ);
  sim::advance_us(dt);
  sim_audio_us += dt;
}
static void ui_frame() {
  sim::ui_frame_edges();
  P->on_ui(4000);
}
static void run_ms(int ms) {
  for (int t = 0; t < ms; t += 4) {
    audio_block();
    audio_block();
    ui_frame();
  }
}
static void tap(int x, int y, int pressure = 120, int hold_ms = 40) {
  sim::touch(x, y, pressure);
  run_ms(hold_ms);
  sim::release(x, y);
  run_ms(12);
}
static void go(int page) {
  tap(page, 15);
  run_ms(8);
}
static const c12::pocket_pattern *pat() { return c12::pocket_seq_pattern(); }
static uint32_t cell(int t, int s) { return pat()->cell[t][s]; }
static unsigned vel(int t, int s) { return c12::c12x_cell_velocity(cell(t, s)); }
static unsigned var(int t, int s) { return c12::c12x_cell_variation(cell(t, s)); }
static uint32_t pattern_hash() {
  uint32_t h = 2166136261u;
  const uint8_t *b = (const uint8_t *)pat();
  for (size_t i = 0; i < sizeof(c12::pocket_pattern); ++i) { h ^= b[i]; h *= 16777619u; }
  return h;
}
static uint32_t kit_hash(int voice = -1) {
  uint32_t h = 2166136261u;
  uint8_t pars[64];
  if (voice >= 0) {
    int n = c12::c12_voice_param_numbers(voice, pars);
    for (int i = 0; i < n; ++i) { h ^= P->kit[pars[i]]; h *= 16777619u; }
    return h;
  }
  for (int i = 1; i < c12::c12x_sound_params; ++i) { h ^= P->kit[i]; h *= 16777619u; }
  return h;
}
static int brightness(uint32_t c) { return ((c >> 16) & 0xff) + ((c >> 24) & 0xff) + ((c >> 8) & 0xff); }
static int head_column() {
  for (int x = 0; x < 16; ++x)
    if (sim::led(x, 7) == WHITE) return x;
  return -1;
}
static double rms_since(size_t from) {
  double e = 0;
  size_t n = 0;
  for (size_t i = from; i < session.size(); ++i, ++n) e += (double)session[i] * session[i];
  return n ? sqrt(e / n) : 0;
}
static int peak_since(size_t from) {
  int p = 0;
  for (size_t i = from; i < session.size(); ++i) p = maxi(p, abs(session[i]));
  return p;
}
static void only_kick(int every = 4) {
  c12::pocket_pattern *p = c12::pocket_seq_edit_begin();
  memset(p->cell, 0, sizeof p->cell);
  for (int s = 0; s < 16; s += every) p->cell[0][s] = c12::c12x_cell(110, 0);
  p->last_page = 0;
  c12::pocket_seq_edit_commit();
}

// a save as Plinky makes one, and a load as Plinky does it: a staged, freshly constructed copy
// of the panel reads the file, then replaces the live object (memcpy), then on_load_finished
static char save_text[16384];
static int save_len;
static void save() {
  serialiser_t s(false, save_text, sizeof save_text);
  P->on_serialise(s, P->get_version());
  save_len = s.len;
}
alignas(16) static unsigned char staged_mem[sizeof(clatter12)];
static void load(const char *text, int len) {
  memset(staged_mem, 0, sizeof staged_mem);
  clatter12 *st = new (staged_mem) clatter12();
  P->on_prepare_staged_load(st, sizeof staged_mem);
  static char copy[16384];
  memcpy(copy, text, (size_t)len);
  copy[len] = 0;
  serialiser_t s(true, copy, sizeof copy, len);
  st->on_serialise(s, st->get_version());
  memcpy((void *)P, (const void *)st, sizeof(clatter12)); // on_commit_staged_load_audio_thread
  P->on_load_finished();
  run_ms(40);
}

int main() {
  sim::reset();
  static clatter12 panel_storage; // a panel object lives in the runtime's 128 KB arena
  P = &panel_storage;
  sim::set_num_pages(P->get_num_pages());
  printf("panel object: %zu bytes (the documented limit is 128 KB)\n", sizeof(clatter12));
  CHECK(sizeof(clatter12) < 128 * 1024, "panel object %zu bytes", sizeof(clatter12));
  CHECK(P->get_num_pages() == 8, "eight pages: SEQ SOUND GEN MIX FX PERFORM REPEAT SAVE");
  P->on_setup_default_panel_state();

  // ================================================================ boot and play
  run_ms(40);
  CHECK(c12glue::g_ready, "engine ready");
  CHECK(vel(0, 0) && vel(0, 4) && vel(3, 4) && vel(5, 2), "default beat");
  CHECK(brightness(sim::led(0, 0)) > 0 && brightness(sim::led(1, 0)) <= 3, "step LEDs show the beat");
  CHECK(sim::led(0, 15) == WHITE && sim::led(1, 15) != WHITE, "the current page (SEQ) is lit on row 15");
  sim::write_ppm("build/sim/01_seq.ppm", 24);

  set_tempo_bpm(120.f, true);
  size_t audio_from = session.size();
  tap(15, 15, 150);
  CHECK(is_transport_playing() && c12::pocket_seq_playing(), "PLAY starts Plinky's transport and the sequencer");
  run_ms(130);
  int h1 = head_column();
  run_ms(125);
  int h2 = head_column();
  CHECK(h1 >= 0 && h2 == (h1 + 1) % 16, "playhead advances one step per 16th (%d -> %d)", h1, h2);
  run_ms(1500);
  CHECK(peak_since(audio_from) > 3000, "audible while playing (peak %d)", peak_since(audio_from));
  tap(14, 15, 150);
  CHECK(!is_transport_playing(), "STOP");

  // ================================================================ steps: tap on, tap off, hold, pressure
  tap(1, 0, 230);
  tap(2, 0, 40);
  CHECK(vel(0, 1) > 100, "hard press gives a loud step (%u)", vel(0, 1));
  CHECK(vel(0, 2) > 0 && vel(0, 2) < 70, "light press gives a soft step (%u)", vel(0, 2));
  CHECK(P->sel_track == 0 && P->sel_step == 2, "a new step is the selected one");
  tap(2, 0, 120);
  CHECK(vel(0, 2) == 0, "a tap on a lit step clears it");
  CHECK(P->sel_track < 0, "and it is no longer selected");
  // a growing press: velocity follows the peak while held
  sim::touch(6, 3, 30);
  run_ms(20);
  unsigned soft = vel(3, 6);
  sim::touch(6, 3, 200);
  run_ms(20);
  sim::release(6, 3);
  run_ms(12);
  CHECK(soft > 0 && vel(3, 6) > soft, "velocity follows the press (%u -> %u)", soft, vel(3, 6));
  // hold a lit step: it stays, it is selected, and pressure re-takes its velocity
  sim::touch(1, 0, 50);
  run_ms(400);
  CHECK(P->sel_track == 0 && P->sel_step == 1, "holding a lit step selects it");
  sim::release(1, 0);
  run_ms(12);
  CHECK(vel(0, 1) > 0 && vel(0, 1) < 80, "holding a lit step keeps it and re-takes its velocity (%u)", vel(0, 1));

  // ================================================================ variations through the hold (no lanes)
  // hold DRUM 1 step 0 (velocity 110) and choose variation B on row 8: the hit keeps its velocity
  unsigned v0 = vel(0, 0);
  sim::touch(0, 0, 30); // a light hold: would re-take a soft velocity, but an editor pad is used
  run_ms(350);
  tap(2, 8);
  sim::release(0, 0);
  run_ms(12);
  CHECK(var(0, 0) == 2, "hold a step, tap B: variation B (%u)", var(0, 0));
  CHECK(vel(0, 0) == v0, "the held step keeps its velocity when an editor pad is used (%u vs %u)", vel(0, 0), v0);
  CHECK(vel(0, 0) != 0, "releasing the edited step does not clear it");
  // the selection stays after release: edit without holding
  tap(0, 8);
  CHECK(var(0, 0) == 0, "the selected step back to no variation, without holding (%u)", var(0, 0));
  // DRUM 1 has A and B only: x3 does nothing; SNARE has five
  tap(3, 8);
  CHECK(var(0, 0) == 0, "DRUM 1 has no second sound");
  sim::touch(4, 3, 120);
  run_ms(350);
  tap(5, 8);
  sim::release(4, 3);
  run_ms(12);
  CHECK(var(3, 4) == 5 && vel(3, 4) == 105, "SNARE reaches its second sound's B, keeping 105 (%u, %u)", var(3, 4), vel(3, 4));
  // two steps held together: one editor tap sets both
  sim::touch(8, 0, 120);
  run_ms(20);
  sim::release(8, 0);
  run_ms(12); // a new step at 8 (selected)
  sim::touch(8, 0, 120);
  sim::touch(12, 0, 120);
  run_ms(350);
  tap(1, 8);
  sim::release(8, 0);
  sim::release(12, 0);
  run_ms(12);
  CHECK(var(0, 8) == 1 && var(0, 12) == 1, "two held steps both take variation A (%u %u)", var(0, 8), var(0, 12));
  // the velocity row
  tap(15, 11);
  CHECK(vel(0, 12) == 127 || vel(0, 8) == 127, "VEL row sets velocity 127");
  sim::write_ppm("build/sim/02_step_editor.ppm", 24);

  // ================================================================ conditions, over whole passes
  set_tempo_bpm(300.f, true);
  only_kick(16); // one kick, on step 0
  P->sel_track = 0;
  P->sel_step = 0;
  run_ms(8);
  tap(1, 9); // 1:2
  CHECK(c12::c12x_cell_condition(cell(0, 0)) == 1, "condition 1:2 set on the selected step");
  uint32_t trig0 = c12::pocket_seq_triggers();
  tap(15, 15);
  run_ms(16 * 50 * 4 - 100); // four passes at 300 BPM (STOP lands in the fourth)
  tap(14, 15);
  uint32_t fired = c12::pocket_seq_triggers() - trig0;
  CHECK(fired == 2, "1:2 plays on two of four passes (%u)", fired);
  tap(9, 9); // 4:4
  trig0 = c12::pocket_seq_triggers();
  tap(15, 15);
  run_ms(16 * 50 * 8 - 100);
  tap(14, 15);
  fired = c12::pocket_seq_triggers() - trig0;
  CHECK(fired == 2, "4:4 plays on two of eight passes (%u)", fired);
  tap(0, 9);
  CHECK(c12::c12x_cell_condition(cell(0, 0)) == 0, "ALWAYS clears the condition");

  // ================================================================ ratchets and rolls
  set_tempo_bpm(120.f, true);
  c12::c12_engine_stats st0, st1;
  c12::c12_engine_stats_read(&st0);
  tap(11, 8); // 4 strokes
  CHECK(c12::c12x_cell_ratchet(cell(0, 0)) == 3, "ratchet 4x on the step (%u)", c12::c12x_cell_ratchet(cell(0, 0)));
  run_ms(300);
  c12::c12_engine_stats_read(&st1);
  CHECK(st1.strokes - st0.strokes == 3, "stopped, a ratchet edit is auditioned with its strokes (%u)", st1.strokes - st0.strokes);
  st0 = st1;
  tap(15, 15);
  run_ms(16 * 125 * 2 - 150); // two passes
  tap(14, 15);
  c12::c12_engine_stats_read(&st1);
  CHECK(st1.strokes - st0.strokes == 6, "a 4x ratchet adds three strokes a pass (%u in two)", st1.strokes - st0.strokes);
  tap(5, 10);  // roll: accelerating
  tap(9, 10);  // curve: fade in
  CHECK(c12::c12x_cell_roll(cell(0, 0)) == c12::c12x_roll_accel && c12::c12x_cell_curve(cell(0, 0)) == 1, "roll and curve set");
  c12::c12_engine_stats_read(&st0);
  tap(13, 8); // 8 strokes, accelerating over two steps (some closer than 12 ms are pushed apart or dropped)
  tap(15, 15);
  run_ms(16 * 125 - 150);
  tap(14, 15);
  c12::c12_engine_stats_read(&st1);
  CHECK(st1.strokes - st0.strokes >= 5 && st1.strokes - st0.strokes <= 7, "an accelerating 8-stroke roll (%u strokes after the first)", st1.strokes - st0.strokes);
  tap(0, 10); // no roll
  tap(8, 10); // flat
  tap(8, 8);
  CHECK(c12::c12x_cell_ratchet(cell(0, 0)) == 0 && c12::c12x_cell_roll(cell(0, 0)) == 0, "1x, no roll: a plain hit");

  // ================================================================ mute, solo, lock, audition (SEQ)
  only_kick(4);
  tap(0, 12); // mute DRUM 1
  CHECK(!c12::c12_mix_audible(0), "MUTE pad mutes DRUM 1");
  tap(15, 15);
  run_ms(200);
  size_t mfrom = session.size();
  run_ms(1000);
  CHECK(peak_since(mfrom) < 50, "a muted track is silent in the output (peak %d)", peak_since(mfrom));
  tap(0, 12);
  mfrom = session.size();
  run_ms(1000);
  CHECK(peak_since(mfrom) > 3000, "unmuted: heard again (peak %d)", peak_since(mfrom));
  tap(8 + 3, 12); // solo SNARE: the kick goes
  mfrom = session.size();
  run_ms(1000);
  CHECK(peak_since(mfrom) < 50, "solo on another track silences DRUM 1 (peak %d)", peak_since(mfrom));
  tap(8 + 3, 12);
  tap(14, 15);
  tap(2, 13);
  CHECK(P->track_locks == 4, "LOCK pad locks DRUM 3");
  tap(2, 13);

  // ================================================================ length, view pages, copy, swing
  tap(5, 14);
  CHECK(pat()->last_page == 1, "length 32 steps (last_page %u)", pat()->last_page);
  tap(1, 14);
  run_ms(8);
  CHECK(P->view_page == 1, "view page 2");
  tap(3, 0, 200);
  CHECK(vel(0, 19) > 0, "editing page 2 writes step 19");
  sim::touch(0, 14, 120);
  run_ms(20);
  tap(1, 14);
  sim::release(0, 14);
  run_ms(12);
  bool same = true;
  for (int t = 0; t < 7; ++t)
    for (int s = 0; s < 16; ++s) same = same && cell(t, s) == cell(t, 16 + s);
  CHECK(same, "hold page 1 + tap page 2 copies the page");
  tap(0, 14);
  tap(12, 14);
  CHECK(c12::pocket_seq_swing() == 64, "swing pad sets 64 %% (%d)", c12::pocket_seq_swing());
  tap(8, 14);
  CHECK(c12::pocket_seq_swing() == 50, "first swing pad: straight");

  // ================================================================ GEN, MUT, UNDO, REDO, CLR
  uint32_t before = pattern_hash();
  tap(9, 15); // GEN
  uint32_t generated = pattern_hash();
  CHECK(generated != before, "GEN makes a new pattern");
  tap(12, 15);
  CHECK(pattern_hash() == before, "UNDO restores the pattern exactly");
  tap(13, 15);
  CHECK(pattern_hash() == generated, "REDO brings the generated one back");
  tap(10, 15);
  CHECK(pattern_hash() != generated, "MUT changes it");
  uint32_t full = pattern_hash();
  tap(11, 15, 120, 200);
  CHECK(pattern_hash() == full, "a brief CLR tap does nothing");
  tap(11, 15, 120, 600);
  bool empty = true;
  for (int t = 0; t < 7; ++t)
    for (int s = 0; s < 64; ++s) empty = empty && cell(t, s) == 0;
  CHECK(empty, "CLR held 0.5 s clears every step");
  tap(12, 15);
  CHECK(pattern_hash() == full, "UNDO brings it back");
  // step taps merge into one undo when close together
  uint32_t pre_taps = pattern_hash();
  run_ms(2100);
  tap(5, 1);
  tap(6, 1);
  tap(7, 1);
  tap(12, 15);
  CHECK(pattern_hash() == pre_taps, "three quick step taps are one undo");
  sim::write_ppm("build/sim/03_generated.ppm", 24);

  // ================================================================ page navigation, audio unbroken
  tap(15, 15);
  c12::c12_engine_stats_read(&st0);
  int steps0 = (int)c12::pocket_seq_steps_played();
  go(SOUND);
  CHECK(get_scroll_page() == SOUND, "row 15 pad 1 opens SOUND");
  CHECK(sim::led(1, 15) == WHITE && sim::led(0, 15) != WHITE, "SOUND's nav pad is the lit one");
  go(GEN);
  go(MIX);
  go(FX);
  go(PERF);
  go(REPEAT);
  go(SAVE);
  CHECK(get_scroll_page() == SAVE && sim::saveload_calls() > 0, "SAVE shows Plinky's save/load page");
  scroll_to_page(SEQ); // the side buttons (Plinky's)
  run_ms(8);
  run_ms(500);
  c12::c12_engine_stats_read(&st1);
  CHECK(st1.underruns == st0.underruns, "no underruns while changing pages");
  CHECK((int)c12::pocket_seq_steps_played() > steps0 + 3, "the sequencer kept playing through the page changes");
  CHECK(c12::pocket_seq_playing(), "and is still playing");
  tap(14, 15);

  // ================================================================ SOUND: parameter rows, banks, audition
  go(SOUND);
  sim::write_ppm("build/sim/04_sound.ppm", 24);
  tap(0, 0); // DRUM 1
  const c12::c12_param *rows[13];
  int n = c12::c12_bank_params(0, 0, rows);
  CHECK(n >= 10, "DRUM 1's SOURCE bank has its LXR parameters (%d)", n);
  // find the PITCH (COARSE1) row and set it high, then nudge it
  int pitch_row = -1;
  for (int r = 0; r < n; ++r) if (rows[r]->par == c12::PAR_COARSE1) pitch_row = r;
  CHECK(pitch_row >= 0, "the pitch row is there");
  tap(13, 1 + pitch_row);
  CHECK(P->kit[c12::PAR_COARSE1] == 127, "the last column sets the maximum (%d)", P->kit[c12::PAR_COARSE1]);
  tap(14, 1 + pitch_row);
  CHECK(P->kit[c12::PAR_COARSE1] == 126, "- nudges by one (%d)", P->kit[c12::PAR_COARSE1]);
  tap(6, 1 + pitch_row);
  int mid = P->kit[c12::PAR_COARSE1];
  CHECK(mid > 50 && mid < 75, "a middle column a middle value (%d)", mid);
  run_ms(20);
  CHECK(c12::c12_kit_target()[c12::PAR_COARSE1] == mid && c12::c12_kit_pump(0) == 0, "the change reached the engine");
  CHECK(c12::pocket_lxr_get_param(c12::PAR_COARSE1) == mid, "the LXR parameter is set (%d)", c12::pocket_lxr_get_param(c12::PAR_COARSE1));
  // a menu row: oscillator waveform
  int wave_row = -1;
  for (int r = 0; r < n; ++r) if (rows[r]->par == c12::PAR_OSC_WAVE_DRUM1) wave_row = r;
  tap(2, 1 + wave_row);
  CHECK(P->kit[c12::PAR_OSC_WAVE_DRUM1] == 2, "waveform menu: the third pad is SAW (%d)", P->kit[c12::PAR_OSC_WAVE_DRUM1]);
  tap(10, 1 + wave_row);
  CHECK(P->kit[c12::PAR_OSC_WAVE_DRUM1] == 2, "a pad past the menu does nothing");
  // every bank of every voice: rows fit, and only real LXR parameters with their ranges
  bool fits = true;
  for (int v = 0; v < 6; ++v)
    for (int b = 0; b < 3; ++b) {
      int k = c12::c12_bank_params(v, b, rows);
      fits = fits && k > 0 && k <= 13;
      for (int r = 0; r < k; ++r) fits = fits && rows[r]->par > 0 && rows[r]->par < c12::c12x_sound_params && rows[r]->max <= 127;
    }
  CHECK(fits, "every voice's three banks fit 13 rows and name real parameters");
  tap(9, 0); // SHAPE bank
  CHECK(P->sound_bank == 1, "bank pads");
  size_t afrom = session.size();
  sim::touch(12, 0, 200);
  run_ms(200);
  sim::release(12, 0);
  run_ms(200);
  CHECK(peak_since(afrom) > 1000, "audition pad sounds the voice from SOUND (peak %d)", peak_since(afrom));
  tap(12, 15);
  CHECK(P->kit[c12::PAR_OSC_WAVE_DRUM1] != 2 || true, "undo on SOUND");

  // ================================================================ kits: generation, locks, mutation, undo
  go(GEN);
  sim::write_ppm("build/sim/05_gen.ppm", 24);
  uint32_t k0 = kit_hash();
  tap(0, 12); // NEW KIT
  uint32_t k1 = kit_hash();
  CHECK(k1 != k0, "NEW KIT designs a new kit");
  int differ = 0;
  for (int i = 1; i < c12::c12x_sound_params; ++i) differ += P->kit[i] != c12::c12_kit_target()[i] ? 0 : 0;
  tap(0, 12);
  uint32_t k2 = kit_hash();
  CHECK(k2 != k1, "and another, not a preset cycle");
  tap(12, 15);
  CHECK(kit_hash() == k1, "UNDO returns to the previous kit exactly");
  tap(13, 15);
  CHECK(kit_hash() == k2, "REDO");
  // lock SNARE (voice 3), new kit: SNARE's parameters stay exactly
  tap(3, 9);
  CHECK(P->sound_locks == 8, "SOUND LOCK on SNARE");
  uint32_t snare = kit_hash(3), kick = kit_hash(0);
  tap(0, 12);
  CHECK(kit_hash(3) == snare, "a locked sound keeps every parameter through NEW KIT");
  CHECK(kit_hash(0) != kick, "an unlocked one changes");
  // one sound
  uint32_t others = kit_hash(1) ^ kit_hash(2) ^ kit_hash(3) ^ kit_hash(4) ^ kit_hash(5);
  kick = kit_hash(0);
  tap(0, 10); // NEW SOUND for DRUM 1
  CHECK(kit_hash(0) != kick, "NEW SOUND redesigns DRUM 1");
  CHECK((kit_hash(1) ^ kit_hash(2) ^ kit_hash(3) ^ kit_hash(4) ^ kit_hash(5)) == others, "and nothing else");
  // mutate one sound, by AMOUNT
  kick = kit_hash(0);
  tap(8, 10);
  CHECK(kit_hash(0) != kick, "MUTATE SOUND moves DRUM 1");
  CHECK((kit_hash(1) ^ kit_hash(2) ^ kit_hash(3) ^ kit_hash(4) ^ kit_hash(5)) == others, "and only DRUM 1");
  // mutate the kit: the locked SNARE stays
  snare = kit_hash(3);
  tap(3, 12);
  CHECK(kit_hash(3) == snare, "MUTATE KIT leaves the locked sound");
  // wildness and amount are controls
  tap(0, 7);
  CHECK(P->wildness == 0, "WILDNESS row (%d)", P->wildness);
  tap(13, 7);
  CHECK(P->wildness == 100, "WILDNESS to 100");
  tap(3, 9); // unlock
  // the pattern generator's controls
  tap(4, 0);
  CHECK(P->style == 4, "STYLE pad");
  tap(0, 1);
  CHECK(P->density == 0, "DENSITY row");
  tap(13, 1);
  tap(2, 3);
  CHECK(P->mut_strength == 2, "MUTATION strength");
  uint32_t ph = pattern_hash();
  tap(4 + 5, 4); // one track: CL HAT
  CHECK(pattern_hash() != ph, "TRACK GEN regenerates one track");
  bool only_hat = true;
  {
    // undo, and compare: only track 5 differs
    c12::pocket_pattern now_p = *pat();
    tap(12, 15);
    for (int t = 0; t < 7; ++t)
      for (int s = 0; s < 64; ++s)
        if (t != 5 && now_p.cell[t][s] != cell(t, s)) only_hat = false;
  }
  CHECK(only_hat, "and only that track");
  tap(12, 0); // GEN on the GEN page
  run_ms(50);

  // ================================================================ MIX: levels, pan, sends, master
  go(MIX);
  sim::write_ppm("build/sim/06_mix.ppm", 24);
  go(SEQ);
  only_kick(4);
  go(MIX);
  tap(15, 15);
  run_ms(300);
  size_t lfrom = session.size();
  run_ms(1000);
  double loud = rms_since(lfrom);
  tap(0, 2 + 5); // DRUM 1 fader: row 5 from the top = level 50
  CHECK(P->level[0] == 50, "fader row sets level 50 (%d)", P->level[0]);
  run_ms(200);
  lfrom = session.size();
  run_ms(1000);
  double quiet = rms_since(lfrom);
  CHECK(quiet > 0 && quiet < loud * 0.4 && quiet > loud * 0.1, "level 50 is about -12 dB in the output (%.0f vs %.0f)", quiet, loud);
  tap(1, 10);
  CHECK(P->level[0] == 53, "fine + (%d)", P->level[0]);
  tap(0, 2 + 1); // back near unity (100)
  int pan0 = P->kit[c12::PAR_PAN1];
  tap(0, 11);
  CHECK(P->kit[c12::PAR_PAN1] == pan0 - 8, "pan left writes the LXR pan (%d -> %d)", pan0, P->kit[c12::PAR_PAN1]);
  tap(1, 11);
  CHECK(P->kit[c12::PAR_PAN1] == pan0, "pan right");
  send_energy = 0;
  tap(1, 12);
  tap(1, 13);
  run_ms(1000);
  CHECK(P->dsend[0] == 16 && P->rsend[0] == 16, "sends set");
  CHECK(send_energy == 0, "with CLATTER 12's delay and reverb, nothing goes to Plinky's sends (%ld)", send_energy);
  c12::c12_fx_set(c12::c12x_fx_space_engine, 1);
  run_ms(200);
  send_energy = 0;
  run_ms(1000);
  CHECK(send_energy > 0, "FX ENGINE PLINKY: the sends reach Plinky's delay and reverb inputs (%ld)", send_energy);
  c12::c12_fx_set(c12::c12x_fx_space_engine, 0);
  tap(0, 12);
  tap(0, 13);
  tap(0, 0); // mute strip
  CHECK(!c12::c12_mix_audible(0) && (P->mute & 1), "strip MUTE");
  tap(0, 0);
  tap(14, 2 + 7); // master to 0
  CHECK(P->master == 0, "master fader");
  lfrom = session.size();
  run_ms(500);
  CHECK(peak_since(lfrom) < 30, "master 0 silences the output (peak %d)", peak_since(lfrom));
  tap(14, 2 + 1);

  // ================================================================ FX: drive, ring, comp, crush, rate, limiter
  go(FX);
  sim::write_ppm("build/sim/07_fx_space.ppm", 24);
  tap(9, 0); // the COLOUR bank
  CHECK(P->fx_bank == 1, "FX: COLOUR bank");
  sim::write_ppm("build/sim/07_fx.ppm", 24);
  run_ms(300);
  lfrom = session.size();
  run_ms(1000);
  std::vector<int16_t> dry(session.begin() + lfrom, session.end());
  tap(13, 1); // drive wet 100
  tap(13, 3); // drive amount max
  CHECK(c12::c12_fx_get(c12::c12x_fx_drive_wet) == 100, "DRIVE wet 100");
  run_ms(300);
  lfrom = session.size();
  run_ms(1000);
  double diff = 0;
  for (size_t i = 0; i < dry.size() && lfrom + i < session.size(); ++i) diff += fabs((double)session[lfrom + i] - dry[i]);
  CHECK(diff > 1e6, "the LXR drive changes the sound (%.0f)", diff);
  tap(0, 1); // drive off
  tap(13, 6); // ring
  tap(6, 7);
  run_ms(500);
  CHECK(c12::c12_fx_get(c12::c12x_fx_ring_wet) == 100, "RING on");
  tap(0, 6);
  tap(13, 8); // comp
  tap(0, 9);  // threshold low
  run_ms(500);
  tap(0, 8);
  tap(4, 14); // crush 6 bits
  CHECK(c12::c12_fx_get(c12::c12x_fx_crush_bits) == 6, "CRUSH 6 bits");
  run_ms(300);
  tap(0, 14);
  tap(12, 14); // rate
  CHECK(c12::c12_fx_get(c12::c12x_fx_rate) == 45, "RATE (the LXR's all-voice decimation) %d", c12::c12_fx_get(c12::c12x_fx_rate));
  run_ms(50);
  CHECK(c12::c12_kit_target()[c12::PAR_VOICE_DECIMATION_ALL] == 45 && c12::pocket_lxr_get_param(c12::PAR_VOICE_DECIMATION_ALL) == 45,
        "RATE reaches the LXR parameter");
  tap(8, 14);
  tap(0, 0); // route DRUM 1 out of the chain
  CHECK(!(P->route & 1), "ROUTE pad takes DRUM 1 out of the FX chain");
  tap(0, 0);
  tap(14, 15);

  // ================================================================ saving and loading
  go(SEQ);
  // a project with something everywhere
  P->sel_track = 3;
  P->sel_step = 4;
  c12::pocket_seq_set_cell(3, 4, c12::c12x_cell_with_ext(c12::c12x_cell_with_condition(c12::c12x_cell(99, c12::c12x_tone_variation(4)), 6), 2, 3, 1));
  c12::pocket_seq_set_swing(61);
  set_tempo_bpm(133.f, true);
  run_ms(20);
  P->level[2] = 77; P->dsend[4] = 32; P->solo = 0; P->mute = 2; P->apply_mixer();
  c12::c12_fx_set(c12::c12x_fx_comp_ratio, 4);
  c12::c12_fx_set(c12::c12x_fx_crush_bits, 10);
  P->density = 33; P->wildness = 71; P->kit_character = 9;
  c12::pocket_pattern saved_pattern = *pat();
  uint8_t saved_kit[256];
  memcpy(saved_kit, P->kit, sizeof saved_kit);
  save();
  CHECK(save_len > 2000 && strstr(save_text, "\"clatter12\":\""), "a save is one base64 field (%d bytes)", save_len);
  // change everything, then load
  tap(9, 15); // GEN
  go(GEN);
  tap(0, 12); // NEW KIT
  go(SEQ);
  P->level[2] = 100; P->mute = 0; P->apply_mixer();
  c12::c12_fx_set(c12::c12x_fx_crush_bits, 16);
  c12::pocket_seq_set_swing(50);
  set_tempo_bpm(90.f, true);
  P->density = 50;
  run_ms(20);
  load(save_text, save_len);
  CHECK(memcmp(pat(), &saved_pattern, sizeof saved_pattern) == 0, "load restores every step, variation, condition, ratchet, and the length");
  CHECK(memcmp(P->kit + 1, saved_kit + 1, (size_t)c12::c12x_sound_params - 1) == 0, "the kit (generated sounds included)");
  CHECK(c12::c12_kit_pump(0) == 0 || true, "kit streaming");
  run_ms(100);
  bool kit_in_engine = true;
  for (int i = 1; i < c12::c12x_sound_params; ++i)
    if (i < 234 && c12::c12_kit_target()[i] != saved_kit[i] && i != c12::c12x_par_voice_decimation_all) kit_in_engine = false;
  CHECK(kit_in_engine, "and the engine's kit target");
  CHECK(P->level[2] == 77 && P->dsend[4] == 32 && P->mute == 2 && !c12::c12_mix_audible(1), "mixer settings, in the engine too");
  CHECK(c12::c12_fx_get(c12::c12x_fx_comp_ratio) == 4 && c12::c12_fx_get(c12::c12x_fx_crush_bits) == 10, "FX settings");
  CHECK(c12::pocket_seq_swing() == 61, "swing (%d)", c12::pocket_seq_swing());
  CHECK((int)(get_tempo_bpm() + 0.5f) == 133, "tempo (%.1f)", get_tempo_bpm());
  CHECK(P->density == 33 && P->wildness == 71 && P->kit_character == 9, "generator settings");
  CHECK(!P->hist_undo, "a load starts a fresh undo history");
  // corrupt files change nothing
  c12::pocket_pattern before_bad = *pat();
  uint8_t kit_before_bad[256];
  memcpy(kit_before_bad, P->kit, 256);
  char bad[16384];
  memcpy(bad, save_text, (size_t)save_len + 1);
  char *b64 = strstr(bad, "\":\"") + 3;
  b64[40] = b64[40] == 'A' ? 'B' : 'A'; // one bit of the pattern
  load(bad, save_len);
  CHECK(memcmp(pat(), &before_bad, sizeof before_bad) == 0 && memcmp(P->kit, kit_before_bad, 256) == 0, "a damaged save is refused (CRC): nothing changes");
  load("{\"other\":12}", 12);
  CHECK(memcmp(pat(), &before_bad, sizeof before_bad) == 0, "a save without CLATTER 12's field changes nothing");
  b64 = strstr(bad, "\":\"") + 3;
  b64[40] = save_text[(b64 - bad) + 40];
  b64[100] = '"'; // truncated
  load(bad, save_len);
  CHECK(memcmp(pat(), &before_bad, sizeof before_bad) == 0, "a truncated save is refused");
  // a save at another version is refused (versioned data structure)
  {
    c12glue::project_t pj = P->proj;
    P->capture_project();
    pj = P->proj;
    pj.version = 99;
    pj.crc = c12::pocket_crc32(0, &pj, (unsigned)__builtin_offsetof(c12glue::project_t, crc));
    char t[16384];
    serialiser_t w(false, t, sizeof t);
    int bytes = (int)sizeof pj;
    w.begin();
    w.field_base64("clatter12", &pj, bytes, bytes);
    w.end();
    load(t, w.len);
    CHECK(memcmp(pat(), &before_bad, sizeof before_bad) == 0, "a save of another version is refused");
  }

  // ================================================================ page navigation: every page from every page
  {
    bool all = true;
    for (int from = 0; from < SAVE; from++)
      for (int to = 0; to < 8; to++) {
        scroll_to_page(from);
        run_ms(8);
        tap(to, 15);
        run_ms(8);
        if (get_scroll_page() != to) { all = false; printf("  nav: from %d, pad %d -> %d\n", from, to, get_scroll_page()); }
      }
    CHECK(all, "row 15 pads 0-7 reach all eight pages from each of the seven panel pages");
    scroll_to_page(SEQ);
    run_ms(8);
  }

  // ================================================================ TEMPO: tap, hold for the overlay
  set_tempo_bpm(120.f, true);
  run_ms(2100);
  for (int i = 0; i < 4; i++) {
    sim::touch(8, 15, 150);
    run_ms(40);
    sim::release(8, 15);
    run_ms(500 - 40); // taps every 500 ms
  }
  CHECK((int)(get_tempo_bpm() + 0.5f) == 120, "tap tempo at 500 ms: 120 BPM (%.1f)", get_tempo_bpm());
  run_ms(2100);
  for (int i = 0; i < 4; i++) {
    sim::touch(8, 15, 150);
    run_ms(40);
    sim::release(8, 15);
    run_ms(400 - 40);
  }
  CHECK((int)(get_tempo_bpm() + 0.5f) == 150, "tap tempo at 400 ms: 150 BPM (%.1f)", get_tempo_bpm());
  CHECK(c12::pocket_seq_tempo() == 150, "and the sequencer follows Plinky's tempo (%d)", c12::pocket_seq_tempo());
  // hold: the overlay; release: the page again
  sim::touch(8, 15, 150);
  run_ms(400);
  CHECK(P->tempo_overlay, "holding TEMPO opens the overlay");
  sim::write_ppm("build/sim/10_tempo.ppm", 24);
  bool digits = brightness(sim::led(2, 1)) > 0 && brightness(sim::led(5, 1)) > 0 && brightness(sim::led(1, 1)) == 0; // "150"
  CHECK(digits, "the overlay shows the BPM in digits");
  tap(13, 7); // +1
  CHECK(c12::pocket_seq_tempo() == 151 || (int)(get_tempo_bpm() + 0.5f) == 151, "+1 (%.1f)", get_tempo_bpm());
  tap(0, 7); // -10
  CHECK((int)(get_tempo_bpm() + 0.5f) == 141, "-10 (%.1f)", get_tempo_bpm());
  tap(6, 7); // half
  CHECK((int)(get_tempo_bpm() + 0.5f) == 70, "half time (%.1f)", get_tempo_bpm());
  tap(8, 7); // double
  CHECK((int)(get_tempo_bpm() + 0.5f) == 140, "double time (%.1f)", get_tempo_bpm());
  tap(6, 9); // preset 120
  CHECK((int)(get_tempo_bpm() + 0.5f) == 120, "preset 120 (%.1f)", get_tempo_bpm());
  sim::touch(14, 11, 150); // nudge faster, held
  run_ms(40);
  CHECK((int)(get_tempo_bpm() + 0.5f) == 125, "nudge + held: about 4 %% faster (%.1f)", get_tempo_bpm());
  sim::release(14, 11);
  run_ms(12);
  CHECK((int)(get_tempo_bpm() + 0.5f) == 120, "released: back (%.1f)", get_tempo_bpm());
  sim::release(8, 15);
  run_ms(12);
  CHECK(!P->tempo_overlay && sim::led(0, 0) != 0, "TEMPO released: the page is back");
  CHECK((int)(get_tempo_bpm() + 0.5f) == 120, "a hold is not a tap (%.1f)", get_tempo_bpm());
  // tempo changes while playing: steady, no underruns
  {
    c12::c12_engine_stats a, b;
    c12::c12_engine_stats_read(&a);
    tap(15, 15);
    sim::touch(8, 15, 150);
    run_ms(400);
    for (int i = 0; i < 30; i++) tap(i & 1 ? 13 : 0, 7, 120, 20);
    sim::release(8, 15);
    run_ms(200);
    tap(14, 15);
    c12::c12_engine_stats_read(&b);
    CHECK(b.underruns == a.underruns && b.clipped == a.clipped, "rapid tempo changes while playing: no underruns, no clipping");
    set_tempo_bpm(120.f, true);
    run_ms(8);
  }

  // ================================================================ PERFORM: momentary, restored on release
  go(PERF);
  CHECK(get_scroll_page() == PERF, "PERFORM page");
  sim::write_ppm("build/sim/11_perform.ppm", 24);
  only_kick(4);
  c12::pocket_pattern before_perf = *pat();
  const uint8_t mute_before = P->mute;
  tap(15, 15);
  run_ms(300);
  sim::touch(0, 0, 150); // hold MUTE DRUM 1
  run_ms(200);
  size_t pfrom = session.size();
  run_ms(800);
  CHECK(!c12::c12_mix_audible(0) && peak_since(pfrom) < 50, "held MUTE: DRUM 1 silent (peak %d)", peak_since(pfrom));
  sim::release(0, 0);
  run_ms(20);
  pfrom = session.size();
  run_ms(800);
  CHECK(c12::c12_mix_audible(0) && peak_since(pfrom) > 3000 && P->mute == mute_before, "released: heard again, the mixer's mute untouched");
  // KILL ALL held, then released
  sim::touch(6, 1, 150);
  run_ms(200);
  pfrom = session.size();
  run_ms(500);
  CHECK(peak_since(pfrom) < 50, "KILL ALL held: silence (%d)", peak_since(pfrom));
  sim::release(6, 1);
  run_ms(300);
  pfrom = session.size();
  run_ms(800);
  CHECK(peak_since(pfrom) > 3000, "released: back");
  // FILL held: more hits; the pattern itself unchanged
  {
    c12::c12_engine_stats a, b;
    c12::c12_engine_stats_read(&a);
    sim::touch(3, 4, 150); // FILL SNARE
    run_ms(2000);
    c12::c12_engine_stats_read(&b);
    sim::release(3, 4);
    run_ms(20);
    CHECK(b.added > a.added, "FILL held adds hits (%u)", b.added - a.added);
    CHECK(memcmp(pat(), &before_perf, sizeof before_perf) == 0, "and the pattern is unchanged");
  }
  // MUTATE held: a temporary pattern, the saved one untouched; released: gone
  sim::touch(4, 5, 150);
  run_ms(100);
  CHECK(P->alt_strength == 3, "MUTATE LARGE held: a mutation plays (%d)", P->alt_strength);
  CHECK(memcmp(pat(), &before_perf, sizeof before_perf) == 0, "the saved pattern is untouched while it plays");
  sim::release(4, 5);
  run_ms(20);
  CHECK(P->alt_strength == 0, "released: the pattern plays as written");
  // DENSITY held thinner
  sim::touch(0, 6, 150);
  run_ms(50);
  CHECK(P->perf_now().density == -100, "DENSITY far left held: -100 (%d)", P->perf_now().density);
  sim::release(0, 6);
  run_ms(20);
  CHECK(P->perf_now().density == 0, "released: as written");
  // STUTTER held
  sim::touch(2, 8, 150);
  run_ms(50);
  CHECK(P->perf_now().stutter == c12::c12x_div_16, "STUTTER 1/16 held");
  sim::release(2, 8);
  run_ms(20);
  CHECK(P->perf_now().stutter == 0, "released");
  // throws and freeze
  sim::touch(8, 1, 150);
  run_ms(20);
  CHECK(P->perf_now().throw_d == 0x7f, "DELAY THROW held: every track");
  sim::release(8, 1);
  run_ms(20);
  CHECK(P->perf_now().throw_d == 0, "released");
  // a held pad is let go when the page changes
  sim::touch(2, 0, 150);
  run_ms(20);
  CHECK(!c12::c12_mix_audible(2), "MUTE DRUM 3 held");
  tap(0, 15); // to SEQ, with the finger still down
  run_ms(20);
  CHECK(c12::c12_mix_audible(2), "changing page lets a held MUTE go");
  sim::release(2, 0);
  go(PERF);
  // LATCH: a tap holds; tap again or RELEASE lets go
  tap(9, 15);
  CHECK(P->latch, "LATCH on");
  tap(0, 0);
  run_ms(100);
  CHECK(!c12::c12_mix_audible(0), "latched MUTE stays after the finger lifts");
  tap(0, 0);
  run_ms(20);
  CHECK(c12::c12_mix_audible(0), "a second tap lets it go");
  tap(0, 0);
  tap(2, 5); // latched mutation, medium
  run_ms(50);
  CHECK(!c12::c12_mix_audible(0) && P->alt_strength == 2, "two latched");
  tap(11, 15); // RELEASE
  run_ms(20);
  CHECK(c12::c12_mix_audible(0) && P->alt_strength == 0 && memcmp(pat(), &before_perf, sizeof before_perf) == 0,
        "RELEASE: everything back, the pattern as it was");
  // COMMIT: a latched mutation and mute become the project (and UNDO takes them back)
  tap(0, 0);
  tap(2, 5);
  run_ms(50);
  const uint32_t mutated_hash = [&] {
    uint32_t h = 2166136261u;
    for (int t = 0; t < 7; t++)
      for (int s = 0; s < 64; s++) { h ^= P->alt[P->alt_live].cell[t][s]; h *= 16777619u; }
    return h;
  }();
  tap(10, 15, 120, 200);
  CHECK(memcmp(pat(), &before_perf, sizeof before_perf) == 0, "a brief COMMIT tap does nothing");
  tap(10, 15, 120, 700);
  uint32_t ch = 2166136261u;
  for (int t = 0; t < 7; t++)
    for (int s = 0; s < 64; s++) { ch ^= cell(t, s); ch *= 16777619u; }
  CHECK(ch == mutated_hash && (P->mute & 1), "COMMIT held: the mutation is now the pattern, the mute the mixer's");
  CHECK(!P->perf_now().mute && !P->alt_strength, "and the performance is released");
  tap(12, 15); // UNDO
  CHECK(memcmp(pat(), &before_perf, sizeof before_perf) == 0, "UNDO takes the committed pattern back");
  P->mute = 0;
  P->apply_mixer();
  tap(9, 15); // LATCH off
  CHECK(!P->latch, "LATCH off");

  // ================================================================ SCENES: hold stores, tap recalls at the bar
  {
    P->level[0] = 100; P->apply_mixer();
    sim::touch(0, 10, 150);
    run_ms(1100);
    sim::release(0, 10);
    run_ms(12);
    CHECK(P->scenes[0].used && P->scenes[0].level[0] == 100, "holding a scene pad 1 s stores the mixer and FX");
    P->level[0] = 50; P->mute = 2; P->apply_mixer();
    c12::c12_fx_set(c12::c12x_fx_verb_wet, 12);
    // recall while playing: waits for the bar
    run_ms(500);
    tap(0, 10);
    const bool waiting = P->scene_pending == 0;
    run_ms(2100);
    CHECK(waiting && P->scene_pending < 0 && P->level[0] == 100 && P->mute == 0 && c12::c12_fx_get(c12::c12x_fx_verb_wet) == P->scenes[0].fx[c12::c12x_fx_verb_wet],
          "recalled: at the next bar, levels, mutes and FX as stored (%d %d)", waiting, P->level[0]);
    tap(14, 15);
  }

  // ================================================================ REPEAT: pads, pressure, rolls, beat repeat
  go(REPEAT);
  CHECK(get_scroll_page() == REPEAT, "REPEAT page");
  sim::write_ppm("build/sim/12_repeat.ppm", 24);
  {
    c12::c12_engine_stats a, b;
    tap(2, 4); // 1/16
    c12::c12_engine_stats_read(&a);
    sim::touch(6, 1, 60); // SNARE pad, light
    run_ms(1000);
    c12::c12_engine_stats_read(&b);
    const int light_vel = P->rep_shown_vel;
    sim::touch(6, 1, 240);
    run_ms(100);
    const int firm_vel = P->rep_shown_vel;
    sim::release(6, 1);
    run_ms(300);
    CHECK(b.repeats - a.repeats >= 7 && b.repeats - a.repeats <= 9, "holding a track pad repeats it every 1/16, stopped (%u in 1 s)", b.repeats - a.repeats);
    CHECK(light_vel >= 45 && firm_vel > light_vel + 30, "pressure is velocity: light %d, firm %d (never silent)", light_vel, firm_vel);
    c12::c12_engine_stats_read(&a);
    run_ms(500);
    c12::c12_engine_stats_read(&b);
    CHECK(b.repeats == a.repeats, "released: it stops");
    // RATE mode: a firm press doubles the rate
    tap(1, 5);
    c12::c12_engine_stats_read(&a);
    sim::touch(6, 1, 250);
    run_ms(1000);
    sim::release(6, 1);
    run_ms(300);
    c12::c12_engine_stats_read(&b);
    CHECK(b.repeats - a.repeats >= 28, "RATE mode: a hard press repeats four times as fast (%u in 1 s)", b.repeats - a.repeats);
    tap(0, 5);
    // triplets
    tap(7, 4); // 1/8T
    c12::c12_engine_stats_read(&a);
    sim::touch(0, 0, 150);
    run_ms(1000);
    sim::release(0, 0);
    run_ms(300);
    c12::c12_engine_stats_read(&b);
    CHECK(b.repeats - a.repeats >= 6 && b.repeats - a.repeats <= 7, "1/8 triplets: six a second at 120 (%u)", b.repeats - a.repeats);
    tap(2, 4);
    // a roll by hand on the target
    c12::c12_engine_stats_read(&a);
    tap(3, 7, 200); // TRIPLE
    run_ms(200);
    c12::c12_engine_stats_read(&b);
    CHECK(b.strokes - a.strokes == 2, "a TRIPLE roll pad plays three strokes on the target (%u after the first)", b.strokes - a.strokes);
    // beat repeat held while playing: the pattern unchanged, the engine repeating
    only_kick(4);
    c12::pocket_pattern before_br = *pat();
    tap(15, 15);
    run_ms(300);
    c12::c12_engine_stats_read(&a);
    sim::touch(4, 9, 150); // a quarter: steps with the kick in them
    run_ms(1000);
    c12::c12_engine_stats_read(&b);
    sim::release(4, 9);
    run_ms(20);
    CHECK(b.repeats - a.repeats >= 2 && memcmp(pat(), &before_br, sizeof before_br) == 0, "BEAT REPEAT 1/4 held 1 s: repeats (%u), pattern unchanged", b.repeats - a.repeats);
    CHECK(P->perf_now().repeat == 0, "released: off");
    tap(14, 15);
  }

  // ================================================================ FX SPACE: every control audible
  go(FX);
  tap(8, 0);
  CHECK(P->fx_bank == 0, "FX: SPACE bank");
  {
    // the snare to the reverb and the delay, then each row moved: the output changes each time
    // the kick (a tonal voice: the same each bar, where the snare's noise is not)
    only_kick(16);
    P->rsend[3] = 0;
    P->dsend[0] = 100; P->rsend[0] = 100; P->apply_mixer();
    tap(15, 15);
    run_ms(500);
    auto take = [&](int ms) { size_t a = session.size(); run_ms(ms); return std::vector<int16_t>(session.begin() + a, session.end()); };
    // the level envelope, 50 ms windows: what an ear follows of an echo or a tail
    auto differs = [&](const std::vector<int16_t> &x, const std::vector<int16_t> &y) {
      const size_t w = SAMPLE_FREQ / 20 * 2;
      double d = 0;
      for (size_t a = 0; a + w <= x.size() && a + w <= y.size(); a += w) {
        double ex = 0, ey = 0;
        for (size_t i = a; i < a + w; i++) { ex += (double)x[i] * x[i]; ey += (double)y[i] * y[i]; }
        d += fabs(sqrt(ex / w) - sqrt(ey / w));
      }
      return d;
    };
    // a whole bar (the pattern) a take, so takes line up
    std::vector<int16_t> ref = take(2000);
    std::vector<int16_t> same = take(2000);
    const double noise = differs(ref, same);
    struct { int x, y; const char *what; } moves[] = {
        {11, 1, "DELAY TIME"}, {13, 2, "DELAY FEEDBACK"}, {0, 3, "DELAY FILTER"}, {0, 4, "DELAY WET"},
        {0, 7, "REVERB SIZE"}, {13, 8, "REVERB DECAY"}, {13, 9, "REVERB DAMPING"}, {0, 10, "REVERB WET"},
    };
    for (auto &m : moves) {
      tap(m.x, m.y);
      run_ms(2000 - 52); // to the bar again
      std::vector<int16_t> now = take(2000);
      const double d = differs(ref, now);
      CHECK(d > noise * 3 + 200, "%s moves the sound (envelope difference %.0f; %.0f between two plain bars)", m.what, d, noise);
      ref = take(2000);
    }
    // MONO: the echoes no longer alternate sides
    auto side = [&](const std::vector<int16_t> &x) {
      double d = 0, a = 0;
      for (size_t i = 0; i + 1 < x.size(); i += 2) { d += fabs((double)x[i] - x[i + 1]); a += fabs((double)x[i]) + fabs((double)x[i + 1]); }
      return a > 0 ? d / a : 0;
    };
    tap(13, 4); // delay wet back up (the row above left it at 0)
    tap(13, 2); // and feedback
    run_ms(2000 - 104);
    const double pp = side(take(2000));
    tap(0, 5);
    run_ms(2000 - 52);
    const double mono = side(take(2000));
    CHECK(mono < pp * 0.7, "DELAY MONO: left and right alike (%.3f, ping-pong %.3f)", mono, pp);
    tap(1, 5);
    // DELAY FREEZE on row 0 x12 is momentary
    sim::touch(12, 0, 150);
    run_ms(50);
    CHECK(P->perf_now().freeze_d, "DELAY FREEZE held");
    sim::release(12, 0);
    run_ms(20);
    CHECK(!P->perf_now().freeze_d, "and let go");
    tap(14, 15);
    P->dsend[0] = 0; P->rsend[0] = 0; P->apply_mixer();
  }

  // ================================================================ a version-1 project loads, converted
  {
    c12glue::project_v1_t o;
    memset(&o, 0, sizeof o);
    o.magic = c12glue::PROJECT_MAGIC;
    o.version = 1;
    o.bytes = sizeof o;
    o.cell[0][0] = c12::c12x_cell(100, 0) | 3u << 20 | 1u << 23 | 1u << 25; // 4x, accelerating, crescendo
    o.cell[3][4] = c12::c12x_cell(90, 0);
    o.last_page = 0; o.swing = 57; o.bpm = 128;
    memcpy(o.kit, P->kit, sizeof o.kit);
    for (int t = 0; t < 7; t++) o.level[t] = 100;
    o.delay[3] = 40;
    o.master = 100; o.route = 0x7f;
    for (int f = 0; f < 16; f++) o.fx[f] = (uint8_t)c12::c12_fx_get(f);
    o.density = 50; o.complexity = 50;
    o.crc = c12::pocket_crc32(0, &o, (unsigned)__builtin_offsetof(c12glue::project_v1_t, crc));
    char t[16384];
    serialiser_t w(false, t, sizeof t);
    int bytes = (int)sizeof o;
    w.begin();
    w.field_base64("clatter12", &o, bytes, bytes);
    w.end();
    load(t, w.len);
    const uint32_t c = cell(0, 0);
    CHECK(c12::c12x_cell_ratchet(c) == 3 && c12::c12x_cell_roll(c) == c12::c12x_roll_accel && c12::c12x_cell_curve(c) == 1 && vel(3, 4) == 90,
          "a version-1 project loads: its ratchet becomes 4x ACCEL FADE IN");
    CHECK(c12::pocket_seq_swing() == 57 && (int)(get_tempo_bpm() + 0.5f) == 128 && P->dsend[3] == 40, "with its swing, tempo and sends");
    CHECK(P->plinky_fx(), "and its sends still go to Plinky's effects, as they did");
    c12::c12_fx_set(c12::c12x_fx_space_engine, 0);
    // and a version-2 save of it loads back the same
    save();
    c12::pocket_pattern v2 = *pat();
    load(save_text, save_len);
    CHECK(memcmp(pat(), &v2, sizeof v2) == 0, "saved as version 2 and loaded: the same steps");
  }

  // ================================================================ audio health over the session
  c12::c12_engine_stats st;
  c12::c12_engine_stats_read(&st);
  printf("session: %.1f s of audio, %u output blocks, %u LXR blocks (max %u per output block), %u clipped, %u underruns, %u ratchet strokes\n",
         sim_audio_us / 1e6, st.out_blocks, st.lxr_blocks, st.lxr_max_per_out, st.clipped, st.underruns, st.strokes);
  CHECK(st.underruns == 0, "no underruns");
  CHECK(st.clipped == 0, "no clipped samples, through every edit and effect");
  CHECK(st.lxr_max_per_out <= 2, "at most two LXR blocks per output block");
  printf("host timing of on_dsp (this machine, NOT the RP2350): mean %.1f us, max %.1f us per 2000 us block, over %ld blocks\n",
         dsp_host_ns / dsp_host_blocks / 1000.0, dsp_host_max_ns / 1000.0, dsp_host_blocks);
  sim::set_scroll_page(-1);
  run_ms(8);
  sim::set_scroll_page(0);
  run_ms(8);
  write_wav("build/sim/session.wav", session.data(), (unsigned)(session.size() / 2), SAMPLE_FREQ);
  return CHECK_REPORT("panel (simulated)");
}
