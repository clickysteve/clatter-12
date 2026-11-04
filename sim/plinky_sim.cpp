// plinky_sim.cpp - see plinky_sim.h. NOT the Plinky firmware: a stand-in written from its docs.
// Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
#include "plinky_sim.h"

// rows -16..-1 (the settings page) and SIM_PAGES pages below
static uint32_t led_store[(SIM_PAGES + 1) * 16 * 16];
uint32_t *rgbs = led_store;
static const int CANVAS_Y0 = -16, CANVAS_ROWS = (SIM_PAGES + 1) * 16;
int32_t qn_period_us = 60000000 / 120;
uint8_t transport_state = TRANSPORT_STOPPED_AT_ZERO;

// fingers are physical: a pad of the 16 x 16 surface, seen on the page shown (and only there),
// with the canvas row where the finger came down as its origin
static int finger[16][16], origin_row[16][16];
static int pressure_now[CANVAS_ROWS][16];
static int down_now[CANVAS_ROWS][16], down_prev[CANVAS_ROWS][16];
static uint64_t now_us;
static int scroll_page, num_pages = 1, saveloads;
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

static bool in(int x, int y) { return x >= 0 && x < 16 && y >= CANVAS_Y0 && y < CANVAS_Y0 + CANVAS_ROWS; }
void set_led(int x, int y, uint32_t rgb15) {
  if (!in(x, y)) return;
  led_store[(y - CANVAS_Y0) * 16 + x] = rgb15;
}
uint32_t get_led(int x, int y) {
  if (!in(x, y)) return 0;
  return led_store[(y - CANVAS_Y0) * 16 + x];
}
void leds_clear() { memset(led_store, 0, sizeof led_store); }
#define ROW(y) ((y) - CANVAS_Y0)
int get_touch_pressure_xy(int x, int y) { return in(x, y) ? pressure_now[ROW(y)][x] : 0; }
int get_touch_down(int x, int y) { return in(x, y) ? down_now[ROW(y)][x] : 0; }
int get_touch_pressed(int x, int y) { return in(x, y) ? (down_now[ROW(y)][x] && !down_prev[ROW(y)][x]) : 0; }
int get_touch_released(int x, int y) { return in(x, y) ? (!down_now[ROW(y)][x] && down_prev[ROW(y)][x]) : 0; }
int get_touch_origin_x(int x, int y) { (void)y; return x; }
int get_touch_origin_y(int x, int y) {
  int py = y - scroll_page * 16;
  if (x < 0 || x > 15 || py < 0 || py > 15) return y;
  return origin_row[py][x];
}

void set_qn_period_us(int us, bool) { qn_period_us = us; }
void stop_transport(int) { transport_state = TRANSPORT_STOPPED_AT_ZERO; }
void start_transport(int) { transport_state = TRANSPORT_PLAYING; }
uint32_t time_us() { return (uint32_t)now_us; }

int get_scroll_target_page(void) { return scroll_page; }
int get_scroll_page(void) { return scroll_page; }
void scroll_to_page(int page) { scroll_page = page < -1 ? -1 : page >= num_pages ? num_pages - 1 : page; }
void panel_page_t::saveload(int slots, bool, int) {
  // a placeholder: the slots as dim pads (Plinky draws its own picker here)
  int y0 = scroll_page * 16;
  for (int i = 0; i < slots && i < 16; ++i) set_led(i, y0 + 2, LED_RGB(6, 6, 2));
  saveloads++;
}

// ---- the serialiser ----
static void put(serialiser_t &s, const char *t) {
  while (*t && s.len < s.cap - 1) s.buf[s.len++] = *t++;
  s.buf[s.len] = 0;
}
void serialiser_t::begin() {
  first = true;
  if (!reading) { len = 0; put(*this, "{"); }
}
void serialiser_t::end() {
  if (!reading) put(*this, "}");
}
static void put_key(serialiser_t &s, const char *name) {
  if (!s.first) put(s, ",");
  s.first = false;
  put(s, "\"");
  put(s, name);
  put(s, "\":");
}
// the text after "name": in the buffer, or NULL
static const char *find_key(const serialiser_t &s, const char *name) {
  int n = (int)strlen(name);
  for (int i = 0; i + n + 3 <= s.len; ++i)
    if (s.buf[i] == '"' && !strncmp(s.buf + i + 1, name, n) && s.buf[i + 1 + n] == '"' && s.buf[i + 2 + n] == ':')
      return s.buf + i + 3 + n;
  return NULL;
}
void serialiser_t::field_int(const char *name, long *v, long lo, long hi) {
  if (!reading) {
    char t[24];
    snprintf(t, sizeof t, "%ld", *v);
    put_key(*this, name);
    put(*this, t);
    return;
  }
  const char *p = find_key(*this, name);
  if (!p) return; // absent: left as it is (Plinky's behaviour, which panels must allow for)
  long x = strtol(p, NULL, 10);
  *v = x < lo ? lo : x > hi ? hi : x;
}
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64v(char c) {
  const char *p = strchr(B64, c);
  return c && p ? (int)(p - B64) : -1;
}
void serialiser_t::field_base64(const char *name, void *data, int &bytes, int max_bytes) {
  unsigned char *d = (unsigned char *)data;
  if (!reading) {
    put_key(*this, name);
    put(*this, "\"");
    for (int i = 0; i < bytes; i += 3) {
      unsigned v = (unsigned)d[i] << 16 | (i + 1 < bytes ? d[i + 1] << 8 : 0) | (i + 2 < bytes ? d[i + 2] : 0);
      char q[5] = {B64[v >> 18 & 63], B64[v >> 12 & 63], i + 1 < bytes ? B64[v >> 6 & 63] : '=', i + 2 < bytes ? B64[v & 63] : '=', 0};
      put(*this, q);
    }
    put(*this, "\"");
    return;
  }
  const char *p = find_key(*this, name);
  if (!p || *p != '"') { bytes = 0; return; }
  ++p;
  int n = 0;
  unsigned acc = 0, bits = 0;
  for (; *p && *p != '"'; ++p) {
    int v = b64v(*p);
    if (v < 0) continue;
    acc = acc << 6 | (unsigned)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n < max_bytes) d[n] = (unsigned char)(acc >> bits);
      n++;
    }
  }
  bytes = n; // what the file held (more than max_bytes: only max_bytes were written)
}
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
  num_pages = 1;
  saveloads = 0;
  memset(pressure_now, 0, sizeof pressure_now);
  memset(finger, 0, sizeof finger);
  memset(origin_row, 0, sizeof origin_row);
  memset(down_now, 0, sizeof down_now);
  memset(down_prev, 0, sizeof down_prev);
  now_us = 1000;
  scroll_page = 0;
  transport_state = TRANSPORT_STOPPED_AT_ZERO;
  qn_period_us = 60000000 / 120;
}
void touch(int x, int y, int pressure) {
  if (x < 0 || x > 15 || y < 0 || y > 15) return;
  if (finger[y][x] < DOWN_THRESHOLD && pressure >= DOWN_THRESHOLD) origin_row[y][x] = y + scroll_page * 16;
  finger[y][x] = pressure;
}
void release(int x, int y) {
  if (x < 0 || x > 15 || y < 0 || y > 15) return;
  finger[y][x] = 0;
}
void release_all() { memset(finger, 0, sizeof finger); }
void set_num_pages(int n) { num_pages = n < 1 ? 1 : n > SIM_PAGES ? SIM_PAGES : n; }
int saveload_calls() { return saveloads; }
// called by the driver once per UI frame, before on_ui: the edges the panel sees
void ui_frame_edges() {
  memcpy(down_prev, down_now, sizeof down_now);
  memset(pressure_now, 0, sizeof pressure_now);
  if (scroll_page >= 0)
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x) pressure_now[ROW(y + scroll_page * 16)][x] = finger[y][x];
  for (int y = 0; y < CANVAS_ROWS; ++y)
    for (int x = 0; x < 16; ++x) down_now[y][x] = pressure_now[y][x] >= DOWN_THRESHOLD;
}
void set_scroll_page(int page) { scroll_page = page; }
void advance_us(uint32_t us) { now_us += us; }
uint32_t led(int x, int y) { return get_led(x, y + (scroll_page > 0 ? scroll_page : 0) * 16); }
#ifndef C12_WASM
void write_ppm(const char *path, int scale) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  int w = 16 * scale, h = 16 * scale;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int py = 0; py < h; ++py)
    for (int px = 0; px < w; ++px) {
      int x = px / scale, y = py / scale;
      bool gap = (px % scale) == 0 || (py % scale) == 0;
      uint32_t c = led(x, y);
      int r = (c >> 16) & 0xff, g = (c >> 24) & 0xff, b = (c >> 8) & 0xff;
      // LED_RGB channels are nominally 0..31 (brighter values saturate); show 31 as 255
      r = mini(255, r * 255 / 31); g = mini(255, g * 255 / 31); b = mini(255, b * 255 / 31);
      if (gap) r = g = b = 12;
      unsigned char px3[3] = {(unsigned char)r, (unsigned char)g, (unsigned char)b};
      fwrite(px3, 1, 3, f);
    }
  fclose(f);
}
#endif
} // namespace sim
