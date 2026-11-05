/*
 * c12_engine.c - see c12_engine.h.
 *
 * Copyright (C) 2026 The CLATTER contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Per LXR block (64 frames at 44 140 Hz), in the audio context, as stages (lxr_stage, below):
 *   1. pocket_lxr_render_q23's sequence of calls: queued events, the block hook (Pocket's
 *      sequencer, then CLATTER 12's ratchet strokes), and the six LXR voices, each kept by Pocket
 *      for inspection (Pocket's own Q23 output stage after them is not run);
 *   2. the mix stages: CLATTER 12's own mix of those six voice blocks, made with Pocket's pan law and
 *      int16 conversion, master gain and DC blocker, so that with the mixer at unity and every
 *      effect off it is Pocket's mix sample for sample (tests/test_engine.c compares them); plus each
 *      track's level, mute and solo, the sends, CLATTER 12's delay and reverb, the FX chain on the
 *      tracks routed to it, bit reduction and the limiter;
 *   3. into the converter, and out at the output rate.
 * The stages need pocket_lxr.c's file-scope names (drain, blockHook, voiceData, addVoice, mixL...):
 * this file is built after it in one unit (tools/amalgamate.py), never on its own.
 */
#include <stdint.h>
#include <string.h>
#include "c12_engine.h"
#include "c12_resampler.h"
#include "c12_space.h"
#include "pocket_lxr.h"
#include "pocket_seq.h"
#include "pocket_kit.h"
#include "pocket_gen.h"
#include "pocket_params.h"
#include "Soundengine/DSPAudio/wavetable.h"
#include "Soundengine/DSPAudio/transientTables.h"
#include "Soundengine/DSPAudio/FX/Distortion.h"
#include "Soundengine/DSPAudio/FX/Ringmod.h"
#include "Soundengine/DSPAudio/FX/Compressor.h"

/* The firmware's marker for per-sample code (Soundengine/config.h): tools/amalgamate.py turns
 * it into C12_LXR_CODE, which the panel build places in flash or SRAM. */
#include "Soundengine/config.h"

_Static_assert(C12_LXR_BLOCK == POCKET_LXR_BLOCK, "LXR block size");
_Static_assert(C12_KIT_PARAMS >= POCKET_SOUND_PARAMS, "kit buffer");
_Static_assert(C12_TRACKS == POCKET_SEQ_TRACKS, "tracks");

static struct c12_resampler rs;
static float lxrF[C12_LXR_BLOCK * 2];
static float outF[128 * 2];
static volatile struct c12_engine_stats st;

/* the kit as the control context wants it, and as already queued to the engine */
static uint8_t kitTarget[C12_KIT_PARAMS], kitSent[C12_KIT_PARAMS];

static int tablesBound;

/* ------------------------------------------------------------------ shared with the audio context */
/* Written by the control context, a byte at a time; read by the audio context. */
static volatile uint8_t mixLevel[C12_TRACKS], mixDelay[C12_TRACKS], mixReverb[C12_TRACKS];
static volatile uint8_t mixMute, mixSolo, mixRoute, mixMaster;
static volatile uint8_t fxValue[C12_FX_PARAMS];
static volatile uint32_t fxSerial; /* bumped by every c12_fx_set: the audio context re-applies */

/* audio context only */
static uint8_t hatOwner = POCKET_TRIG_CLOSED_HAT; /* the hat track that last took voice 5 */
static float gainNow[POCKET_LXR_VOICES];
static float masterNow = 1.f;
static int32_t dcX[2], dcY[2];
#define DC_R_Q30 1072976913 /* Pocket's DC blocker (pocket_lxr.c), here on CLATTER 12's own mix */
/* (a * b + round) >> shift with a 64-bit product of two 32-bit values: one SMULL */
static inline int32_t mul_shift(int32_t a, int32_t b, int shift, int64_t round)
{
	return (int32_t)(((int64_t)a * b + round) >> shift);
}
static float limitEnv;
static uint32_t fxApplied;
static MasterDistortion fxDrive;
static Ringmod fxRing;
static Compressor fxComp;
static int16_t fxIn[C12_LXR_BLOCK * 2], fxOut[C12_LXR_BLOCK * 2];
static float fxWet[3];
static float crushStep, crushInv;
static uint8_t limiterOn;
static uint8_t spaceOn = 1; /* the sends feed CLATTER 12's delay and reverb (else Plinky's) */
static int spaceBpm;

/* sends: built per LXR block, converted to the output rate linearly (they feed a reverb and a delay) */
#define SEND_CAP 512u
static int16_t sendRing[2][SEND_CAP];
static uint32_t sendWrite, sendRead, sendFrac;
static int32_t sendOut[2][128];
static uint8_t sendsActive;
static uint32_t outRate;
/* the LXR block in stages (c12_engine_render) */
enum { STG_HEAD, STG_V0, STG_V1, STG_V2, STG_V3, STG_V4, STG_V5, STG_MIX, STG_SPACE, STG_FX0, STG_FX1, STG_MASTER, STG_N };
static uint8_t lxrStage;    /* the next stage of the LXR block under way (STG_HEAD: none under way) */
static uint8_t spreadOn = 1;

/* profile */
static c12_clock_fn clockFn;
static inline uint32_t now_us(void) { return clockFn ? clockFn() : 0u; }
static volatile struct c12_engine_profile prof;

/* ------------------------------------------------------------------ strokes, rolls, performance (audio context) */
/* Time inside a step is Pocket's: phase units, 20 * bpm a sample, a straight sixteenth 300 * rate
 * (pocket_seq.c). Strokes of a ratchet or roll are kept in those units from the moment they were
 * scheduled, so they follow tempo changes, and fire on the first LXR block at or after their time,
 * as Pocket's steps do (1.45 ms resolution, never accumulating). */
#define SIXTEENTH (300u * C12_LXR_RATE)
#define TWELFTH (25u * C12_LXR_RATE)
#define MAX_STROKES 12
#define CLOSED_HAT_T 5
#define OPEN_HAT_T 6

static const uint8_t ratchetStrokes[C12_RATCHETS] = { 1, 2, 3, 4, 6, 8 };
unsigned c12_ratchet_strokes(unsigned ratchet) { return ratchet < C12_RATCHETS ? ratchetStrokes[ratchet] : 1u; }

static const uint8_t divTwelfths[C12_DIVS] = { 0, 192, 96, 48, 24, 12, 6, 3, 32, 16, 8, 4, 36, 18 };
unsigned c12_div_twelfths(unsigned div) { return div < C12_DIVS ? divTwelfths[div] : 0u; }

uint32_t c12_cell_from_v1(uint32_t cell)
{
	static const uint8_t rampCurve[4] = { C12_CURVE_FADE_OUT, C12_CURVE_FADE_IN, C12_CURVE_FADE_OUT, C12_CURVE_HUMAN };
	const unsigned n = cell >> 20 & 7u, spacing = cell >> 23 & 3u, ramp = cell >> 25 & 3u;
	if (!n || n >= C12_RATCHETS) return cell & 0xfffffu;
	const unsigned roll = spacing == 1u ? C12_ROLL_ACCEL : spacing == 2u ? C12_ROLL_DECEL : C12_ROLL_NONE;
	return C12_CELL_WITH_EXT(cell & 0xfffffu, n, rampCurve[ramp], roll);
}

int c12_curve_velocity(unsigned curve, int v, unsigned k, unsigned n, uint32_t u)
{
	const float t = n > 1 ? (float)k / (float)(n - 1) : 0.f;
	float out = (float)v;
	switch (curve)
	{
	case C12_CURVE_FADE_IN: out *= 0.35f + 0.65f * t; break;
	case C12_CURVE_FADE_OUT: out *= 1.f - 0.6f * t; break;
	case C12_CURVE_ACCENT_FIRST: if (k) out *= 0.6f; break;
	case C12_CURVE_ACCENT_LAST: if (k + 1u < n) out *= 0.6f; break;
	case C12_CURVE_ALTERNATE: if (k & 1u) out *= 0.6f; break;
	/* CLATTER's roll shape (destruction/rolls.ts shapeVel 'human') */
	case C12_CURVE_HUMAN: out *= 0.45f + 0.3f * (float)(u >> 16 & 255u) / 255.f + 0.15f * t; break;
	default: break;
	}
	const int r = (int)(out + 0.5f);
	return r < 8 ? 8 : r > 127 ? 127 : r;
}

struct strokes {
	uint8_t n, next, note, variation, manual;
	uint8_t vel[MAX_STROKES];
	uint32_t due[MAX_STROKES]; /* phase units after the moment they were scheduled */
	uint32_t elapsed;
};
static struct strokes strk[C12_TRACKS];  /* a step's (or a hand-played) ratchet or roll */
static struct strokes grace[C12_TRACKS]; /* grace notes ahead of the next step */
static uint32_t graceFor[C12_TRACKS];    /* the step count (pocket_seq_steps_played) they lead into */

/* what the sequencer emitted in this block (all of it: mute and solo are applied as voices are
   triggered, after the performance has had its say) */
static uint8_t emitted[C12_TRACKS][4];
static uint8_t emittedCount;

/* control -> audio: performance (single bytes and one pointer) */
static volatile uint8_t pfThrowD, pfThrowR, pfFreezeD, pfFreezeR, pfFill, pfAltMask;
static volatile int8_t pfDensity;
static const uint32_t* volatile pfAlt;
static volatile uint8_t brDivReq, brMaskReq, brShapeReq;
static volatile uint8_t nrDiv[C12_TRACKS], nrVel[C12_TRACKS], nrChance[C12_TRACKS], nrCurve[C12_TRACKS];
static volatile uint32_t playCell[C12_TRACKS], playSeq[C12_TRACKS];
/* scenes */
static struct c12_mix_state stage;
static volatile uint8_t stagePending, stageAtBar;

/* audio only */
static uint64_t musNow, musStepStart, gridOrigin; /* musical time: phase units of straight sixteenths */
static uint32_t stepElapsed, stepLen = SIXTEENTH, musFrac;
static float stepRatio = 1.f;
static uint8_t wasRunning;
static uint32_t playSeen[C12_TRACKS];
static uint8_t playWaiting[C12_TRACKS];
/* beat repeat */
static uint8_t brNow, brPending, brOn, brP0, brCount, brPass, brMask, brShape;
static uint32_t brW, brR, brRep;
static uint64_t brLast;
/* note repeat */
static uint8_t nrNow[C12_TRACKS];
static uint16_t nrK[C12_TRACKS];
static uint64_t nrDue[C12_TRACKS];
static uint32_t nrI[C12_TRACKS], nrBeat[C12_TRACKS];

static int audible(int track)
{
	const unsigned bit = 1u << track;
	if (mixMute & bit) return 0;
	return !mixSolo || (mixSolo & bit);
}

/* the trigger log (c12_engine_trigger_log) */
static struct c12_trigger_event trigLog[C12_TRIG_LOG];
static volatile uint32_t trigCount, blockCount;
static uint8_t trigKind;

static void trigger_now(int track, int velocity, int note, int variation)
{
	if (!audible(track)) return;
	if (track >= POCKET_TRIG_CLOSED_HAT) hatOwner = (uint8_t)track;
	pocket_lxr_trigger_now(track, velocity, note, variation);
	struct c12_trigger_event* e = &trigLog[trigCount % C12_TRIG_LOG];
	e->block = blockCount;
	e->track = (uint8_t)track;
	e->velocity = (uint8_t)velocity;
	e->kind = trigKind;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	trigCount++;
}

unsigned c12_engine_trigger_log(struct c12_trigger_event* out)
{
	const uint32_t n = trigCount;
	const unsigned m = n < C12_TRIG_LOG ? n : C12_TRIG_LOG;
	for (unsigned i = 0; i < m; i++) out[i] = trigLog[(n - m + i) % C12_TRIG_LOG];
	return m;
}
uint32_t c12_engine_block_count(void) { return blockCount; }

/* The sequencer's emit, inside pocket_seq_block: noted, and played by block_hook as soon as the
 * step is known (the same block, so the same moment), with its ratchet. */
static void seq_emit(int track, int velocity, int note, int variation)
{
	if (emittedCount >= C12_TRACKS) return;
	uint8_t* e = emitted[emittedCount++];
	e[0] = (uint8_t)track;
	e[1] = (uint8_t)velocity;
	e[2] = (uint8_t)note;
	e[3] = (uint8_t)variation;
}

static inline int cell_note(uint32_t cell) { return POCKET_CELL_NOTE(cell) ? (int)POCKET_CELL_NOTE(cell) : POCKET_SEQ_NOTE_PROGRAMMED; }

/* Pocket's condition table (pocket_seq.c): which of 12 passes a condition sounds on */
static const uint16_t condPasses[POCKET_CONDITION_25] = { 0xfff, 0x555, 0xaaa, 0x249, 0x492, 0x924, 0x111, 0x222, 0x444, 0x888 };
/* 1 sounds, 0 does not, -1 chance (unknown ahead of time) */
static int cell_predict(uint32_t cell, unsigned pass)
{
	if (!POCKET_CELL_VELOCITY(cell)) return 0;
	const unsigned c = POCKET_CELL_CONDITION(cell);
	if (c < POCKET_CONDITION_25) return (int)(condPasses[c] >> pass & 1u);
	return -1;
}
static int cell_sounds(uint32_t cell, unsigned pass, uint32_t u)
{
	const int p = cell_predict(cell, pass);
	if (p >= 0) return p;
	return (u >> 24) < (POCKET_CELL_CONDITION(cell) - POCKET_CONDITION_25 + 1u) * 64u;
}

static uint32_t phase_per_sample(void) { return 20u * (uint32_t)pocket_seq_tempo(); }
static uint32_t ms_phase(unsigned ms) { return (uint32_t)(ms * C12_LXR_RATE / 1000u) * phase_per_sample(); }
/* a flam's grace leads its hit by a 64th note (CLATTER's FLAM_TICKS), 15-35 ms; a drag's two by a 128th each, 10-20 ms */
static uint32_t grace_gap(unsigned roll)
{
	const uint32_t q = SIXTEENTH / (roll == C12_ROLL_FLAM ? 4u : 8u);
	const uint32_t lo = ms_phase(roll == C12_ROLL_FLAM ? 15u : 10u), hi = ms_phase(roll == C12_ROLL_FLAM ? 35u : 20u);
	return q < lo ? lo : q > hi ? hi : q;
}
static uint32_t grace_lead(unsigned roll) { return roll == C12_ROLL_DRAG ? 2u * grace_gap(roll) : grace_gap(roll); }

static void strokes_add(struct strokes* s, uint32_t due, int vel)
{
	if (s->n >= MAX_STROKES) return;
	s->due[s->n] = due;
	s->vel[s->n] = (uint8_t)(vel < 8 ? 8 : vel > 127 ? 127 : vel);
	s->n++;
}

/* A step's (or a hand-played cell's) strokes: its ratchet, its roll, its graces if they were not
 * played ahead. `len` is the step's length in phase units. */
static void schedule(int t, uint32_t cell, int v, int note, int variation, uint32_t len, int graced, uint32_t seed, int manual)
{
	struct strokes* s = &strk[t];
	const unsigned roll = C12_CELL_ROLL(cell), curve = C12_CELL_CURVE(cell);
	unsigned n = c12_ratchet_strokes(C12_CELL_RATCHET(cell));
	const uint32_t minGap = ms_phase(C12_MIN_STROKE_MS);
	s->n = s->next = 0;
	s->elapsed = 0;
	s->note = (uint8_t)note;
	s->variation = (uint8_t)variation;
	s->manual = (uint8_t)manual;
	uint32_t start = 0;
	if ((roll == C12_ROLL_FLAM || roll == C12_ROLL_DRAG) && !graced)
	{
		/* not played ahead (first step, a chance condition, a hand-played roll): now, the hit after */
		const uint32_t g = grace_gap(roll);
		if (roll == C12_ROLL_DRAG)
		{
			strokes_add(s, 0, v * 45 / 100);
			strokes_add(s, g, v * 50 / 100);
			start = 2u * g;
		}
		else
		{
			strokes_add(s, 0, v * 55 / 100);
			start = g;
		}
	}
	switch (roll)
	{
	case C12_ROLL_DOUBLE:
		strokes_add(s, start, v);
		strokes_add(s, start + len / 2u, v * 82 / 100);
		break;
	case C12_ROLL_TRIPLE:
		strokes_add(s, start, v);
		strokes_add(s, start + len / 3u, v * 72 / 100);
		strokes_add(s, start + 2u * (len / 3u), v * 86 / 100);
		break;
	case C12_ROLL_ACCEL:
	case C12_ROLL_DECEL:
	{
		/* over two sixteenths (the pair's length whatever the swing): gaps closing or opening */
		const uint32_t span = 2u * SIXTEENTH;
		const unsigned m = n >= 3u ? n : 6u;
		uint32_t last = 0;
		for (unsigned k = 0; k < m; k++)
		{
			const float x = (float)k / (float)m;
			const float p = roll == C12_ROLL_ACCEL ? 1.f - (1.f - x) * (1.f - x) : x * x;
			uint32_t at = (uint32_t)((float)span * p);
			if (k && at < last + minGap) at = last + minGap;
			if (k && at >= span) break;
			last = at;
			const float tt = (float)k / (float)(m - 1u);
			int sv;
			if (curve != C12_CURVE_FLAT) sv = c12_curve_velocity(curve, v, k, m, pocket_gen_mix(seed, k));
			else sv = (int)((float)v * (roll == C12_ROLL_ACCEL ? 0.45f + 0.55f * tt : 1.f - 0.6f * tt));
			strokes_add(s, start + at, sv);
		}
		break;
	}
	default:
	{
		/* a ratchet: n even strokes in the step, fewer if they would come too close */
		while (n > 1u && len / n < minGap)
		{
			n = n == 8u ? 6u : n == 6u ? 4u : n - 1u;
			st.thinned_strokes++;
		}
		for (unsigned k = 0; k < n; k++)
			strokes_add(s, start + (uint32_t)((uint64_t)len * k / n), c12_curve_velocity(curve, v, k, n, pocket_gen_mix(seed, k)));
		break;
	}
	}
}

/* the strokes due by now; then time moves on a block */
static void strokes_run(struct strokes* s, int t, uint32_t advance, int count)
{
	if (!s->n) return;
	while (s->next < s->n && s->elapsed >= s->due[s->next])
	{
		const unsigned k = s->next++;
		if (!audible(t)) continue;
		trigKind = count == 2 ? C12_TRIG_GRACE : s->manual == 1 ? C12_TRIG_HAND : s->manual == 2 ? C12_TRIG_REPEAT : k ? C12_TRIG_STROKE : C12_TRIG_STEP;
		trigger_now(t, s->vel[k], s->note, s->variation);
		if (count && k) st.strokes++;
		if (count == 2) st.graces++;
	}
	if (s->next >= s->n) s->n = 0;
	else s->elapsed += advance;
}

/* ---- FILL and DENSITY (pure: the panel's COMMIT calls it too) ---- */
/* DOWNBEAT's DENSITY_WEIGHT by velocity level, as CLATTER (rhythm/realize.ts): soft hits go first */
static const float densityWeight[5] = { 1.6f, 1.3f, 1.0f, 0.7f, 0.4f };
uint32_t c12_perf_cell(int t, int s, unsigned pass, uint32_t base, int bar_hits, unsigned fill_mask, int density)
{
	const uint32_t h = pocket_gen_mix(pocket_gen_mix(0x9e11u + (uint32_t)t, (uint32_t)s), pass);
	const float u = (float)(h >> 8 & 0xffffu) * (1.f / 65536.f);
	const unsigned b = (unsigned)s & 15u;
	const unsigned v = POCKET_CELL_VELOCITY(base);
	if (v)
	{
		if (density < 0)
		{
			const float dens = 1.f + (float)density * 0.01f;
			const unsigned level = v * 5u / 128u;
			if (!(dens > 0.f && u < powf(dens, densityWeight[level > 4u ? 4u : level]))) return 0;
		}
		return base;
	}
	const uint32_t h2 = pocket_gen_mix(h, 0xf111u);
	const float u2 = (float)(h2 >> 8 & 0xffffu) * (1.f / 65536.f), u3 = (float)(h2 >> 24) * (1.f / 256.f);
	if (fill_mask >> t & 1u)
	{
		/* CLATTER's fill: into the gaps, more and louder towards the bar's end */
		const float k = (float)b / 15.f;
		if (u2 >= 0.25f + 0.75f * k) return 0;
		const int fv = (int)(40.f + u3 * 45.f + k * 25.f);
		const unsigned rat = b >= 14u && (h2 & 1u) ? (b == 15u ? C12_RATCHET_3 : C12_RATCHET_2) : C12_RATCHET_1;
		return C12_CELL_WITH_EXT(POCKET_CELL(fv > 127 ? 127 : fv, 0u), rat, rat ? C12_CURVE_FADE_IN : C12_CURVE_FLAT, C12_ROLL_NONE);
	}
	if (density > 0 && bar_hits)
	{
		/* ghost notes, most on the sixteenths between the eighths, fewest on the beat */
		const float w = (b & 1u) ? 1.f : (b & 3u) ? 0.6f : 0.3f;
		if (u2 < (float)density * 0.01f * w * 0.7f) return POCKET_CELL((unsigned)(30.f + u3 * 30.f), 0u);
	}
	return 0;
}

static int bar_has_hits(const struct pocket_pattern* p, int t, int step)
{
	const int b0 = step & ~15;
	for (int i = 0; i < 16; i++)
		if (POCKET_CELL_VELOCITY(p->cell[t][b0 + i])) return 1;
	return 0;
}

static inline uint32_t track_cell(const struct pocket_pattern* p, int t, int step)
{
	const uint32_t* alt = pfAlt;
	return alt && (pfAltMask >> t & 1u) ? alt[t * POCKET_SEQ_MAX_STEPS + step] : p->cell[t][step];
}

static void play_cell(int t, uint32_t cell, int velocity, uint32_t seed, int graced, int manual)
{
	schedule(t, cell, velocity, cell_note(cell), (int)POCKET_CELL_VARIATION(cell), stepLen, graced, seed, manual);
	strokes_run(&strk[t], t, 0, 1);
}

/* a new step: what each track plays, through the performance, and their ratchets and rolls */
static void play_step(int step)
{
	const struct pocket_pattern* p = pocket_seq_pattern();
	const unsigned pass = pocket_seq_pass();
	const uint32_t played = pocket_seq_steps_played();
	uint32_t hit[C12_TRACKS];
	for (int t = 0; t < C12_TRACKS; t++) hit[t] = 0;
	for (unsigned i = 0; i < emittedCount; i++) hit[emitted[i][0]] = p->cell[emitted[i][0]][step];
	const unsigned fill = pfFill, altMask = pfAlt ? pfAltMask : 0u;
	const int density = pfDensity;
	int changed = 0;
	for (int t = 0; t < C12_TRACKS; t++)
	{
		if (brOn && (brMask >> t & 1u)) { hit[t] = 0; continue; }
		uint32_t c = hit[t];
		if (altMask >> t & 1u)
		{
			const uint32_t a = track_cell(p, t, step);
			c = cell_sounds(a, pass, pocket_gen_mix(0xa17u + (uint32_t)t, played)) ? a : 0u;
		}
		if (fill || density)
		{
			const uint32_t before = c;
			c = c12_perf_cell(t, step, pass, c, density > 0 ? bar_has_hits(p, t, step) : 0, fill, density);
			if (!before && c) st.added++;
			if (before && !c) st.dropped++;
		}
		changed |= c != hit[t];
		hit[t] = c;
	}
	/* the hats are one voice: an open hat that sounds takes it (Pocket's rule, kept for what the
	   performance added) */
	if (changed && hit[OPEN_HAT_T]) hit[CLOSED_HAT_T] = 0;
	for (int t = 0; t < C12_TRACKS; t++)
	{
		if (!hit[t]) continue;
		const uint32_t seed = pocket_gen_mix(pocket_gen_mix(0xc12u + (uint32_t)t, (uint32_t)step), pass);
		play_cell(t, hit[t], (int)POCKET_CELL_VELOCITY(hit[t]), seed, graceFor[t] == played, 0);
	}
}

/* flams and drags: their graces go out before the step, while this one is still playing */
static void graces_ahead(uint32_t advance)
{
	const struct pocket_pattern* p = pocket_seq_pattern();
	const int pos = pocket_seq_position();
	if (pos == POCKET_SEQ_NOT_PLAYING) return;
	const uint32_t played = pocket_seq_steps_played();
	const int steps = (int)POCKET_PATTERN_STEPS(p);
	int next = pos + 1;
	unsigned pass = pocket_seq_pass();
	if (next >= steps) { next = 0; pass = pass == 11u ? 0u : pass + 1u; }
	const uint32_t remaining = stepLen > stepElapsed ? stepLen - stepElapsed : 0u;
	for (int t = 0; t < C12_TRACKS; t++)
	{
		if (graceFor[t] == played + 1u || (brOn && (brMask >> t & 1u)) || grace[t].n) continue;
		const uint32_t c = track_cell(p, t, next);
		const unsigned roll = C12_CELL_ROLL(c);
		if (roll != C12_ROLL_FLAM && roll != C12_ROLL_DRAG) continue;
		if (cell_predict(c, pass) != 1) continue;
		if (pfDensity < 0 && !c12_perf_cell(t, next, pass, c, 1, 0u, pfDensity)) continue;
		const uint32_t lead = grace_lead(roll);
		if (remaining > lead + advance) continue; /* a later block */
		const int v = (int)POCKET_CELL_VELOCITY(c);
		struct strokes* g = &grace[t];
		g->n = g->next = 0;
		g->elapsed = 0;
		g->note = (uint8_t)cell_note(c);
		g->variation = (uint8_t)POCKET_CELL_VARIATION(c);
		g->manual = 0;
		const uint32_t first = remaining > lead ? remaining - lead : 0u;
		if (roll == C12_ROLL_DRAG)
		{
			strokes_add(g, first, v * 45 / 100);
			strokes_add(g, first + grace_gap(roll), v * 50 / 100);
		}
		else
			strokes_add(g, first, v * 55 / 100);
		graceFor[t] = played + 1u;
	}
}

/* ---- the musical clock: where the sequence is, in straight sixteenths, swing followed ---- */
static void clock_update(int running, int newStep, uint32_t advance)
{
	if (running && newStep)
	{
		const int step = pocket_seq_position();
		const unsigned swing = (unsigned)pocket_seq_swing();
		if (wasRunning) musStepStart += SIXTEENTH;
		else
		{
			/* PLAY: the grid starts here; note repeats held through it take it up */
			musStepStart = musNow;
			gridOrigin = musNow;
			for (int t = 0; t < C12_TRACKS; t++)
				if (nrNow[t]) { nrDue[t] = musNow; nrBeat[t] = 0; }
		}
		musNow = musStepStart;
		musFrac = 0;
		stepElapsed = 0;
		/* Pocket's step length in its phase units: 6 * rate * (swing or 100 - swing) */
		stepLen = 6u * C12_LXR_RATE * (step & 1 ? 100u - swing : swing);
		stepRatio = (float)SIXTEENTH / (float)stepLen;
	}
	else if (running)
	{
		stepElapsed += advance;
		float f = (float)stepElapsed * stepRatio;
		const uint32_t frac = f >= (float)(SIXTEENTH - 1u) ? SIXTEENTH - 1u : (uint32_t)f;
		musNow = musStepStart + frac;
		musFrac = frac;
	}
	else
	{
		musNow += advance;
		stepLen = SIXTEENTH;
		stepRatio = 1.f;
	}
	wasRunning = (uint8_t)running;
}

/* ---- beat repeat ---- */
static void repeat_fire(int j)
{
	const struct pocket_pattern* p = pocket_seq_pattern();
	const int steps = (int)POCKET_PATTERN_STEPS(p);
	const int step = (brP0 + j) % steps;
	float g = 1.f;
	if (brShape == 1) { g = 1.f - 0.18f * (float)brRep; if (g < 0.25f) g = 0.25f; }
	else if (brShape == 2) { g = 0.4f + 0.15f * (float)brRep; if (g > 1.f) g = 1.f; }
	uint32_t hit[C12_TRACKS];
	for (int t = 0; t < C12_TRACKS; t++)
	{
		hit[t] = 0;
		if (!(brMask >> t & 1u)) continue;
		const uint32_t c = track_cell(p, t, step);
		if (cell_sounds(c, brPass, pocket_gen_mix(0xb7u + (uint32_t)t, (uint32_t)step))) hit[t] = c;
	}
	if (hit[OPEN_HAT_T]) hit[CLOSED_HAT_T] = 0;
	for (int t = 0; t < C12_TRACKS; t++)
	{
		if (!hit[t]) continue;
		int v = (int)((float)POCKET_CELL_VELOCITY(hit[t]) * g + 0.5f);
		if (v < 8) v = 8;
		play_cell(t, hit[t], v, pocket_gen_mix(0xbe7u + (uint32_t)step, brRep), 0, 2);
		st.repeats++;
	}
}
/* fire the captured steps whose offsets fall in (lo, hi] of the window */
static void repeat_range(int64_t lo, int64_t hi)
{
	for (unsigned j = 0; j < brCount; j++)
	{
		const int64_t o = (int64_t)j * SIXTEENTH;
		if (o > lo && o <= hi) repeat_fire((int)j);
	}
}
static void repeat_anchor(int fireNow)
{
	brOn = 1;
	brPending = 0;
	brP0 = (uint8_t)pocket_seq_position();
	brPass = (uint8_t)pocket_seq_pass();
	brRep = 0;
	brMask = brMaskReq;
	brShape = brShapeReq;
	brCount = (uint8_t)(brW >= SIXTEENTH ? (brW + SIXTEENTH - 1u) / SIXTEENTH : 1u);
	if (brCount > 16) brCount = 16;
	brLast = musNow;
	if (fireNow)
	{
		brR = 0;
		repeat_range(-1, 0);
	}
	else
		brR = musFrac; /* this step already sounded: the window goes on from here */
}
static void repeat_update(int running, int newStep)
{
	const unsigned want = running ? brDivReq : 0u;
	if (want != brNow)
	{
		brNow = (uint8_t)want;
		brOn = brPending = 0;
		if (want)
		{
			brW = c12_div_twelfths(want) * TWELFTH;
			if (newStep) repeat_anchor(1);
			else if (musFrac < SIXTEENTH / 2u) repeat_anchor(0);
			else brPending = 1; /* the step is half gone: from the next */
		}
		return;
	}
	if (!brNow) return;
	if (brPending)
	{
		if (newStep) repeat_anchor(1);
		return;
	}
	brMask = brMaskReq;
	brShape = brShapeReq;
	const uint32_t d = (uint32_t)(musNow - brLast);
	brLast = musNow;
	int64_t lo = brR;
	brR += d;
	while (brR >= brW)
	{
		repeat_range(lo, (int64_t)brW - 1);
		brR -= brW;
		brRep++;
		lo = -1;
	}
	repeat_range(lo, brR);
}

/* ---- note repeat ---- */
static void note_repeat_fire(int t)
{
	const unsigned k = nrK[t]++;
	const uint32_t u = pocket_gen_mix(0x4e7u + (uint32_t)t, k);
	if ((u >> 24) * 100u >= (uint32_t)nrChance[t] * 256u) return;
	float v = (float)nrVel[t];
	const float kk = (float)(k > 7u ? 7u : k) / 7.f;
	switch (nrCurve[t])
	{
	case C12_CURVE_FADE_IN: v *= 0.35f + 0.65f * kk; break;
	case C12_CURVE_FADE_OUT: v *= 1.f - 0.6f * kk; break;
	case C12_CURVE_ACCENT_FIRST: if (k) v *= 0.6f; break;
	case C12_CURVE_ACCENT_LAST: if (nrBeat[t]) v *= 0.6f; break; /* held: each beat accented */
	case C12_CURVE_ALTERNATE: if (k & 1u) v *= 0.6f; break;
	case C12_CURVE_HUMAN: v *= 0.7f + 0.3f * (float)(u >> 8 & 255u) / 255.f; break;
	default: break;
	}
	int iv = (int)(v + 0.5f);
	trigKind = C12_TRIG_REPEAT;
	trigger_now(t, iv < 8 ? 8 : iv > 127 ? 127 : iv, POCKET_SEQ_NOTE_PROGRAMMED, 0);
	st.repeats++;
}
static void note_repeat_update(int running)
{
	const uint32_t minI = ms_phase(10u);
	for (int t = 0; t < C12_TRACKS; t++)
	{
		const unsigned want = nrDiv[t] < C12_DIVS ? nrDiv[t] : 0u;
		if (want != nrNow[t])
		{
			const int starting = !nrNow[t];
			nrNow[t] = (uint8_t)want;
			if (!want) continue;
			uint32_t I = c12_div_twelfths(want) * TWELFTH;
			while (I < minI) I *= 2u;
			nrI[t] = I;
			if (starting) { nrK[t] = 0; note_repeat_fire(t); }
			/* the next strike on the grid (from PLAY; from now when stopped) */
			if (running)
			{
				const uint64_t rel = musNow - gridOrigin;
				nrDue[t] = gridOrigin + (rel / I + 1u) * I;
				if (starting && nrDue[t] - musNow < I / 2u) nrDue[t] += I;
				nrBeat[t] = (uint32_t)((nrDue[t] - gridOrigin) % (4u * SIXTEENTH));
			}
			else
			{
				nrDue[t] = musNow + I;
				nrBeat[t] = I % (4u * SIXTEENTH);
			}
		}
		if (!nrNow[t]) continue;
		for (int guard = 0; guard < 4 && musNow >= nrDue[t]; guard++)
		{
			note_repeat_fire(t);
			nrDue[t] += nrI[t];
			nrBeat[t] += nrI[t];
			while (nrBeat[t] >= 4u * SIXTEENTH) nrBeat[t] -= 4u * SIXTEENTH;
		}
	}
}

static void stage_apply(void)
{
	for (int t = 0; t < C12_TRACKS; t++)
	{
		mixLevel[t] = stage.level[t];
		mixDelay[t] = stage.delay[t];
		mixReverb[t] = stage.reverb[t];
	}
	mixMute = stage.mute;
	mixSolo = stage.solo;
	mixRoute = stage.route;
	mixMaster = stage.master;
	__atomic_store_n(&stagePending, 0, __ATOMIC_RELEASE);
}

/* Pocket's block hook: its sequencer, then the performance, the strokes due in this block. */
static void block_hook(void)
{
	blockCount++;
	const uint32_t before = pocket_seq_steps_played();
	emittedCount = 0;
	pocket_seq_block();
	const int pos = pocket_seq_position();
	const int running = pocket_seq_playing() && pos != POCKET_SEQ_NOT_PLAYING;
	const int newStep = pocket_seq_steps_played() != before;
	const uint32_t advance = 20u * C12_LXR_BLOCK * (uint32_t)pocket_seq_tempo();
	/* a scene: on the first step of a bar, before it sounds */
	if (__atomic_load_n(&stagePending, __ATOMIC_ACQUIRE) && (!running || !stageAtBar || (newStep && (pos & 15) == 0))) stage_apply();
	clock_update(running, newStep, advance);
	repeat_update(running, newStep);
	if (newStep && running) play_step(pos);
	if (running) graces_ahead(advance);
	note_repeat_update(running);
	/* cells played by hand: at the next step when playing (in time), now when stopped */
	for (int t = 0; t < C12_TRACKS; t++)
	{
		const uint32_t q = playSeq[t];
		if (q != playSeen[t]) { playSeen[t] = q; playWaiting[t] = 1; }
		if (playWaiting[t] && (!running || newStep))
		{
			playWaiting[t] = 0;
			const uint32_t c = playCell[t];
			play_cell(t, c, (int)POCKET_CELL_VELOCITY(c), pocket_gen_mix(0x9a7u + (uint32_t)t, q), 0, 1);
		}
	}
	for (int t = 0; t < C12_TRACKS; t++)
	{
		if (!running && strk[t].manual != 1) strk[t].n = 0;
		if (!running) grace[t].n = 0;
		strokes_run(&grace[t], t, advance, 2);
		strokes_run(&strk[t], t, advance, 1);
	}
}

/* ---- control context ---- */
void c12_perf_set_throw(unsigned delay_mask, unsigned verb_mask)
{
	pfThrowD = (uint8_t)(delay_mask & 0x7fu);
	pfThrowR = (uint8_t)(verb_mask & 0x7fu);
}
void c12_perf_set_freeze(int delay, int verb)
{
	if (pfFreezeD == (delay ? 1 : 0) && pfFreezeR == (verb ? 1 : 0)) return;
	pfFreezeD = delay ? 1 : 0;
	pfFreezeR = verb ? 1 : 0;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	fxSerial++;
}
void c12_perf_set_fill(unsigned mask) { pfFill = (uint8_t)(mask & 0x7fu); }
void c12_perf_set_density(int amount) { pfDensity = (int8_t)(amount < -100 ? -100 : amount > 100 ? 100 : amount); }
void c12_perf_set_alt(const uint32_t* cells, unsigned mask)
{
	if (!cells || !mask)
	{
		pfAltMask = 0;
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		pfAlt = 0;
		return;
	}
	pfAlt = cells;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	pfAltMask = (uint8_t)(mask & 0x7fu);
}
void c12_perf_set_repeat(unsigned div, unsigned mask, unsigned shape)
{
	brMaskReq = (uint8_t)(mask & 0x7fu);
	brShapeReq = (uint8_t)(shape < 3u ? shape : 0u);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	brDivReq = (uint8_t)(div < C12_DIVS ? div : 0u);
}
void c12_perf_note_repeat(int track, unsigned div, int velocity, int chance, unsigned curve)
{
	if (track < 0 || track >= C12_TRACKS) return;
	nrVel[track] = (uint8_t)(velocity < 1 ? 1 : velocity > 127 ? 127 : velocity);
	nrChance[track] = (uint8_t)(chance < 0 ? 0 : chance > 100 ? 100 : chance);
	nrCurve[track] = (uint8_t)(curve < C12_CURVES ? curve : 0u);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	nrDiv[track] = (uint8_t)(div < C12_DIVS ? div : 0u);
}
void c12_perf_play(int track, uint32_t cell)
{
	if (track < 0 || track >= C12_TRACKS || !POCKET_CELL_VELOCITY(cell)) return;
	playCell[track] = cell;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	playSeq[track]++;
}

int c12_mix_schedule(const struct c12_mix_state* s, int at_bar)
{
	if (__atomic_load_n(&stagePending, __ATOMIC_ACQUIRE)) return -1;
	stage = *s;
	stageAtBar = at_bar ? 1 : 0;
	__atomic_store_n(&stagePending, 1, __ATOMIC_RELEASE);
	return 0;
}
int c12_mix_schedule_pending(void) { return __atomic_load_n(&stagePending, __ATOMIC_ACQUIRE); }
void c12_mix_get(struct c12_mix_state* out)
{
	for (int t = 0; t < C12_TRACKS; t++)
	{
		out->level[t] = mixLevel[t];
		out->delay[t] = mixDelay[t];
		out->reverb[t] = mixReverb[t];
	}
	out->mute = mixMute;
	out->solo = mixSolo;
	out->route = mixRoute;
	out->master = mixMaster;
}

/* ------------------------------------------------------------------ init */
/* Only built packed (tools/amalgamate.py), where the tables are pointers (see PACKED_FIXES). */
void c12_tables_bind(void* mem)
{
#ifdef POCKET_PACKED_TABLES
	uint8_t* p = (uint8_t*)mem;
	sine_table = (int16_t*)p;
	p += 8196; /* 4097 int16, rounded up to 4 bytes */
	triTable = (int16_t(*)[1024])p;
	p += 11 * 1024 * 2;
	sawTable = (int16_t(*)[1024])p;
	p += 11 * 1024 * 2;
	recTable = (int16_t(*)[1024])p;
	p += 11 * 1024 * 2;
	transientData = (int8_t(*)[2205])p;
	p += 12 * 2205;
	_Static_assert(8196 + 3 * 11 * 1024 * 2 + 12 * 2205 <= C12_TABLE_BYTES, "table memory");
	tablesBound = 1;
#else
	(void)mem;
	tablesBound = 1;
#endif
}

static const uint8_t fxMin[C12_FX_PARAMS] = { [C12_FX_CRUSH_BITS] = 4 };
static const uint8_t fxMax[C12_FX_PARAMS] = {
	[C12_FX_DRIVE_WET] = 100, [C12_FX_DRIVE_TYPE] = 2, [C12_FX_DRIVE_AMOUNT] = 127, [C12_FX_DRIVE_TONE] = 127,
	[C12_FX_DRIVE_LEVEL] = 127, [C12_FX_RING_WET] = 100, [C12_FX_RING_FREQ] = 127, [C12_FX_COMP_WET] = 100,
	[C12_FX_COMP_THRESH] = 127, [C12_FX_COMP_RATIO] = 5, [C12_FX_COMP_ATTACK] = 127, [C12_FX_COMP_RELEASE] = 127,
	[C12_FX_COMP_GAIN] = 127, [C12_FX_CRUSH_BITS] = 16, [C12_FX_RATE] = 127, [C12_FX_LIMITER] = 1,
	[C12_FX_DLY_TIME] = C12_DELAY_DIVISIONS - 1, [C12_FX_DLY_FEEDBACK] = 100, [C12_FX_DLY_FILTER] = 127, [C12_FX_DLY_WET] = 100,
	[C12_FX_DLY_PINGPONG] = 1, [C12_FX_VERB_SIZE] = 100, [C12_FX_VERB_DECAY] = 100, [C12_FX_VERB_DAMP] = 100,
	[C12_FX_VERB_WET] = 100, [C12_FX_SPACE_ENGINE] = 1,
};
/* off, and the LXR FX bus's own starting points where on */
static const uint8_t fxDefault[C12_FX_PARAMS] = {
	[C12_FX_DRIVE_WET] = 0, [C12_FX_DRIVE_TYPE] = 0, [C12_FX_DRIVE_AMOUNT] = 40, [C12_FX_DRIVE_TONE] = 90,
	[C12_FX_DRIVE_LEVEL] = 80, [C12_FX_RING_WET] = 0, [C12_FX_RING_FREQ] = 40, [C12_FX_COMP_WET] = 0,
	[C12_FX_COMP_THRESH] = 90, [C12_FX_COMP_RATIO] = 3, [C12_FX_COMP_ATTACK] = 10, [C12_FX_COMP_RELEASE] = 40,
	[C12_FX_COMP_GAIN] = 20, [C12_FX_CRUSH_BITS] = 16, [C12_FX_RATE] = 127, [C12_FX_LIMITER] = 1,
	/* a dotted eighth, a few repeats, a little darker; a mid-sized room */
	[C12_FX_DLY_TIME] = 7, [C12_FX_DLY_FEEDBACK] = 40, [C12_FX_DLY_FILTER] = 44, [C12_FX_DLY_WET] = 70,
	[C12_FX_DLY_PINGPONG] = 1, [C12_FX_VERB_SIZE] = 60, [C12_FX_VERB_DECAY] = 45, [C12_FX_VERB_DAMP] = 45,
	[C12_FX_VERB_WET] = 60, [C12_FX_SPACE_ENGINE] = 0,
};
int c12_fx_max(int fx) { return fx >= 0 && fx < C12_FX_PARAMS ? fxMax[fx] : 0; }
int c12_fx_min(int fx) { return fx >= 0 && fx < C12_FX_PARAMS ? fxMin[fx] : 0; }
int c12_fx_get(int fx) { return fx >= 0 && fx < C12_FX_PARAMS ? fxValue[fx] : 0; }

void c12_fx_set(int fx, int value)
{
	if (fx < 0 || fx >= C12_FX_PARAMS) return;
	if (value < fxMin[fx]) value = fxMin[fx];
	if (value > fxMax[fx]) value = fxMax[fx];
	fxValue[fx] = (uint8_t)value;
	/* the LXR's own sample-rate reduction is a kit parameter: kept apart from the kit */
	if (fx == C12_FX_RATE) kitTarget[POCKET_PAR_VOICE_DECIMATION_ALL] = (uint8_t)value;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	fxSerial++;
}

/* What fx_apply last applied (audio context): each group of setters runs again only when one of its
 * own values moved. The setters are pure (they store values and coefficients worked out from them),
 * so skipping an unchanged group leaves exactly the state running it would. 0xff: never applied. */
static uint8_t fxSeen[C12_FX_PARAMS + 2];
#define FX_SEEN_FREEZE_D C12_FX_PARAMS
#define FX_SEEN_FREEZE_R (C12_FX_PARAMS + 1)
static int fx_moved(int lo, int hi)
{
	int moved = 0;
	for (int f = lo; f <= hi; f++)
		if (fxSeen[f] != fxValue[f])
		{
			fxSeen[f] = fxValue[f];
			moved = 1;
		}
	return moved;
}

/* audio context, at an LXR block start, when anything moved: the LXR FX's own setters (the
 * compressor's are double-precision exp), CLATTER 12's bits and limiter, the delay and reverb. A
 * tempo change alone only moves the delay time (c12_space_set_tempo). */
static void fx_apply(void)
{
	const float k = 1.f / 127.f;
	if (fx_moved(C12_FX_DRIVE_TYPE, C12_FX_DRIVE_LEVEL))
	{
		fxDrive.distortionType = fxValue[C12_FX_DRIVE_TYPE];
		Distortion_setDrive(&fxDrive, fxValue[C12_FX_DRIVE_AMOUNT] * k);
		Distortion_setParam1(&fxDrive, fxValue[C12_FX_DRIVE_AMOUNT] * k);
		Distortion_setParam2(&fxDrive, fxValue[C12_FX_DRIVE_TONE] * k);
		Distortion_setParam3(&fxDrive, fxValue[C12_FX_DRIVE_LEVEL] * k);
	}
	if (fx_moved(C12_FX_RING_FREQ, C12_FX_RING_FREQ)) Ringmod_setFreq(&fxRing, fxValue[C12_FX_RING_FREQ] * k);
	if (fx_moved(C12_FX_COMP_THRESH, C12_FX_COMP_GAIN))
	{
		Compressor_setTreshold(&fxComp, fxValue[C12_FX_COMP_THRESH] * k);
		Compressor_setRatio(&fxComp, fxValue[C12_FX_COMP_RATIO]);
		Compressor_setAttack(&fxComp, fxValue[C12_FX_COMP_ATTACK] * k);
		Compressor_setRelease(&fxComp, fxValue[C12_FX_COMP_RELEASE] * k);
		Compressor_setGain(&fxComp, fxValue[C12_FX_COMP_GAIN] * k);
	}
	fxWet[0] = fxValue[C12_FX_DRIVE_WET] * 0.01f;
	fxWet[1] = fxValue[C12_FX_RING_WET] * 0.01f;
	fxWet[2] = fxValue[C12_FX_COMP_WET] * 0.01f;
	const unsigned bits = fxValue[C12_FX_CRUSH_BITS];
	crushStep = bits >= 16u ? 0.f : (float)(1u << (16u - bits));
	crushInv = crushStep > 0.f ? 1.f / crushStep : 0.f;
	limiterOn = fxValue[C12_FX_LIMITER];
	const int tempo = pocket_seq_tempo();
	const int spaceMoved = fx_moved(C12_FX_DLY_TIME, C12_FX_VERB_WET) | (fxSeen[FX_SEEN_FREEZE_D] != pfFreezeD) | (fxSeen[FX_SEEN_FREEZE_R] != pfFreezeR);
	if (spaceMoved)
	{
		struct c12_space_params sp;
		sp.delay_division = fxValue[C12_FX_DLY_TIME];
		sp.delay_feedback = fxValue[C12_FX_DLY_FEEDBACK];
		sp.delay_filter = fxValue[C12_FX_DLY_FILTER];
		sp.delay_wet = fxValue[C12_FX_DLY_WET];
		sp.delay_pingpong = fxValue[C12_FX_DLY_PINGPONG];
		sp.verb_size = fxValue[C12_FX_VERB_SIZE];
		sp.verb_decay = fxValue[C12_FX_VERB_DECAY];
		sp.verb_damp = fxValue[C12_FX_VERB_DAMP];
		sp.verb_wet = fxValue[C12_FX_VERB_WET];
		sp.delay_freeze = fxSeen[FX_SEEN_FREEZE_D] = pfFreezeD;
		sp.verb_freeze = fxSeen[FX_SEEN_FREEZE_R] = pfFreezeR;
		c12_space_set(&sp, tempo);
	}
	else if (tempo != spaceBpm)
		c12_space_set_tempo(tempo);
	spaceBpm = tempo;
	spaceOn = fxValue[C12_FX_SPACE_ENGINE] == 0;
}

int c12_engine_init(uint32_t out_rate, uint32_t seed)
{
	if (!tablesBound) return -1;
	if (pocket_lxr_init(seed) != 0) return -1;
	/* Pocket's own mix is not used (mix_block below makes the same one, and more), so its DC
	 * blocker and master gain are left at their cheapest. */
	pocket_lxr_set_dc_block(0);
	pocket_lxr_set_master(POCKET_GAIN_UNITY);
	/* The sequencer counts engine samples: its rate is the LXR's, whose blocks are its clock. */
	pocket_seq_init(C12_LXR_RATE, POCKET_LXR_BLOCK, seq_emit);
	pocket_lxr_set_block_hook(block_hook);
	if (c12_rs_init(&rs, C12_LXR_RATE, out_rate, 1.0f) != 0) return -1;
	outRate = out_rate;
	memset((void*)&st, 0, sizeof st);
	memset((void*)&prof, 0, sizeof prof);
	memset(strk, 0, sizeof strk);
	memset(grace, 0, sizeof grace);
	memset(graceFor, 0, sizeof graceFor);
	pfThrowD = pfThrowR = pfFreezeD = pfFreezeR = pfFill = pfAltMask = 0;
	pfDensity = 0;
	pfAlt = 0;
	brDivReq = brMaskReq = brShapeReq = 0;
	brNow = brPending = brOn = 0;
	for (int t = 0; t < C12_TRACKS; t++) { nrDiv[t] = nrNow[t] = 0; playSeen[t] = playSeq[t]; playWaiting[t] = 0; }
	stagePending = 0;
	trigCount = blockCount = 0;
	musNow = musStepStart = gridOrigin = 0;
	wasRunning = 0;
	for (int p = 1; p < POCKET_SOUND_PARAMS; p++)
		kitTarget[p] = kitSent[p] = (uint8_t)pocket_lxr_init_kit_value(p);
	for (int t = 0; t < C12_TRACKS; t++)
	{
		mixLevel[t] = C12_LEVEL_UNITY;
		mixDelay[t] = mixReverb[t] = 0;
	}
	mixMute = mixSolo = 0;
	mixRoute = (1u << C12_TRACKS) - 1u;
	mixMaster = C12_LEVEL_UNITY;
	for (int v = 0; v < POCKET_LXR_VOICES; v++) gainNow[v] = 1.f;
	masterNow = 1.f;
	hatOwner = POCKET_TRIG_CLOSED_HAT;
	dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0;
	limitEnv = 0.f;
	Distortion_init(&fxDrive);
	Ringmod_init(&fxRing);
	Compressor_init(&fxComp);
	for (int f = 0; f < C12_FX_PARAMS; f++) fxValue[f] = fxDefault[f];
	kitTarget[POCKET_PAR_VOICE_DECIMATION_ALL] = fxDefault[C12_FX_RATE];
	memset(fxSeen, 0xff, sizeof fxSeen);
	fx_apply();
	fxApplied = fxSerial;
	memset(sendRing, 0, sizeof sendRing);
	lxrStage = STG_HEAD;
	sendWrite = 0;
	sendRead = 0;
	sendFrac = 0;
	sendsActive = 0;
	return 0;
}

/* ------------------------------------------------------------------ mix (audio context) */
static inline float level_gain(unsigned level) { return (float)(level * level) * 1e-4f; }

static IN_ITCM_RAM int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

/* the FX chain on the routed voices, one chunk of 32 frames (the LXR FX's stack buffers): frames
 * 32 * c to 32 * c + 31, each effect in turn */
static IN_ITCM_RAM void fx_chunk(unsigned c)
{
	int16_t* in = fxIn + 2 * 32 * c;
	int16_t* out = fxOut + 2 * 32 * c;
	if (fxWet[0] > 0.f)
	{
		memset(out, 0, 32 * 2 * sizeof(int16_t));
		Distortion_process(&fxDrive, in, out, out + 1, 32, 2, fxWet[0]);
		memcpy(in, out, 32 * 2 * sizeof(int16_t));
	}
	if (fxWet[1] > 0.f)
	{
		memset(out, 0, 32 * 2 * sizeof(int16_t));
		Ringmod_process(&fxRing, in, out, out + 1, 32, 2, fxWet[1]);
		memcpy(in, out, 32 * 2 * sizeof(int16_t));
	}
	if (fxWet[2] > 0.f)
	{
		memset(out, 0, 32 * 2 * sizeof(int16_t));
		Compressor_process(&fxComp, in, out, out + 1, 32, 2, fxWet[2]);
		memcpy(in, out, 32 * 2 * sizeof(int16_t));
	}
}

/* static: nothing large on the audio core's stack */
static int32_t busMixL[C12_LXR_BLOCK], busMixR[C12_LXR_BLOCK];
static int32_t busL[C12_LXR_BLOCK], busR[C12_LXR_BLOCK];
static float sendD[C12_LXR_BLOCK], sendR[C12_LXR_BLOCK];

/* The mix of one LXR block, in stages (the scheduler below may put a callback between them): the
 * voices (levels, mutes, sends, meters), the delay and reverb, the FX chain in two chunks, then the
 * master, the limiter and the sends ring. What one stage leaves for the next: */
static int mixAnySend, mixAnyDelay, mixAnyVerb, mixFxOn;

static IN_ITCM_RAM void mix_voices(void)
{
	const int fxOn = fxWet[0] > 0.f || fxWet[1] > 0.f || fxWet[2] > 0.f;
	int anySend = 0, anyD = 0, anyR = 0;
	float target[POCKET_LXR_VOICES], g0[POCKET_LXR_VOICES];
	int neutral = !fxOn;
	for (int v = 0; v < POCKET_LXR_VOICES; v++)
	{
		const int track = v < POCKET_TRIG_CLOSED_HAT ? v : hatOwner;
		target[v] = audible(track) ? level_gain(mixLevel[track]) : 0.f;
		g0[v] = gainNow[v];
		gainNow[v] = target[v];
		neutral &= g0[v] == 1.f && target[v] == 1.f;
	}
	/* The mixer at unity and no FX: Pocket's own mix of the block (the six voices summed by its
	   addVoice, in mixL/mixR) is this mix, unless it would saturate Pocket's Q23 stage, which this
	   one does not. The first version read it back from pocket_lxr_render_q23's Q23 output at
	   unity gain (c12_engine_init) without the DC blocker: the same values, (mixL << 8) >> 8, and
	   the same test (Q23 full scale is reached exactly when |mixL| >= 32768); that stage is no
	   longer run (lxr_stage). */
	if (neutral)
		for (int i = 0; i < C12_LXR_BLOCK && neutral; i++)
		{
			const int32_t a = mixL[i], b = mixR[i];
			neutral = a > -32768 && a < 32768 && b > -32768 && b < 32768;
			busMixL[i] = a;
			busMixR[i] = b;
		}
	if (!neutral)
		for (int i = 0; i < C12_LXR_BLOCK; i++) busMixL[i] = busMixR[i] = busL[i] = busR[i] = 0;
	for (int v = 0; v < POCKET_LXR_VOICES; v++)
	{
		float panL, panR;
		const int16_t* data = pocket_lxr_voice_block(v, &panL, &panR);
		const int track = v < POCKET_TRIG_CLOSED_HAT ? v : hatOwner;
		/* the voice's loudest sample (every fourth), for the meters, before the level */
		uint32_t pk = 0;
		for (int i = 0; i < C12_LXR_BLOCK; i += 4)
		{
			const uint32_t a = (uint32_t)(data[i] < 0 ? -data[i] : data[i]);
			if (a > pk) pk = a;
		}
		if (pk > prof.track_peak[track]) prof.track_peak[track] = (uint16_t)pk;
		if (g0[v] == 0.f && target[v] == 0.f) continue;
		if (neutral) goto sends;
		{
		int32_t* oL = fxOn && (mixRoute >> track & 1u) ? busL : busMixL;
		int32_t* oR = fxOn && (mixRoute >> track & 1u) ? busR : busMixR;
		if (g0[v] == 1.f && target[v] == 1.f)
		{
			/* Pocket's addVoice exactly */
			for (int i = 0; i < C12_LXR_BLOCK; i++)
			{
				oL[i] += (int16_t)(data[i] * panL);
				oR[i] += (int16_t)(data[i] * panR);
			}
		}
		else
		{
			/* a level change ramps over the block: no step, no click */
			const float dg = (target[v] - g0[v]) * (1.f / C12_LXR_BLOCK);
			float g = g0[v];
			for (int i = 0; i < C12_LXR_BLOCK; i++)
			{
				g += dg;
				oL[i] += (int32_t)(data[i] * panL * g);
				oR[i] += (int32_t)(data[i] * panR * g);
			}
		}
		}
	sends:;
		const unsigned d = (pfThrowD >> track & 1u) ? 127u : mixDelay[track], r = (pfThrowR >> track & 1u) ? 127u : mixReverb[track];
		anyD |= d != 0;
		anyR |= r != 0;
		if (d | r)
		{
			if (!anySend)
			{
				for (int i = 0; i < C12_LXR_BLOCK; i++) sendD[i] = sendR[i] = 0.f;
				anySend = 1;
			}
			const float gd = target[v] * level_gain(d) * 0.5f, gr = target[v] * level_gain(r) * 0.5f;
			for (int i = 0; i < C12_LXR_BLOCK; i++)
			{
				const float m = (float)data[i] * (panL + panR);
				sendD[i] += m * gd;
				sendR[i] += m * gr;
			}
		}
	}
	mixAnySend = anySend;
	mixAnyDelay = anyD;
	mixAnyVerb = anyR;
	mixFxOn = fxOn;
}

static IN_ITCM_RAM void mix_space(void)
{
	/* CLATTER 12's delay and reverb: their returns join the mix before the master and the limiter */
	if (spaceOn)
	{
		const uint32_t ts = now_us();
		if (c12_space_process(sendD, sendR, mixAnyDelay, mixAnyVerb, busMixL, busMixR, C12_LXR_BLOCK)) st.space_blocks++;
		prof.space_us += now_us() - ts;
	}
}

/* the routed voices through the FX chain, at the master gain Pocket's mix has: the first chunk
 * of 32 frames (and the conversion of the whole block), then the second (two stages) */
static IN_ITCM_RAM void mix_fx(unsigned chunk)
{
	if (!mixFxOn) return;
	if (chunk == 0)
		for (int i = 0; i < C12_LXR_BLOCK; i++)
		{
			fxIn[2 * i] = sat16(mul_shift(busL[i], C12_MASTER_Q12, 12, 0));
			fxIn[2 * i + 1] = sat16(mul_shift(busR[i], C12_MASTER_Q12, 12, 0));
		}
	fx_chunk(chunk);
}

static IN_ITCM_RAM void mix_master(void)
{
	const int fxOn = mixFxOn;
	int anySend = mixAnySend;
	const float m0 = masterNow, m1 = level_gain(mixMaster);
	masterNow = m1;
	const float dm = (m1 - m0) * (1.f / C12_LXR_BLOCK);
	const int unityMaster = m0 == 1.f && m1 == 1.f;
	/* 0.89 of full scale, -1 dBFS: room for the converter's ringing */
	const float threshold = 29204.f;
	const float release = 0.99972f; /* about 80 ms at 44 140 Hz */
	float mm = m0;
	uint32_t peak = prof.peak, limited = 0;
	for (int i = 0; i < C12_LXR_BLOCK; i++)
	{
		/* Pocket's master gain and DC blocker, to the bit (pocket_lxr.c), with 32-bit state: each
		   multiply one SMULL on the Cortex-M33, where Pocket's 64 by 64 bits are a library call */
		/* x 256, not << 8: the bus is signed, and a negative left shift is undefined in C and C++17
		   (the same instruction either way) */
		int32_t l = mul_shift(busMixL[i] * 256, C12_MASTER_Q12, 12, 0);
		int32_t r = mul_shift(busMixR[i] * 256, C12_MASTER_Q12, 12, 0);
		if (fxOn)
		{
			l += (int32_t)fxIn[2 * i] * 256;
			r += (int32_t)fxIn[2 * i + 1] * 256;
		}
		const int32_t yl = l - dcX[0] + mul_shift(dcY[0], DC_R_Q30, 30, 1 << 29);
		const int32_t yr = r - dcX[1] + mul_shift(dcY[1], DC_R_Q30, 30, 1 << 29);
		dcX[0] = l;
		dcX[1] = r;
		dcY[0] = yl;
		dcY[1] = yr;
		float fl = (float)yl * (1.f / 256.f), fr = (float)yr * (1.f / 256.f);
		if (!unityMaster)
		{
			mm += dm;
			fl *= mm;
			fr *= mm;
		}
		if (crushStep > 0.f)
		{
			fl = (float)(int32_t)(fl * crushInv + (fl >= 0.f ? 0.5f : -0.5f)) * crushStep;
			fr = (float)(int32_t)(fr * crushInv + (fr >= 0.f ? 0.5f : -0.5f)) * crushStep;
		}
		const float al = fl < 0.f ? -fl : fl, ar = fr < 0.f ? -fr : fr;
		const float a = al > ar ? al : ar;
		if (limiterOn)
		{
			/* peak follower, instant attack: nothing passes the threshold */
			limitEnv = a > limitEnv ? a : limitEnv * release;
			if (limitEnv > threshold)
			{
				const float g = threshold / limitEnv;
				fl *= g;
				fr *= g;
				limited++;
			}
		}
		lxrF[2 * i] = fl;
		lxrF[2 * i + 1] = fr;
		const uint32_t pa = (uint32_t)(a > 32767.f ? 32767.f : a);
		if (pa > peak) peak = pa;
	}
	prof.peak = peak;
	prof.limited += limited;
	/* the sends' ring (for Plinky's delay and reverb) */
	if (spaceOn) anySend = 0;
	for (int i = 0; i < C12_LXR_BLOCK; i++)
	{
		const uint32_t w = (sendWrite + (uint32_t)i) & (SEND_CAP - 1u);
		sendRing[0][w] = anySend ? sat16((int32_t)sendD[i]) : 0;
		sendRing[1][w] = anySend ? sat16((int32_t)sendR[i]) : 0;
	}
	sendWrite += C12_LXR_BLOCK;
	if (anySend) sendsActive = 8; /* stays on a few blocks, for the tails to leave */
}

/* the sends at the output rate: linear interpolation, the exact rate ratio */
static IN_ITCM_RAM void sends_out(unsigned frames)
{
	for (unsigned i = 0; i < frames; i++)
	{
		const uint32_t a = sendRead & (SEND_CAP - 1u), b = (sendRead + 1u) & (SEND_CAP - 1u);
		const float t = (float)sendFrac / (float)outRate;
		for (int c = 0; c < 2; c++)
			sendOut[c][i] = (int32_t)((float)sendRing[c][a] + ((float)sendRing[c][b] - (float)sendRing[c][a]) * t);
		sendFrac += C12_LXR_RATE;
		while (sendFrac >= outRate)
		{
			sendFrac -= outRate;
			sendRead++;
		}
	}
	/* never read past what was written: the ring follows the converter, a block behind */
	if (sendWrite - sendRead > SEND_CAP - C12_LXR_BLOCK) sendRead = sendWrite - SEND_CAP / 2u;
}

const int32_t* c12_engine_send(int which)
{
	return !spaceOn && sendsActive && (which == 0 || which == 1) ? sendOut[which] : 0;
}


/* ------------------------------------------------------------------ the LXR block in stages */
/*
 * One LXR block is pocket_lxr_render_q23's sequence of calls (pocket_lxr.c: drain, the block hook,
 * renderBlock), here cut into stages where nothing depends on the time between them: the head
 * (FX values applied, queued events, the block hook, modulation, LFOs, filter coefficients, each
 * voice's asynchronous part), each of the six voices (its samples, its decimator, Pocket's addVoice),
 * then CLATTER 12's mix in five stages (the voices, the delay and reverb, the LXR FX chain's two
 * 32-frame chunks, the master): twelve stages a block. Each stage runs the same code as before, in the same order,
 * so the audio is the same; Pocket's own Q23 output stage, which CLATTER 12 never used beyond the
 * sum of the voices, is not run (mix_voices reads that sum itself).
 *
 * Why stages: 64 output frames at 32 kHz need 1.379 LXR blocks, so a callback renders one block or
 * two, and the busiest callbacks (two blocks) cost far more than the average. With spreading on
 * (the default), after making this callback's output the engine goes on with the next block's
 * stages until the callback has done its average share of work: the next callback then has less to
 * do. It never runs more than the next callback will need, so the output samples are the same
 * samples; only control changes made between callbacks may reach the engine one LXR block (1.45 ms)
 * later than without (c12_engine_set_spread). Weights are instruction counts per stage measured in
 * QEMU (tests/m33/profile.c), in hundreds: they decide only where a callback stops, never what is
 * computed.
 */

void c12_engine_set_spread(int on) { spreadOn = on ? 1 : 0; }

static IN_ITCM_RAM unsigned stage_weight(unsigned s)
{
	static const uint8_t w[STG_N] = { 70, 144, 143, 143, 116, 141, 140, 60, 0, 1, 1, 40 };
	unsigned x = w[s];
	if (s == STG_MIX && (mixFxOn || sendsActive)) x += 60u;
	if ((s == STG_FX0 || s == STG_FX1) && mixFxOn)
		x += (fxWet[0] > 0.f ? 56u : 0u) + (fxWet[1] > 0.f ? 29u : 0u) + (fxWet[2] > 0.f ? 95u : 0u) + (s == STG_FX0 ? 10u : 0u);
	if (s == STG_SPACE)
	{
		const int b = spaceOn ? c12_space_busy() : 0;
		x = 1u + (b & 1 ? 80u : 0u) + (b & 2 ? 77u : 0u);
	}
	return x;
}

static IN_ITCM_RAM void lxr_stage(unsigned s)
{
	switch (s)
	{
	case STG_HEAD:
		if (fxApplied != fxSerial || spaceBpm != pocket_seq_tempo())
		{
			fxApplied = fxSerial;
			__atomic_thread_fence(__ATOMIC_SEQ_CST);
			fx_apply();
		}
		drain();
		if (blockHook) blockHook();
		/* renderBlock, up to the voices */
		modNode_resetTargets();
		modNode_reassignVeloMod();
		lfo_dispatchNextValue(&voiceArray[0].lfo);
		lfo_dispatchNextValue(&voiceArray[1].lfo);
		lfo_dispatchNextValue(&voiceArray[2].lfo);
		lfo_dispatchNextValue(&snareVoice.lfo);
		lfo_dispatchNextValue(&cymbalVoice.lfo);
		lfo_dispatchNextValue(&hatVoice.lfo);
		SVF_recalcFreq(&voiceArray[0].filter);
		SVF_recalcFreq(&voiceArray[1].filter);
		SVF_recalcFreq(&voiceArray[2].filter);
		SVF_recalcFreq(&snareVoice.filter);
		SVF_recalcFreq(&cymbalVoice.filter);
		SVF_recalcFreq(&hatVoice.filter);
		calcDrumVoiceAsync(0);
		calcDrumVoiceAsync(1);
		calcDrumVoiceAsync(2);
		Snare_calcAsync();
		Cymbal_calcAsync();
		HiHat_calcAsync();
		for (int i = 0; i < POCKET_LXR_BLOCK; i++) mixL[i] = mixR[i] = 0;
		break;
	case STG_V0:
	case STG_V1:
	case STG_V2:
	{
		const uint8_t v = (uint8_t)(s - STG_V0);
		calcDrumVoiceSyncBlock(v, voiceData[v], POCKET_LXR_BLOCK);
		mixer_decimateBlock(v, voiceData[v]);
		addVoice(v, voiceArray[v].triggeredPanL, voiceArray[v].triggeredPanR);
		break;
	}
	case STG_V3:
		Snare_calcSyncBlock(voiceData[3], POCKET_LXR_BLOCK);
		mixer_decimateBlock(3, voiceData[3]);
		addVoice(3, snareVoice.triggeredPanL, snareVoice.triggeredPanR);
		break;
	case STG_V4:
		Cymbal_calcSyncBlock(voiceData[4], POCKET_LXR_BLOCK);
		mixer_decimateBlock(4, voiceData[4]);
		addVoice(4, cymbalVoice.triggeredPanL, cymbalVoice.triggeredPanR);
		break;
	case STG_V5:
		HiHat_calcSyncBlock(voiceData[5], POCKET_LXR_BLOCK);
		mixer_decimateBlock(5, voiceData[5]);
		addVoice(5, hatVoice.triggeredPanL, hatVoice.triggeredPanR);
		break;
	case STG_MIX:
		stats.blocks++;
		mix_voices();
		break;
	case STG_SPACE:
		mix_space();
		break;
	case STG_FX0:
	case STG_FX1:
		mix_fx(s - STG_FX0);
		break;
	default:
		mix_master();
		c12_rs_push(&rs, lxrF, C12_LXR_BLOCK);
		break;
	}
}

/* per render: what the stages took (profile: one clock reading a stage, each stage's time from the
 * end of the one before) and how much was done */
struct render_work {
	uint32_t lxr_us, mix_us, t;
	unsigned weight, blocks, stages;
};

static IN_ITCM_RAM void run_stage(struct render_work* w)
{
	const unsigned s = lxrStage;
	lxr_stage(s);
	if (clockFn)
	{
		const uint32_t t = now_us();
		if (s < STG_MIX) w->lxr_us += t - w->t;
		else w->mix_us += t - w->t;
		w->t = t;
	}
	w->weight += stage_weight(s);
	w->stages++;
	if (s + 1u < STG_N)
		lxrStage = (uint8_t)(s + 1u);
	else
	{
		lxrStage = STG_HEAD;
		w->blocks++;
	}
}

IN_ITCM_RAM void c12_engine_render(int16_t* out_lr, unsigned frames)
{
	if (frames > 128) frames = 128;
	const uint32_t t0 = now_us();
	struct render_work w = { 0, 0, t0, 0, 0, 0 };
	/* this callback's output: every block it needs, finished */
	const uint32_t need = c12_rs_needed(&rs, frames);
	while (rs.count < need)
	{
		if (lxrStage == STG_HEAD && c12_rs_room(&rs) < C12_LXR_BLOCK)
		{
			st.underruns++;
			memset(out_lr, 0, frames * 2 * sizeof(int16_t));
			return;
		}
		run_stage(&w);
	}
	const unsigned rendered = w.blocks;
	c12_rs_process(&rs, outF, frames);
	uint32_t clipped = 0;
	for (unsigned i = 0; i < frames * 2; i++)
	{
		float v = outF[i];
		int s = (int)(v + (v >= 0.f ? 0.5f : -0.5f));
		if (s > 32767) { s = 32767; clipped++; }
		else if (s < -32768) { s = -32768; clipped++; }
		out_lr[i] = (int16_t)s;
	}
	if (sendsActive)
	{
		sends_out(frames);
		if (rendered) sendsActive--;
	}
	else
	{
		/* keep the reader with the writer, so sends start clean */
		const uint32_t lag = (uint32_t)(rs.count > 16u ? rs.count - 16u : 0u);
		sendRead = sendWrite - lag;
		sendFrac = 0;
	}
	/* spreading: on with the next block, up to this callback's average share of the work, never
	   past what the next callback needs, and never into a second block beyond this callback's:
	   one head at most runs early, so a control change reaches the engine at most one LXR block
	   later than without (tests/test_perform.c) */
	if (spreadOn)
	{
		const uint32_t next = c12_rs_needed(&rs, frames);
		unsigned block = 0;
		for (unsigned k = 0; k < STG_N; k++) block += stage_weight(k);
		const unsigned share = (unsigned)((uint64_t)block * frames * rs.in_rate / ((uint64_t)rs.out_rate * C12_LXR_BLOCK));
		int head = lxrStage == STG_HEAD; /* may this callback still start a block? */
		w.t = now_us();
		while (w.weight < share && rs.count < next)
		{
			if (lxrStage == STG_HEAD)
			{
				if (!head || c12_rs_room(&rs) < C12_LXR_BLOCK) break;
				head = 0;
			}
			run_stage(&w);
		}
	}
	st.out_blocks++;
	st.lxr_blocks += w.blocks;
	if (rendered > st.lxr_max_per_out) st.lxr_max_per_out = rendered;
	if (w.stages > prof.stages_max) prof.stages_max = w.stages;
	st.clipped += clipped;
	if (clockFn)
	{
		const uint32_t total = now_us() - t0;
		prof.calls++;
		prof.lxr_us += w.lxr_us;
		prof.mix_us += w.mix_us;
		prof.rs_us += total - w.lxr_us - w.mix_us;
		if (total > prof.total_max_us) prof.total_max_us = total;
	}
}

void c12_engine_stats_read(struct c12_engine_stats* out)
{
	out->out_blocks = st.out_blocks;
	out->lxr_blocks = st.lxr_blocks;
	out->lxr_max_per_out = st.lxr_max_per_out;
	out->clipped = st.clipped;
	out->underruns = st.underruns;
	out->strokes = st.strokes;
	out->graces = st.graces;
	out->repeats = st.repeats;
	out->added = st.added;
	out->dropped = st.dropped;
	out->thinned_strokes = st.thinned_strokes;
	out->space_blocks = st.space_blocks;
}

void c12_engine_set_clock(c12_clock_fn fn) { clockFn = fn; }

void c12_engine_profile_read(struct c12_engine_profile* out)
{
	memcpy(out, (const void*)&prof, sizeof *out);
	memset((void*)&prof, 0, sizeof prof);
	c12_space_peaks(&out->delay_peak, &out->verb_peak);
}

/* ------------------------------------------------------------------ mixer (control) */
void c12_mix_set_level(int track, int level)
{
	if (track < 0 || track >= C12_TRACKS) return;
	mixLevel[track] = (uint8_t)(level < 0 ? 0 : level > 127 ? 127 : level);
}
void c12_mix_set_mute(unsigned mask) { mixMute = (uint8_t)(mask & 0x7fu); }
void c12_mix_set_solo(unsigned mask) { mixSolo = (uint8_t)(mask & 0x7fu); }
void c12_mix_set_send(int track, int delay, int reverb)
{
	if (track < 0 || track >= C12_TRACKS) return;
	mixDelay[track] = (uint8_t)(delay < 0 ? 0 : delay > 127 ? 127 : delay);
	mixReverb[track] = (uint8_t)(reverb < 0 ? 0 : reverb > 127 ? 127 : reverb);
}
void c12_mix_set_route(unsigned mask) { mixRoute = (uint8_t)(mask & 0x7fu); }
void c12_mix_set_master(int level) { mixMaster = (uint8_t)(level < 0 ? 0 : level > 127 ? 127 : level); }
int c12_mix_audible(int track) { return track >= 0 && track < C12_TRACKS && audible(track); }

/* ------------------------------------------------------------------ kits (control) */
void c12_kit_set_target(const uint8_t* kit, unsigned params)
{
	if (params > POCKET_SOUND_PARAMS) params = POCKET_SOUND_PARAMS;
	for (unsigned p = 1; p < params; p++) kitTarget[p] = kit[p];
	/* The FX bus is not built (engine/fx_off.c): its parameters stay 0, as in Pocket. */
	for (int p = POCKET_PAR_FX_TYPE; p <= POCKET_PAR_FX_DELAY_TYPE; p++) kitTarget[p] = 0;
	/* the LXR's all-voice sample rate is FX RATE's, not the kit's */
	kitTarget[POCKET_PAR_VOICE_DECIMATION_ALL] = fxValue[C12_FX_RATE];
}

const uint8_t* c12_kit_target(void) { return kitTarget; }

unsigned c12_kit_pump(unsigned budget)
{
	unsigned left = 0;
	for (int p = 1; p < POCKET_SOUND_PARAMS; p++)
	{
		if (kitSent[p] == kitTarget[p]) continue;
		if (budget && pocket_lxr_set_param(p, kitTarget[p]) == 0)
		{
			kitSent[p] = kitTarget[p];
			budget--;
		}
		else
			left++;
	}
	return left;
}

void c12_kit_design(uint8_t* kit, int character, uint32_t seed, unsigned wildness)
{
	memset(kit, 0, C12_KIT_PARAMS);
	for (int p = 1; p < POCKET_SOUND_PARAMS; p++) kit[p] = (uint8_t)pocket_lxr_init_kit_value(p);
	/* velocity reaches each voice's volume, as Pocket's kit_design sets it */
	for (int v = 0; v < POCKET_LXR_VOICES; v++) kit[POCKET_PAR_VOLUME_MOD_ON_OFF1 + v] = 1;
	if (character >= 0 && character < POCKET_KIT_CHARACTERS)
		pocket_kit_generate(kit, (unsigned)character, seed, wildness);
	for (int p = POCKET_PAR_FX_TYPE; p <= POCKET_PAR_FX_DELAY_TYPE; p++) kit[p] = 0;
}
