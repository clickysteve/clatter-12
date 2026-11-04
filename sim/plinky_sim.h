// plinky_sim.h - a host stand-in for the part of the Plinky 12 panel API that CLATTER 12 uses.
//
// Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
//
// THIS IS NOT THE PLINKY FIRMWARE OR ITS EMULATOR. It is written by the CLATTER contributors from
// the published API documentation (plinky12.com/api_docs.html, docs/ide_api/llm.txt), declaring
// only what panel/clatter12.cpp calls, with the documented names and signatures, and behaving as
// the documentation describes (simplified: one finger per pad, no slides between pads, no
// debounce, instantaneous transport). It exists so the real panel source can be compiled and
// driven on a desktop, with deterministic touches and time, and its LEDs and audio inspected.
// Anything it shows is evidence about CLATTER 12's own logic, not about Plinky behaviour.
// tools/check_api.py checks that every name declared here also appears in the published headers.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef SAMPLE_FREQ
#define SAMPLE_FREQ 32000 // not published by Plinky; their reverb runs at 16 kHz, "half the normal rate"
#endif
#define BLOCK_SIZE 64
#define BLOCK_SIZE_SH 6

// ---- LEDs (blocks_ll.h) ----
#define LED_RGB(r, g, b) (uint32_t)(((g) << 24) | ((r) << 16) | ((b) << 8))
#define PINK LED_RGB(20, 0, 31)
#define RED LED_RGB(31, 0, 0)
#define ORANGE LED_RGB(31, 13, 0)
#define YELLOW LED_RGB(31, 31, 1)
#define GREEN LED_RGB(0, 31, 0)
#define CYAN LED_RGB(0, 31, 31)
#define BLUE LED_RGB(0, 0, 31)
#define WHITE LED_RGB(15, 15, 15)
#define BLACK 0
#define BRIGHTEST(rgb) ((rgb) << 2)
#define BRIGHTER(rgb) ((rgb) << 1)
#define DIMMER(rgb) ((rgb & 0xfefefe00) >> 1)
#define DIMMEST(rgb) (((rgb) & 0xfcfcfc00) >> 2)
#define DIMMESTEST(rgb) (((rgb) & 0xf8f8f800) >> 3)

static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int clampi(int a, int b, int c) { return maxi(b, mini(a, c)); }
static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

static inline uint32_t fade_col(uint32_t rgb, int brightness_q8) {
  int r = mini(255, (((rgb >> 16) & 0xff) * brightness_q8) >> 8);
  int g = mini(255, (((rgb >> 24) & 0xff) * brightness_q8) >> 8);
  int b = mini(255, (((rgb >> 8) & 0xff) * brightness_q8) >> 8);
  return LED_RGB(r, g, b);
}
static inline uint32_t add_col(uint32_t x, uint32_t y) {
  int r = mini(255, (((x >> 16) & 0xff) + ((y >> 16) & 0xff)));
  int g = mini(255, (((x >> 24) & 0xff) + ((y >> 24) & 0xff)));
  int b = mini(255, (((x >> 8) & 0xff) + ((y >> 8) & 0xff)));
  return LED_RGB(r, g, b);
}
static inline uint32_t lerp_col(uint32_t y, uint32_t x, int t) {
  int r = clampi((((x >> 16) & 0xff) * t + ((y >> 16) & 0xff) * (256 - t)) >> 8, 0, 255);
  int g = clampi((((x >> 24) & 0xff) * t + ((y >> 24) & 0xff) * (256 - t)) >> 8, 0, 255);
  int b = clampi((((x >> 8) & 0xff) * t + ((y >> 8) & 0xff) * (256 - t)) >> 8, 0, 255);
  return LED_RGB(r, g, b);
}
extern uint32_t palette[16][16]; // sim: an approximation, not Plinky's palette.h (const there)
static inline uint32_t palette_hue_to_col(uint8_t hue_q4, int brightness_q3 = 8) {
  return palette[brightness_q3 & 15][hue_q4 & 15];
}
// Pages stack vertically: page p is rows p*16 .. p*16+15 of one canvas (the settings page, above
// page 0, is drawn by the system). The sim keeps SIM_PAGES pages.
#define SIM_PAGES 8
extern uint32_t *rgbs;
void set_led(int x, int y, uint32_t rgb15);
uint32_t get_led(int x, int y);
void leds_clear();

// ---- touch (blocks_ll.h) ----
static inline int touch_pressure_curve_q7(int pressure) {
  pressure = clampi(pressure, 0, 255);
  return ((pressure * pressure) + (pressure << 7)) >> 8;
}
int get_touch_pressure_xy(int x, int y);
int get_touch_down(int x, int y);
int get_touch_pressed(int x, int y);
int get_touch_released(int x, int y);
int get_touch_origin_x(int x, int y);
int get_touch_origin_y(int x, int y);
static inline bool does_touch_originate_here(int x, int y) {
  return get_touch_origin_x(x, y) == x && get_touch_origin_y(x, y) == y;
}

// ---- transport and clock (blocks_ll.h) ----
extern int32_t qn_period_us;
static inline float get_tempo_bpm(void) { return qn_period_us > 0 ? 60000000.f / (qn_period_us) : 120.f; }
void set_qn_period_us(int us, bool switch_to_internal_clock);
static inline void set_tempo_bpm(float bpm, bool switch_to_internal_clock) {
  bpm = clampf(bpm, 30.f, 300.f);
  set_qn_period_us((int)(60000000.f / bpm), switch_to_internal_clock);
}
extern uint8_t transport_state;
enum { TRANSPORT_PLAYING = 1, TRANSPORT_ARMED = 2, TRANSPORT_STOPPED_AT_ZERO = 4 };
static inline bool is_transport_playing(void) { return transport_state & TRANSPORT_PLAYING; }
void stop_transport(int midi_ports = -1);
void start_transport(int midi_ports = -1);
uint32_t time_us();

// ---- paging, system UI (blocks_ll.h) ----
int get_scroll_target_page(void);
int get_scroll_page(void);
void scroll_to_page(int page);
void show_ripple(int16_t x, int16_t y, uint32_t color);
int draw_system_style_settings_page(const char *name4, const char *value4 = NULL, int bar_percent = -1,
                                    int highlight_x1 = 0, int highlight_x2 = 0, uint32_t value_color = 0);

// ---- PSRAM (blocks_ll.h): the sim gives 1 MiB, the device about 7.5 MiB ----
extern uint8_t sim_psram[];
static inline int16_t *get_psram_ptr(void) { return (int16_t *)sim_psram; }
uint32_t get_psram_size(void);

// ---- audio (synth.h) ----
#define REVERBBUF_SAMPLES 32768
typedef struct mix_buffers_t {
  int16_t reverbbuf[REVERBBUF_SAMPLES];
  int dry[BLOCK_SIZE * 2];
  int delaysend[BLOCK_SIZE];
  int reverbsend[BLOCK_SIZE];
} mix_buffers_t;

// ---- saving (save_and_load.h), as the published panels use it: OBJECT_BEGIN, FIELD,
// FIELD_BASE64, OBJECT_END, s.reading. The sim's serialiser writes and reads a flat JSON object of
// numbers and base64 strings in a caller's buffer; Plinky's writes JSON to the SD card. ----
struct serialiser_t {
  bool reading;
  char *buf;
  int cap, len;
  bool first;
  serialiser_t(bool reading_, char *buf_, int cap_, int len_ = 0) : reading(reading_), buf(buf_), cap(cap_), len(len_), first(true) {}
  void begin();
  void end();
  void field_int(const char *name, long *v, long lo, long hi);
  void field_base64(const char *name, void *data, int &bytes, int max_bytes);
  template <typename T> void field(const char *name, T &v, long lo, long hi) {
    long x = (long)v;
    field_int(name, &x, lo, hi);
    if (reading) v = (T)x;
  }
};
#define OBJECT_BEGIN(s) { serialiser_t &sim_ser_ = (s); sim_ser_.begin();
#define FIELD(name, var, lo, hi) sim_ser_.field(name, var, (long)(lo), (long)(hi))
#define FIELD_BASE64(name, data, bytes, max_bytes) sim_ser_.field_base64(name, data, bytes, max_bytes)
#define OBJECT_END(s) sim_ser_.end(); }
#define FLAG_PICKER_ENABLE_DELETE 1
// The stock save/load page (blocks.h panel_page_t::saveload): the sim draws a placeholder and
// counts the calls; saving and loading are driven by the test (sim::save, sim::load_into).
struct panel_page_t {
  void saveload(int slots, bool show_name = true, int flags = 0);
};

// ---- the panel base (blocks.h), the hooks CLATTER 12 overrides ----
struct panel_t {
  virtual int get_num_pages(void) { return 1; }
  virtual int get_num_panel_settings_pages(void) { return 0; }
  virtual void on_setup_default_panel_state() {}
  virtual void on_sequence(int delta_time_us) { (void)delta_time_us; }
  virtual void on_ui(int delta_time_us) { (void)delta_time_us; }
  virtual void on_click(uint8_t button_mask) { (void)button_mask; }
  virtual void on_midi(uint32_t midimsg) { (void)midimsg; }
  virtual bool on_dsp(const int16_t *audiobuf_in, int16_t *audiobuf_out, mix_buffers_t *mix_buffers_out) {
    (void)audiobuf_in; (void)audiobuf_out; (void)mix_buffers_out;
    return false;
  }
  virtual int get_num_tracks(void) { return 8; }
  virtual int get_version(void) { return 0; }
  virtual bool on_serialise(serialiser_t &s, int version) { (void)s; (void)version; return true; }
  virtual bool on_serialise_settings(serialiser_t &s, int version) { (void)s; (void)version; return true; }
  virtual void on_load_finished(void) {}
  // runs on the live panel before a staged load is read into `staged` (blocks.h)
  virtual bool on_prepare_staged_load(panel_t *staged, size_t panel_memory_size) { (void)staged; (void)panel_memory_size; return true; }
  virtual const char *get_track_name(int i, char buf[16]) { (void)i; return buf; }
  virtual ~panel_t(void) {}
};

// ---- simulator control (not Plinky API) ----
namespace sim {
void reset();
void touch(int x, int y, int pressure); // finger down (or pressure change) on a pad of the shown page
void release(int x, int y);
void release_all();
void ui_frame_edges();                  // the driver calls this before each on_ui
void set_scroll_page(int page);
void advance_us(uint32_t us);           // move simulated time
uint32_t led(int x, int y);             // an LED of the page shown
void set_num_pages(int n);              // the panel's get_num_pages (scroll_to_page clamps to it)
int saveload_calls();
void write_ppm(const char *path, int scale);
} // namespace sim
