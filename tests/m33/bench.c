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
#include "pocket_lxr.h"
#include "pocket_seq.h"

#define SYST_CSR (*(volatile uint32_t*)0xE000E010)
#define SYST_RVR (*(volatile uint32_t*)0xE000E014)
#define SYST_CVR (*(volatile uint32_t*)0xE000E018)

static uint32_t tables[C12_TABLE_BYTES / 4];
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

static void scenario(const char* name, double ipt, unsigned blocks)
{
	struct c12_engine_profile pr;
	c12_engine_profile_read(&pr);
	double sum = 0, max = 0;
	for (unsigned b = 0; b < blocks; b++)
	{
		uint32_t t0 = ticks();
		c12_engine_render(out, 64);
		double i = since(t0) * ipt;
		sum += i;
		if (i > max) max = i;
		c12_kit_pump(48);
	}
	c12_engine_profile_read(&pr);
	const double c = pr.calls ? pr.calls : 1;
	printf("  %-40s mean %7.0f  max %7.0f | LXR %6.0f  mix/FX %6.0f  convert %6.0f\n", name, sum / blocks, max,
	       pr.lxr_us * ipt / c, pr.mix_us * ipt / c, pr.rs_us * ipt / c);
}

static void pattern(int busy, int ratchet)
{
	struct pocket_pattern* p = pocket_seq_edit_begin();
	memset(p, 0, sizeof *p);
	for (int t = 0; t < 7; t++)
		for (int s = 0; s < 16; s++)
			if (busy || (t == 0 && s % 4 == 0) || (t == 5 && s % 2 == 0))
				p->cell[t][s] = 100u | (ratchet ? C12_RATCHET_4 << 20 : 0);
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
	printf("  instructions per 64-frame output block (2000 us at 32 kHz); the stages are means\n");
	lastCvr = SYST_CVR;
	c12_engine_set_clock(mono_ticks);
	c12_tables_bind(tables);
	if (c12_engine_init(32000, 1)) { printf("init failed\n"); return 1; }
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
	pattern(1, 1);
	scenario("all tracks, 4x ratchets, 300 BPM", ipt, 800);
	pocket_seq_set_tempo(120);
	pattern(1, 0);
	c12_mix_set_send(0, 64, 64);
	c12_mix_set_send(3, 64, 64);
	c12_mix_set_level(2, 70);
	scenario("+ sends and a level change", ipt, 800);
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
	struct c12_engine_stats st;
	c12_engine_stats_read(&st);
	printf("  underruns %u, clipped %u, ratchet strokes %u\n", (unsigned)st.underruns, (unsigned)st.clipped, (unsigned)st.strokes);
	return 0;
}
