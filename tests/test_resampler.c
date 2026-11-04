/*
 * test_resampler.c - the 44 140 -> 32 000 Hz converter: gain, alias rejection, exact rate.
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "check.h"
#include "c12_resampler.h"

static struct c12_resampler rs;

/* Convert a sine at `hz` (input rate 44140) and return the output RMS of the bin at `probe_hz`,
 * measured by correlation over the last second, relative to the input amplitude. */
static double tone_response(double hz, double probe_hz, unsigned out_rate)
{
	c12_rs_init(&rs, 44140, out_rate, 1.0f);
	enum { SECONDS = 2 };
	static float out[128 * 2];
	static float in[64 * 2];
	double phase = 0, si = 0, co = 0;
	unsigned n_out = 0;
	const unsigned total = SECONDS * out_rate;
	while (n_out < total)
	{
		while (rs.count < c12_rs_needed(&rs, 64))
		{
			for (int i = 0; i < 64; i++)
			{
				float v = (float)sin(phase);
				phase += 2 * M_PI * hz / 44140.0;
				in[2 * i] = v;
				in[2 * i + 1] = -v;
			}
			c12_rs_push(&rs, in, 64);
		}
		c12_rs_process(&rs, out, 64);
		for (int i = 0; i < 64; i++, n_out++)
		{
			if (n_out < out_rate) continue; /* settle */
			double t = (double)n_out / out_rate;
			si += out[2 * i] * sin(2 * M_PI * probe_hz * t);
			co += out[2 * i] * cos(2 * M_PI * probe_hz * t);
		}
	}
	const double n = (double)(total - out_rate);
	return 2.0 * sqrt(si * si + co * co) / n; /* amplitude at probe_hz */
}

static double db(double a) { return 20.0 * log10(a > 1e-12 ? a : 1e-12); }

int main(void)
{
	/* passband */
	const double f100 = tone_response(100, 100, 32000);
	const double f1k = tone_response(1000, 1000, 32000);
	const double f8k = tone_response(8000, 8000, 32000);
	const double f12k = tone_response(12000, 12000, 32000);
	printf("  gain: 100 Hz %.3f dB, 1 kHz %.3f dB, 8 kHz %.3f dB, 12 kHz %.3f dB\n", db(f100), db(f1k), db(f8k), db(f12k));
	CHECK(fabs(db(f100)) < 0.05, "100 Hz gain %.3f dB", db(f100));
	CHECK(fabs(db(f1k)) < 0.05, "1 kHz gain %.3f dB", db(f1k));
	CHECK(fabs(db(f8k)) < 0.5, "8 kHz gain %.3f dB", db(f8k));
	CHECK(db(f12k) > -3.0, "12 kHz gain %.3f dB", db(f12k));
	/* aliases: an input tone above 16 kHz folds to 32000 - f */
	const double a19 = tone_response(19000, 13000, 32000);
	const double a20 = tone_response(20000, 12000, 32000);
	const double a22 = tone_response(22000, 10000, 32000);
	printf("  alias: 19 kHz->13 kHz %.1f dB, 20 kHz->12 kHz %.1f dB, 22 kHz->10 kHz %.1f dB\n", db(a19), db(a20), db(a22));
	CHECK(db(a19) < -40.0, "19 kHz alias %.1f dB", db(a19));
	CHECK(db(a20) < -55.0, "20 kHz alias %.1f dB", db(a20));
	CHECK(db(a22) < -60.0, "22 kHz alias %.1f dB", db(a22));

	/* exact rate: 44140 * 100 input frames give exactly 32000 * 100 outputs, no drift */
	c12_rs_init(&rs, 44140, 32000, 1.0f);
	static float in[64 * 2], out[64 * 2];
	memset(in, 0, sizeof in);
	unsigned long long pushed = 0, produced = 0;
	while (produced < 32000ull * 100)
	{
		while (rs.count < c12_rs_needed(&rs, 64)) { c12_rs_push(&rs, in, 64); pushed += 64; }
		c12_rs_process(&rs, out, 64);
		produced += 64;
	}
	/* input consumed = pushed - still buffered - the filter's look-ahead */
	const long long consumed = (long long)pushed - (long long)rs.count;
	const long long expect = (long long)(produced * 44140ull / 32000ull);
	CHECK(llabs(consumed - expect) <= 1, "consumed %lld input frames, expected %lld", consumed, expect);
	CHECK(rs.count <= C12_RS_CAP, "buffer %u", rs.count);
	/* other output rates (in case SAMPLE_FREQ is not 32000) */
	CHECK(c12_rs_init(&rs, 44140, 31250, 1.0f) == 0, "31250 Hz");
	CHECK(c12_rs_init(&rs, 44140, 44100, 1.0f) == 0, "44100 Hz");
	CHECK(c12_rs_init(&rs, 44140, 48000, 1.0f) != 0, "upsampling is refused");
	return CHECK_REPORT("resampler");
}
