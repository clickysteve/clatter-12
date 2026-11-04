/*
 * c12_wasm.c - CLATTER 12's engine as a WebAssembly module for the project page.
 *
 * Copyright (C) 2026 The CLATTER contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Not part of the Plinky panel. Linked with the generated engine text (build/c12_engine_c.c: the code
 * the panel contains) and CLATTER's libm shim for WebAssembly. The page uses two instances of the
 * module: one in an AudioWorklet (everything: sound and sequencer) and one on the main thread, only
 * to run CLATTER's generator and mutator for GEN and MUT without waiting for the audio thread.
 *
 * Calls follow the panel's (panel/clatter12.cpp), in the same order and with the same arguments, so
 * what the page plays is what the panel would. The pattern crosses the boundary as the sequencer's own
 * 32-bit cells (velocity, tone, condition), 7 tracks by 64 steps, in c12w_io().
 */
#include <stdint.h>
#include <string.h>
#include "c12_engine.h"
#include "pocket_lxr.h"
#include "pocket_seq.h"
#include "pocket_gen.h"
#include "pocket_kit.h"

#define EXPORT(n) __attribute__((export_name(n)))

static uint32_t tableMem[C12_TABLE_BYTES / 4];
static uint32_t io[POCKET_SEQ_TRACKS * POCKET_SEQ_MAX_STEPS];
static int16_t pcm[128 * 2];
static uint8_t kit[C12_KIT_PARAMS];
static struct pocket_pattern work;

EXPORT("c12w_io") uint32_t* c12w_io(void) { return io; }
EXPORT("c12w_pcm") int16_t* c12w_pcm(void) { return pcm; }

/* ---- audio instance ---- */
EXPORT("c12w_init") int c12w_init(uint32_t rate, uint32_t seed)
{
	c12_tables_bind(tableMem);
	return c12_engine_init(rate, seed);
}
/* One audio quantum (up to 128 frames). The kit is streamed 48 parameters a quantum, as the panel's
 * on_ui does every 4 ms (a 128-frame quantum at 32 kHz is 4 ms). */
EXPORT("c12w_render") void c12w_render(int frames)
{
	c12_kit_pump(48);
	c12_engine_render(pcm, (unsigned)frames);
}
EXPORT("c12w_cell") void c12w_cell(int track, int step, uint32_t cell) { pocket_seq_set_cell(track, step, cell); }
EXPORT("c12w_pages") void c12w_pages(int n) { pocket_seq_set_last_page((unsigned)(n - 1)); }
EXPORT("c12w_commit") void c12w_commit(int pages)
{
	struct pocket_pattern* p = pocket_seq_edit_begin();
	memcpy(p->cell, io, sizeof p->cell);
	p->last_page = (uint8_t)(pages - 1);
	pocket_seq_edit_commit();
}
EXPORT("c12w_play") void c12w_play(int on) { pocket_seq_play(on); }
EXPORT("c12w_tempo") void c12w_tempo(int bpm)
{
	pocket_seq_set_tempo(bpm);
	pocket_lxr_set_bpm(bpm);
}
EXPORT("c12w_position") int c12w_position(void) { return pocket_seq_position(); }
EXPORT("c12w_trigger") void c12w_trigger(int track, int velocity, int variation)
{
	pocket_lxr_trigger_variation(track, velocity, POCKET_SEQ_NOTE_PROGRAMMED, variation);
}
/* `character` below 0: the LXR firmware's init kit. As the panel's next_kit and engine bring-up. */
EXPORT("c12w_kit") void c12w_kit(int character, uint32_t seed, int wildness)
{
	c12_kit_design(kit, character, seed, (unsigned)wildness);
	c12_kit_set_target(kit, C12_KIT_PARAMS);
}

/* ---- generator (either instance): io in, io out ---- */
EXPORT("c12w_generate") void c12w_generate(uint32_t seed, int density, int complexity, int style, int pages)
{
	memcpy(work.cell, io, sizeof work.cell);
	work.last_page = (uint8_t)(pages - 1);
	struct pocket_gen_settings s = { seed, (uint8_t)density, (uint8_t)complexity, (uint8_t)style, 0, work.last_page };
	pocket_gen_generate(&work, &s);
	memcpy(io, work.cell, sizeof io);
}
EXPORT("c12w_mutate") void c12w_mutate(uint32_t seed, int strength, int pages)
{
	memcpy(work.cell, io, sizeof work.cell);
	work.last_page = (uint8_t)(pages - 1);
	pocket_gen_mutate(&work, seed, (unsigned)strength, 0);
	memcpy(io, work.cell, sizeof io);
}
EXPORT("c12w_style_name") const char* c12w_style_name(int i) { return pocket_gen_style_name((unsigned)i); }
EXPORT("c12w_kit_name") const char* c12w_kit_name(int i) { return pocket_kit_name((unsigned)i); }
