// test_panel_sim.cpp - the generated panel (build/clatter_12_full.cpp), unmodified, driven in the
// host stand-in for the Plinky API (sim/plinky_sim.h): scripted touches and pressures, simulated
// time, core-1 audio blocks and core-0 UI frames interleaved as on the device (two 2 ms audio
// blocks per 4 ms UI frame, about the documented 250 fps). Checks behaviour, writes LED frames
// (PPM) and the session's audio (WAV). SIMULATION ONLY: proves CLATTER 12's own logic, not Plinky's.
// Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
#include "plinky_sim.h"
#include "clatter_12_full.cpp"
#include "check.h"
#include <vector>

static clatter12 *P;
static mix_buffers_t mb;
static std::vector<int16_t> session;
static uint32_t sim_audio_us;

static void audio_block() {
  memset(mb.dry, 0, sizeof mb.dry);
  int16_t in[BLOCK_SIZE * 2] = {0}, out[BLOCK_SIZE * 2];
  bool own = P->on_dsp(in, out, &mb);
  for (int i = 0; i < BLOCK_SIZE * 2; ++i) session.push_back((int16_t)clampi(own ? out[i] : mb.dry[i], -32768, 32767));
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
static void tap(int x, int y, int pressure, int hold_ms = 40) {
  sim::touch(x, y, pressure);
  run_ms(hold_ms);
  sim::release(x, y);
  run_ms(12);
}
static const c12::pocket_pattern *pat() { return c12::pocket_seq_pattern(); }
static unsigned vel(int t, int s) { return c12::c12x_cell_velocity(pat()->cell[t][s]); }
static unsigned var(int t, int s) { return c12::c12x_cell_variation(pat()->cell[t][s]); }
static uint32_t pattern_hash() {
  uint32_t h = 2166136261u;
  const uint8_t *b = (const uint8_t *)pat();
  for (size_t i = 0; i < sizeof(c12::pocket_pattern); ++i) { h ^= b[i]; h *= 16777619u; }
  return h;
}
static int brightness(uint32_t c) { return ((c >> 16) & 0xff) + ((c >> 24) & 0xff) + ((c >> 8) & 0xff); }
static int white_pads_in_row(int y) {
  int n = 0;
  for (int x = 0; x < 16; ++x) n += sim::led(x, y) == WHITE;
  return n;
}
static int head_column() {
  for (int x = 0; x < 16; ++x)
    if (sim::led(x, 7) == WHITE) return x;
  return -1;
}

int main() {
  sim::reset();
  static clatter12 panel_storage; // a panel object lives in the runtime's 128 KB arena
  P = &panel_storage;
  printf("panel object: %zu bytes (the documented limit is 128 KB)\n", sizeof(clatter12));
  CHECK(sizeof(clatter12) < 128 * 1024, "panel object %zu bytes", sizeof(clatter12));
  P->on_setup_default_panel_state();

  // --- boot: the engine comes up in on_ui, the default beat appears, audio is silent until play
  run_ms(40);
  CHECK(c12glue::g_ready, "engine ready");
  CHECK(vel(0, 0) && vel(0, 4) && vel(3, 4) && vel(5, 2), "default beat");
  CHECK(brightness(sim::led(0, 0)) > 0 && brightness(sim::led(1, 0)) <= 3, "step LEDs show the beat");
  sim::write_ppm("build/sim/01_boot.ppm", 24);

  // --- PLAY at 120 BPM: transport, playhead, audio
  set_tempo_bpm(120.f, true);
  size_t audio_from = session.size();
  tap(15, 15, 150);
  CHECK(is_transport_playing() && c12::pocket_seq_playing(), "PLAY starts Plinky's transport and the sequencer");
  run_ms(130); // one 16th at 120 BPM is 125 ms
  int h1 = head_column();
  run_ms(125);
  int h2 = head_column();
  CHECK(white_pads_in_row(7) == 1, "exactly one playhead pad");
  CHECK(h1 >= 0 && h2 == (h1 + 1) % 16, "playhead advances one step per 16th (%d -> %d)", h1, h2);
  sim::write_ppm("build/sim/02_playing.ppm", 24);
  run_ms(2000);
  int peak = 0;
  for (size_t i = audio_from; i < session.size(); ++i) peak = maxi(peak, abs(session[i]));
  CHECK(peak > 3000, "audible while playing (peak %d)", peak);

  // --- step entry with pressure (stopped, so the hit is auditioned)
  tap(14, 15, 150); // STOP
  CHECK(!is_transport_playing(), "STOP");
  tap(1, 0, 230);            // hard press: loud step
  tap(2, 0, 40);             // light press: soft step
  CHECK(vel(0, 1) > 100, "hard press gives a loud step (%u)", vel(0, 1));
  CHECK(vel(0, 2) > 0 && vel(0, 2) < 70, "light press gives a soft step (%u)", vel(0, 2));
  run_ms(8);
  CHECK(brightness(sim::led(1, 0)) > brightness(sim::led(2, 0)), "brightness shows velocity");
  tap(2, 0, 120);            // tap a lit step: off
  CHECK(vel(0, 2) == 0, "a tap on a lit step clears it");
  // hold a lit step: stays on, velocity re-taken from the hold
  sim::touch(1, 0, 50);
  run_ms(400);
  sim::release(1, 0);
  run_ms(12);
  CHECK(vel(0, 1) > 0 && vel(0, 1) < 80, "holding a lit step re-takes its velocity (%u)", vel(0, 1));
  // a growing press: velocity follows the peak while held
  sim::touch(6, 3, 30);
  run_ms(20);
  unsigned soft = vel(3, 6);
  sim::touch(6, 3, 200);
  run_ms(20);
  sim::release(6, 3);
  run_ms(12);
  CHECK(soft > 0 && vel(3, 6) > soft, "velocity follows the press (%u -> %u)", soft, vel(3, 6));

  // --- variations: the lane under a track cycles that step's variation
  tap(1, 8, 120);
  CHECK(var(0, 1) == 1, "variation A (%u)", var(0, 1));
  tap(1, 8, 120);
  CHECK(var(0, 1) == 2, "variation B (%u)", var(0, 1));
  tap(1, 8, 120);
  CHECK(var(0, 1) == 0, "DRUM 1 has A and B only, back to none (%u)", var(0, 1));
  tap(4, 11, 120); // SNARE step 4 (default beat): A, B, second sound, its A and B
  for (int i = 2; i <= 5; ++i) tap(4, 11, 120);
  CHECK(var(3, 4) == 5, "SNARE reaches its second sound's B (%u)", var(3, 4));
  tap(4, 11, 120);
  CHECK(var(3, 4) == 0, "and wraps (%u)", var(3, 4));
  tap(9, 9, 120); // no step under it: nothing
  CHECK(vel(1, 9) == 0 && var(1, 9) == 0, "a variation pad under an empty step does nothing");
  sim::write_ppm("build/sim/03_edited.ppm", 24);

  // --- pages and length
  tap(5, 15, 120); // length 32
  CHECK(pat()->last_page == 1, "length 32 steps (last_page %u)", pat()->last_page);
  tap(1, 15, 120); // view page 2
  run_ms(8);
  CHECK(brightness(sim::led(0, 0)) <= 3, "page 2 starts empty");
  tap(3, 0, 200); // a step on page 2 = step 19
  CHECK(vel(0, 19) > 0, "editing page 2 writes step 19");
  // copy page 1 onto page 2: hold page pad 1, tap page pad 2
  sim::touch(0, 15, 120);
  run_ms(20);
  tap(1, 15, 120);
  sim::release(0, 15);
  run_ms(12);
  bool same = true;
  for (int t = 0; t < 7; ++t)
    for (int s = 0; s < 16; ++s) same = same && pat()->cell[t][s] == pat()->cell[t][16 + s];
  CHECK(same, "hold page 1 + tap page 2 copies the page");
  tap(0, 15, 120); // back to page 1 (and no copy: nothing held)
  sim::write_ppm("build/sim/04_pages.ppm", 24);

  // --- generate, mutate, undo/redo, clear
  uint32_t before = pattern_hash();
  tap(8, 15, 120); // GEN
  uint32_t generated = pattern_hash();
  CHECK(generated != before, "GEN makes a new pattern");
  tap(12, 15, 120); // UNDO
  CHECK(pattern_hash() == before, "UNDO restores the pattern exactly");
  tap(12, 15, 120); // UNDO again = redo
  CHECK(pattern_hash() == generated, "UNDO again redoes it");
  tap(9, 15, 120); // MUT
  CHECK(pattern_hash() != generated, "MUT changes it");
  tap(11, 15, 120); // STYLE
  tap(8, 15, 120);  // GEN in the next style
  sim::write_ppm("build/sim/05_generated.ppm", 24);
  uint32_t full = pattern_hash();
  tap(13, 15, 120, 200); // CLR tapped briefly: nothing
  CHECK(pattern_hash() == full, "a brief CLR tap does nothing");
  tap(13, 15, 120, 600); // held: clears
  bool empty = true;
  for (int t = 0; t < 7; ++t)
    for (int s = 0; s < 64; ++s) empty = empty && pat()->cell[t][s] == 0;
  CHECK(empty, "CLR held 0.5 s clears every step");
  tap(12, 15, 120);
  CHECK(pattern_hash() == full, "UNDO brings it back");

  // --- kits while playing
  tap(15, 15, 150);
  size_t kit_audio = session.size();
  tap(10, 15, 120);
  run_ms(1000);
  tap(10, 15, 120);
  run_ms(1000);
  CHECK(c12::c12_kit_pump(0) == 0, "the new kits reached the engine");
  int kpeak = 0;
  for (size_t i = kit_audio; i < session.size(); ++i) kpeak = maxi(kpeak, abs(session[i]));
  CHECK(kpeak > 3000, "audible with new kits (peak %d)", kpeak);
  sim::write_ppm("build/sim/06_kit.ppm", 24);

  // --- the panel settings page (above page 0) shows the DSP load
  sim::set_scroll_page(-1);
  run_ms(8);
  sim::set_scroll_page(0);
  run_ms(8);

  // --- engine health over the whole session
  c12::c12_engine_stats st;
  c12::c12_engine_stats_read(&st);
  printf("session: %.1f s of audio, %u output blocks, %u LXR blocks (max %u per output block), %u clipped, %u underruns\n",
         sim_audio_us / 1e6, st.out_blocks, st.lxr_blocks, st.lxr_max_per_out, st.clipped, st.underruns);
  CHECK(st.underruns == 0, "no underruns");
  CHECK(st.lxr_max_per_out <= 2, "at most two LXR blocks per output block");
  write_wav("build/sim/session.wav", session.data(), (unsigned)(session.size() / 2), SAMPLE_FREQ);
  return CHECK_REPORT("panel (simulated)");
}
