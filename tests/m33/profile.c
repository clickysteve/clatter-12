/*
 * profile.c - where the instructions go, stage by stage, on a Cortex-M33 (QEMU mps2-an505, -icount).
 * AN ESTIMATE, NOT A MEASUREMENT OF A PLINKY (see bench.c and TESTING.md "CPU").
 *
 * Includes the generated engine (build/c12_engine_c.c) as one unit, so it can time the stages of
 * c12_engine_render one by one (lxr_stage: the head with the event drain and the sequencer, each
 * of the six voices, the three stages of CLATTER 12's mix) and the converter. It replays the
 * engine's render loop, spreading included, with a timer around each stage.
 *
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 */
#include "../../build/c12_engine_c.c"
/* the generated text is preprocessed: its macros are gone (values from c12_engine.h, c12_space.h,
   pocket_lxr.h, pocket_kit.h; checked against the engine's own sizes in main) */
#define C12_TABLE_BYTES 102240u
#define C12_KIT_PARAMS 256
#define C12_LXR_BLOCK 64u
#define POCKET_LXR_BLOCK 64
#define POCKET_KIT_CHARACTERS 16
#define C12_SPACE_BYTES (c12x_space_bytes)

#define SYST_CSR (*(volatile uint32_t*)0xE000E010)
#define SYST_RVR (*(volatile uint32_t*)0xE000E014)
#define SYST_CVR (*(volatile uint32_t*)0xE000E018)

static uint32_t ptables[C12_TABLE_BYTES / 4];
static uint32_t pspace[(4u * 65536u + 8u * 3072u + 4u * 512u) / 4u];
static uint8_t pkit[C12_KIT_PARAMS];
static int16_t pout[64 * 2];

static inline uint32_t tk(void) { return SYST_CVR; }
static inline uint32_t dt(uint32_t t0) { return (t0 - SYST_CVR) & 0xffffffu; }
static double ipt;

enum { S_HEAD, S_V0, S_V1, S_V2, S_V3, S_V4, S_V5, S_MIX, S_SPACE, S_FX0, S_FX1, S_MASTER, S_RS, S_TOTAL, S_N };
static const char* const sname[S_N] = { "head (events, seq, mod, LFO, SVF)", "DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "HI-HAT",
	"mix: voices, sends, meters", "mix: delay and reverb", "mix: LXR FX chain, frames 0-31", "mix: LXR FX chain, frames 32-63",
	"mix: master, limiter, sends", "converter, output, sends", "TOTAL per output block" };
static double ssum[S_N], smax[S_N];
static unsigned scount[S_N];
static void acc(int s, uint32_t t)
{
	const double i = t * ipt;
	ssum[s] += i;
	scount[s]++;
	if (i > smax[s]) smax[s] = i;
}
static void reset_stats(void)
{
	for (int s = 0; s < S_N; s++) ssum[s] = smax[s] = 0, scount[s] = 0;
}

/* the engine's own stage function, timed stage by stage (S_* follow STG_*) */
static void prof_stage(struct render_work* w)
{
	const unsigned s = lxrStage;
	const uint32_t t = tk();
	lxr_stage(s);
	acc((int)s, dt(t));
	w->weight += stage_weight(s);
	if (s + 1u < STG_N) lxrStage = (uint8_t)(s + 1u);
	else { lxrStage = STG_HEAD; w->blocks++; }
}

/* c12_engine_render with a timer per stage (the same sequence: mandatory blocks, output, spreading) */
static void prof_render(int16_t* out_lr, unsigned frames)
{
	const uint32_t t0 = tk();
	struct render_work w = { 0 };
	const uint32_t need = c12_rs_needed(&rs, frames);
	while (rs.count < need) prof_stage(&w);
	uint32_t t = tk();
	c12_rs_process(&rs, outF, frames);
	for (unsigned i = 0; i < frames * 2; i++)
	{
		float v = outF[i];
		int s = (int)(v + (v >= 0.f ? 0.5f : -0.5f));
		if (s > 32767) s = 32767;
		else if (s < -32768) s = -32768;
		out_lr[i] = (int16_t)s;
	}
	if (sendsActive)
	{
		sends_out(frames);
		if (w.blocks) sendsActive--;
	}
	else
	{
		const uint32_t lag = (uint32_t)(rs.count > 16u ? rs.count - 16u : 0u);
		sendRead = sendWrite - lag;
		sendFrac = 0;
	}
	acc(S_RS, dt(t));
	if (spreadOn)
	{
		const uint32_t next = c12_rs_needed(&rs, frames);
		unsigned block = 0;
		for (unsigned k = 0; k < STG_N; k++) block += stage_weight(k);
		const unsigned share = (unsigned)((uint64_t)block * frames * rs.in_rate / ((uint64_t)rs.out_rate * C12_LXR_BLOCK));
		int head = lxrStage == STG_HEAD;
		while (w.weight < share && rs.count < next)
		{
			if (lxrStage == STG_HEAD)
			{
				if (!head) break;
				head = 0;
			}
			prof_stage(&w);
		}
	}
	acc(S_TOTAL, dt(t0));
}

/* a monotonic clock for the engine's own space_us counter */
static uint32_t lastCvr, mono;
static uint32_t mono_ticks(void)
{
	const uint32_t c = SYST_CVR;
	mono += (lastCvr - c) & 0xffffffu;
	lastCvr = c;
	return mono;
}

static void report(const char* name)
{
	printf("%s\n", name);
	for (int s = 0; s < S_N; s++)
	{
		if (!scount[s]) continue;
		/* per LXR block for the LXR stages, per output block for the rest */
		printf("  %-34s mean %7.0f  max %7.0f  (%u calls)\n", sname[s], ssum[s] / scount[s], smax[s], scount[s]);
	}
}

static void run(const char* name, unsigned blocks, int pump)
{
	reset_stats();
	for (unsigned b = 0; b < blocks; b++)
	{
		prof_render(pout, 64);
		if (pump) c12_kit_pump(48);
	}
	report(name);
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
	{
		uint32_t n = 200000, t0 = tk();
		__asm volatile("1: subs %0, %0, #1\n nop\n nop\n bne 1b\n" : "+r"(n));
		ipt = 800000.0 / dt(t0);
	}
	printf("CLATTER 12 stage profile, QEMU mps2-an505 (Cortex-M33) -icount, instructions (%.0f per tick)\n", ipt);
	printf("stages are per LXR block (64 frames at 44 140 Hz); converter and TOTAL per output block (64 at 32 kHz)\n");
	lastCvr = SYST_CVR;
	c12_engine_set_clock(mono_ticks);
	c12_tables_bind(ptables);
	if (c12_engine_init(32000, 1)) { printf("init failed\n"); return 1; }
	c12_space_bind(pspace);
	c12_kit_design(pkit, 0, 37, 50);
	c12_kit_set_target(pkit, C12_KIT_PARAMS);
	while (c12_kit_pump(48)) c12_engine_render(pout, 64);
	pocket_seq_set_tempo(120);
	run("stopped, silent", 300, 0);
	pattern(1, 0);
	pocket_seq_play(1);
	run("playing: all seven tracks every 16th", 1500, 0);
	for (int t = 0; t < 7; t++) c12_mix_set_send(t, 64, 64);
	run("all tracks, every track sent to the delay and reverb", 1500, 0);
	c12_fx_set(C12_FX_COMP_WET, 100);
	c12_fx_set(C12_FX_DRIVE_WET, 60);
	run("+ LXR drive and compressor on every track", 1500, 0);
	c12_fx_set(C12_FX_COMP_WET, 0);
	c12_fx_set(C12_FX_DRIVE_WET, 0);
	/* a new kit while playing: every parameter queued at once, drained 32 a block */
	reset_stats();
	for (int k = 0; k < 6; k++)
	{
		c12_kit_design(pkit, k % POCKET_KIT_CHARACTERS, 100u + (uint32_t)k, 80);
		c12_kit_set_target(pkit, C12_KIT_PARAMS);
		for (unsigned b = 0; b < 120; b++)
		{
			c12_kit_pump(255);
			prof_render(pout, 64);
		}
	}
	report("six new kits, 120 blocks apart, while playing, sends on (each kit queued at once)");
	return 0;
}
