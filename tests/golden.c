/*
 * golden.c - CLATTER 12's audio for a fixed set of sessions, written as raw PCM, so that two builds
 * (before and after an optimisation) can be compared sample for sample: tools/compare_builds.sh.
 *
 * Each session is deterministic and drives the engine only through its public API, at 32 kHz in
 * 64-frame blocks, as the panel does:
 *   steady  generated pattern on all seven tracks, ratchets, rolls, every send to the delay
 *           (ping-pong) and reverb, everything set before PLAY and nothing touched after: the
 *           output depends on the DSP alone, not on when control changes reach it
 *   live    the same with the hands on it: tempo changes (rapid ones too), levels, mutes and solos,
 *           FX values (delay time, filter, feedback, mono/ping-pong, reverb size, decay, damping),
 *           freezes, throws, FILL, DENSITY, beat and note repeats, a scene at the bar, a new kit,
 *           the LXR drive, ring and compressor, bits, the limiter, Plinky's FX engine, stop/start
 *   colour  every track through the LXR drive, ring modulator and compressor, with the delay and
 *           reverb: the busiest DSP path
 *   apart   the delay and the reverb one at a time: sends to one, then the other, then none (each
 *           tail left to die away and the effect to fall idle), mono and ping-pong, freezes, tempo
 *           changes while an effect is idle, the delay's glide
 * Usage: golden OUTDIR [nospread] (writes OUTDIR/steady.raw, live.raw, colour.raw, apart.raw: interleaved int16
 * stereo). `nospread` renders every LXR block in the callback that needs it, as the first version did
 * (c12_engine_set_spread): control changes then reach the engine at exactly the same LXR block.
 *
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "c12_engine.h"
#include "c12_space.h"
#include "pocket_seq.h"
#include "pocket_gen.h"
#include "pocket_lxr.h"

/* weak: tools/compare_builds.sh compiles this file against builds from before spreading existed */
#if defined(__GNUC__) && !defined(__APPLE__)
void c12_engine_set_spread(int on) __attribute__((weak));
#define SET_SPREAD(on) do { if (c12_engine_set_spread) c12_engine_set_spread(on); } while (0)
#else
#define SET_SPREAD(on) c12_engine_set_spread(on)
#endif

#define RATE 32000u
#define SECONDS 24u
static int16_t pcm[RATE * SECONDS * 2];
static uint32_t tables[C12_TABLE_BYTES / 4];
static uint32_t space[C12_SPACE_BYTES / 4];
static uint8_t kit[C12_KIT_PARAMS];

static int setup(uint32_t seed)
{
	c12_tables_bind(tables);
	if (c12_engine_init(RATE, seed) != 0) return -1;
	c12_space_bind(space);
	c12_kit_design(kit, 3, seed, 55);
	c12_kit_set_target(kit, C12_KIT_PARAMS);
	while (c12_kit_pump(255)) { int16_t o[128]; c12_engine_render(o, 64); }
	struct pocket_gen_settings g;
	g.seed = seed; g.density = 75; g.complexity = 60; g.style = 1; g.locks = 0; g.last_page = 1;
	pocket_gen_generate(pocket_seq_edit_begin(), &g);
	pocket_seq_edit_commit();
	/* articulation in CLATTER 12's cell bits */
	struct pocket_pattern* p = pocket_seq_edit_begin();
	for (int s = 0; s < 32; s++)
	{
		if (POCKET_CELL_VELOCITY(p->cell[5][s]) && s % 4 == 3) p->cell[5][s] = C12_CELL_WITH_EXT(p->cell[5][s], C12_RATCHET_4, C12_CURVE_FADE_OUT, C12_ROLL_NONE);
		if (s % 8 == 4) p->cell[3][s] = C12_CELL_WITH_EXT(110u, C12_RATCHET_1, C12_CURVE_FLAT, s % 16 == 4 ? C12_ROLL_FLAM : C12_ROLL_DRAG);
		if (s == 14) p->cell[1][s] = C12_CELL_WITH_EXT(100u, C12_RATCHET_1, C12_CURVE_FLAT, C12_ROLL_ACCEL);
		if (s == 30) p->cell[2][s] = C12_CELL_WITH_EXT(100u, C12_RATCHET_6, C12_CURVE_ACCENT_FIRST, C12_ROLL_NONE);
	}
	pocket_seq_edit_commit();
	pocket_seq_set_last_page(1);
	pocket_seq_set_tempo(126);
	pocket_lxr_set_bpm(126);
	for (int t = 0; t < C12_TRACKS; t++) c12_mix_set_send(t, 30 + 12 * t, 90 - 10 * t);
	c12_fx_set(C12_FX_DLY_TIME, 6);
	c12_fx_set(C12_FX_DLY_FEEDBACK, 55);
	c12_fx_set(C12_FX_DLY_WET, 60);
	c12_fx_set(C12_FX_DLY_PINGPONG, 1);
	c12_fx_set(C12_FX_VERB_SIZE, 70);
	c12_fx_set(C12_FX_VERB_DECAY, 60);
	c12_fx_set(C12_FX_VERB_WET, 45);
	return 0;
}

/* block b of the live session: what the hands do at its start */
static void live_hands(unsigned b)
{
	const unsigned s = b * 64u / RATE; /* seconds */
	const unsigned at = b % (RATE / 64u); /* block within the second */
	if (at != 0 && at != 250) goto continuous;
	switch (s * 2 + (at ? 1 : 0))
	{
	case 2: c12_fx_set(C12_FX_DLY_TIME, 8); break;
	case 3: c12_fx_set(C12_FX_DLY_FILTER, 30); c12_mix_set_level(0, 70); break;
	case 4: c12_fx_set(C12_FX_DLY_PINGPONG, 0); c12_fx_set(C12_FX_DLY_FILTER, 100); break;
	case 5: c12_fx_set(C12_FX_VERB_SIZE, 20); c12_fx_set(C12_FX_VERB_DAMP, 80); c12_mix_set_mute(0x08); break;
	case 6: c12_perf_set_freeze(1, 0); c12_mix_set_mute(0); c12_mix_set_solo(0x21); break;
	case 7: c12_perf_set_freeze(1, 1); c12_mix_set_solo(0); break;
	case 8: c12_perf_set_freeze(0, 0); c12_fx_set(C12_FX_DLY_PINGPONG, 1); c12_perf_set_throw(0x01, 0x10); break;
	case 9: c12_perf_set_throw(0, 0); c12_perf_set_fill(0x0e); c12_perf_set_density(50); break;
	case 10: c12_perf_set_fill(0); c12_perf_set_density(-60); break;
	case 11: c12_perf_set_density(0); c12_perf_set_repeat(C12_DIV_16, 0x7f, 1); break;
	case 12: c12_perf_set_repeat(C12_DIV_OFF, 0, 0); break;
	case 13:
	{
		struct c12_mix_state m;
		c12_mix_get(&m);
		m.level[3] = 115; m.reverb[0] = 120; m.mute = 0x40; m.master = 90;
		c12_mix_schedule(&m, 1);
		break;
	}
	case 14: c12_kit_design(kit, 9, 4242u, 70); c12_kit_set_target(kit, C12_KIT_PARAMS); break;
	case 15: c12_fx_set(C12_FX_DRIVE_WET, 70); c12_fx_set(C12_FX_RING_WET, 30); c12_mix_set_route(0x0f); break;
	case 16: c12_fx_set(C12_FX_COMP_WET, 100); c12_fx_set(C12_FX_CRUSH_BITS, 9); break;
	case 17: c12_fx_set(C12_FX_DRIVE_WET, 0); c12_fx_set(C12_FX_RING_WET, 0); c12_fx_set(C12_FX_CRUSH_BITS, 16); c12_fx_set(C12_FX_LIMITER, 0); break;
	case 18: c12_fx_set(C12_FX_COMP_WET, 0); c12_fx_set(C12_FX_LIMITER, 1); c12_fx_set(C12_FX_SPACE_ENGINE, 1); break;
	case 19: c12_fx_set(C12_FX_SPACE_ENGINE, 0); break;
	case 20: pocket_seq_play(0); break;
	case 21: pocket_seq_play(1); break;
	case 22: c12_mix_set_master(110); c12_fx_set(C12_FX_VERB_DECAY, 100); break;
	default: break;
	}
continuous:
	/* rapid tempo changes for a second, note repeats for another */
	if (s == 16) pocket_seq_set_tempo(90 + (int)(b * 37u % 150u));
	if (s == 17 && at == 0) pocket_seq_set_tempo(126);
	if (s == 18) for (int t = 0; t < 3; t++) c12_perf_note_repeat(t, t == 0 ? C12_DIV_32 : C12_DIV_16T, 100, 80, C12_CURVE_FLAT);
	if (s == 19 && at == 0) for (int t = 0; t < 3; t++) c12_perf_note_repeat(t, C12_DIV_OFF, 100, 100, 0);
	if (b % 2 == 0) c12_kit_pump(48);
}

/* the delay and reverb one at a time (block b) */
static void apart_hands(unsigned b)
{
	const unsigned at = b % (RATE / 64u);
	const unsigned s = b * 64u / RATE;
	if (at != 0) return;
	switch (s)
	{
	case 0: for (int t = 0; t < C12_TRACKS; t++) c12_mix_set_send(t, t == 3 ? 90 : 0, 0); c12_fx_set(C12_FX_DLY_PINGPONG, 0); break;
	case 3: c12_mix_set_send(3, 0, 0); break;                    /* the echoes die away, the delay falls idle */
	case 5: pocket_seq_set_tempo(100); break;                   /* a new delay time while idle */
	case 6: c12_mix_set_send(0, 0, 100); c12_mix_set_send(5, 0, 70); break; /* the reverb alone */
	case 8: c12_mix_set_send(3, 80, 0); c12_fx_set(C12_FX_DLY_PINGPONG, 1); break; /* both */
	case 10: c12_mix_set_send(0, 0, 0); c12_mix_set_send(5, 0, 0); break;  /* the delay alone again */
	case 11: c12_perf_set_freeze(1, 0); break;
	case 12: c12_perf_set_freeze(0, 0); c12_mix_set_send(3, 0, 0); c12_fx_set(C12_FX_DLY_PINGPONG, 0); break;
	case 13: c12_mix_set_send(2, 60, 60); c12_perf_set_freeze(0, 1); break;
	case 15: c12_perf_set_freeze(0, 0); c12_mix_set_send(2, 0, 0); break; /* everything dies away */
	case 19: c12_perf_set_throw(0x08, 0); break;                 /* a throw from silence */
	case 20: c12_perf_set_throw(0, 0x01); pocket_seq_set_tempo(140); break;
	case 21: c12_perf_set_throw(0, 0); break;
	default: break;
	}
}

static int spread = 1;

static void session(const char* dir, const char* name, int which)
{
	SET_SPREAD(spread);
	if (setup(which == 2 ? 99u : 7u)) { printf("golden: init failed\n"); return; }
	if (which == 2)
	{
		c12_fx_set(C12_FX_DRIVE_WET, 80);
		c12_fx_set(C12_FX_RING_WET, 40);
		c12_fx_set(C12_FX_COMP_WET, 100);
		c12_fx_set(C12_FX_CRUSH_BITS, 10);
	}
	pocket_seq_play(1);
	const unsigned blocks = RATE * SECONDS / 64u;
	for (unsigned b = 0; b < blocks; b++)
	{
		if (which == 1) live_hands(b);
		if (which == 3) apart_hands(b);
		c12_engine_render(&pcm[b * 128u], 64);
	}
	struct c12_engine_stats st;
	c12_engine_stats_read(&st);
	char path[512];
	snprintf(path, sizeof path, "%s/%s.raw", dir, name);
	FILE* f = fopen(path, "wb");
	if (!f) { printf("golden: cannot write %s\n", path); return; }
	fwrite(pcm, 4, blocks * 64u, f);
	fclose(f);
	printf("golden %-7s %u s, %u LXR blocks, underruns %u, clipped %u, strokes %u, graces %u, repeats %u, space blocks %u\n", name, SECONDS,
	       (unsigned)st.lxr_blocks, (unsigned)st.underruns, (unsigned)st.clipped, (unsigned)st.strokes, (unsigned)st.graces,
	       (unsigned)st.repeats, (unsigned)st.space_blocks);
}

int main(int argc, char** argv)
{
	const char* dir = argc > 1 ? argv[1] : "build/golden";
	if (argc > 2 && !strcmp(argv[2], "nospread")) spread = 0;
	session(dir, "steady", 0);
	session(dir, "live", 1);
	session(dir, "colour", 2);
	session(dir, "apart", 3);
	return 0;
}
