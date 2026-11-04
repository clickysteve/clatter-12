// plinky_sim.cpp - see plinky_sim.h. NOT the Plinky firmware: a stand-in written from its docs.
// Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
#include "plinky_sim.h"

static uint32_t led_store[256];
uint32_t *rgbs = led_store;
int32_t qn_period_us = 60000000 / 120;
uint8_t transport_state = TRANSPORT_STOPPED_AT_ZERO;

static int pressure_now[16][16];
static int down_now[16][16], down_prev[16][16];
static uint64_t now_us;
static int scroll_page;
static const int DOWN_THRESHOLD = 16;

// 16 hues x 16 brightnesses, a smooth approximation of a rainbow (sim only)
static uint32_t make_palette_entry(int b, int h) {
  float hue = h / 16.f * 6.f;
  int i = (int)hue;
  float f = hue - i;
  float r = 0, g = 0, bl = 0;
  switch (i % 6) {
  case 0: r = 1; g = f; break;
  case 1: r = 1 - f; g = 1; break;
  case 2: g = 1; bl = f; break;
  case 3: g = 1 - f; bl = 1; break;
  case 4: r = f; bl = 1; break;
  default: r = 1; bl = 1 - f; break;
  }
  float s = b / 8.f * 31.f;
  return LED_RGB((int)(r * s), (int)(g * s), (int)(bl * s));
}
uint32_t palette[16][16];
alignas(4) uint8_t sim_psram[1u << 20];
uint32_t get_psram_size(void) { return sizeof sim_psram; }

void set_led(int x, int y, uint32_t rgb15) {
  if (x < 0 || x > 15 || y < 0 || y > 15) return;
  led_store[y * 16 + x] = rgb15;
}
uint32_t get_led(int x, int y) {
  if (x < 0 || x > 15 || y < 0 || y > 15) return 0;
  return led_store[y * 16 + x];
}
static bool in(int x, int y) { return x >= 0 && x < 16 && y >= 0 && y < 16; }
int get_touch_pressure_xy(int x, int y) { return in(x, y) ? pressure_now[y][x] : 0; }
int get_touch_down(int x, int y) { return in(x, y) ? down_now[y][x] : 0; }
int get_touch_pressed(int x, int y) { return in(x, y) ? (down_now[y][x] && !down_prev[y][x]) : 0; }
int get_touch_released(int x, int y) { return in(x, y) ? (!down_now[y][x] && down_prev[y][x]) : 0; }
int get_touch_origin_x(int x, int y) { (void)y; return x; }
int get_touch_origin_y(int x, int y) { (void)x; return y; }

void set_qn_period_us(int us, bool) { qn_period_us = us; }
void stop_transport(int) { transport_state = TRANSPORT_STOPPED_AT_ZERO; }
void start_transport(int) { transport_state = TRANSPORT_PLAYING; }
uint32_t time_us() { return (uint32_t)now_us; }

int get_scroll_target_page(void) { return scroll_page; }
static int ripples;
void show_ripple(int16_t, int16_t, uint32_t) { ripples++; }
int draw_system_style_settings_page(const char *name4, const char *value4, int bar_percent, int, int, uint32_t) {
  leds_clear();
  printf("[sim settings page] %s %s %d%%\n", name4, value4 ? value4 : "", bar_percent);
  return 0;
}

namespace sim {
void reset() {
  for (int b = 0; b < 16; ++b)
    for (int h = 0; h < 16; ++h) palette[b][h] = make_palette_entry(b, h);
  memset(led_store, 0, sizeof led_store);
  memset(pressure_now, 0, sizeof pressure_now);
  memset(down_now, 0, sizeof down_now);
  memset(down_prev, 0, sizeof down_prev);
  now_us = 1000;
  scroll_page = 0;
  transport_state = TRANSPORT_STOPPED_AT_ZERO;
  qn_period_us = 60000000 / 120;
}
void touch(int x, int y, int pressure) {
  if (!in(x, y)) return;
  pressure_now[y][x] = pressure;
}
void release(int x, int y) {
  if (!in(x, y)) return;
  pressure_now[y][x] = 0;
}
// called by the driver once per UI frame, before on_ui: the edges the panel sees
void ui_frame_edges() {
  memcpy(down_prev, down_now, sizeof down_now);
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x) down_now[y][x] = pressure_now[y][x] >= DOWN_THRESHOLD;
}
void set_scroll_page(int page) { scroll_page = page; }
void advance_us(uint32_t us) { now_us += us; }
uint32_t led(int x, int y) { return get_led(x, y); }
void write_ppm(const char *path, int scale) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  int w = 16 * scale, h = 16 * scale;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int py = 0; py < h; ++py)
    for (int px = 0; px < w; ++px) {
      int x = px / scale, y = py / scale;
      bool gap = (px % scale) == 0 || (py % scale) == 0;
      uint32_t c = get_led(x, y);
      int r = (c >> 16) & 0xff, g = (c >> 24) & 0xff, b = (c >> 8) & 0xff;
      // LED_RGB channels are nominally 0..31 (brighter values saturate); show 31 as 255
      r = mini(255, r * 255 / 31); g = mini(255, g * 255 / 31); b = mini(255, b * 255 / 31);
      if (gap) r = g = b = 12;
      unsigned char px3[3] = {(unsigned char)r, (unsigned char)g, (unsigned char)b};
      fwrite(px3, 1, 3, f);
    }
  fclose(f);
}
} // namespace sim
