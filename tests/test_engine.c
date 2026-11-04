/*
 * test_engine.c - CLATTER 12's engine against the LXR at its own rate.
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 *
 * Linked with build/c12_unity.c compiled natively as C (the same unity unit the panel's C++ is
 * generated from). Checks that converting to 32 kHz keeps the LXR's pitch, level and envelope
 * timing, that the sequencer keeps time at 32 kHz, and that the converter never runs dry.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "c12_engine.h"
#include "pocket_lxr.h"
#include "pocket_seq.h"

#define OUT_RATE 32000u
#define SECONDS 2

static int32_t refQ23[C12_LXR_RATE * SECONDS * 2 + 128];
static int16_t outPcm[OUT_RATE * 6 * 2];

struct shape { double peak, f0, t20; };

/* peak, fundamental from zero crossings between 30 and 250 ms, time to fall 20 dB below peak */
static struct shape analyse(const float* x, unsigned n, double rate)
{
	struct shape s = {0, 0, 0};
	unsigned ipk = 0;
	for (unsigned i = 0; i < n; i++)
		if (fabs(x[i]) > s.peak) { s.peak = fabs(x[i]); ipk = i; }
	/* envelope: max |x| over 5 ms windows */
	const unsigned w = (unsigned)(rate * 0.005);
	for (unsigned i = ipk; i + w < n; i += w)
	{
		double m = 0;
		for (unsigned k = 0; k < w; k++) m = fmax(m, fabs(x[i + k]));
		if (m < s.peak * 0.1) { s.t20 = (double)i / rate; break; }
	}
	const unsigned a = (unsigned)(rate * 0.030), b = (unsigned)(rate * 0.250);
	int crossings = 0;
	double first = -1, last = -1;
	for (unsigned i = a + 1; i < b && i < n; i++)
		if (x[i - 1] < 0 && x[i] >= 0)
		{
			double t = (i - 1 + (-x[i - 1]) / (x[i] - x[i - 1])) / rate;
			if (first < 0) first = t;
			last = t;
			crossings++;
		}
	if (crossings > 1) s.f0 = (crossings - 1) / (last - first);
	return s;
}

static float* to_float_left_q23(const int32_t* q, unsigned frames)
{
	float* f = malloc(frames * sizeof(float));
	for (unsigned i = 0; i < frames; i++) f[i] = (float)q[2 * i] / 256.f;
	return f;
}
static float* to_float_left_16(const int16_t* q, unsigned frames)
{
	float* f = malloc(frames * sizeof(float));
	for (unsigned i = 0; i < frames; i++) f[i] = (float)q[2 * i];
	return f;
}

/* An independent reference for the conversion: the 44 140 Hz render evaluated at each 32 kHz output
 * instant with a long double-precision windowed sinc (511 taps, cutoff 15.5 kHz), at the same
 * instants c12_resampler uses (its group delay is 15 input samples). */
static double ideal_at(const float* x, unsigned n, double t)
{
	const int half = 255;
	const double fc = 15500.0 / C12_LXR_RATE;
	const long c = (long)floor(t);
	double acc = 0, wsum = 0;
	for (long k = c - half; k <= c + half; k++)
	{
		double d = t - (double)k;
		double w = 0.42 + 0.5 * cos(M_PI * d / (half + 1)) + 0.08 * cos(2 * M_PI * d / (half + 1)); /* Blackman */
		double s = fabs(d) < 1e-12 ? 2 * fc : sin(2 * M_PI * fc * d) / (M_PI * d);
		double h = s * w;
		wsum += h;
		if (k >= 0 && k < (long)n) acc += h * x[k];
	}
	return acc / wsum;
}

/* 255-tap low-pass at 11 kHz for 32 kHz signals: the comparison band (well inside both filters'
 * passbands, so it measures the conversion, not where the two filters start to roll off). */
static void lowpass11k(const double* in, double* out, unsigned n)
{
	enum { H = 127 };
	static double h[2 * H + 1];
	const double fc = 11000.0 / OUT_RATE;
	double sum = 0;
	for (int k = -H; k <= H; k++)
	{
		double w = 0.42 + 0.5 * cos(M_PI * k / (H + 1)) + 0.08 * cos(2 * M_PI * k / (H + 1));
		h[k + H] = (k == 0 ? 2 * fc : sin(2 * M_PI * fc * k) / (M_PI * k)) * w;
		sum += h[k + H];
	}
	for (unsigned i = 0; i < n; i++)
	{
		double a = 0;
		for (int k = -H; k <= H; k++)
		{
			long j = (long)i - k;
			if (j >= 0 && j < (long)n) a += h[k + H] * in[j];
		}
		out[i] = a / sum;
	}
}

static const char* const trigName[] = {"DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "CL HAT", "OP HAT"};

static void one_hit(int trig)
{
	/* reference: the LXR at 44 140 Hz, as CLATTER Pocket renders it */
	pocket_lxr_init(7);
	pocket_lxr_set_dc_block(1);
	pocket_lxr_set_master(C12_MASTER_Q12);
	pocket_lxr_trigger(trig, 127, 60);
	const unsigned refFrames = C12_LXR_RATE * SECONDS / 64 * 64;
	for (unsigned i = 0; i < refFrames; i += 64) pocket_lxr_render_q23(&refQ23[2 * i], 64, 0);
	/* CLATTER 12: the same, converted to 32 kHz */
	c12_engine_init(OUT_RATE, 7);
	pocket_lxr_trigger(trig, 127, 60);
	const unsigned outFrames = OUT_RATE * SECONDS;
	for (unsigned i = 0; i < outFrames; i += 64) c12_engine_render(&outPcm[2 * i], 64);

	float* r = to_float_left_q23(refQ23, refFrames);
	float* o = to_float_left_16(outPcm, outFrames);
	struct shape sr = analyse(r, refFrames, C12_LXR_RATE);
	struct shape so = analyse(o, outFrames, OUT_RATE);
	/* what running the unchanged LXR straight at 32 kHz would do: the same samples, slower */
	struct shape naive = analyse(r, refFrames, OUT_RATE);

	const unsigned n = OUT_RATE; /* the first second */
	double* ideal = malloc(n * sizeof(double));
	double* got = malloc(n * sizeof(double));
	double* li = malloc(n * sizeof(double));
	double* lo = malloc(n * sizeof(double));
	for (unsigned i = 0; i < n; i++)
	{
		ideal[i] = ideal_at(r, refFrames, (double)i * C12_LXR_RATE / OUT_RATE + 15.0);
		got[i] = o[i];
	}
	lowpass11k(ideal, li, n);
	lowpass11k(got, lo, n);
	double es = 0, ee = 0;
	for (unsigned i = 0; i < n; i++) { es += li[i] * li[i]; ee += (li[i] - lo[i]) * (li[i] - lo[i]); }
	const double snr = 10 * log10(es / (ee > 0 ? ee : 1e-30));
	printf("  %-7s 0-11 kHz match with an ideal conversion of the 44.14 kHz LXR: %5.1f dB SNR | peak %5.0f/%5.0f",
	       trigName[trig], snr, sr.peak, so.peak);
	if (trig <= POCKET_TRIG_DRUM3)
		printf(", f0 %.2f/%.2f Hz, -20 dB %.0f/%.0f ms (LXR clocked at 32 kHz: %.2f Hz, %.0f ms)", sr.f0, so.f0,
		       sr.t20 * 1e3, so.t20 * 1e3, naive.f0, naive.t20 * 1e3);
	printf("\n");
	CHECK(snr > 45.0, "%s conversion SNR %.1f dB", trigName[trig], snr);
	if (trig <= POCKET_TRIG_DRUM3)
	{
		CHECK(fabs(so.peak / sr.peak - 1) < 0.03, "%s peak %.0f vs %.0f", trigName[trig], so.peak, sr.peak);
		CHECK(fabs(so.f0 / sr.f0 - 1) < 0.005, "%s f0 %.2f vs %.2f", trigName[trig], so.f0, sr.f0);
		CHECK(fabs(so.t20 - sr.t20) < 0.006, "%s decay %.1f vs %.1f ms", trigName[trig], so.t20 * 1e3, sr.t20 * 1e3);
	}
	free(ideal); free(got); free(li); free(lo);
	free(r);
	free(o);
}

static void sequencer_timing(void)
{
	c12_engine_init(OUT_RATE, 9);
	struct pocket_pattern* p = pocket_seq_edit_begin();
	memset(p, 0, sizeof *p);
	for (int s = 0; s < 16; s += 4) p->cell[POCKET_TRIG_DRUM1][s] = POCKET_CELL(127, 0);
	pocket_seq_edit_commit();
	pocket_seq_set_tempo(120);
	pocket_seq_play(1);
	const unsigned frames = OUT_RATE * 4;
	for (unsigned i = 0; i < frames; i += 64) c12_engine_render(&outPcm[2 * i], 64);
	write_wav("build/test/seq_kick_120bpm.wav", outPcm, frames, OUT_RATE);
	/* onsets: |x| rises above 8000 after 50 ms below 4000 (the kick peaks near 14000) */
	double onsets[16];
	int n = 0;
	unsigned quiet = OUT_RATE;
	int armed = 1;
	for (unsigned i = 0; i < frames && n < 16; i++)
	{
		int v = abs(outPcm[2 * i]);
		if (quiet > OUT_RATE / 20) armed = 1;
		if (v > 8000 && armed) { onsets[n++] = (double)i / OUT_RATE; armed = 0; }
		quiet = v < 4000 ? quiet + 1 : 0;
	}
	printf("  sequencer: %u triggers, %d onsets in 4 s at 120 BPM:", pocket_seq_triggers(), n);
	for (int i = 0; i < n; i++) printf(" %.4f", onsets[i]);
	printf("\n");
	CHECK(n == 8, "onsets %d, expected 8", n);
	/* Steps land on the LXR's 64-sample block boundaries (1.45 ms), as on the LXR-02, so each
	 * interval is within one block of 500 ms. The first is skipped: that kick starts from silence and
	 * reaches the threshold sooner than a retriggered one, which says nothing about timing. */
	for (int i = 2; i < n; i++)
		CHECK(fabs(onsets[i] - onsets[i - 1] - 0.5) < 64.0 / C12_LXR_RATE + 1e-4, "interval %d: %.4f s", i, onsets[i] - onsets[i - 1]);
	CHECK(fabs(onsets[n - 1] - onsets[1] - 0.5 * (n - 2)) < 64.0 / C12_LXR_RATE + 1e-4, "no drift: %.4f s over %d beats",
	      onsets[n - 1] - onsets[1], n - 2);
	struct c12_engine_stats st;
	c12_engine_stats_read(&st);
	double ratio = (double)st.lxr_blocks / st.out_blocks;
	printf("  blocks: %u out, %u LXR (ratio %.4f, expected %.4f), max %u LXR per out, underruns %u\n", st.out_blocks,
	       st.lxr_blocks, ratio, 44140.0 / 32000.0, st.lxr_max_per_out, st.underruns);
	CHECK(st.underruns == 0, "underruns %u", st.underruns);
	CHECK(st.lxr_max_per_out <= 2, "max per out %u", st.lxr_max_per_out);
	CHECK(fabs(ratio - 44140.0 / 32000.0) < 0.002, "ratio %.4f", ratio);
}

/* The engine may be asked for any block size up to 128 frames (Plinky's audio callback: 64; a browser's
 * audio worklet: 128). The audio must not depend on it. (The first version ran out of converter buffer at
 * 128 and went silent for good: nothing tested any block size but 64.) */
static void block_size_independence(void)
{
	static int16_t a[OUT_RATE * 2 * 2], b[OUT_RATE * 2 * 2], c[OUT_RATE * 2 * 2];
	const unsigned frames = OUT_RATE * 2;
	for (int variant = 0; variant < 3; variant++)
	{
		c12_engine_init(OUT_RATE, 21);
		struct pocket_pattern* p = pocket_seq_edit_begin();
		memset(p, 0, sizeof *p);
		for (int t = 0; t < POCKET_SEQ_TRACKS; t++)
			for (int s = 0; s < 16; s += 2) p->cell[t][s] = POCKET_CELL(100, 0);
		pocket_seq_edit_commit();
		pocket_seq_set_tempo(150);
		pocket_seq_play(1);
		const unsigned chunk = variant == 0 ? 64 : (variant == 1 ? 128 : 96);
		int16_t* out = variant == 0 ? a : (variant == 1 ? b : c);
		for (unsigned i = 0; i < frames; i += chunk) c12_engine_render(&out[2 * i], chunk > frames - i ? frames - i : chunk);
		struct c12_engine_stats st;
		c12_engine_stats_read(&st);
		CHECK(st.underruns == 0, "block %u: %u underruns", chunk, st.underruns);
	}
	int diff128 = memcmp(a, b, sizeof a), diff96 = memcmp(a, c, sizeof a);
	printf("  block sizes 64, 96, 128 frames render identical audio: %s\n", (diff128 || diff96) ? "NO" : "yes");
	CHECK(diff128 == 0, "128-frame blocks differ from 64-frame blocks");
	CHECK(diff96 == 0, "96-frame blocks differ from 64-frame blocks");
}

static void all_voices_and_kits(void)
{
	static uint8_t kit[C12_KIT_PARAMS];
	c12_engine_init(OUT_RATE, 11);
	struct pocket_pattern* p = pocket_seq_edit_begin();
	memset(p, 0, sizeof *p);
	for (int t = 0; t < POCKET_SEQ_TRACKS; t++)
		for (int s = 0; s < 16; s++) p->cell[t][s] = POCKET_CELL(100, 0);
	pocket_seq_edit_commit();
	pocket_seq_set_tempo(140);
	pocket_seq_play(1);
	const unsigned frames = OUT_RATE * 6;
	unsigned kitsLeftMax = 0;
	int kitNo = 0;
	for (unsigned i = 0; i < frames; i += 64)
	{
		if (i % (OUT_RATE * 2) == 0)
		{
			c12_kit_design(kit, kitNo, 100u + (unsigned)kitNo, 50);
			c12_kit_set_target(kit, C12_KIT_PARAMS);
			kitNo += 3;
		}
		if (i % 128 == 0) /* a UI frame every 4 ms */
		{
			unsigned left = c12_kit_pump(48);
			if (left > kitsLeftMax) kitsLeftMax = left;
		}
		c12_engine_render(&outPcm[2 * i], 64);
	}
	write_wav("build/test/all_voices_kits.wav", outPcm, frames, OUT_RATE);
	int finite = 1, loud = 0;
	for (unsigned i = 0; i < frames * 2; i++)
	{
		if (abs(outPcm[i]) > 1000) loud++;
	}
	struct c12_engine_stats st;
	c12_engine_stats_read(&st);
	printf("  all seven triggers every 16th, three kits: clipped %u of %u samples, underruns %u, kit backlog max %u\n",
	       st.clipped, frames * 2, st.underruns, kitsLeftMax);
	CHECK(finite, "finite");
	CHECK(st.clipped < frames * 2 / 1000, "clipping %u samples", st.clipped);
	CHECK(loud > (int)frames / 4, "audible (%d loud samples)", loud);
	CHECK(st.underruns == 0, "underruns %u", st.underruns);
	CHECK(c12_kit_pump(1000) == 0, "kit fully sent");
}

static uint32_t tableMem[C12_TABLE_BYTES / 4];

int main(void)
{
	c12_tables_bind(tableMem);
	one_hit(POCKET_TRIG_DRUM1);
	one_hit(POCKET_TRIG_DRUM2);
	one_hit(POCKET_TRIG_DRUM3);
	one_hit(POCKET_TRIG_SNARE);
	one_hit(POCKET_TRIG_CYMBAL);
	one_hit(POCKET_TRIG_CLOSED_HAT);
	one_hit(POCKET_TRIG_OPEN_HAT);
	sequencer_timing();
	block_size_independence();
	all_voices_and_kits();
	return CHECK_REPORT("engine");
}
