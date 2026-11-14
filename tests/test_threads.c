/*
 * test_threads.c - the pattern lock under real concurrency: one thread renders audio (the engine's
 * audio context, which switches patterns), another edits, replaces and queues patterns under the
 * lock (the control context), as fast as they can, on two cores.
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 *
 * What must hold (c12_engine.h, "pattern switching"):
 *   - while the control context holds the lock, the audio context makes no switch;
 *   - the pattern playing is always whole, and is the slot the engine says is playing;
 *   - after a switch, the pattern it replaced is whole, and is the slot that was playing before,
 *     with the edits made to it;
 *   - nothing deadlocks, and the audio never underruns.
 * Every pattern carries its slot in each cell it uses (velocity = slot + 1), so a torn or misplaced
 * pattern shows. Linked with build/c12_unity.o. The host schedules threads as it likes, so a switch
 * may find the lock held longer than the engine waits (c12_seq_late): reported, not a failure.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "c12_engine.h"
#include "pocket_lxr.h"
#include "pocket_seq.h"

static uint32_t tableMem[C12_TABLE_BYTES / 4];
static atomic_int stop;
static atomic_uint blocks;

static void* audio(void* arg)
{
	(void)arg;
	int16_t out[64 * 2];
	while (!atomic_load(&stop))
	{
		c12_engine_render(out, 64);
		atomic_fetch_add(&blocks, 1);
	}
	return 0;
}

#define SLOTS 16
static void make(struct pocket_pattern* p, unsigned slot, uint32_t seed)
{
	memset(p, 0, sizeof *p);
	p->last_page = (uint8_t)(seed & 3u);
	for (int t = 0; t < 7; t++)
		for (int s = 0; s < 64; s += 1 + (int)((seed >> t) & 3u)) p->cell[t][s] = POCKET_CELL(slot + 1u, 0);
	p->cell[0][0] = POCKET_CELL(slot + 1u, 0);
}
/* every used cell says `slot`, and cell 0 is used: the pattern is whole and is that slot's */
static int is_slot(const struct pocket_pattern* p, unsigned slot)
{
	if (POCKET_CELL_VELOCITY(p->cell[0][0]) != slot + 1u) return 0;
	for (int t = 0; t < 7; t++)
		for (int s = 0; s < 64; s++)
		{
			const unsigned v = POCKET_CELL_VELOCITY(p->cell[t][s]);
			if (v && v != slot + 1u) return 0;
		}
	return 1;
}

int main(void)
{
	c12_tables_bind(tableMem);
	if (c12_engine_init(32000, 1) != 0) return 1;
	pocket_seq_set_tempo(300);
	pocket_lxr_set_bpm(300);
	static struct pocket_pattern pat, copy;
	make(&pat, 0, 1);
	c12_seq_lock();
	c12_seq_put_locked(&pat, 0);
	c12_seq_unlock();
	pocket_seq_play(1);

	pthread_t th;
	pthread_create(&th, 0, audio, 0);
	uint32_t seen = c12_seq_switches(), rnd = 12345, during = 0, torn = 0, wrong_live = 0, wrong_replaced = 0, edits = 0, queued = 0;
	unsigned cur = 0, lastQueued = 0;
	const unsigned target = 1500; /* switches */
	for (unsigned iter = 0; c12_seq_switches() < target && iter < 400000000u; iter++)
	{
		rnd = rnd * 1664525u + 1013904223u;
		c12_seq_lock();
		const uint32_t sw = c12_seq_switches();
		if (sw != seen)
		{
			/* exactly one switch can have been made: the one queued */
			if (sw != seen + 1) wrong_live++;
			if (!is_slot(pocket_seq_replaced(), cur)) wrong_replaced++;
			seen = sw;
			cur = c12_seq_live_slot();
			if (cur != lastQueued) wrong_live++;
		}
		copy = *pocket_seq_pattern();
		if (!is_slot(&copy, cur)) torn++;
		switch (rnd >> 28)
		{
		case 0: case 1: case 2: /* an edit of the pattern playing, in place */
			pocket_seq_set_cell((int)(rnd >> 8) % 7, 1 + (int)(rnd >> 12) % 63, rnd & 1u ? POCKET_CELL(cur + 1u, 0) : 0u);
			edits++;
			break;
		case 3: /* the pattern playing replaced whole (an undo, a generated pattern) */
			make(&pat, cur, rnd);
			c12_seq_put_locked(&pat, cur);
			edits++;
			break;
		case 4: case 5: /* another slot queued, unless one is waiting */
			if (!c12_seq_queued(0))
			{
				lastQueued = (cur + 1u + (rnd >> 16) % (SLOTS - 1u)) % SLOTS;
				make(&pat, lastQueued, rnd);
				c12_seq_queue_locked(&pat, lastQueued, 50, rnd & 2u ? C12_SWITCH_NOW : C12_SWITCH_BAR);
				queued++;
			}
			break;
		default: break;
		}
		if (c12_seq_switches() != sw) during++; /* a switch while the lock was held */
		c12_seq_unlock();
		if ((rnd >> 20 & 7u) == 0) sched_yield();
	}
	atomic_store(&stop, 1);
	pthread_join(th, 0);
	struct c12_engine_stats st;
	c12_engine_stats_read(&st);
	printf("threads: %u switches over %u audio blocks; %u edits, %u queued, %u made a step late (host scheduling)\n",
	       (unsigned)c12_seq_switches(), (unsigned)atomic_load(&blocks), (unsigned)edits, (unsigned)queued, (unsigned)c12_seq_late());
	CHECK(c12_seq_switches() >= target, "the engine went on switching: no deadlock (%u switches)", (unsigned)c12_seq_switches());
	CHECK(during == 0, "no switch was made while the control thread held the lock (%u)", (unsigned)during);
	CHECK(torn == 0, "the pattern playing was always whole and the slot the engine names (%u)", (unsigned)torn);
	CHECK(wrong_replaced == 0, "after each switch the pattern it replaced was whole, with its edits, and the slot before (%u)", (unsigned)wrong_replaced);
	CHECK(wrong_live == 0, "one switch at a time, to the slot queued (%u)", (unsigned)wrong_live);
	CHECK(st.underruns == 0, "no underruns");
	return CHECK_REPORT("threads");
}
