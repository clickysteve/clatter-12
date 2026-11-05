/*
 * c12_resampler.c - see c12_resampler.h.
 *
 * Copyright (C) 2026 The CLATTER contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <string.h>
#include "c12_resampler.h"
#include "Soundengine/config.h" /* IN_ITCM_RAM: see c12_engine.c */

#define C12_RS_BETA 6.8f
#define C12_RS_HALF (C12_RS_TAPS / 2)
_Static_assert(C12_RS_TAPS % 4 == 0, "the window is applied four taps at a time");

/* Zeroth-order modified Bessel function of the first kind, by its power series. */
static float bessel_i0(float x)
{
	float sum = 1.f, term = 1.f;
	const float q = x * x * 0.25f;
	for (int k = 1; k < 32; k++)
	{
		term *= q / (float)(k * k);
		sum += term;
		if (term < sum * 1e-9f) break;
	}
	return sum;
}

int c12_rs_init(struct c12_resampler* rs, uint32_t in_rate, uint32_t out_rate, float cutoff)
{
	if (in_rate == 0 || out_rate == 0 || in_rate > 4u * out_rate || out_rate > 4u * in_rate) return -1;
	memset(rs, 0, sizeof *rs);
	rs->in_rate = in_rate;
	rs->out_rate = out_rate;
	/* cutoff in cycles per input sample: the output's Nyquist frequency when converting down (the
	 * panel's case), the input's when converting up (a browser that will not give 32 kHz) */
	const float fc = 0.5f * cutoff * (out_rate < in_rate ? (float)out_rate / (float)in_rate : 1.f);
	const float i0beta = bessel_i0(C12_RS_BETA);
	for (int p = 0; p <= C12_RS_PHASES; p++)
	{
		/* phase p: the output instant lies p/PHASES of a sample after tap HALF-1 */
		const float f = (float)p / (float)C12_RS_PHASES;
		float sum = 0.f;
		for (int k = 0; k < C12_RS_TAPS; k++)
		{
			const float d = (float)k - (float)(C12_RS_HALF - 1) - f; /* tap time minus output time */
			const float x = d / (float)C12_RS_HALF;
			float w = 0.f;
			if (x > -1.f && x < 1.f) w = bessel_i0(C12_RS_BETA * sqrtf(1.f - x * x)) / i0beta;
			const float a = 2.f * (float)M_PI * fc * d;
			const float s = (d > -1e-6f && d < 1e-6f) ? 1.f : sinf(a) / a;
			rs->table[p][k] = 2.f * fc * s * w;
			sum += rs->table[p][k];
		}
		/* unity gain at DC for every phase, so the fractional position never modulates level */
		for (int k = 0; k < C12_RS_TAPS; k++) rs->table[p][k] /= sum;
	}
	return 0;
}

uint32_t c12_rs_needed(const struct c12_resampler* rs, unsigned frames)
{
	/* position of the last output's window start, plus the window */
	const uint64_t adv = (uint64_t)rs->frac + (uint64_t)(frames - 1u) * rs->in_rate;
	return rs->pos + (uint32_t)(adv / rs->out_rate) + C12_RS_TAPS;
}

void c12_rs_push(struct c12_resampler* rs, const float* lr, unsigned frames)
{
	for (unsigned i = 0; i < frames && rs->count < C12_RS_CAP; i++, rs->count++)
	{
		rs->buf[0][rs->count] = lr[2 * i];
		rs->buf[1][rs->count] = lr[2 * i + 1];
	}
}

/* One output frame: the two neighbouring tabulated phases interpolated tap by tap, and the window
 * applied to both channels in the same pass. The arithmetic is the first version's, operation for
 * operation and in the same order (coefficient a + (b - a) * t, then one running sum per channel,
 * tap 0 first), so the output is the same to the bit; it no longer stores the 32 interpolated
 * coefficients and reads them back, and the taps are written out four at a time. */
#define C12_RS_TAP(k)                                   \
	do {                                                \
		const float c = a[k] + (b[k] - a[k]) * t;       \
		accl += c * l[k];                               \
		accr += c * r[k];                               \
	} while (0)

IN_ITCM_RAM void c12_rs_process(struct c12_resampler* rs, float* out_lr, unsigned frames)
{
	const float scale = (float)C12_RS_PHASES / (float)rs->out_rate;
	const uint32_t in_rate = rs->in_rate, out_rate = rs->out_rate;
	uint32_t frac = rs->frac, pos = rs->pos;
	for (unsigned n = 0; n < frames; n++)
	{
		/* phase: frac/out_rate in [0,1) mapped onto the table, interpolated between rows */
		const float ph = (float)frac * scale;
		int row = (int)ph;
		if (row >= C12_RS_PHASES) row = C12_RS_PHASES - 1;
		const float t = ph - (float)row;
		const float* a = rs->table[row];
		const float* b = rs->table[row + 1];
		const float* l = &rs->buf[0][pos];
		const float* r = &rs->buf[1][pos];
		float accl = 0.f, accr = 0.f;
		for (int k = 0; k < C12_RS_TAPS; k += 4)
		{
			C12_RS_TAP(k);
			C12_RS_TAP(k + 1);
			C12_RS_TAP(k + 2);
			C12_RS_TAP(k + 3);
		}
		out_lr[2 * n] = accl;
		out_lr[2 * n + 1] = accr;
		frac += in_rate;
		while (frac >= out_rate)
		{
			frac -= out_rate;
			pos++;
		}
	}
	rs->frac = frac;
	rs->pos = pos;
	/* drop what no later output can reach */
	const uint32_t keep = rs->count > rs->pos ? rs->count - rs->pos : 0;
	if (rs->pos)
	{
		memmove(&rs->buf[0][0], &rs->buf[0][rs->pos], keep * sizeof(float));
		memmove(&rs->buf[1][0], &rs->buf[1][rs->pos], keep * sizeof(float));
	}
	rs->count = keep;
	rs->pos = 0;
}
