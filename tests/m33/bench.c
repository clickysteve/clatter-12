/*
 * bench.c - instructions per output block of CLATTER 12's engine on a Cortex-M33 (QEMU mps2-an505,
 * -icount). AN ESTIMATE, NOT A MEASUREMENT OF A PLINKY: QEMU counts instructions, not cycles; the
 * RP2350 adds wait states (XIP flash cache misses, PSRAM), multi-cycle loads, branches and divides.
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c12_engine.h"
#include "c12_space.h"
#include "pocket_lxr.h"
#include "pocket_seq.h"

#define SYST_CSR (*(volatile uint32_t*)0xE000E010)
#define SYST_RVR (*(volatile uint32_t*)0xE000E014)
#define SYST_CVR (*(volatile uint32_t*)0xE000E018)

static uint32_t tables[C12_TABLE_BYTES / 4];
static uint32_t space[C12_SPACE_BYTES / 4]; /* PSRAM on the Plinky: here plain RAM (a lower bound) */
static uint8_t kit[C12_KIT_PARAMS];
static int16_t out[64 * 2];

static inline uint32_t ticks(void) { return SYST_CVR; }
static inline uint32_t since(uint32_t t0) { return (t0 - SYST_CVR) & 0xffffffu; }

/* instructions per SysTick tick, from a loop of known length (4 instructions an iteration) */
static double calibrate(void)
{
	uint32_t n = 200000, t0 = ticks();
	__asm volatile("1: subs %0, %0, #1\n nop\n nop\n bne 1b\n" : "+r"(n));
	return 800000.0 / since(t0);
}

/* a monotonic tick count for the engine's per-stage profile (SysTick counts down, 24 bits) */
static uint32_t lastCvr, mono;
static uint32_t mono_ticks(void)
{
	const uint32_t c = SYST_CVR;
	mono += (lastCvr - c) & 0xffffffu;
	lastCvr = c;
	return mono;
}

/* The share of a 2000 us block the instructions would take at 150 MHz, one instruction a cycle: a
 * LOWER BOUND on the RP2350 (see the header). */
static double pct(double instructions) { return instructions / 300000.0 * 100.0; }

static void report(const char* name, double ipt, double sum, double max, unsigned blocks)
{
	struct c12_engine_profile pr;
	c12_engine_profile_read(&pr);
	const double c = pr.calls ? pr.calls : 1;
	printf("  %-46s mean %7.0f (%3.0f%%)  max %7.0f (%3.0f%%) | LXR %6.0f  mix/FX %6.0f (space %6.0f)  convert %6.0f | stages %2u\n", name,
	       sum / blocks, pct(sum / blocks), max, pct(max), pr.lxr_us * ipt / c, pr.mix_us * ipt / c, pr.space_us * ipt / c, pr.rs_us * ipt / c,
	       (unsigned)pr.stages_max);
}

/* `hands`, if given, runs before each block (what a player does between callbacks) */
static void scenario_with(const char* name, double ipt, unsigned blocks, void (*hands)(unsigned))
{
	struct c12_engine_profile pr;
	c12_engine_profile_read(&pr);
	double sum = 0, max = 0;
	for (unsigned b = 0; b < blocks; b++)
	{
		if (hands) hands(b);
		uint32_t t0 = ticks();
		c12_engine_render(out, 64);
		double i = since(t0) * ipt;
		sum += i;
		if (i > max) max = i;
		c12_kit_pump(48);
	}
	report(name, ipt, sum, max, blocks);
}
static void scenario(const char* name, double ipt, unsigned blocks) { scenario_with(name, ipt, blocks, 0); }

/* hands */
static void fx_editing(unsigned b)
{
	c12_fx_set(C12_FX_COMP_ATTACK, (int)(b % 128));
	c12_fx_set(C12_FX_DLY_FILTER, (int)(b * 7u % 128u));
	c12_fx_set(C12_FX_VERB_DAMP, (int)(b * 3u % 101u));
}
static uint8_t kit2[C12_KIT_PARAMS];
static void kit_regen(unsigned b)
{
	if (b % 100 == 0)
	{
		c12_kit_design(kit2, (int)(b / 100u) % 16, b, 80);
		c12_kit_set_target(kit2, C12_KIT_PARAMS);
		c12_kit_pump(255); /* the whole kit queued at once (the panel streams 48 a frame) */
	}
}
static void scenes(unsigned b)
{
	if (b % 50 == 0)
	{
		struct c12_mix_state m;
		c12_mix_get(&m);
		m.mute = (uint8_t)((b / 50u) & 0x7f);
		m.master = (uint8_t)(80u + b % 40u);
		for (int t = 0; t < 7; t++) m.level[t] = (uint8_t)(60u + (b + 13u * t) % 60u);
		c12_mix_schedule(&m, 0);
	}
}
static void start_stop(unsigned b)
{
	if (b % 25 == 0) pocket_seq_play((b / 25u) & 1u ? 0 : 1);
}

static void pattern(int busy, int ratchet)
{
	struct pocket_pattern* p = pocket_seq_edit_begin();
	memset(p, 0, sizeof *p);
	for (int t = 0; t < 7; t++)
		for (int s = 0; s < 16; s++)
			if (busy || (t == 0 && s % 4 == 0) || (t == 5 && s % 2 == 0))
				p->cell[t][s] = 100u | (ratchet ? (uint32_t)ratchet << 20 : 0);
	p->last_page = 0;
	pocket_seq_edit_commit();
}

extern void initialise_monitor_handles(void);

int main(void)
{
	initialise_monitor_handles();
	SYST_RVR = 0xffffff;
	SYST_CVR = 0;
	SYST_CSR = 5;
	const double ipt = calibrate();
	printf("CLATTER 12 engine on QEMU mps2-an505 (Cortex-M33), -icount: %.2f instructions per tick\n", ipt);
	printf("  instructions per 64-frame output block (2000 us at 32 kHz); stages are means; %% of a 2000 us block at\n"
	       "  150 MHz if every instruction took one cycle (a lower bound); stages: most LXR stages in one block (12 a block)\n");
	lastCvr = SYST_CVR;
	c12_engine_set_clock(mono_ticks);
	c12_tables_bind(tables);
	if (c12_engine_init(32000, 1)) { printf("init failed\n"); return 1; }
	c12_space_bind(space);
	c12_kit_design(kit, 0, 37, 50);
	c12_kit_set_target(kit, C12_KIT_PARAMS);
	while (c12_kit_pump(48)) c12_engine_render(out, 64);
	pocket_seq_set_tempo(120);
	scenario("stopped, silent", ipt, 200);
	pattern(0, 0);
	pocket_seq_play(1);
	scenario("playing: kick, hats", ipt, 800);
	pattern(1, 0);
	scenario("playing: all seven tracks every 16th", ipt, 800);
	pocket_seq_set_tempo(300);
	pattern(1, C12_RATCHET_4);
	scenario("all tracks, 4x ratchets, 300 BPM", ipt, 800);
	pocket_seq_set_tempo(120);
	pattern(1, 0);
	c12_fx_set(C12_FX_SPACE_ENGINE, 1);
	c12_mix_set_send(0, 64, 64);
	c12_mix_set_send(3, 64, 64);
	c12_mix_set_level(2, 70);
	scenario("+ sends (to Plinky's FX) and a level change", ipt, 800);
	c12_fx_set(C12_FX_SPACE_ENGINE, 0);
	scenario("  sends to CLATTER 12's delay and reverb", ipt, 800);
	c12_perf_set_freeze(1, 1);
	scenario("  both frozen", ipt, 400);
	c12_perf_set_freeze(0, 0);
	c12_fx_set(C12_FX_DLY_PINGPONG, 0);
	scenario("  delay mono", ipt, 400);
	c12_fx_set(C12_FX_DLY_PINGPONG, 1);
	c12_fx_set(C12_FX_DRIVE_WET, 100);
	scenario("+ LXR drive (TUBE) on all tracks", ipt, 800);
	c12_fx_set(C12_FX_RING_WET, 50);
	scenario("+ LXR ring modulator", ipt, 800);
	c12_fx_set(C12_FX_COMP_WET, 100);
	scenario("+ LXR compressor (the whole chain)", ipt, 800);
	c12_fx_set(C12_FX_CRUSH_BITS, 8);
	scenario("+ bit reduction", ipt, 800);
	c12_fx_set(C12_FX_DRIVE_WET, 0);
	c12_fx_set(C12_FX_RING_WET, 0);
	c12_fx_set(C12_FX_CRUSH_BITS, 16);
	scenario("compressor alone", ipt, 800);
	c12_fx_set(C12_FX_COMP_WET, 0);
	/* the performance on top of the delay and reverb */
	c12_perf_set_fill(0x0e);
	c12_perf_set_density(60);
	scenario("+ FILL (3 tracks) and DENSITY +60", ipt, 800);
	c12_perf_set_fill(0);
	c12_perf_set_density(0);
	c12_perf_set_repeat(C12_DIV_32, 0x7f, 1);
	scenario("+ BEAT REPEAT 1/32, all tracks", ipt, 800);
	c12_perf_set_repeat(C12_DIV_OFF, 0, 0);
	for (int t = 0; t < 4; t++) c12_perf_note_repeat(t, C12_DIV_32T, 100, 100, 0);
	scenario("+ four note repeats at 1/32T", ipt, 800);
	for (int t = 0; t < 4; t++) c12_perf_note_repeat(t, C12_DIV_OFF, 100, 100, 0);
	/* hands on it, with the delay and reverb on */
	scenario_with("+ FX values moving every block (comp, filters)", ipt, 800, fx_editing);
	scenario_with("+ a new kit every 100 blocks, queued at once", ipt, 800, kit_regen);
	scenario_with("+ a scene (levels, mutes, master) every 50", ipt, 800, scenes);
	{
		/* the mixer as it was */
		struct c12_mix_state m;
		c12_mix_get(&m);
		m.mute = 0;
		m.master = 100;
		for (int t = 0; t < 7; t++) m.level[t] = t == 2 ? 70 : 100;
		c12_mix_schedule(&m, 0);
	}
	scenario_with("+ PLAY and STOP every 25 blocks", ipt, 800, start_stop);
	pocket_seq_play(1);
	/* the worst case asked for: tempo changing every block, 8x ratchets, flams, repeats, both
	   effects, the compressor */
	pattern(1, C12_RATCHET_8);
	{
		struct pocket_pattern* p = pocket_seq_edit_begin();
		for (int s = 0; s < 16; s += 2) p->cell[3][s] = C12_CELL_WITH_EXT(110u, C12_RATCHET_1, C12_CURVE_FLAT, C12_ROLL_FLAM);
		pocket_seq_edit_commit();
	}
	c12_fx_set(C12_FX_COMP_WET, 100);
	c12_perf_set_repeat(C12_DIV_16T, 0x30, 0);
	for (int t = 0; t < 3; t++) c12_perf_note_repeat(t, C12_DIV_32, 100, 100, 0);
	{
		struct c12_engine_profile pr;
		c12_engine_profile_read(&pr);
		double sum = 0, max = 0;
		uint32_t seed = 7;
		for (unsigned b = 0; b < 1500; b++)
		{
			seed = seed * 1664525u + 1013904223u;
			pocket_seq_set_tempo(60 + (int)(seed >> 8) % 241);
			uint32_t t0 = ticks();
			c12_engine_render(out, 64);
			double i = since(t0) * ipt;
			sum += i;
			if (i > max) max = i;
		}
		report("WORST: tempo every block, 8x, flams, repeats, FX", ipt, sum, max, 1500);
	}
	/* the same with spreading off (every LXR block rendered in the callback that needs it, as
	   before): what spreading does to the busiest blocks */
	printf("  spreading off (c12_engine_set_spread(0)):\n");
	c12_engine_set_spread(0);
	pattern(0, 0);
	pocket_seq_play(0);
	c12_fx_set(C12_FX_COMP_WET, 0);
	c12_perf_set_repeat(C12_DIV_OFF, 0, 0);
	for (int t = 0; t < 3; t++) c12_perf_note_repeat(t, C12_DIV_OFF, 100, 100, 0);
	for (int t = 0; t < 7; t++) c12_mix_set_send(t, 0, 0);
	pocket_seq_set_tempo(120);
	for (int k = 0; k < 2000; k++) { c12_engine_render(out, 64); c12_kit_pump(48); } /* tails gone */
	scenario("  stopped, silent", ipt, 400);
	pattern(1, 0);
	pocket_seq_play(1);
	c12_mix_set_send(0, 64, 64);
	c12_mix_set_send(3, 64, 64);
	scenario("  playing all tracks, delay and reverb", ipt, 800);
	c12_engine_set_spread(1);
	scenario("  (spreading on again, the same)", ipt, 800);
	struct c12_engine_stats st;
	c12_engine_stats_read(&st);
	printf("  underruns %u, clipped %u, strokes %u, graces %u, repeats %u, added %u, thinned ratchets %u\n", (unsigned)st.underruns,
	       (unsigned)st.clipped, (unsigned)st.strokes, (unsigned)st.graces, (unsigned)st.repeats, (unsigned)st.added, (unsigned)st.thinned_strokes);
	return 0;
}
