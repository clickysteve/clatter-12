/*
 * export_patterns.c - real CLATTER patterns for the project page (docs/assets/patterns.js).
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 *
 * Runs CLATTER Pocket's generator and mutator (the code the panel runs) for every style, three
 * seeds each, at 64 steps, density 50 and complexity 50 (the panel's defaults), then two successive
 * medium mutations of each. Writes a binary file of 2 bytes per cell (velocity 0-127, variation 0-5),
 * seven tracks by 64 steps per pattern, and prints the style and kit names as JSON lines for
 * tools/make_site_data.py. The page replays these: it does not generate anything itself.
 *   export_patterns OUT.bin
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pocket_seq.h"
#include "pocket_gen.h"
#include "pocket_kit.h"

#define SEEDS 3
#define CHAIN 3 /* the generated pattern, then two mutations */

static void put(FILE* f, const struct pocket_pattern* p)
{
	for (int t = 0; t < POCKET_SEQ_TRACKS; t++)
		for (int s = 0; s < POCKET_SEQ_MAX_STEPS; s++)
		{
			const uint32_t c = p->cell[t][s];
			uint8_t b[2] = { (uint8_t)POCKET_CELL_VELOCITY(c), (uint8_t)POCKET_CELL_VARIATION(c) };
			fwrite(b, 1, 2, f);
		}
}

int main(int argc, char** argv)
{
	FILE* f = fopen(argc > 1 ? argv[1] : "patterns.bin", "wb");
	static struct pocket_pattern p;
	printf("{\"styles\":[");
	for (unsigned s = 0; s < POCKET_GEN_STYLES; s++) printf("%s\"%s\"", s ? "," : "", pocket_gen_style_name(s));
	printf("],\"kits\":[");
	for (unsigned k = 0; k < POCKET_KIT_CHARACTERS; k++) printf("%s\"%s\"", k ? "," : "", pocket_kit_name(k));
	printf("],\"seeds\":%d,\"chain\":%d,\"tracks\":%d,\"steps\":%d}\n", SEEDS, CHAIN, POCKET_SEQ_TRACKS, POCKET_SEQ_MAX_STEPS);
	for (unsigned style = 0; style < POCKET_GEN_STYLES; style++)
		for (uint32_t seed = 1; seed <= SEEDS; seed++)
		{
			struct pocket_gen_settings g = { seed * 7919u, 50, 50, (uint8_t)style, 0, 3 };
			memset(&p, 0, sizeof p);
			pocket_gen_generate(&p, &g);
			put(f, &p);
			for (unsigned m = 1; m < CHAIN; m++)
			{
				pocket_gen_mutate(&p, (seed << 16) | m, POCKET_MUTATE_MEDIUM, 0);
				put(f, &p);
			}
		}
	fclose(f);
	return 0;
}
