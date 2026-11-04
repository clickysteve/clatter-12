/*
 * c12_engine.c - see c12_engine.h.
 *
 * Copyright (C) 2026 The CLATTER contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Per LXR block (64 frames at 44 140 Hz), in the audio context:
 *   1. pocket_lxr_render_q23: queued events, the block hook (Pocket's sequencer, then CLATTER 12's
 *      ratchet strokes), and the six LXR voices, each kept by Pocket for inspection;
 *   2. mix_block: CLATTER 12's own mix of those six voice blocks, made with Pocket's pan law and
 *      int16 conversion, master gain and DC blocker, so that with the mixer at unity and every
 *      effect off it is Pocket's mix sample for sample (tests/test_engine.c compares them); plus each
 *      track's level, mute and solo, the sends to Plinky's delay and reverb, the FX chain on the
 *      tracks routed to it, bit reduction and the limiter;
 *   3. into the converter, and out at the output rate.
 */
#include <stdint.h>
#include <string.h>
#include "c12_engine.h"
#include "c12_resampler.h"
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
static int32_t lxrQ23[C12_LXR_BLOCK * 2];
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

/* sends: built per LXR block, converted to the output rate linearly (they feed a reverb and a delay) */
#define SEND_CAP 512u
static int16_t sendRing[2][SEND_CAP];
static uint32_t sendWrite, sendRead, sendFrac;
static int32_t sendOut[2][128];
static uint8_t sendsActive;
static uint32_t outRate;

/* profile */
static c12_clock_fn clockFn;
static volatile struct c12_engine_profile prof;

/* ------------------------------------------------------------------ ratchets (audio context) */
static const uint8_t ratchetStrokes[C12_RATCHETS] = { 1, 2, 3, 4, 6, 8 };
unsigned c12_ratchet_strokes(unsigned ratchet) { return ratchet < C12_RATCHETS ? ratchetStrokes[ratchet] : 1u; }

struct roll {
	uint8_t n, next, velocity, note, variation, ramp;
	uint32_t elapsed;      /* Pocket's phase units since the step (sample_rate * 20 a sample) */
	uint32_t due[8];       /* of each stroke */
	uint32_t seed;
};
static struct roll rolls[C12_TRACKS];
/* what the sequencer emitted in this block, for its ratchets */
static uint8_t emitted[C12_TRACKS][4];
static uint8_t emittedCount;

static int audible(int track)
{
	const unsigned bit = 1u << track;
	if (mixMute & bit) return 0;
	return !mixSolo || (mixSolo & bit);
}

static void trigger_now(int track, int velocity, int note, int variation)
{
	if (track >= POCKET_TRIG_CLOSED_HAT) hatOwner = (uint8_t)track;
	pocket_lxr_trigger_now(track, velocity, note, variation);
}

/* The sequencer's emit, inside pocket_seq_block: noted, and played by block_hook as soon as the
 * step is known (the same block, so the same moment), with its ratchet. Mute and solo hold a track
 * back here, before it reaches its voice, so a muted closed hat does not choke an open one. */
static void seq_emit(int track, int velocity, int note, int variation)
{
	if (!audible(track) || emittedCount >= C12_TRACKS) return;
	uint8_t* e = emitted[emittedCount++];
	e[0] = (uint8_t)track;
	e[1] = (uint8_t)velocity;
	e[2] = (uint8_t)note;
	e[3] = (uint8_t)variation;
}

/* x in 0..256 (Q8) along the step -> where the stroke falls, Q8 */
static uint32_t roll_place(unsigned shape, uint32_t x, uint32_t gap, uint32_t u)
{
	switch (shape)
	{
	case C12_ROLL_ACCEL: return 256u - ((256u - x) * (256u - x) >> 8); /* the gaps shrink */
	case C12_ROLL_DECEL: return x * x >> 8;                           /* the gaps grow */
	case C12_ROLL_RANDOM:
	{
		/* each stroke up to a quarter of a gap early or late, the same on the same pass */
		const int32_t j = (int32_t)((u >> 8 & 255u) - 128u) * (int32_t)gap / 512;
		const int32_t p = (int32_t)x + j;
		return (uint32_t)(p < 1 ? 1 : p > 255 ? 255 : p);
	}
	default: return x;
	}
}

/* rolls.ts shapeVel (crescendo, decrescendo, human) and realize.ts's ratchet (12 % a stroke) */
static int stroke_velocity(unsigned ramp, int v, unsigned k, unsigned n, uint32_t u)
{
	const float t = n > 1 ? (float)k / (float)(n - 1) : 1.f;
	float out;
	switch (ramp)
	{
	case C12_RAMP_CRESC: out = (float)v * (0.3f + 0.55f * t); break;
	case C12_RAMP_DECRESC: out = (float)v * (0.85f - 0.5f * t); break;
	case C12_RAMP_HUMAN: out = (float)v * (0.45f + 0.3f * (float)(u >> 16 & 255u) / 255.f + 0.15f * t); break;
	default: out = (float)v * (1.f - (float)k * 0.12f); break;
	}
	int r = (int)(out + 0.5f);
	return r < 8 ? 8 : r > 127 ? 127 : r;
}

/* the step's first stroke, now, and the rest of its ratchet scheduled */
static void play_step(int track, int step, const uint8_t* e, uint32_t cell)
{
	const unsigned n = c12_ratchet_strokes(C12_CELL_RATCHET(cell));
	struct roll* r = &rolls[track];
	r->n = 0;
	if (n < 2)
	{
		trigger_now(track, e[1], e[2], e[3]);
		return;
	}
	const unsigned swing = (unsigned)pocket_seq_swing();
	/* Pocket's step length in its phase units: 6 * rate * (swing or 100 - swing) */
	const uint32_t length = 6u * C12_LXR_RATE * (step & 1 ? 100u - swing : swing);
	const unsigned shape = C12_CELL_ROLL(cell);
	r->seed = pocket_gen_mix(pocket_gen_mix(0xc12u + (uint32_t)track, (uint32_t)step), pocket_seq_pass());
	const uint32_t gap = 256u / n;
	for (unsigned k = 1; k < n; k++)
	{
		const uint32_t u = pocket_gen_mix(r->seed, k);
		const uint32_t q8 = roll_place(shape, k * 256u / n, gap, u);
		r->due[k] = (uint32_t)((uint64_t)length * q8 >> 8);
	}
	r->velocity = e[1];
	r->note = e[2];
	r->variation = e[3];
	r->ramp = (uint8_t)C12_CELL_RAMP(cell);
	r->elapsed = 0;
	r->next = 1;
	r->n = (uint8_t)n;
	trigger_now(track, stroke_velocity(r->ramp, r->velocity, 0, n, pocket_gen_mix(r->seed, 0)), r->note, r->variation);
}

static void run_rolls(void)
{
	const uint32_t advance = 20u * C12_LXR_BLOCK * (uint32_t)pocket_seq_tempo();
	for (int t = 0; t < C12_TRACKS; t++)
	{
		struct roll* r = &rolls[t];
		if (!r->n) continue;
		if (!pocket_seq_playing()) { r->n = 0; continue; }
		r->elapsed += advance;
		while (r->next < r->n && r->elapsed >= r->due[r->next])
		{
			const unsigned k = r->next++;
			if (audible(t))
			{
				trigger_now(t, stroke_velocity(r->ramp, r->velocity, k, r->n, pocket_gen_mix(r->seed, k)), r->note, r->variation);
				st.strokes++;
			}
		}
		if (r->next >= r->n) r->n = 0;
	}
}

/* Pocket's block hook: its sequencer, then the strokes of ratchets due in this block. */
static void block_hook(void)
{
	const uint32_t before = pocket_seq_steps_played();
	emittedCount = 0;
	pocket_seq_block();
	if (pocket_seq_steps_played() != before)
	{
		const int step = pocket_seq_position();
		const struct pocket_pattern* p = pocket_seq_pattern();
		/* a new step: what it plays, and their ratchets (a track it plays starts its roll again;
		   other tracks' rolls carry on) */
		for (unsigned i = 0; i < emittedCount; i++)
		{
			const int t = emitted[i][0];
			play_step(t, step, emitted[i], p->cell[t][step]);
		}
	}
	run_rolls();
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
};
/* off, and the LXR FX bus's own starting points where on */
static const uint8_t fxDefault[C12_FX_PARAMS] = {
	[C12_FX_DRIVE_WET] = 0, [C12_FX_DRIVE_TYPE] = 0, [C12_FX_DRIVE_AMOUNT] = 40, [C12_FX_DRIVE_TONE] = 90,
	[C12_FX_DRIVE_LEVEL] = 80, [C12_FX_RING_WET] = 0, [C12_FX_RING_FREQ] = 40, [C12_FX_COMP_WET] = 0,
	[C12_FX_COMP_THRESH] = 90, [C12_FX_COMP_RATIO] = 3, [C12_FX_COMP_ATTACK] = 10, [C12_FX_COMP_RELEASE] = 40,
	[C12_FX_COMP_GAIN] = 20, [C12_FX_CRUSH_BITS] = 16, [C12_FX_RATE] = 127, [C12_FX_LIMITER] = 1,
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

/* audio context, at a block start, when anything moved: the LXR FX's own setters */
static void fx_apply(void)
{
	const float k = 1.f / 127.f;
	fxDrive.distortionType = fxValue[C12_FX_DRIVE_TYPE];
	Distortion_setDrive(&fxDrive, fxValue[C12_FX_DRIVE_AMOUNT] * k);
	Distortion_setParam1(&fxDrive, fxValue[C12_FX_DRIVE_AMOUNT] * k);
	Distortion_setParam2(&fxDrive, fxValue[C12_FX_DRIVE_TONE] * k);
	Distortion_setParam3(&fxDrive, fxValue[C12_FX_DRIVE_LEVEL] * k);
	Ringmod_setFreq(&fxRing, fxValue[C12_FX_RING_FREQ] * k);
	Compressor_setTreshold(&fxComp, fxValue[C12_FX_COMP_THRESH] * k);
	Compressor_setRatio(&fxComp, fxValue[C12_FX_COMP_RATIO]);
	Compressor_setAttack(&fxComp, fxValue[C12_FX_COMP_ATTACK] * k);
	Compressor_setRelease(&fxComp, fxValue[C12_FX_COMP_RELEASE] * k);
	Compressor_setGain(&fxComp, fxValue[C12_FX_COMP_GAIN] * k);
	fxWet[0] = fxValue[C12_FX_DRIVE_WET] * 0.01f;
	fxWet[1] = fxValue[C12_FX_RING_WET] * 0.01f;
	fxWet[2] = fxValue[C12_FX_COMP_WET] * 0.01f;
	const unsigned bits = fxValue[C12_FX_CRUSH_BITS];
	crushStep = bits >= 16u ? 0.f : (float)(1u << (16u - bits));
	crushInv = crushStep > 0.f ? 1.f / crushStep : 0.f;
	limiterOn = fxValue[C12_FX_LIMITER];
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
	memset(rolls, 0, sizeof rolls);
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
	fx_apply();
	fxApplied = fxSerial;
	memset(sendRing, 0, sizeof sendRing);
	sendWrite = 0;
	sendRead = 0;
	sendFrac = 0;
	sendsActive = 0;
	return 0;
}

/* ------------------------------------------------------------------ mix (audio context) */
static inline float level_gain(unsigned level) { return (float)(level * level) * 1e-4f; }

static IN_ITCM_RAM int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

/* the FX chain on the routed voices, in chunks of 32 frames (the LXR FX's stack buffers) */
static IN_ITCM_RAM void fx_chain(void)
{
	for (unsigned c = 0; c < C12_LXR_BLOCK; c += 32)
	{
		int16_t* in = fxIn + 2 * c;
		int16_t* out = fxOut + 2 * c;
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
}

/* static: nothing large on the audio core's stack */
static int32_t busMixL[C12_LXR_BLOCK], busMixR[C12_LXR_BLOCK];
static int32_t busL[C12_LXR_BLOCK], busR[C12_LXR_BLOCK];
static float sendD[C12_LXR_BLOCK], sendR[C12_LXR_BLOCK];

static IN_ITCM_RAM void mix_block(void)
{
	const int fxOn = fxWet[0] > 0.f || fxWet[1] > 0.f || fxWet[2] > 0.f;
	int anySend = 0;
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
	/* The mixer at unity and no FX: Pocket's own mix of the block is this mix (its master gain is
	   unity, c12_engine_init), unless it saturated, which this one does not. */
	if (neutral)
		for (int i = 0; i < C12_LXR_BLOCK && neutral; i++)
		{
			const int32_t a = lxrQ23[2 * i], b = lxrQ23[2 * i + 1];
			neutral = a != POCKET_Q23_MAX && a != POCKET_Q23_MIN && b != POCKET_Q23_MAX && b != POCKET_Q23_MIN;
			busMixL[i] = a >> 8;
			busMixR[i] = b >> 8;
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
		const unsigned d = mixDelay[track], r = mixReverb[track];
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
	/* the routed voices through the FX chain, at the master gain Pocket's mix has */
	if (fxOn)
	{
		for (int i = 0; i < C12_LXR_BLOCK; i++)
		{
			fxIn[2 * i] = sat16(mul_shift(busL[i], C12_MASTER_Q12, 12, 0));
			fxIn[2 * i + 1] = sat16(mul_shift(busR[i], C12_MASTER_Q12, 12, 0));
		}
		fx_chain();
	}
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
		int32_t l = mul_shift(busMixL[i] << 8, C12_MASTER_Q12, 12, 0);
		int32_t r = mul_shift(busMixR[i] << 8, C12_MASTER_Q12, 12, 0);
		if (fxOn)
		{
			l += (int32_t)fxIn[2 * i] << 8;
			r += (int32_t)fxIn[2 * i + 1] << 8;
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
	/* the sends' ring */
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
	return sendsActive && (which == 0 || which == 1) ? sendOut[which] : 0;
}

static inline uint32_t now_us(void) { return clockFn ? clockFn() : 0u; }

IN_ITCM_RAM void c12_engine_render(int16_t* out_lr, unsigned frames)
{
	if (frames > 128) frames = 128;
	const uint32_t t0 = now_us();
	uint32_t tl = 0, tm = 0;
	if (fxApplied != fxSerial)
	{
		fxApplied = fxSerial;
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		fx_apply();
	}
	unsigned rendered = 0;
	while (rs.count < c12_rs_needed(&rs, frames))
	{
		if (c12_rs_room(&rs) < C12_LXR_BLOCK)
		{
			st.underruns++;
			memset(out_lr, 0, frames * 2 * sizeof(int16_t));
			return;
		}
		const uint32_t a = now_us();
		/* Pocket's mix of the block is made and not used: mix_block makes it again, with more */
		pocket_lxr_render_q23(lxrQ23, C12_LXR_BLOCK, 0);
		const uint32_t b = now_us();
		mix_block();
		tl += b - a;
		tm += now_us() - b;
		c12_rs_push(&rs, lxrF, C12_LXR_BLOCK);
		rendered++;
	}
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
	st.out_blocks++;
	st.lxr_blocks += rendered;
	if (rendered > st.lxr_max_per_out) st.lxr_max_per_out = rendered;
	st.clipped += clipped;
	if (clockFn)
	{
		const uint32_t total = now_us() - t0;
		prof.calls++;
		prof.lxr_us += tl;
		prof.mix_us += tm;
		prof.rs_us += total - tl - tm;
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
}

void c12_engine_set_clock(c12_clock_fn fn) { clockFn = fn; }

void c12_engine_profile_read(struct c12_engine_profile* out)
{
	memcpy(out, (const void*)&prof, sizeof *out);
	memset((void*)&prof, 0, sizeof prof);
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
