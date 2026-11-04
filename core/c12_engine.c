/*
 * c12_engine.c - see c12_engine.h.
 *
 * Copyright (C) 2026 The CLATTER contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdint.h>
#include <string.h>
#include "c12_engine.h"
#include "c12_resampler.h"
#include "pocket_lxr.h"
#include "pocket_seq.h"
#include "pocket_kit.h"
#include "pocket_params.h"
#include "Soundengine/DSPAudio/wavetable.h"
#include "Soundengine/DSPAudio/transientTables.h"

/* The firmware's marker for per-sample code (Soundengine/config.h): tools/amalgamate.py turns
 * it into C12_LXR_CODE, which the panel build places in flash or SRAM. */
#include "Soundengine/config.h"

_Static_assert(C12_LXR_BLOCK == POCKET_LXR_BLOCK, "LXR block size");
_Static_assert(C12_KIT_PARAMS >= POCKET_SOUND_PARAMS, "kit buffer");

static struct c12_resampler rs;
static int32_t lxrQ23[C12_LXR_BLOCK * 2];
static float lxrF[C12_LXR_BLOCK * 2];
static float outF[128 * 2];
static volatile struct c12_engine_stats st;

/* the kit as the control context wants it, and as already queued to the engine */
static uint8_t kitTarget[C12_KIT_PARAMS], kitSent[C12_KIT_PARAMS];

static int tablesBound;

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

int c12_engine_init(uint32_t out_rate, uint32_t seed)
{
	if (!tablesBound) return -1;
	if (pocket_lxr_init(seed) != 0) return -1;
	/* The LXR-02's outputs are AC-coupled; Pocket's FM-1 build removes the decimator's held DC
	 * the same way (pocket_lxr.h). */
	pocket_lxr_set_dc_block(1);
	/* 3 dB under the LXR's unity master: the six voices can sum past full scale, and here the
	 * mix is saturated to 16 bits before Plinky's own output stage (level, EQ, limiter). */
	pocket_lxr_set_master(C12_MASTER_Q12);
	/* The sequencer counts engine samples: its rate is the LXR's, whose blocks are its clock. */
	pocket_seq_init(C12_LXR_RATE, POCKET_LXR_BLOCK, pocket_lxr_trigger_now);
	pocket_lxr_set_block_hook(pocket_seq_block);
	if (c12_rs_init(&rs, C12_LXR_RATE, out_rate, 1.0f) != 0) return -1;
	memset((void*)&st, 0, sizeof st);
	for (int p = 1; p < POCKET_SOUND_PARAMS; p++)
		kitTarget[p] = kitSent[p] = (uint8_t)pocket_lxr_init_kit_value(p);
	return 0;
}

IN_ITCM_RAM void c12_engine_render(int16_t* out_lr, unsigned frames)
{
	if (frames > 128) frames = 128;
	unsigned rendered = 0;
	while (rs.count < c12_rs_needed(&rs, frames))
	{
		if (c12_rs_room(&rs) < C12_LXR_BLOCK)
		{
			st.underruns++;
			memset(out_lr, 0, frames * 2 * sizeof(int16_t));
			return;
		}
		pocket_lxr_render_q23(lxrQ23, C12_LXR_BLOCK, 0);
		/* Q23 full scale is int16 full scale (pocket_lxr.h) */
		for (unsigned i = 0; i < C12_LXR_BLOCK * 2; i++) lxrF[i] = (float)lxrQ23[i] * (1.f / 256.f);
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
	st.out_blocks++;
	st.lxr_blocks += rendered;
	if (rendered > st.lxr_max_per_out) st.lxr_max_per_out = rendered;
	st.clipped += clipped;
}

void c12_engine_stats_read(struct c12_engine_stats* out)
{
	out->out_blocks = st.out_blocks;
	out->lxr_blocks = st.lxr_blocks;
	out->lxr_max_per_out = st.lxr_max_per_out;
	out->clipped = st.clipped;
	out->underruns = st.underruns;
}

/* ------------------------------------------------------------------ kits (control) */
void c12_kit_set_target(const uint8_t* kit, unsigned params)
{
	if (params > POCKET_SOUND_PARAMS) params = POCKET_SOUND_PARAMS;
	for (unsigned p = 1; p < params; p++) kitTarget[p] = kit[p];
	/* The FX bus is not built (engine/fx_off.c): its parameters stay 0, as in Pocket. */
	for (int p = POCKET_PAR_FX_TYPE; p <= POCKET_PAR_FX_DELAY_TYPE; p++) kitTarget[p] = 0;
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
