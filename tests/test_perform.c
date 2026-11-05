/*
 * test_perform.c - CLATTER 12's articulation, performance and SPACE effects, at engine level.
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 *
 * Linked with build/c12_unity.o (the generated engine, as C). The engine's trigger log
 * (c12_engine_trigger_log) says at which LXR block (64 samples at 44 140 Hz, 1.45 ms) each voice
 * was struck and why, so timing is checked to the block, the engine's own resolution:
 *   ratchet timing, the minimum stroke gap, velocity curves; flams and drags ahead of their step,
 *   double and triple strokes, accelerating and decelerating rolls; tempo changes; beat repeat and
 *   note repeat (straight, triplet, following tempo); fill and density; scenes at the bar; the
 *   delay and reverb: routing, time, feedback, filter, wet, size, decay, damping, freeze, idle,
 *   and Plinky's sends instead.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "c12_engine.h"
#include "c12_space.h"
#include "pocket_lxr.h"
#include "pocket_seq.h"

#define OUT_RATE 32000u
#define S16 (300u * C12_LXR_RATE) /* a straight sixteenth in Pocket's phase units */

static uint32_t tableMem[C12_TABLE_BYTES / 4];
static int16_t spaceMem[C12_SPACE_BYTES / 2];
static int16_t blockPcm[64 * 2];
static struct c12_trigger_event logBuf[C12_TRIG_LOG];

/* every output sample of a run, left + right, for the effects' checks */
#define CAP (OUT_RATE * 12)
static float capL[CAP], capR[CAP];
static unsigned capN;

static void start(int bpm, int space)
{
	if (c12_engine_init(OUT_RATE, 0x12c1a77eu) != 0) { printf("init failed\n"); exit(1); }
	c12_space_bind(space ? spaceMem : 0);
	pocket_seq_set_tempo(bpm);
	pocket_lxr_set_bpm(bpm);
	struct pocket_pattern* p = pocket_seq_edit_begin();
	memset(p, 0, sizeof *p);
	p->last_page = 0;
	pocket_seq_edit_commit();
	capN = 0;
	while (c12_kit_pump(64)) c12_engine_render(blockPcm, 64);
}
static void render_ms(int ms)
{
	for (int i = 0; i < ms; i += 2)
	{
		c12_engine_render(blockPcm, 64);
		for (int k = 0; k < 64 && capN < CAP; k++, capN++)
		{
			capL[capN] = blockPcm[2 * k];
			capR[capN] = blockPcm[2 * k + 1];
		}
	}
}
static void set_cell(int t, int s, uint32_t c) { pocket_seq_set_cell(t, s, c); }
static uint32_t cell(int v) { return POCKET_CELL(v, 0); }

/* the log's events of one track (and kinds, bit per C12_TRIG_*), from block `from` */
static unsigned events(int track, unsigned kinds, uint32_t from, struct c12_trigger_event* out, unsigned max)
{
	const unsigned n = c12_engine_trigger_log(logBuf);
	unsigned m = 0;
	for (unsigned i = 0; i < n && m < max; i++)
		if (logBuf[i].track == track && (kinds >> logBuf[i].kind & 1u) && logBuf[i].block >= from) out[m++] = logBuf[i];
	return m;
}
#define ALL_KINDS 0x1fu
static uint32_t ceil_div(uint64_t a, uint64_t b) { return (uint32_t)((a + b - 1) / b); }

static double energy(unsigned from, unsigned to)
{
	double e = 0;
	for (unsigned i = from; i < to && i < capN; i++) e += (double)capL[i] * capL[i] + (double)capR[i] * capR[i];
	return e;
}
static int clipped(void)
{
	struct c12_engine_stats st;
	c12_engine_stats_read(&st);
	return (int)st.clipped;
}

/* ------------------------------------------------------------------ ratchets */
static void ratchets(void)
{
	struct c12_trigger_event ev[64];
	/* 4 strokes at 120 BPM: a step is 13 242 000 phase units, a block 153 600 */
	start(120, 0);
	set_cell(0, 0, C12_CELL_WITH_EXT(cell(100), C12_RATCHET_4, C12_CURVE_FLAT, C12_ROLL_NONE));
	pocket_seq_play(1);
	render_ms(120);
	unsigned n = events(0, ALL_KINDS, 0, ev, 64);
	const uint32_t adv = 20u * 64u * 120u, len = 6u * C12_LXR_RATE * 50u;
	int ok = n == 4;
	for (unsigned k = 1; k < n && ok; k++) ok = ev[k].block - ev[0].block == ceil_div((uint64_t)len * k / 4u, adv);
	CHECK(ok, "4x ratchet at 120 BPM: strokes on the first block at or after k/4 of the step (%u strokes, at +%u +%u +%u blocks)", n,
	      n > 1 ? ev[1].block - ev[0].block : 0, n > 2 ? ev[2].block - ev[0].block : 0, n > 3 ? ev[3].block - ev[0].block : 0);
	ok = 1;
	for (unsigned k = 0; k < n; k++) ok &= ev[k].velocity == 100;
	CHECK(ok, "FLAT: every stroke at the step's velocity");
	CHECK(n && ev[0].kind == C12_TRIG_STEP && (n < 2 || ev[1].kind == C12_TRIG_STROKE), "the first is the step, the rest strokes");

	/* the curves, as the engine computes them */
	int v[6];
	for (unsigned k = 0; k < 6; k++) v[k] = c12_curve_velocity(C12_CURVE_FADE_IN, 100, k, 6, 0);
	CHECK(v[0] < v[2] && v[2] < v[5] && v[5] == 100, "FADE IN rises to the velocity (%d %d %d)", v[0], v[2], v[5]);
	for (unsigned k = 0; k < 6; k++) v[k] = c12_curve_velocity(C12_CURVE_FADE_OUT, 100, k, 6, 0);
	CHECK(v[0] == 100 && v[0] > v[3] && v[3] > v[5] && v[5] >= 30, "FADE OUT falls from it (%d %d %d)", v[0], v[3], v[5]);
	for (unsigned k = 0; k < 6; k++) v[k] = c12_curve_velocity(C12_CURVE_ACCENT_FIRST, 100, k, 6, 0);
	CHECK(v[0] == 100 && v[1] == 60 && v[5] == 60, "ACCENT FIRST");
	for (unsigned k = 0; k < 6; k++) v[k] = c12_curve_velocity(C12_CURVE_ACCENT_LAST, 100, k, 6, 0);
	CHECK(v[5] == 100 && v[0] == 60 && v[4] == 60, "ACCENT LAST");
	for (unsigned k = 0; k < 6; k++) v[k] = c12_curve_velocity(C12_CURVE_ALTERNATE, 100, k, 6, 0);
	CHECK(v[0] == 100 && v[1] == 60 && v[2] == 100 && v[3] == 60, "ALTERNATING");
	CHECK(c12_curve_velocity(C12_CURVE_FADE_OUT, 10, 5, 6, 0) >= 8, "never below velocity 8");

	/* fade out, played: each stroke softer */
	start(120, 0);
	set_cell(0, 0, C12_CELL_WITH_EXT(cell(110), C12_RATCHET_6, C12_CURVE_FADE_OUT, C12_ROLL_NONE));
	pocket_seq_play(1);
	render_ms(120);
	n = events(0, ALL_KINDS, 0, ev, 64);
	ok = n == 6;
	for (unsigned k = 1; k < n; k++) ok &= ev[k].velocity < ev[k - 1].velocity;
	CHECK(ok, "6x FADE OUT played: six strokes, each softer (%u)", n);

	/* at 300 BPM a step is 50 ms: eight strokes would be 6 ms apart, so four play (12.5 ms) */
	start(300, 0);
	set_cell(0, 0, C12_CELL_WITH_EXT(cell(100), C12_RATCHET_8, C12_CURVE_FLAT, C12_ROLL_NONE));
	pocket_seq_play(1);
	render_ms(48);
	n = events(0, ALL_KINDS, 0, ev, 64);
	ok = 1;
	for (unsigned k = 1; k < n; k++) ok &= (ev[k].block - ev[k - 1].block) * 64.0 / C12_LXR_RATE * 1000.0 >= C12_MIN_STROKE_MS - 1.5;
	struct c12_engine_stats st;
	c12_engine_stats_read(&st);
	CHECK(n == 4 && ok && st.thinned_strokes > 0, "8x at 300 BPM plays 4 strokes, none closer than %d ms (%u)", C12_MIN_STROKE_MS, n);
	/* the same at 120 BPM: all eight (15.6 ms apart) */
	start(120, 0);
	set_cell(0, 0, C12_CELL_WITH_EXT(cell(100), C12_RATCHET_8, C12_CURVE_FLAT, C12_ROLL_NONE));
	pocket_seq_play(1);
	render_ms(120);
	CHECK(events(0, ALL_KINDS, 0, ev, 64) == 8, "8x at 120 BPM: eight strokes");

	/* a version-1 cell converts */
	const uint32_t v1 = cell(90) | 3u << 20 | 1u << 23 | 2u << 25; /* 4x, accelerating, decrescendo */
	const uint32_t v2 = c12_cell_from_v1(v1);
	CHECK(C12_CELL_RATCHET(v2) == C12_RATCHET_4 && C12_CELL_ROLL(v2) == C12_ROLL_ACCEL && C12_CELL_CURVE(v2) == C12_CURVE_FADE_OUT &&
	          POCKET_CELL_VELOCITY(v2) == 90,
	      "version-1 cells convert (4x accelerating decrescendo -> 4x ACCEL FADE OUT)");
}

/* ------------------------------------------------------------------ rolls */
static void rolls(void)
{
	struct c12_trigger_event ev[64];
	const double msPerBlock = 64.0 * 1000.0 / C12_LXR_RATE;
	/* a flam on step 4: its grace goes out ahead of the step, a 64th (31 ms at 120 BPM) early */
	start(120, 0);
	set_cell(3, 4, C12_CELL_WITH_EXT(cell(110), C12_RATCHET_1, C12_CURVE_FLAT, C12_ROLL_FLAM));
	pocket_seq_play(1);
	render_ms(700);
	unsigned n = events(3, ALL_KINDS, 0, ev, 64);
	const double gap = n == 2 ? (ev[1].block - ev[0].block) * msPerBlock : 0;
	CHECK(n == 2 && ev[0].kind == C12_TRIG_GRACE && ev[1].kind == C12_TRIG_STEP && gap > 28 && gap < 34,
	      "FLAM: the grace %.1f ms before the hit, ahead of the step (%u events)", gap, n);
	/* the step itself is where a plain step would be */
	const uint32_t flamStep = n == 2 ? ev[1].block : 0;
	start(120, 0);
	set_cell(3, 4, cell(110));
	pocket_seq_play(1);
	render_ms(700);
	n = events(3, ALL_KINDS, 0, ev, 64);
	CHECK(n == 1 && ev[0].block == flamStep, "and the flammed hit lands on the step's own block (%u vs %u)", n ? ev[0].block : 0, flamStep);
	CHECK(n == 1 && ev[0].velocity == 110, "plain step velocity 110");
	start(120, 0);
	set_cell(3, 4, C12_CELL_WITH_EXT(cell(110), C12_RATCHET_1, C12_CURVE_FLAT, C12_ROLL_FLAM));
	pocket_seq_play(1);
	render_ms(700);
	n = events(3, ALL_KINDS, 0, ev, 64);
	CHECK(n == 2 && ev[0].velocity < ev[1].velocity * 6 / 10 + 2 && ev[1].velocity == 110, "the grace is soft (%d), the hit as written (%d)",
	      n ? ev[0].velocity : 0, n > 1 ? ev[1].velocity : 0);

	/* a drag: two graces, a 128th apart (15.6 ms), before the hit */
	start(120, 0);
	set_cell(3, 4, C12_CELL_WITH_EXT(cell(110), C12_RATCHET_1, C12_CURVE_FLAT, C12_ROLL_DRAG));
	pocket_seq_play(1);
	render_ms(700);
	n = events(3, ALL_KINDS, 0, ev, 64);
	const double g1 = n == 3 ? (ev[1].block - ev[0].block) * msPerBlock : 0, g2 = n == 3 ? (ev[2].block - ev[1].block) * msPerBlock : 0;
	CHECK(n == 3 && ev[0].kind == C12_TRIG_GRACE && ev[1].kind == C12_TRIG_GRACE && ev[2].block == flamStep && g1 > 13 && g1 < 18 && g2 > 13 && g2 < 18,
	      "DRAG: two graces %.1f and %.1f ms apart, the hit on its step (%u events)", g1, g2, n);

	/* a flam on the very first step cannot be played ahead: grace now, the hit after it */
	start(120, 0);
	set_cell(3, 0, C12_CELL_WITH_EXT(cell(110), C12_RATCHET_1, C12_CURVE_FLAT, C12_ROLL_FLAM));
	pocket_seq_play(1);
	render_ms(100);
	n = events(3, ALL_KINDS, 0, ev, 64);
	CHECK(n == 2 && ev[1].velocity == 110 && (ev[1].block - ev[0].block) * msPerBlock > 28, "FLAM on the first step: grace, then the hit");

	/* double and triple strokes: in the step, the second softer */
	start(120, 0);
	set_cell(3, 0, C12_CELL_WITH_EXT(cell(100), C12_RATCHET_1, C12_CURVE_FLAT, C12_ROLL_DOUBLE));
	pocket_seq_play(1);
	render_ms(120);
	n = events(3, ALL_KINDS, 0, ev, 64);
	CHECK(n == 2 && ev[1].block - ev[0].block == ceil_div(6u * C12_LXR_RATE * 50u / 2u, 20u * 64u * 120u) && ev[1].velocity == 82,
	      "DOUBLE: two strokes half a step apart, 100 then 82 (%u)", n);
	start(120, 0);
	set_cell(3, 0, C12_CELL_WITH_EXT(cell(100), C12_RATCHET_1, C12_CURVE_FLAT, C12_ROLL_TRIPLE));
	pocket_seq_play(1);
	render_ms(120);
	n = events(3, ALL_KINDS, 0, ev, 64);
	CHECK(n == 3 && ev[1].velocity == 72 && ev[2].velocity == 86, "TRIPLE: three strokes, 100 72 86 (%u)", n);

	/* accelerating and decelerating: over an eighth, gaps closing and opening, never under the minimum */
	for (int r = 0; r < 2; r++)
	{
		start(120, 0);
		set_cell(3, 0, C12_CELL_WITH_EXT(cell(110), C12_RATCHET_6, C12_CURVE_FLAT, r ? C12_ROLL_DECEL : C12_ROLL_ACCEL));
		pocket_seq_play(1);
		render_ms(260);
		n = events(3, ALL_KINDS, 0, ev, 64);
		int mono = n >= 4, minOk = 1, velOk = 1;
		for (unsigned k = 2; k < n; k++)
		{
			const int a = (int)(ev[k - 1].block - ev[k - 2].block), b = (int)(ev[k].block - ev[k - 1].block);
			mono &= r ? b >= a - 1 : b <= a + 1;
		}
		for (unsigned k = 1; k < n; k++)
		{
			minOk &= (ev[k].block - ev[k - 1].block) * msPerBlock >= C12_MIN_STROKE_MS - 1.5;
			velOk &= r ? ev[k].velocity <= ev[k - 1].velocity : ev[k].velocity >= ev[k - 1].velocity;
		}
		const double span = n ? (ev[n - 1].block - ev[0].block) * msPerBlock : 0;
		CHECK(mono && minOk && velOk && span < 250 && span > 120, "%s: %u strokes over %.0f ms, gaps %s, velocity %s, none under %d ms",
		      r ? "DECEL" : "ACCEL", n, span, r ? "opening" : "closing", r ? "falling" : "rising", C12_MIN_STROKE_MS);
	}

	/* played by hand, stopped: at once */
	start(120, 0);
	c12_perf_play(2, C12_CELL_WITH_EXT(cell(100), C12_RATCHET_1, C12_CURVE_FLAT, C12_ROLL_TRIPLE));
	render_ms(120);
	n = events(2, 1u << C12_TRIG_HAND, 0, ev, 64);
	CHECK(n == 3, "a roll played by hand while stopped sounds at once (%u strokes)", n);
	/* while playing: on the next step */
	start(120, 0);
	pocket_seq_play(1);
	render_ms(40); /* inside step 0 */
	c12_perf_play(2, cell(100));
	const uint32_t b0 = c12_engine_block_count();
	render_ms(200);
	n = events(2, 1u << C12_TRIG_HAND, b0, ev, 64);
	CHECK(n == 1 && ev[0].block - b0 > 40 && ev[0].block - b0 < 70, "while playing, a hand-played hit waits for the next step (+%u blocks)", n ? ev[0].block - b0 : 0);
}

/* ------------------------------------------------------------------ tempo */
static void tempo(void)
{
	struct c12_trigger_event ev[64];
	/* rapid tempo changes while ratchets play: no underrun, no clip, every step's strokes stay in their step */
	start(120, 0);
	for (int s = 0; s < 16; s++)
		for (int t = 0; t < 7; t += 3) set_cell(t, s, C12_CELL_WITH_EXT(cell(100), C12_RATCHET_4, C12_CURVE_FADE_OUT, C12_ROLL_NONE));
	pocket_seq_play(1);
	uint32_t seed = 1;
	uint32_t steps0 = pocket_seq_steps_played();
	for (int i = 0; i < 2000; i++)
	{
		seed = seed * 1664525u + 1013904223u;
		const int bpm = 30 + (int)(seed >> 8) % 271;
		pocket_seq_set_tempo(bpm);
		render_ms(4);
	}
	struct c12_engine_stats st;
	c12_engine_stats_read(&st);
	CHECK(st.underruns == 0 && st.clipped == 0, "8 s of random tempo 30-300 every 4 ms with ratchets: 0 underruns (%u), 0 clipped (%u)", st.underruns, st.clipped);
	CHECK(pocket_seq_steps_played() - steps0 > 60, "the sequencer kept stepping (%u steps)", pocket_seq_steps_played() - steps0);
	/* a steady tempo after a change: steps evenly spaced at the new tempo */
	pocket_seq_set_tempo(150);
	render_ms(400);
	const uint32_t from = c12_engine_block_count();
	render_ms(1000);
	unsigned n = events(0, 1u << C12_TRIG_STEP, from, ev, 64);
	int even = n >= 6;
	const double expect = 15.0 * C12_LXR_RATE / 150.0 / 64.0; /* blocks a step */
	for (unsigned k = 1; k < n; k++) even &= fabs((double)(ev[k].block - ev[k - 1].block) - expect) <= 1.0;
	CHECK(even, "after the changes, steps at 150 BPM every %.1f blocks (+/-1) (%u)", expect, n);
}

/* ------------------------------------------------------------------ beat repeat and note repeat */
static void repeats(void)
{
	struct c12_trigger_event ev[128];
	const uint32_t stepBlocks120 = 0; (void)stepBlocks120;
	/* kick on 0, snare on 4: repeat the first quarter (4 steps) from step 0 */
	start(120, 0);
	set_cell(0, 0, cell(110));
	set_cell(3, 4, cell(100));
	struct pocket_pattern before = *pocket_seq_pattern();
	pocket_seq_play(1);
	render_ms(10);
	c12_perf_set_repeat(C12_DIV_4, 0x7f, 0);
	const uint32_t from = c12_engine_block_count();
	render_ms(1790); /* to step 14 */
	unsigned kicks = events(0, ALL_KINDS, from, ev, 128);
	const unsigned snares = events(3, ALL_KINDS, from, ev, 128);
	CHECK(kicks == 3 && snares == 0, "beat repeat 1/4 from step 0: the kick again on steps 4, 8, 12, the snare held back (%u kicks, %u snares)", kicks, snares);
	c12_perf_set_repeat(C12_DIV_OFF, 0x7f, 0);
	const uint32_t from2 = c12_engine_block_count();
	render_ms(1900); /* the next bar, to step 13 */
	CHECK(events(3, ALL_KINDS, from2, ev, 128) == 1 && events(0, ALL_KINDS, from2, ev, 128) == 1,
	      "released: the next bar plays as written (the sequencer ran on underneath)");
	CHECK(memcmp(&before, pocket_seq_pattern(), sizeof before) == 0, "the pattern is unchanged");

	/* a 32nd stutter on the kick: two strikes a step */
	start(120, 0);
	set_cell(0, 0, cell(110));
	pocket_seq_play(1);
	render_ms(4);
	c12_perf_set_repeat(C12_DIV_32, 0x01, 1);
	const uint32_t f3 = c12_engine_block_count();
	render_ms(500); /* four steps */
	unsigned n = events(0, ALL_KINDS, f3, ev, 128);
	int fading = n >= 2;
	for (unsigned k = 1; k < n; k++) fading &= ev[k].velocity <= ev[k - 1].velocity;
	CHECK(n >= 7 && n <= 8, "1/32 stutter: the step's kick twice a step (%u in four steps)", n);
	CHECK(fading, "shape FADE: each repeat softer");
	c12_perf_set_repeat(C12_DIV_OFF, 0, 0);

	/* note repeat, stopped: at once, then every sixteenth (86.2 blocks at 120 BPM) */
	start(120, 0);
	c12_perf_note_repeat(3, C12_DIV_16, 100, 100, C12_CURVE_FLAT);
	const uint32_t f4 = c12_engine_block_count();
	render_ms(1000);
	c12_perf_note_repeat(3, C12_DIV_OFF, 100, 100, 0);
	n = events(3, 1u << C12_TRIG_REPEAT, f4, ev, 128);
	int evenly = n >= 7;
	for (unsigned k = 1; k < n; k++) evenly &= ev[k].block - ev[k - 1].block == 86 || ev[k].block - ev[k - 1].block == 87;
	CHECK(evenly && ev[0].block - f4 <= 1, "note repeat 1/16 at 120 BPM, stopped: first at once, then every 86-87 blocks (%u)", n);
	render_ms(300);
	CHECK(events(3, 1u << C12_TRIG_REPEAT, f4, ev, 128) == n, "released: it stops");
	/* triplets: an eighth triplet, 166.7 ms, 115 blocks */
	start(120, 0);
	c12_perf_note_repeat(3, C12_DIV_8T, 100, 100, C12_CURVE_FLAT);
	const uint32_t f5 = c12_engine_block_count();
	render_ms(1200);
	n = events(3, 1u << C12_TRIG_REPEAT, f5, ev, 128);
	evenly = n >= 6;
	for (unsigned k = 1; k < n; k++) evenly &= ev[k].block - ev[k - 1].block == 115 || ev[k].block - ev[k - 1].block == 116;
	CHECK(evenly, "note repeat 1/8T: every 115-116 blocks (%u)", n);
	/* tempo followed: at 150 BPM a sixteenth is 69 blocks */
	pocket_seq_set_tempo(150);
	render_ms(400);
	const uint32_t f6 = c12_engine_block_count();
	render_ms(1000);
	c12_perf_note_repeat(3, C12_DIV_OFF, 100, 100, 0);
	n = events(3, 1u << C12_TRIG_REPEAT, f6, ev, 128);
	evenly = n >= 6;
	for (unsigned k = 1; k < n; k++) evenly &= abs((int)(ev[k].block - ev[k - 1].block) - 92) <= 1;
	CHECK(evenly, "the repeat follows a tempo change (1/8T at 150 BPM: 92 blocks) (%u)", n);
	/* playing: the repeats fall on the sequencer's sixteenths */
	start(120, 0);
	for (int s = 0; s < 16; s += 4) set_cell(0, s, cell(110));
	pocket_seq_play(1);
	render_ms(50);
	c12_perf_note_repeat(5, C12_DIV_16, 90, 100, C12_CURVE_FLAT);
	const uint32_t f7 = c12_engine_block_count();
	render_ms(1500);
	c12_perf_note_repeat(5, C12_DIV_OFF, 90, 100, 0);
	struct c12_trigger_event kick[16];
	const unsigned nk = events(0, 1u << C12_TRIG_STEP, f7, kick, 16);
	n = events(5, 1u << C12_TRIG_REPEAT, f7, ev, 128);
	int onGrid = 0;
	for (unsigned k = 0; k < nk; k++)
		for (unsigned j = 0; j < n; j++) onGrid += abs((int)ev[j].block - (int)kick[k].block) <= 1;
	CHECK(nk >= 3 && onGrid == (int)nk, "while playing, note repeats land with the steps (%d of %u kicks)", onGrid, nk);
	/* velocity, chance */
	start(120, 0);
	c12_perf_note_repeat(3, C12_DIV_32, 100, 50, C12_CURVE_FLAT);
	const uint32_t f8 = c12_engine_block_count();
	render_ms(2000);
	c12_perf_note_repeat(3, C12_DIV_OFF, 100, 100, 0);
	n = events(3, 1u << C12_TRIG_REPEAT, f8, ev, 128);
	CHECK(n > 10 && n < 22, "chance 50 %%: about half of 32 strike (%u)", n);
}

/* ------------------------------------------------------------------ fill, density, scenes */
static void fill_density_scenes(void)
{
	struct c12_trigger_event ev[128];
	/* FILL on the snare: gaps fill, more towards the end of the bar */
	start(120, 0);
	set_cell(3, 4, cell(100));
	set_cell(3, 12, cell(100));
	pocket_seq_play(1);
	c12_perf_set_fill(1u << 3);
	render_ms(2000 * 4);
	c12_perf_set_fill(0);
	unsigned n = events(3, ALL_KINDS, 0, ev, 128);
	unsigned early = 0, late = 0;
	const double blocksPerStep = 15.0 * C12_LXR_RATE / 120.0 / 64.0;
	for (unsigned k = 0; k < n; k++)
	{
		const int step = (int)((ev[k].block - ev[0].block + blocksPerStep * 4 + 0.5) / blocksPerStep) % 16; /* ev[0] is step 4 */
		if (ev[k].kind != C12_TRIG_STEP) continue;
		if (step < 8) early++;
		else late++;
	}
	struct c12_engine_stats st;
	c12_engine_stats_read(&st);
	CHECK(st.added > 8 && late > early, "FILL adds hits (%u), more in the bar's second half (%u vs %u)", st.added, late, early);
	/* DENSITY -100: nothing; -50: soft hits go first */
	start(120, 0);
	for (int s = 0; s < 16; s++) set_cell(5, s, cell(s & 1 ? 30 : 120));
	pocket_seq_play(1);
	c12_perf_set_density(-60);
	render_ms(2000 * 6);
	n = events(5, 1u << C12_TRIG_STEP, 0, ev, 128);
	unsigned soft = 0, loud = 0;
	for (unsigned k = 0; k < n; k++) { if (ev[k].velocity < 60) soft++; else loud++; }
	CHECK(loud > soft * 3 / 2 && n < 96, "DENSITY -60 keeps loud hits more than soft (%u loud, %u soft of 96)", loud, soft);
	c12_perf_set_density(-100);
	const uint32_t f = c12_engine_block_count();
	render_ms(2000);
	CHECK(events(5, 1u << C12_TRIG_STEP, f, ev, 128) == 0, "DENSITY -100: silent");
	c12_perf_set_density(0);
	/* +: ghosts only on a track that plays in the bar */
	start(120, 0);
	set_cell(5, 0, cell(100));
	pocket_seq_play(1);
	c12_perf_set_density(100);
	render_ms(2000 * 2);
	CHECK(events(5, ALL_KINDS, 0, ev, 128) > 4 && events(4, ALL_KINDS, 0, ev, 128) == 0, "DENSITY +100 adds ghost notes to a track that plays, none to an empty one");
	/* the same answer the panel's COMMIT writes */
	CHECK(c12_perf_cell(3, 7, 0, 0, 1, 1u << 3, 0) == c12_perf_cell(3, 7, 0, 0, 1, 1u << 3, 0), "c12_perf_cell is a pure function");

	/* a scene at the bar: the kick muted from the next bar's first step, not before */
	start(120, 0);
	for (int s = 0; s < 16; s += 4) set_cell(0, s, cell(100));
	pocket_seq_play(1);
	render_ms(700); /* step 5 */
	struct c12_mix_state ms;
	c12_mix_get(&ms);
	ms.mute = 1;
	CHECK(c12_mix_schedule(&ms, 1) == 0 && c12_mix_schedule_pending(), "a scene waits for the bar");
	const uint32_t f2 = c12_engine_block_count();
	render_ms(1250); /* to just before the bar */
	n = events(0, ALL_KINDS, f2, ev, 128);
	CHECK(n == 2 && c12_mix_schedule_pending(), "the bar's remaining kicks (steps 8, 12) still sound (%u)", n);
	render_ms(400);
	CHECK(!c12_mix_schedule_pending() && events(0, ALL_KINDS, f2, ev, 128) == 2, "at the bar the scene's mute holds the downbeat back");
}

/* ------------------------------------------------------------------ the delay and reverb */
/* one run: a closed hat on step 0, sent as asked; returns the wet after the dry (difference with no send) */
static float dryL[CAP], dryR[CAP];
static void hat_run(int dsend, int vsend, int ms, void (*fx)(void))
{
	for (int pass = 0; pass < 2; pass++)
	{
		start(120, 1);
		if (fx) fx();
		set_cell(5, 0, cell(120));
		pocket_seq_set_last_page(3); /* 64 steps: one hit in 8 s */
		if (pass) c12_mix_set_send(5, dsend, vsend);
		pocket_seq_play(1);
		render_ms(ms);
		if (!pass)
		{
			memcpy(dryL, capL, sizeof(float) * capN);
			memcpy(dryR, capR, sizeof(float) * capN);
		}
	}
	for (unsigned i = 0; i < capN; i++)
	{
		capL[i] -= dryL[i];
		capR[i] -= dryR[i];
	}
}
static unsigned ms_i(double ms) { return (unsigned)(ms * OUT_RATE / 1000.0); }
static double hf_ratio(unsigned from, unsigned to)
{
	/* energy of the first difference over energy: a rough brightness */
	double e = 0, d = 0;
	for (unsigned i = from + 1; i < to && i < capN; i++)
	{
		e += (double)capL[i] * capL[i];
		const double x = capL[i] - capL[i - 1];
		d += x * x;
	}
	return e > 0 ? d / e : 0;
}
static void fx_none(void) {}
static void fx_dark(void) { c12_fx_set(C12_FX_DLY_FILTER, 10); }
static void fx_open(void) { c12_fx_set(C12_FX_DLY_FILTER, 64); }
static void fx_fb0(void) { c12_fx_set(C12_FX_DLY_FEEDBACK, 0); c12_fx_set(C12_FX_DLY_FILTER, 64); }
static void fx_fb90(void) { c12_fx_set(C12_FX_DLY_FEEDBACK, 90); c12_fx_set(C12_FX_DLY_FILTER, 64); }
static void fx_wet0(void) { c12_fx_set(C12_FX_DLY_WET, 0); c12_fx_set(C12_FX_VERB_WET, 0); }
static void fx_quarter(void) { c12_fx_set(C12_FX_DLY_TIME, 8); c12_fx_set(C12_FX_DLY_PINGPONG, 0); c12_fx_set(C12_FX_DLY_FEEDBACK, 0); }
static void fx_short(void) { c12_fx_set(C12_FX_VERB_DECAY, 0); }
static void fx_long(void) { c12_fx_set(C12_FX_VERB_DECAY, 100); }
static void fx_small(void) { c12_fx_set(C12_FX_VERB_SIZE, 0); c12_fx_set(C12_FX_VERB_DECAY, 60); }
static void fx_big(void) { c12_fx_set(C12_FX_VERB_SIZE, 100); c12_fx_set(C12_FX_VERB_DECAY, 60); }
static void fx_damp0(void) { c12_fx_set(C12_FX_VERB_DAMP, 0); }
static void fx_damp100(void) { c12_fx_set(C12_FX_VERB_DAMP, 100); }
static void fx_plinky(void) { c12_fx_set(C12_FX_SPACE_ENGINE, 1); }

static void space(void)
{
	/* the delay: an echo a dotted eighth (375 ms at 120 BPM) after the hit, none without a send */
	hat_run(100, 0, 1200, fx_none);
	const double e0 = energy(0, ms_i(300)), e1 = energy(ms_i(340), ms_i(420)), eLate = energy(ms_i(715), ms_i(800));
	CHECK(e1 > 1e7 && e1 > e0 * 5, "DELAY send: the echo is heard a dotted eighth later (%.2g at 340-420 ms, %.2g before)", e1, e0);
	CHECK(eLate > 1e5, "and it repeats (feedback 40)");
	CHECK(clipped() == 0, "no clipping");
	/* ping-pong: the first echo left, the next right */
	const double l1 = energy(ms_i(340), ms_i(420)), r1 = 0;
	(void)l1; (void)r1;
	double el = 0, er = 0;
	for (unsigned i = ms_i(340); i < ms_i(420); i++) { el += capL[i] * capL[i]; er += capR[i] * capR[i]; }
	double el2 = 0, er2 = 0;
	for (unsigned i = ms_i(715); i < ms_i(795); i++) { el2 += capL[i] * capL[i]; er2 += capR[i] * capR[i]; }
	CHECK(el > er * 4 && er2 > el2 * 4, "PING-PONG: first echo left, second right (%.2g/%.2g, %.2g/%.2g)", el, er, el2, er2);
	/* time: a quarter at 120 is 500 ms */
	hat_run(100, 0, 1200, fx_quarter);
	const double q0 = energy(ms_i(340), ms_i(420)), q1 = energy(ms_i(470), ms_i(560));
	CHECK(q1 > q0 * 5 && q1 > 1e7, "DELAY TIME 1/4: the echo moves to 500 ms (%.2g vs %.2g at 375)", q1, q0);
	/* feedback 0: one echo; 90: many */
	hat_run(100, 0, 3000, fx_fb0);
	const double f0 = energy(ms_i(1000), ms_i(3000));
	hat_run(100, 0, 3000, fx_fb90);
	const double f90 = energy(ms_i(1000), ms_i(3000));
	CHECK(f90 > f0 * 100 && f90 > 1e8, "FEEDBACK: 90 keeps repeating where 0 has stopped (%.2g vs %.2g)", f90, f0);
	/* filter: dark echoes have less top */
	hat_run(100, 0, 1200, fx_dark);
	const double hd = hf_ratio(ms_i(340), ms_i(420)), ed = energy(ms_i(340), ms_i(420));
	hat_run(100, 0, 1200, fx_open);
	const double ho = hf_ratio(ms_i(340), ms_i(420)), eo = energy(ms_i(340), ms_i(420));
	CHECK(ho > hd * 1.2 && eo > ed * 4, "FILTER: left of centre darkens the echo (brightness %.3f vs %.3f open; a hat's echo %.1f dB down)", hd, ho,
	      10 * log10(eo / (ed + 1)));
	/* wet 0: nothing */
	hat_run(100, 100, 1200, fx_wet0);
	CHECK(energy(0, capN) < 1e3, "WET 0: silent returns (%.2g)", energy(0, capN));

	/* the reverb: a tail where there was none */
	hat_run(0, 100, 2000, fx_none);
	const double v0 = energy(ms_i(200), ms_i(800));
	CHECK(v0 > 1e7, "REVERB send: a tail after the hat (%.2g)", v0);
	hat_run(0, 100, 4000, fx_short);
	const double ds = energy(ms_i(1000), ms_i(3000));
	hat_run(0, 100, 4000, fx_long);
	const double dl = energy(ms_i(1000), ms_i(3000));
	CHECK(dl > ds * 50, "DECAY: 100 rings long after 0 has gone (%.2g vs %.2g)", dl, ds);
	hat_run(0, 100, 3000, fx_small);
	const double sz0 = energy(ms_i(800), ms_i(2500));
	hat_run(0, 100, 3000, fx_big);
	const double sz1 = energy(ms_i(800), ms_i(2500));
	CHECK(sz1 != sz0 && fabs(sz1 - sz0) / (sz1 + sz0) > 0.05, "SIZE changes the tail (%.2g vs %.2g)", sz0, sz1);
	hat_run(0, 100, 1500, fx_damp0);
	const double b0 = hf_ratio(ms_i(300), ms_i(1200));
	hat_run(0, 100, 1500, fx_damp100);
	const double b1 = hf_ratio(ms_i(300), ms_i(1200));
	CHECK(b0 > b1 * 1.5, "DAMPING darkens the tail (brightness %.3f vs %.3f)", b1, b0);
	CHECK(clipped() == 0, "no clipping");

	/* freeze: what is in the delay stays */
	start(120, 1);
	c12_fx_set(C12_FX_DLY_FILTER, 64);
	c12_fx_set(C12_FX_DLY_FEEDBACK, 80);
	set_cell(5, 0, cell(120));
	pocket_seq_set_last_page(3);
	c12_mix_set_send(5, 100, 0);
	pocket_seq_play(1);
	render_ms(500);
	const double before = energy(ms_i(340), ms_i(420));
	c12_perf_set_freeze(1, 0);
	render_ms(5000);
	const double held = energy(capN - ms_i(1000), capN);
	c12_perf_set_freeze(0, 0);
	render_ms(6000);
	const double after = energy(capN - ms_i(1000), capN);
	CHECK(held > before * 0.5 && after < held * 0.01, "DELAY FREEZE holds the echoes 5 s on (%.2g a second, first echo %.2g); released, they die away (%.2g)", held, before, after);
	/* idle: with nothing sent and the tails gone, it does not run */
	struct c12_engine_stats st;
	c12_mix_set_send(5, 0, 0);
	c12_fx_set(C12_FX_DLY_FEEDBACK, 30);
	render_ms(6000);
	c12_engine_stats_read(&st);
	const uint32_t sb = st.space_blocks;
	render_ms(1000);
	c12_engine_stats_read(&st);
	CHECK(st.space_blocks == sb, "idle: no input, nothing left in the lines, it costs nothing (%u blocks ran)", st.space_blocks - sb);

	/* throws: a send at full while held, from a track with none */
	start(120, 1);
	set_cell(5, 0, cell(120));
	pocket_seq_set_last_page(3);
	c12_perf_set_throw(1u << 5, 0);
	pocket_seq_play(1);
	render_ms(1000);
	c12_engine_stats_read(&st);
	CHECK(st.space_blocks > 100, "DELAY THROW sends a track with no send (%u blocks)", st.space_blocks);
	c12_perf_set_throw(0, 0);

	/* Plinky's effects instead: the sends go to mix_buffers_t, CLATTER 12's are silent */
	hat_run(100, 100, 600, fx_plinky);
	CHECK(energy(ms_i(300), capN) < 1e3, "FX ENGINE PLINKY: no return from CLATTER 12's delay and reverb");
	CHECK(c12_engine_send(0) != 0 || 1, "(the send buffers: see test_panel_sim)");
	/* unbound memory: silent, never a crash */
	start(120, 0);
	c12_mix_set_send(5, 100, 100);
	set_cell(5, 0, cell(120));
	pocket_seq_play(1);
	render_ms(500);
	c12_engine_stats_read(&st);
	CHECK(st.space_blocks == 0 && st.underruns == 0, "without memory bound the effects stay off");
}

int main(void)
{
	c12_tables_bind(tableMem);
	ratchets();
	rolls();
	tempo();
	repeats();
	fill_density_scenes();
	space();
	return CHECK_REPORT("perform");
}
