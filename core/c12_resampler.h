/*
 * c12_resampler.h - stereo sample-rate converter from the LXR's own rate to the output's.
 *
 * Copyright (C) 2026 The CLATTER contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Why it exists: the LXR-02 firmware's coefficients are written for REAL_FS = 44 140 Hz, and only
 * some of them are derived from REAL_FS (oscillator increments, LFO rates, one-pole filters).
 * Envelope slopes, filter cutoffs (a fraction of the sample rate), the decimator and the recorded
 * transient samples are per-sample quantities. Run at the Plinky 12's 32 kHz they would all
 * change: envelopes 38 % longer, transients a fifth lower. So CLATTER 12 runs the LXR at its own
 * rate, unchanged, and converts its stereo mix to the output rate here.
 *
 * Method: windowed-sinc (Kaiser, beta 6.8, about 70 dB stopband) interpolation, 32 taps at the
 * input rate, 64 tabulated phases linearly interpolated, -6 dB at the output Nyquist frequency
 * (16 kHz for 32 kHz output). Position is kept as an exact rational (in_rate / out_rate) so it
 * never drifts. Group delay: 15.5 input samples (0.35 ms).
 *
 * Real-time: no allocation; the table is built by c12_rs_init (control context, before audio).
 */
#ifndef C12_RESAMPLER_H
#define C12_RESAMPLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define C12_RS_TAPS 32
#define C12_RS_PHASES 64
/* Input frames held: the filter span plus up to two LXR blocks rendered ahead of one output
 * block, with room to spare. */
#define C12_RS_CAP 256

struct c12_resampler {
	uint32_t in_rate, out_rate;
	uint32_t frac;   /* 0..out_rate-1: position between buf[pos] and buf[pos+1], in 1/out_rate */
	uint32_t pos;    /* first input frame of the next output's window */
	uint32_t count;  /* valid input frames in buf */
	float buf[2][C12_RS_CAP];
	float table[C12_RS_PHASES + 1][C12_RS_TAPS];
};

/* Control context. `cutoff` is the -6 dB point as a fraction of the output Nyquist frequency
 * (1.0 = the output Nyquist frequency). Returns 0, or -1 for rates it cannot convert. */
int c12_rs_init(struct c12_resampler* rs, uint32_t in_rate, uint32_t out_rate, float cutoff);

/* Input frames that must be available (count) before c12_rs_process can produce `frames`. */
uint32_t c12_rs_needed(const struct c12_resampler* rs, unsigned frames);
/* Room for more input. */
static inline uint32_t c12_rs_room(const struct c12_resampler* rs) { return C12_RS_CAP - rs->count; }
/* Append `frames` interleaved stereo frames (any scale). The caller checks c12_rs_room first. */
void c12_rs_push(struct c12_resampler* rs, const float* lr, unsigned frames);
/* Produce `frames` interleaved stereo output frames. The caller ensures
 * rs->count >= c12_rs_needed(rs, frames). Consumed input is discarded. */
void c12_rs_process(struct c12_resampler* rs, float* out_lr, unsigned frames);

#ifdef __cplusplus
}
#endif

#endif
