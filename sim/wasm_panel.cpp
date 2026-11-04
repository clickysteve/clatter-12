// wasm_panel.cpp - CLATTER 12's panel, as it is (build/clatter_12_full.cpp), with the simulator's
// stand-in for the Plinky API, as one WebAssembly module for the project page.
// Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
//
// The page drives it as a Plinky would: touches in, one UI frame and two 64-frame audio blocks per
// 4 ms (c12p_frame), LEDs and audio out. So the grid on the page is the firmware's own interface,
// not a copy of it. It is NOT a Plinky: no pressure sensor, no Plinky output stage or effects
// (the delay and reverb sends go nowhere), no CPU limit (sim/plinky_sim.h).
#include "plinky_sim.h"
#include "clatter_12_full.cpp"
#include "panel_help.h"

#define EXPORT(n) __attribute__((export_name(n)))

void *operator new(size_t, void *p) noexcept { return p; }
void operator delete(void *) noexcept {}
void operator delete(void *, size_t) noexcept {}
extern "C" void __cxa_pure_virtual() {}

alignas(16) static unsigned char panel_mem[sizeof(clatter12)];
alignas(16) static unsigned char staged_mem[sizeof(clatter12)];
static clatter12 *P;
static mix_buffers_t mb;
static int16_t pcm[BLOCK_SIZE * 2 * 2];
static uint32_t leds[256];
static char text[16384];
static c12_help_t help;
// A click can press and release between two UI frames, which no finger does: a press is held until
// a frame has seen it, and a release that comes sooner waits for that frame.
static uint8_t unseen[16][16], release_due[16][16];
extern "C" char c12w_log[];

extern "C" {
EXPORT("c12p_init") int c12p_init(void) {
  sim::reset();
  memset(panel_mem, 0, sizeof panel_mem);
  P = new (panel_mem) clatter12();
  sim::set_num_pages(P->get_num_pages());
  P->on_setup_default_panel_state();
  for (int i = 0; i < 10; ++i) {
    sim::ui_frame_edges();
    P->on_ui(4000);
  }
  return c12glue::g_ready ? 0 : -1;
}
// One UI frame (4 ms): two audio blocks, then on_ui. The 128 frames of audio are in c12p_pcm.
EXPORT("c12p_frame") void c12p_frame(void) {
  int16_t in[BLOCK_SIZE * 2] = {0};
  for (int b = 0; b < 2; ++b) {
    memset(mb.dry, 0, sizeof mb.dry);
    P->on_dsp(in, pcm + b * BLOCK_SIZE * 2, &mb);
    for (int i = 0; i < BLOCK_SIZE * 2; ++i) pcm[b * BLOCK_SIZE * 2 + i] = (int16_t)clampi(mb.dry[i], -32768, 32767);
    sim::advance_us(2000);
  }
  sim::ui_frame_edges();
  P->on_ui(4000);
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x) {
      unseen[y][x] = 0;
      if (release_due[y][x]) {
        release_due[y][x] = 0;
        sim::release(x, y);
      }
    }
}
EXPORT("c12p_pcm") int16_t *c12p_pcm(void) { return pcm; }
EXPORT("c12p_touch") void c12p_touch(int x, int y, int pressure) {
  if (x < 0 || x > 15 || y < 0 || y > 15) return;
  if (pressure > 0) {
    sim::touch(x, y, pressure);
    unseen[y][x] = 1;
    release_due[y][x] = 0;
  } else if (unseen[y][x]) {
    release_due[y][x] = 1;
  } else {
    sim::release(x, y);
  }
}
EXPORT("c12p_release_all") void c12p_release_all(void) {
  sim::release_all();
  memset(unseen, 0, sizeof unseen);
  memset(release_due, 0, sizeof release_due);
}
// the shown page's LEDs, LED_RGB words, row by row
EXPORT("c12p_leds") uint32_t *c12p_leds(void) {
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x) leds[y * 16 + x] = sim::led(x, y);
  return leds;
}
EXPORT("c12p_page") int c12p_page(void) { return get_scroll_page(); }
EXPORT("c12p_set_page") void c12p_set_page(int page) { scroll_to_page(page); }
EXPORT("c12p_tempo") int c12p_tempo(void) { return (int)(get_tempo_bpm() + 0.5f); }
EXPORT("c12p_set_tempo") void c12p_set_tempo(int bpm) { set_tempo_bpm((float)bpm, true); }
EXPORT("c12p_playing") int c12p_playing(void) { return is_transport_playing() ? 1 : 0; }
EXPORT("c12p_position") int c12p_position(void) { return c12::pocket_seq_position(); }
EXPORT("c12p_text") char *c12p_text(void) { return text; }
EXPORT("c12p_log") char *c12p_log(void) { return c12w_log; }
// the project as Plinky's save would write it (JSON with one base64 field), into c12p_text
EXPORT("c12p_save") int c12p_save(void) {
  serialiser_t s(false, text, sizeof text);
  P->on_serialise(s, P->get_version());
  return s.len;
}
// load c12p_text (len bytes) as Plinky loads a song: a staged copy reads it, then replaces the panel
EXPORT("c12p_load") int c12p_load(int len) {
  if (len < 0 || len >= (int)sizeof text) return -1;
  text[len] = 0;
  memset(staged_mem, 0, sizeof staged_mem);
  clatter12 *st = new (staged_mem) clatter12();
  P->on_prepare_staged_load(st, sizeof staged_mem);
  serialiser_t s(true, text, sizeof text, len);
  st->on_serialise(s, st->get_version());
  const bool ok = st->load_pending;
  memcpy((void *)P, (const void *)st, sizeof(clatter12));
  P->on_load_finished();
  return ok ? 0 : 1;
}
// what pad (x, y) of the shown page does: name \n kind \n description, or "" (c12p_text)
EXPORT("c12p_help") char *c12p_help(int x, int y) {
  if (!c12_help(P, get_scroll_page(), x, y, help)) {
    text[0] = 0;
    return text;
  }
  snprintf(text, sizeof text, "%s\n%s\n%s", help.name, help.kind, help.does);
  return text;
}
// a few values for the page's status line
EXPORT("c12p_status") int c12p_status(int what) {
  switch (what) {
  case 0: return P->style;
  case 1: return P->kit_character;
  case 2: return (int)(c12::pocket_seq_pattern()->last_page & 3u) + 1;
  case 3: return P->view_page;
  case 4: return c12::pocket_seq_swing();
  case 5: return P->sound_track;
  case 6: return P->sound_bank;
  case 7: return P->hist_undo;
  case 8: return P->hist_redo;
  default: return 0;
  }
}
}
