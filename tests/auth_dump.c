/*
 * auth_dump.c - the authenticity scenario of test_authentic.cpp against any C build of the LXR
 * engine host (CLATTER Pocket's double or single-precision library, or CLATTER 12's C build), so the
 * builds can be compared with each other in separate processes. Writes the 44 140 Hz Q23 mix.
 *   auth_dump OUT.raw MODE     MODE bits: 1 kits, 2 parameter moves, 4 no variations
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "pocket_lxr.h"
#include "pocket_kit.h"
#include "pocket_params.h"

static void design(uint8_t* kit, unsigned ch, uint32_t seed)
{
	memset(kit, 0, 256);
	for (int p = 1; p < POCKET_SOUND_PARAMS; p++) kit[p] = (uint8_t)pocket_lxr_init_kit_value(p);
	for (int v = 0; v < POCKET_LXR_VOICES; v++) kit[POCKET_PAR_VOLUME_MOD_ON_OFF1 + v] = 1;
	pocket_kit_generate(kit, ch, seed, 50);
	for (int p = POCKET_PAR_FX_TYPE; p <= POCKET_PAR_FX_DELAY_TYPE; p++) kit[p] = 0;
}

int main(int argc, char** argv)
{
	const int MODE = argc > 2 ? atoi(argv[2]) : 3;
	const int BLOCKS = 44140 * 20 / 64;
	static int32_t q[64 * 2];
	static uint8_t kit[256];
	FILE* f = fopen(argv[1], "wb");
#ifdef C12_BUILD
	static uint32_t tables[102240 / 4];
	extern void c12_tables_bind(void*);
	c12_tables_bind(tables);
#endif
	pocket_lxr_init(3);
	pocket_lxr_set_dc_block(1);
	uint32_t rng = 1;
	int kits = 0;
	for (int blk = 0; blk < BLOCKS; ++blk)
	{
		if ((MODE & 1) && blk % (44140 * 4 / 64) == 0)
		{
			design(kit, (unsigned)(kits * 5 % 16), 1000u + (unsigned)kits);
			for (int p = 1; p < 247; ++p) pocket_lxr_set_param(p, kit[p]);
			kits++;
		}
		if (blk % 6 == 0)
		{
			rng = rng * 1664525u + 1013904223u;
			int trig = (int)((rng >> 8) % 7), vel = 30 + (int)((rng >> 16) % 98), note = 48 + (int)((rng >> 4) % 25);
			int var = (MODE & 4) ? 0 : (int)((rng >> 24) % 3);
			pocket_lxr_trigger_variation(trig, vel, note, var);
		}
		if ((MODE & 2) && blk % 40 == 7)
		{
			rng = rng * 1664525u + 1013904223u;
			int p = 1 + (int)((rng >> 8) % 200), v = (int)((rng >> 16) % 128);
			if (p < 234) pocket_lxr_set_param(p, v);
		}
		pocket_lxr_render_q23(q, 64, 0);
		fwrite(q, 4, 128, f);
	}
	fclose(f);
	return 0;
}
