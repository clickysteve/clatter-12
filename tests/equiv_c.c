/* equiv_c.c - the scenario against the native C unity build. Writes raw int16 to argv[1].
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <stdint.h>
#include "c12_engine.h"
#include "pocket_seq.h"
#include "pocket_gen.h"
#include "scenario.inc"

static int16_t pcm[32000 * 5 * 2];
int main(int argc, char** argv)
{
	unsigned n = scenario_render(pcm, 32000 * 5);
	FILE* f = fopen(argc > 1 ? argv[1] : "build/test/equiv_c.raw", "wb");
	fwrite(pcm, 4, n, f);
	fclose(f);
	printf("equiv (C): %u frames\n", n);
	return n ? 0 : 1;
}
