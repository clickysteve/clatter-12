/*
 * c12_space.h - CLATTER 12's own delay and reverb ("SPACE"), fed by the mixer's per-track sends.
 *
 * Copyright (C) 2026 The CLATTER contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Not the LXR's: the LXR-02 firmware's FX bus (its delay, and a reverb its release leaves out) is
 * not built into CLATTER 12. These are CLATTER 12's, written for a small budget:
 *
 *   delay   tempo-synchronised (twelve divisions, from the sequencer's tempo), feedback, one
 *           bipolar filter in the feedback path (left of centre low-pass, right high-pass), wet,
 *           mono or ping-pong, freeze (holds what is in the line, takes no more input).
 *   reverb  a four-line feedback delay network (Hadamard matrix) behind two allpass diffusers:
 *           size, decay (T60 0.3-8 s), damping (a low-pass in each line), wet, freeze.
 *
 * The delay runs at the LXR's 44 140 Hz; the reverb at half that (22 070 Hz), as Plinky's own
 * reverb runs at half its rate. Their lines are 16-bit, in memory the caller binds: the delay's
 * 256 KB (PSRAM on the Plinky 12, read and written as one sequential stream), the reverb's 26 KB
 * (the panel's SRAM on the Plinky 12: six streams, scattered).
 * Audio context only, except c12_space_bind (control context, before the audio context starts).
 * When neither has input or anything left in its lines, c12_space_process returns at once.
 */
#ifndef C12_SPACE_H
#define C12_SPACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define C12_SPACE_DELAY_LINE 65536u   /* samples a channel at 44 140 Hz: 1.48 s */
#define C12_SPACE_VERB_LINE 3072u     /* each of the four network lines */
#define C12_SPACE_AP_LINE 512u        /* each of the two diffusers */
#define C12_SPACE_DELAY_BYTES (C12_SPACE_DELAY_LINE * 4u)                              /* 256 KB: PSRAM */
#define C12_SPACE_VERB_BYTES ((4u * C12_SPACE_VERB_LINE + 2u * C12_SPACE_AP_LINE) * 2u) /* 26 KB: SRAM */
#define C12_SPACE_BYTES (C12_SPACE_DELAY_BYTES + C12_SPACE_VERB_BYTES)

/* Delay divisions, in sixteenths x 6 (24 ppqn ticks): 1/32 1/16T 1/16 1/8T 1/16D 1/8 1/4T 1/8D 1/4 1/2T 1/4D 1/2 */
#define C12_DELAY_DIVISIONS 12
extern const uint8_t c12_delay_ticks[C12_DELAY_DIVISIONS];

struct c12_space_params {
	uint8_t delay_division; /* 0..C12_DELAY_DIVISIONS-1 */
	uint8_t delay_feedback; /* 0..100 */
	uint8_t delay_filter;   /* 0..127, 64 open */
	uint8_t delay_wet;      /* 0..100 */
	uint8_t delay_pingpong; /* 0 mono, 1 ping-pong */
	uint8_t verb_size;      /* 0..100 */
	uint8_t verb_decay;     /* 0..100 */
	uint8_t verb_damp;      /* 0..100 */
	uint8_t verb_wet;       /* 0..100 */
	uint8_t delay_freeze, verb_freeze;
};

/* The delay's lines (C12_SPACE_DELAY_BYTES) and the reverb's (C12_SPACE_VERB_BYTES), each 4-byte
 * aligned, or NULL (then the effects are silent). The delay is read and written as one sequential
 * stream (PSRAM is fine); the reverb as six scattered ones (keep it in SRAM). */
void c12_space_bind2(void* delay, void* verb);
/* Both in one block of C12_SPACE_BYTES, 4-byte aligned (the delay's first). */
void c12_space_bind(void* mem);
int c12_space_bound(void);
/* Audio context. New parameters (from the control context's bytes), at a block start. */
void c12_space_set(const struct c12_space_params* p, int bpm);
/* Audio context. A new tempo with the parameters unchanged (the delay time follows it). */
void c12_space_set_tempo(int bpm);
/* Audio context. `frames` (even) samples of the delay and reverb sends at 44 140 Hz in, the wet
 * stereo out, added to outL/outR (the mixer's 16-bit-scale integer bus). `any_delay`/`any_verb`:
 * whether anything is sent to each this block (when either is set both buffers are read). Each
 * effect with no input and only zeros in its lines does nothing. Returns 1 if either ran. */
int c12_space_process(const float* send_delay, const float* send_verb, int any_delay, int any_verb, int32_t* outL, int32_t* outR,
                      unsigned frames);
/* Which ran in the last c12_space_process (bit 0 the delay, bit 1 the reverb). */
int c12_space_busy(void);
/* Largest |wet| of the last blocks (0..32767), delay and reverb, for the meters; reset on read. */
void c12_space_peaks(uint16_t* delay, uint16_t* verb);
/* Clear the lines (control context, while the audio context is not running). */
void c12_space_clear(void);

#ifdef __cplusplus
}
#endif

#endif
