/*
 * bench.c - host timing of c12_engine_render per 64-frame, 32 kHz block (2000 us of audio).
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 *
 * HOST ONLY. A desktop CPU with large caches says nothing reliable about the RP2350's
 * Cortex-M33 reading tables through a small XIP cache. Use it to compare versions of the code
 * against each other; measure the device with the panel's own timing (DSP settings page, and
 * the "c12 dsp" log line).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "c12_engine.h"
#include "pocket_seq.h"

static double now_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static int cmp(const void* a, const void* b)
{
	double x = *(const double*)a, y = *(const double*)b;
	return x < y ? -1 : x > y;
}

static void run(const char* name, int all)
{
	enum { BLOCKS = 16000 }; /* 32 s */
	static double t[BLOCKS];
	static int16_t out[128];
	c12_engine_init(32000, 5);
	struct pocket_pattern* p = pocket_seq_edit_begin();
	memset(p, 0, sizeof *p);
	if (all)
		for (int tr = 0; tr < POCKET_SEQ_TRACKS; tr++)
			for (int s = 0; s < 16; s++) p->cell[tr][s] = POCKET_CELL(110, 0);
	pocket_seq_edit_commit();
	pocket_seq_set_tempo(150);
	pocket_seq_play(1);
	for (int i = 0; i < BLOCKS; i++)
	{
		double a = now_us();
		c12_engine_render(out, 64);
		t[i] = now_us() - a;
	}
	double sum = 0;
	for (int i = 0; i < BLOCKS; i++) sum += t[i];
	qsort(t, BLOCKS, sizeof t[0], cmp);
	printf("  %-38s mean %6.2f us  p99 %6.2f us  max %6.2f us  (%.2f%% of a 2000 us block, host)\n", name,
	       sum / BLOCKS, t[BLOCKS * 99 / 100], t[BLOCKS - 1], sum / BLOCKS / 20.0);
}

static uint32_t tableMem[C12_TABLE_BYTES / 4];

int main(void)
{
	c12_tables_bind(tableMem);
	printf("bench (host CPU, not the RP2350):\n");
	run("idle (pattern empty, playing)", 0);
	run("all seven triggers every 1/16 @150", 1);
	return 0;
}
