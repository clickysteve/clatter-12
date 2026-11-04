/*
 * render_clips.c - audio clips of CLATTER 12's engine for the project page (docs/assets/audio/).
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 *
 * Renders, on a desktop, with the generated engine text compiled as C (the code the panel
 * contains, build/c12_engine_c.c): a pattern from CLATTER's generator, a kit from CLATTER's
 * kit designer, the sequencer at the LXR's own block timing, the 44.14 -> 32 kHz converter. Not a
 * recording of a Plinky. 32 kHz, 16-bit stereo WAV, peak-normalised to -1 dBFS (the engine itself
 * runs 3 dB under the LXR's unity). Two passes of a 32-step pattern, then 0.6 s of tail.
 *   render_clips OUTDIR     writes clip1.wav ... and prints a JSON description of each
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "c12_engine.h"
#include "pocket_seq.h"
#include "pocket_gen.h"

struct clip { const char* file; unsigned style, kit, bpm, seed; };
static const struct clip CLIPS[] = {
	{ "machine-techno", 0, 6, 124, 11 },
	{ "electro-electro", 1, 7, 118, 5 },
	{ "dub-dub", 10, 8, 100, 3 },
	{ "glitch-glitch", 8, 5, 132, 9 },
};
#define OUT_RATE 32000u

static uint32_t tableMem[C12_TABLE_BYTES / 4];

int main(int argc, char** argv)
{
	const char* dir = argc > 1 ? argv[1] : ".";
	static uint8_t kit[C12_KIT_PARAMS];
	printf("[");
	for (unsigned i = 0; i < sizeof CLIPS / sizeof CLIPS[0]; i++)
	{
		const struct clip* c = &CLIPS[i];
		c12_tables_bind(tableMem);
		if (c12_engine_init(OUT_RATE, 7u + i) != 0) return 1;
		c12_kit_design(kit, (int)c->kit, 100u + c->seed, 50);
		c12_kit_set_target(kit, C12_KIT_PARAMS);
		struct pocket_gen_settings g = { c->seed * 7919u, 50, 50, (uint8_t)c->style, 0, 1 };
		pocket_gen_generate(pocket_seq_edit_begin(), &g);
		pocket_seq_edit_commit();
		pocket_seq_set_last_page(1);
		pocket_seq_set_tempo((int)c->bpm);
		const double loop_s = 32.0 * 60.0 / c->bpm / 4.0;
		const unsigned warm = OUT_RATE / 4, body = (unsigned)(loop_s * 2.0 * OUT_RATE), tail = OUT_RATE * 6 / 10;
		const unsigned total = (warm + body + tail + 63) / 64 * 64;
		int16_t* pcm = calloc((size_t)total * 2, sizeof(int16_t));
		for (unsigned n = 0; n < total; n += 64)
		{
			if (n % 128 == 0) c12_kit_pump(48);
			if (n == warm) pocket_seq_play(1);
			if (n == warm + body) pocket_seq_play(0);
			c12_engine_render(&pcm[2 * n], 64);
		}
		int peak = 1;
		for (unsigned n = 0; n < total * 2; n++) if (abs(pcm[n]) > peak) peak = abs(pcm[n]);
		const double gain = 0.891 * 32767.0 / peak;
		for (unsigned n = 0; n < total * 2; n++) pcm[n] = (int16_t)lrint(pcm[n] * gain);
		char path[512];
		snprintf(path, sizeof path, "%s/%s.wav", dir, c->file);
		FILE* f = fopen(path, "wb");
		const uint32_t data = (total - warm) * 4u, riff = 36u + data, fmt = 16u, br = OUT_RATE * 4u, rate = OUT_RATE;
		const uint16_t pcmf = 1, ch = 2, align = 4, bits = 16;
		fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
		fwrite(&fmt, 4, 1, f); fwrite(&pcmf, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
		fwrite(&br, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
		fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
		fwrite(pcm + 2 * (size_t)warm, 4, total - warm, f); /* drop the silent warm-up */
		fclose(f);
		free(pcm);
		printf("%s{\"file\":\"%s\",\"style\":%u,\"kit\":%u,\"bpm\":%u,\"seconds\":%.1f}", i ? "," : "", c->file, c->style, c->kit,
		       c->bpm, (double)(total - warm) / OUT_RATE);
	}
	printf("]\n");
	return 0;
}
