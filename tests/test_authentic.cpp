// test_authentic.cpp - CLATTER 12's LXR (the generated C++ engine: single precision, platform libm,
// no fast-math) against CLATTER Pocket's own host build of the same firmware files (C, double
// precision, CLATTER's libm, -O3 -ffast-math: the build Pocket shows to match CLATTER's WebAssembly
// engine within 3 LSB).
//
// Both run at the LXR's 44 140 Hz, before any rate conversion, from the same seed, Pocket-designed
// kits, triggers (all seven, with variations, varied velocity and pitch). Measured per voice:
//   - sample-level signal/difference (informational: noise voices decorrelate once any rounding
//     differs, which says nothing about how they sound; see docs/AUTHENTICITY.md);
//   - the 10 ms RMS envelope, frame by frame (the hit's shape and level);
//   - the long-term spectrum in third-octave bands (its timbre).
// Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <algorithm>
#define C12_LXR_CODE
#define C12_LXR_DATA
#include "c12_engine.inc" // namespace c12
#define C12_TABLE_BYTES 102240u // core/c12_engine.h (macros do not survive amalgamation)
#include "pocket_lxr.h"   // ::pocket_lxr_* : CLATTER Pocket's liblxr_nofx.a
#include "pocket_seq.h"   // ::pocket_seq_* / ::pocket_gen_* / ::pocket_kit_* : Pocket's seq/, compiled as C
#include "pocket_gen.h"
#include "pocket_kit.h"
#include "check.h"

static const int BLOCKS = 44140 * 20 / 64; // 20 s
static const int FFT = 1024;

static void fft(std::vector<double> &re, std::vector<double> &im) {
  const int n = (int)re.size();
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
  }
  for (int len = 2; len <= n; len <<= 1) {
    double a = -2 * M_PI / len;
    for (int i = 0; i < n; i += len)
      for (int k = 0; k < len / 2; ++k) {
        double wr = cos(a * k), wi = sin(a * k);
        double xr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
        double xi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
        re[i + k + len / 2] = re[i + k] - xr; im[i + k + len / 2] = im[i + k] - xi;
        re[i + k] += xr; im[i + k] += xi;
      }
  }
}

struct voice_stats {
  double sig = 0, err = 0;
  std::vector<double> env_a, env_b;     // 10 ms RMS frames
  std::vector<double> spec_a, spec_b;   // averaged power spectrum
  std::vector<int16_t> buf_a, buf_b;    // pending samples
  double frame_a = 0, frame_b = 0;
  int frame_n = 0;
  voice_stats() : spec_a(FFT / 2, 0.0), spec_b(FFT / 2, 0.0) {}
  void add(const int16_t *a, const int16_t *b) {
    for (int i = 0; i < 64; ++i) {
      double d = (double)a[i] - b[i];
      sig += (double)a[i] * a[i];
      err += d * d;
      frame_a += (double)a[i] * a[i];
      frame_b += (double)b[i] * b[i];
      if (++frame_n == 441) {
        env_a.push_back(sqrt(frame_a / 441));
        env_b.push_back(sqrt(frame_b / 441));
        frame_a = frame_b = 0;
        frame_n = 0;
      }
      buf_a.push_back(a[i]);
      buf_b.push_back(b[i]);
    }
    if ((int)buf_a.size() >= FFT) {
      std::vector<double> ra(FFT), ia(FFT, 0.0), rb(FFT), ib(FFT, 0.0);
      for (int i = 0; i < FFT; ++i) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / FFT);
        ra[i] = buf_a[i] * w;
        rb[i] = buf_b[i] * w;
      }
      fft(ra, ia);
      fft(rb, ib);
      for (int k = 0; k < FFT / 2; ++k) {
        spec_a[k] += ra[k] * ra[k] + ia[k] * ia[k];
        spec_b[k] += rb[k] * rb[k] + ib[k] * ib[k];
      }
      buf_a.erase(buf_a.begin(), buf_a.begin() + FFT);
      buf_b.erase(buf_b.begin(), buf_b.begin() + FFT);
    }
  }
};

static void send_kit(const uint8_t *kit) {
  for (int p = 1; p < 247; ++p) {
    ::pocket_lxr_set_param(p, kit[p]);
    c12::pocket_lxr_set_param(p, kit[p]);
  }
}

// The generator and the kit designer: CLATTER 12's copies (inside the generated C++) against
// CLATTER Pocket's own C, for every style and kit character over many seeds. Must be identical.
static void generators() {
  static ::pocket_pattern pa;
  static c12::pocket_pattern pb;
  int pat_same = 0, pat_n = 0, mut_same = 0, kit_same = 0, kit_n = 0;
  for (unsigned style = 0; style < 11; ++style)
    for (uint32_t seed = 1; seed <= 40; ++seed)
      for (unsigned pages = 0; pages < 4; pages += 3) {
        ::pocket_gen_settings ga = {seed, (uint8_t)(seed * 7 % 101), (uint8_t)(seed * 13 % 101), (uint8_t)style, 0, (uint8_t)pages};
        c12::pocket_gen_settings gb = {seed, ga.density, ga.complexity, (uint8_t)style, 0, (uint8_t)pages};
        memset(&pa, 0, sizeof pa);
        memset(&pb, 0, sizeof pb);
        ::pocket_gen_generate(&pa, &ga);
        c12::pocket_gen_generate(&pb, &gb);
        pat_same += memcmp(&pa, &pb, sizeof pa) == 0;
        ::pocket_gen_mutate(&pa, seed * 3, seed % 3, 0);
        c12::pocket_gen_mutate(&pb, seed * 3, seed % 3, 0);
        mut_same += memcmp(&pa, &pb, sizeof pa) == 0;
        pat_n++;
      }
  static uint8_t ka[256], kb[256];
  for (unsigned ch = 0; ch < 16; ++ch)
    for (uint32_t seed = 0; seed < 30; ++seed)
      for (unsigned wild = 0; wild <= 100; wild += 50) {
        for (int p = 0; p < 256; ++p) ka[p] = kb[p] = (uint8_t)(p < 247 ? ::pocket_lxr_init_kit_value(p) : 0);
        ::pocket_kit_generate(ka, ch, seed, wild);
        c12::pocket_kit_generate(kb, ch, seed, wild);
        kit_same += memcmp(ka, kb, 256) == 0;
        kit_n++;
      }
  printf("generators: %d/%d patterns, %d/%d mutations, %d/%d kits identical to CLATTER Pocket's\n", pat_same, pat_n,
         mut_same, pat_n, kit_same, kit_n);
  CHECK(pat_same == pat_n, "patterns");
  CHECK(mut_same == pat_n, "mutations");
  CHECK(kit_same == kit_n, "kits");
}

int main() {
  generators();
  static uint8_t kit[256];
  static int32_t qa[128], qb[128];
  static uint32_t tables[C12_TABLE_BYTES / 4];
  c12::c12_tables_bind(tables);
  ::pocket_lxr_init(3);
  c12::pocket_lxr_init(3);
  ::pocket_lxr_set_dc_block(1);
  c12::pocket_lxr_set_dc_block(1);
  uint32_t rng = 1;
  std::vector<voice_stats> vs(6);
  double mix_sig = 0, mix_err = 0;
  int kits = 0;
  for (int blk = 0; blk < BLOCKS; ++blk) {
    if (blk % (44140 * 4 / 64) == 0) { // a new designed kit every 4 s (Pocket's own designer)
      c12::c12_kit_design(kit, kits * 5 % 16, 1000u + (unsigned)kits, 50);
      send_kit(kit);
      kits++;
    }
    if (blk % 6 == 0) { // a hit every 8.7 ms, the seven triggers with varied velocity, pitch, variation
      rng = rng * 1664525u + 1013904223u;
      int trig = (int)((rng >> 8) % 7), vel = 30 + (int)((rng >> 16) % 98), note = 48 + (int)((rng >> 4) % 25);
      int var = (int)((rng >> 24) % 3);
      ::pocket_lxr_trigger_variation(trig, vel, note, var);
      c12::pocket_lxr_trigger_variation(trig, vel, note, var);
    }
    ::pocket_lxr_render_q23(qa, 64, 0);
    c12::pocket_lxr_render_q23(qb, 64, 0);
    for (int i = 0; i < 128; ++i) {
      double d = (double)qa[i] - qb[i];
      mix_sig += (double)qa[i] * qa[i];
      mix_err += d * d;
    }
    for (int v = 0; v < 6; ++v) {
      float l, r;
      vs[v].add(::pocket_lxr_voice_block(v, &l, &r), c12::pocket_lxr_voice_block(v, &l, &r));
    }
  }
  printf("authentic: 20 s at 44 140 Hz, %d Pocket-designed kits, all seven triggers with variations,\n"
         "  CLATTER 12 (C++, single precision, platform libm) against CLATTER Pocket (C, double, CLATTER libm)\n",
         kits);
  printf("  mix sample-level signal/difference %.1f dB (informational)\n", 10 * log10(mix_sig / mix_err));
  static const char *const names[6] = {"DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "HI-HAT"};
  for (int v = 0; v < 6; ++v) {
    voice_stats &s = vs[v];
    std::vector<double> d;
    for (size_t i = 0; i < s.env_a.size(); ++i)
      if (s.env_a[i] > 33 && s.env_b[i] > 33) d.push_back(fabs(20 * log10(s.env_a[i] / s.env_b[i])));
    std::sort(d.begin(), d.end());
    double env95 = d.empty() ? 0 : d[(size_t)(d.size() * 0.95)], envmax = d.empty() ? 0 : d.back();
    // third-octave bands from 40 Hz to 20 kHz
    double worst = 0;
    int bands = 0;
    for (double lo = 40; lo < 20000; lo *= pow(2.0, 1.0 / 3)) {
      double hi = lo * pow(2.0, 1.0 / 3), ea = 0, eb = 0;
      for (int k = 1; k < FFT / 2; ++k) {
        double f = k * 44140.0 / FFT;
        if (f >= lo && f < hi) { ea += s.spec_a[k]; eb += s.spec_b[k]; }
      }
      if (ea <= 0 || eb <= 0) continue;
      double total = 0;
      for (int k = 1; k < FFT / 2; ++k) total += s.spec_a[k];
      if (ea < total * 1e-4) continue; // bands within 40 dB of the voice's total
      worst = std::max(worst, fabs(10 * log10(ea / eb)));
      bands++;
    }
    double snr = 10 * log10(s.sig / (s.err > 0 ? s.err : 1e-30));
    printf("  %-7s envelope: 95%% of 10 ms frames within %.2f dB, worst %.2f dB | spectrum: worst of %2d third-octave bands %.2f dB | samples %.1f dB\n",
           names[v], env95, envmax, bands, worst, snr);
    CHECK(env95 < 0.5, "%s envelope 95th percentile %.2f dB", names[v], env95);
    CHECK(envmax < 3.0, "%s envelope worst frame %.2f dB", names[v], envmax);
    CHECK(worst < 0.5, "%s spectrum worst band %.2f dB", names[v], worst);
  }
  return CHECK_REPORT("authentic");
}
