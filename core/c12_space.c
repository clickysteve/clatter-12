/*
 * c12_space.c - see c12_space.h.
 *
 * Copyright (C) 2026 The CLATTER contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Per LXR block (64 frames at 44 140 Hz), when there is anything to do: the delay runs at the
 * LXR's rate (hats keep their top in the echoes); the reverb's send is low-passed (one pole near
 * 8 kHz) and averaged in pairs to 22 070 Hz, where the reverb runs, and its output is interpolated
 * back. Both are added to the bus before the master gain and the limiter (so the limiter sees them).
 *
 * Lines hold 16-bit samples at half scale (headroom for feedback), truncated towards zero so a
 * decaying tail reaches exactly zero: then, with no input, nothing runs (c12_space_process
 * returns 0) until a send moves again. No allocation, no division in the sample loops.
 */
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "c12_space.h"

const uint8_t c12_delay_ticks[C12_DELAY_DIVISIONS] = { 3, 4, 6, 8, 9, 12, 16, 18, 24, 32, 36, 48 };

#define FS2 22070.f   /* the reverb's rate */
#define FS1 44140.f   /* the delay's */
#define DMASK (C12_SPACE_DELAY_LINE - 1u)
#define STORE 0.5f   /* line scale */
#define VERB_IN 0.7f  /* into the network */
#define VERB_OUT 2.2f /* out of it: a full send at full wet about as loud as the dry hit */
#define LOAD 2.f

static int16_t* dline[2];
static int16_t* vline[4];
static int16_t* apline[2];
static uint32_t dw, vw, apw;

/* delay */
static float dTime, dTarget, dFb, dWet, dLpC, dHpC;
static float dLp[2], dHpLow[2];
static uint8_t dPing, dFreeze;
/* reverb */
static const float verbBase[4] = { 2861.f, 2477.f, 2129.f, 1789.f };
static const uint32_t apLen[2] = { 223u, 367u };
static float vLen[4], vTargetLen[4], vG[4], vDampC, vWet, vLp[4];
static uint8_t vFreeze;
static float vSize = -1.f, vT60;
/* in and out */
static float inLp;
static float lastOut[4];   /* the reverb's last half-rate output, left and right */
static uint32_t quiet;     /* samples (44 140 Hz) since anything non-zero was written */
static uint16_t pkDelay, pkVerb;
static int bound;

void c12_space_bind(void* mem)
{
	bound = 0;
	if (!mem) return;
	int16_t* p = (int16_t*)mem;
	dline[0] = p; p += C12_SPACE_DELAY_LINE;
	dline[1] = p; p += C12_SPACE_DELAY_LINE;
	for (int i = 0; i < 4; i++) { vline[i] = p; p += C12_SPACE_VERB_LINE; }
	for (int i = 0; i < 2; i++) { apline[i] = p; p += C12_SPACE_AP_LINE; }
	bound = 1;
	c12_space_clear();
}

int c12_space_bound(void) { return bound; }

void c12_space_clear(void)
{
	if (!bound) return;
	memset(dline[0], 0, C12_SPACE_BYTES);
	dw = vw = apw = 0;
	dLp[0] = dLp[1] = dHpLow[0] = dHpLow[1] = 0.f;
	for (int i = 0; i < 4; i++) { vLp[i] = 0.f; lastOut[i] = 0.f; }
	inLp = 0.f;
	quiet = 0xffffffffu;
	dTime = dTarget;
}

static float pole(float hz) { return 1.f - expf(-6.2831853f * hz / FS2); }
static float pole1(float hz) { return 1.f - expf(-6.2831853f * hz / FS1); }

void c12_space_set(const struct c12_space_params* p, int bpm)
{
	if (bpm < 30) bpm = 30;
	/* delay time: the division at this tempo, halved until the line holds it */
	const unsigned div = p->delay_division < C12_DELAY_DIVISIONS ? p->delay_division : 5u;
	float t = (float)c12_delay_ticks[div] * 2.5f * FS1 / (float)bpm;
	while (t > (float)(C12_SPACE_DELAY_LINE - 4u)) t *= 0.5f;
	if (t < 8.f) t = 8.f;
	dTarget = t;
	if (dTime <= 0.f) dTime = t;
	dFb = (float)(p->delay_feedback > 100 ? 100 : p->delay_feedback) * 0.0095f;
	dWet = (float)(p->delay_wet > 100 ? 100 : p->delay_wet) * 0.01f;
	const int f = p->delay_filter > 127 ? 127 : p->delay_filter;
	if (f < 64)
	{
		dLpC = pole1(400.f * powf(16000.f / 400.f, (float)f / 63.f));
		dHpC = pole1(20.f);
	}
	else
	{
		dLpC = 1.f;
		dHpC = pole1(20.f * powf(2500.f / 20.f, (float)(f - 64) / 63.f));
	}
	dPing = p->delay_pingpong ? 1 : 0;
	dFreeze = p->delay_freeze ? 1 : 0;
	/* reverb */
	const float size = 0.25f + 0.75f * (float)(p->verb_size > 100 ? 100 : p->verb_size) * 0.01f;
	const float t60 = 0.3f * powf(8.f / 0.3f, (float)(p->verb_decay > 100 ? 100 : p->verb_decay) * 0.01f);
	if (size != vSize || t60 != vT60)
	{
		for (int i = 0; i < 4; i++)
		{
			vTargetLen[i] = verbBase[i] * size;
			if (vSize < 0.f) vLen[i] = vTargetLen[i];
			/* each pass round the line loses 60 dB in t60 */
			vG[i] = powf(10.f, -3.f * vTargetLen[i] / (t60 * FS2));
		}
		vSize = size;
		vT60 = t60;
	}
	vDampC = pole(10000.f * powf(1200.f / 10000.f, (float)(p->verb_damp > 100 ? 100 : p->verb_damp) * 0.01f));
	vWet = (float)(p->verb_wet > 100 ? 100 : p->verb_wet) * 0.01f;
	vFreeze = p->verb_freeze ? 1 : 0;
}

/* saturate to the lines' 16 bits (towards zero, so a tail ends) */
static inline int16_t store_i(float v)
{
	int32_t i = (int32_t)(v * STORE);
	return (int16_t)(i > 32767 ? 32767 : i < -32767 ? -32767 : i);
}
static inline int16_t sat_line(float v)
{
	int32_t i = (int32_t)v;
	return (int16_t)(i > 32767 ? 32767 : i < -32767 ? -32767 : i);
}
static inline float absf(float x) { return x < 0.f ? -x : x; }

static uint32_t nzAll;

/* The delay over a block: the read position glides to its target over the block (a change of
 * time sounds as tape does), the integer part and fraction worked out once a sample. */
static void delay_block(const float* in, int any, int32_t* outL, int32_t* outR, unsigned frames, float* peak)
{
	const float start = dTime;
	float step = 0.f;
	if (!dFreeze)
	{
		const float d = dTarget - dTime;
		if (d > 0.5f || d < -0.5f) step = d * 0.096f / (float)frames; /* about 15 ms to settle */
		else dTime = dTarget;
	}
	float back = dFreeze ? (float)(uint32_t)(dTime + 0.5f) : dTime;
	const int16_t* L0 = dline[0];
	const int16_t* L1 = dline[1];
	uint32_t w = dw, nz = 0;
	float pk = *peak;
	const float wet = dWet * LOAD, fb = dFb, lpc = dLpC, hpc = dHpC;
	float lp0 = dLp[0], lp1 = dLp[1], hl0 = dHpLow[0], hl1 = dHpLow[1];
	for (unsigned n = 0; n < frames; n++)
	{
		const uint32_t ib = (uint32_t)back;
		const float fr = back - (float)ib;
		const uint32_t i0 = (w - ib) & DMASK, i1 = (i0 - 1u) & DMASK;
		const float a0 = (float)L0[i0], y0 = a0 + ((float)L0[i1] - a0) * fr; /* line units */
		float y1 = y0;
		if (dPing)
		{
			const float b0 = (float)L1[i0];
			y1 = b0 + ((float)L1[i1] - b0) * fr;
		}
		const float x = any ? in[n] : 0.f;
		float w0, w1;
		if (dFreeze)
		{
			/* frozen: round and round, unfiltered, nothing new in */
			w0 = (dPing ? y1 : y0) * LOAD;
			w1 = y0 * LOAD;
		}
		else
		{
			/* into each line, through the filter: the first echo filtered once, each repeat once
			   more (ping-pong: the left line takes the input and the right's return) */
			const float f0 = x + (dPing ? y1 : y0) * (LOAD * fb);
			lp0 += lpc * (f0 - lp0);
			hl0 += hpc * (lp0 - hl0);
			w0 = lp0 - hl0;
			w1 = 0.f;
			if (dPing)
			{
				const float f1 = y0 * (LOAD * fb);
				lp1 += lpc * (f1 - lp1);
				hl1 += hpc * (lp1 - hl1);
				w1 = lp1 - hl1;
			}
		}
		const int16_t s0 = store_i(w0), s1 = dPing ? store_i(w1) : 0;
		dline[0][w] = s0;
		dline[1][w] = s1;
		nz |= (uint32_t)(uint16_t)(s0 | s1);
		w = (w + 1u) & DMASK;
		const float oL = y0 * wet, oR = y1 * wet;
		outL[n] += (int32_t)oL;
		outR[n] += (int32_t)oR;
		if ((n & 3u) == 0)
		{
			const float a = absf(oL) + absf(oR);
			if (a > pk) pk = a;
		}
		back += step;
	}
	dw = w;
	if (!dFreeze) dTime = start + step * (float)frames;
	dLp[0] = lp0; dLp[1] = lp1; dHpLow[0] = hl0; dHpLow[1] = hl1;
	nzAll |= nz;
	*peak = pk;
}

/* a network line read: `ib` whole samples back (and one more, for the fraction) */
static inline float vread(const int16_t* line, uint32_t ib, float fr)
{
	const uint32_t n = C12_SPACE_VERB_LINE;
	const uint32_t i0 = vw >= ib ? vw - ib : vw + n - ib;
	const uint32_t i1 = i0 == 0 ? n - 1u : i0 - 1u;
	const float a = (float)line[i0];
	return a + ((float)line[i1] - a) * fr;
}

/* The reverb over a block, at half rate: two diffusers, then the network. */
static void verb_block(const float* in, int any, int32_t* outL, int32_t* outR, unsigned frames, float* peak)
{
	/* the lengths glide to a new size over a few blocks; within a block they stand still */
	uint32_t ib[4];
	float fr[4];
	for (int i = 0; i < 4; i++)
	{
		if (!vFreeze) vLen[i] += (vTargetLen[i] - vLen[i]) * 0.12f;
		const float l = vFreeze ? (float)(uint32_t)(vLen[i] + 0.5f) : vLen[i];
		ib[i] = (uint32_t)l;
		fr[i] = l - (float)ib[i];
	}
	const float c = 0.68f; /* the input's low-pass near 8 kHz at 44 140 Hz, before halving the rate */
	const float dampc = vFreeze ? 1.f : vDampC;
	float g0 = vFreeze ? 1.f : vG[0], g1 = vFreeze ? 1.f : vG[1], g2 = vFreeze ? 1.f : vG[2], g3 = vFreeze ? 1.f : vG[3];
	const float wet = vWet * VERB_OUT * LOAD;
	float lp = inLp, pk = *peak;
	float last0 = lastOut[0], last1 = lastOut[1];
	float d0 = vLp[0], d1 = vLp[1], d2 = vLp[2], d3 = vLp[3];
	uint32_t nz = 0;
	for (unsigned n = 0; n + 1u < frames; n += 2u)
	{
		float x = 0.f;
		if (any && !vFreeze)
		{
			lp += c * (in[n] - lp);
			const float h = lp;
			lp += c * (in[n + 1u] - lp);
			x = (h + lp) * 0.5f;
		}
		for (int a = 0; a < 2; a++)
		{
			const uint32_t r = (apw + C12_SPACE_AP_LINE - apLen[a]) & (C12_SPACE_AP_LINE - 1u);
			const float d = (float)apline[a][r] * LOAD;
			const float v = x + 0.62f * d;
			x = d - 0.62f * v;
			const int16_t sv = store_i(v);
			apline[a][apw] = sv;
			nz |= (uint32_t)(uint16_t)sv;
		}
		apw = (apw + 1u) & (C12_SPACE_AP_LINE - 1u);
		const float s0 = vread(vline[0], ib[0], fr[0]), s1 = vread(vline[1], ib[1], fr[1]);
		const float s2 = vread(vline[2], ib[2], fr[2]), s3 = vread(vline[3], ib[3], fr[3]);
		/* the damping low-pass in each line, then its loss (none frozen) */
		d0 += dampc * (s0 - d0);
		d1 += dampc * (s1 - d1);
		d2 += dampc * (s2 - d2);
		d3 += dampc * (s3 - d3);
		const float e0 = d0 * g0, e1 = d1 * g1, e2 = d2 * g2, e3 = d3 * g3;
		/* Hadamard, / 2: lossless, every line feeds every other. Line units: x * 0.5 for STORE */
		const float a0 = e0 + e1, a1 = e0 - e1, a2 = e2 + e3, a3 = e2 - e3;
		const float k = x * (VERB_IN * STORE);
		const int16_t v0 = sat_line((a0 + a2) * 0.5f + k), v1 = sat_line((a1 + a3) * 0.5f + k);
		const int16_t v2 = sat_line((a0 - a2) * 0.5f + k), v3 = sat_line((a1 - a3) * 0.5f + k);
		vline[0][vw] = v0;
		vline[1][vw] = v1;
		vline[2][vw] = v2;
		vline[3][vw] = v3;
		nz |= (uint32_t)(uint16_t)(v0 | v1 | v2 | v3);
		vw = vw + 1u >= C12_SPACE_VERB_LINE ? 0u : vw + 1u;
		const float o0 = (s0 + 0.6f * s2 - 0.3f * s3) * wet, o1 = (s1 + 0.6f * s3 - 0.3f * s2) * wet;
		/* back to 44 140 Hz: halfway, then the new sample */
		outL[n] += (int32_t)((o0 + last0) * 0.5f);
		outR[n] += (int32_t)((o1 + last1) * 0.5f);
		outL[n + 1u] += (int32_t)o0;
		outR[n + 1u] += (int32_t)o1;
		last0 = o0;
		last1 = o1;
		if ((n & 7u) == 0)
		{
			const float a = absf(o0) + absf(o1);
			if (a > pk) pk = a;
		}
	}
	inLp = any ? lp : 0.f;
	lastOut[0] = last0;
	lastOut[1] = last1;
	vLp[0] = d0; vLp[1] = d1; vLp[2] = d2; vLp[3] = d3;
	nzAll |= nz;
	*peak = pk;
}

int c12_space_process(const float* send_delay, const float* send_verb, int any_send, int32_t* outL, int32_t* outR, unsigned frames)
{
	if (!bound) return 0;
	/* nothing in, nothing left in any line: idle (frozen lines are never quiet) */
	if (!any_send && quiet > C12_SPACE_DELAY_LINE + 8u)
	{
		for (int k = 0; k < 4; k++) lastOut[k] = 0.f;
		return 0;
	}
	float pd = pkDelay, pv = pkVerb;
	nzAll = 0;
	delay_block(send_delay, any_send, outL, outR, frames, &pd);
	verb_block(send_verb, any_send, outL, outR, frames, &pv);
	if (nzAll) quiet = 0;
	else if (quiet < 0xfffffff0u) quiet += frames;
	pkDelay = (uint16_t)(pd > 32767.f ? 32767.f : pd);
	pkVerb = (uint16_t)(pv > 32767.f ? 32767.f : pv);
	return 1;
}

void c12_space_peaks(uint16_t* delay, uint16_t* verb)
{
	*delay = pkDelay;
	*verb = pkVerb;
	pkDelay = pkVerb = 0;
}
