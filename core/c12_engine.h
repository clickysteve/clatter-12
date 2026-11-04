/*
 * c12_engine.h - CLATTER 12's audio engine: CLATTER Pocket's LXR host and sequencer, run at
 * the LXR's own rate and converted to the output rate.
 *
 * Copyright (C) 2026 The CLATTER contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Portable C: nothing here knows about the Plinky 12. The panel (panel/clatter12.cpp) and the
 * host tests (tests/) are its only callers.
 *
 * Contexts (the same two as pocket_lxr.h, which this wraps):
 *   control context  c12_engine_init and every pocket_lxr_* / pocket_seq_* control call. On the
 *                    Plinky 12: on_ui, core 0, foreground only (never on_sequence or on_midi, so
 *                    there is one producer for Pocket's event ring).
 *   audio context    c12_engine_render. On the Plinky 12: on_dsp, core 1.
 *
 * Signal path per output block:
 *   while the resampler lacks input: pocket_lxr_render_q23 one 64-frame LXR block at 44 140 Hz
 *     (Pocket drains queued events and runs pocket_seq_block at its start, as on the LXR-02),
 *   c12_rs_process to the output rate, saturate to int16.
 * So the sequencer and triggers keep the LXR's own 64-sample (1.45 ms) timing.
 */
#ifndef C12_ENGINE_H
#define C12_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The rate the LXR-02 firmware's coefficients are written for (config.h REAL_FS). */
#define C12_LXR_RATE 44140u
#define C12_LXR_BLOCK 64u
/* Master gain into the 16-bit output, Q12: 3 dB under the LXR's unity (c12_engine_init). */
#define C12_MASTER_Q12 2896

struct c12_engine_stats {
	uint32_t out_blocks;     /* c12_engine_render calls */
	uint32_t lxr_blocks;     /* LXR blocks rendered */
	uint32_t lxr_max_per_out;/* most LXR blocks rendered in one output block (1 or 2 at 32 kHz) */
	uint32_t clipped;        /* output samples saturated */
	uint32_t underruns;      /* output blocks the resampler could not fill (must stay 0) */
};

/* The LXR's constant tables (sine, triangle/saw/square wavetables, transient samples) are kept
 * packed in the program and unpacked at start-up into memory the caller provides: C12_TABLE_BYTES,
 * 4-byte aligned, never moved or reused afterwards (static SRAM or PSRAM on the Plinky 12). Bind
 * once, before the first c12_engine_init or pocket_lxr_init. */
#define C12_TABLE_BYTES 102240u
void c12_tables_bind(void* mem);

/* Control context, before the audio context runs. `out_rate` is the output's sample rate (the
 * Plinky 12's SAMPLE_FREQ). Initialises the LXR (init kit), the sequencer (empty pattern,
 * stopped), the resampler and the DC blocker. Returns 0, or -1 (tables unbound or corrupt, or
 * rates it cannot convert). */
int c12_engine_init(uint32_t out_rate, uint32_t seed);

/* Audio context. `frames` stereo frames, interleaved int16, at out_rate. Any frame count up to
 * 128. */
void c12_engine_render(int16_t* out_lr, unsigned frames);

/* Either context: copies of the counters (single words, read without locking). */
void c12_engine_stats_read(struct c12_engine_stats* out);

/* ---- kits (control context) ------------------------------------------------------------ */
/* Sound parameters 1..C12_KIT_PARAMS-1 (Parameters.h numbering, 0-127). */
#define C12_KIT_PARAMS 256
/* Set the kit the engine should reach; c12_kit_pump sends the differences a few at a time. */
void c12_kit_set_target(const uint8_t* kit, unsigned params);
/* The kit as targeted. */
const uint8_t* c12_kit_target(void);
/* Queue up to `budget` changed parameters. Returns how many are still to send. */
unsigned c12_kit_pump(unsigned budget);
/* A kit from CLATTER Pocket's designer: `character` below POCKET_KIT_CHARACTERS, or the
 * firmware's init kit when character < 0. Written to `kit` (C12_KIT_PARAMS bytes). */
void c12_kit_design(uint8_t* kit, int character, uint32_t seed, unsigned wildness);

#ifdef __cplusplus
}
#endif

#endif
