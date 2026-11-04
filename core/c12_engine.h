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
	uint32_t strokes;        /* ratchet strokes after a step's first */
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
 * 128 (tests/test_engine.c: the audio does not depend on the block size). */
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

/* ---- steps: CLATTER 12's bits of a sequencer cell -------------------------------------- */
/* Pocket's cell (pocket_seq.h) uses bits 0-19: velocity, tone (note or variation), condition. The
 * sequencer reads nothing above them, so CLATTER 12 keeps a step's ratchet there, where it moves,
 * copies, undoes and saves with the step: bits 20-22 the number of strokes (C12_RATCHET_*), 23-24
 * how they are spaced in the step, 25-26 how their velocity runs. A cell without them plays once,
 * exactly as Pocket plays it. */
enum { C12_RATCHET_1, C12_RATCHET_2, C12_RATCHET_3, C12_RATCHET_4, C12_RATCHET_6, C12_RATCHET_8, C12_RATCHETS };
enum { C12_ROLL_EVEN, C12_ROLL_ACCEL, C12_ROLL_DECEL, C12_ROLL_RANDOM, C12_ROLL_SHAPES };
/* velocity: CLATTER Web's ratchet (each stroke 12 % softer), and its ROLLS shapes */
enum { C12_RAMP_DECAY, C12_RAMP_CRESC, C12_RAMP_DECRESC, C12_RAMP_HUMAN, C12_RAMPS };
#define C12_CELL_RATCHET(cell) ((unsigned)((cell) >> 20 & 7u))
#define C12_CELL_ROLL(cell) ((unsigned)((cell) >> 23 & 3u))
#define C12_CELL_RAMP(cell) ((unsigned)((cell) >> 25 & 3u))
#define C12_CELL_EXT_MASK (0x7fu << 20)
#define C12_CELL_WITH_EXT(cell, ratchet, roll, ramp) \
	(((cell) & ~C12_CELL_EXT_MASK) | ((uint32_t)(ratchet) & 7u) << 20 | ((uint32_t)(roll) & 3u) << 23 | ((uint32_t)(ramp) & 3u) << 25)
/* strokes of a ratchet index */
unsigned c12_ratchet_strokes(unsigned ratchet);

/* ---- mixer (control context writes single bytes; the audio context reads them) ---------- */
#define C12_TRACKS 7
#define C12_LEVEL_UNITY 100 /* level 0-127: gain (level / 100)^2, so 127 is +4.2 dB, 50 is -12 dB */
void c12_mix_set_level(int track, int level);
void c12_mix_set_mute(unsigned mask);   /* bit per track */
void c12_mix_set_solo(unsigned mask);
void c12_mix_set_send(int track, int delay, int reverb); /* 0-127 into Plinky's delay and reverb */
void c12_mix_set_route(unsigned mask);  /* tracks through the FX chain (bit per track) */
void c12_mix_set_master(int level);     /* 0-127, as a track level */
/* Whether a track is heard now (mute and solo). */
int c12_mix_audible(int track);

/* ---- FX (control context sets values; the audio context applies them at a block start) --- */
/* The chain, in this order, on the tracks routed to it: the LXR-02 firmware's own master
 * distortion, ring modulator and compressor (DSPAudio/FX, unchanged), each with its dry/wet.
 * Then on the whole mix: CLATTER 12's bit reduction and its output limiter. Sample-rate reduction
 * is the LXR's own: VOICE_DECIMATION_ALL, every voice's decimator. Off (wet 0) costs nothing. */
enum {
	C12_FX_DRIVE_WET, C12_FX_DRIVE_TYPE, C12_FX_DRIVE_AMOUNT, C12_FX_DRIVE_TONE, C12_FX_DRIVE_LEVEL,
	C12_FX_RING_WET, C12_FX_RING_FREQ,
	C12_FX_COMP_WET, C12_FX_COMP_THRESH, C12_FX_COMP_RATIO, C12_FX_COMP_ATTACK, C12_FX_COMP_RELEASE, C12_FX_COMP_GAIN,
	C12_FX_CRUSH_BITS, C12_FX_RATE, C12_FX_LIMITER,
	C12_FX_PARAMS
};
/* Value ranges: _WET 0-100 (%), _TYPE 0-2 (TUBE, FOLD, RAT), _RATIO 0-5 (1:1 2:1 3:1 4:1 6:1 8:1),
 * CRUSH_BITS 4-16 (16 = off), RATE 0-127 (127 = full rate), LIMITER 0-1, the rest 0-127 as the
 * LXR's FX P1-P4 (value / 127). */
void c12_fx_set(int fx, int value);
int c12_fx_get(int fx);
int c12_fx_max(int fx);
int c12_fx_min(int fx);

/* ---- sends to Plinky's own delay and reverb ----------------------------------------------- */
/* Mono, at the output rate, for the frames of the last c12_engine_render (int, full scale
 * 32767), ready for mix_buffers_t delaysend / reverbsend. NULL until a send is non-zero. */
const int32_t* c12_engine_send(int which); /* 0 delay, 1 reverb */

/* ---- measurement -------------------------------------------------------------------------- */
/* A microsecond clock (time_us on the Plinky) for per-stage timing of c12_engine_render, or NULL. */
typedef uint32_t (*c12_clock_fn)(void);
void c12_engine_set_clock(c12_clock_fn fn);
struct c12_engine_profile {
	uint32_t calls;
	uint32_t lxr_us;     /* LXR voices and sequencer (pocket_lxr_render_q23) */
	uint32_t mix_us;     /* CLATTER 12's mix, FX, limiter */
	uint32_t rs_us;      /* rate conversion and output */
	uint32_t total_max_us;
	uint32_t limited;    /* samples the limiter turned down */
	uint32_t peak;       /* largest output |sample| since the last read, 0..32767 */
	uint16_t track_peak[C12_TRACKS]; /* each voice's largest |sample|, by its track */
};
/* Copies and resets (counters restart). */
void c12_engine_profile_read(struct c12_engine_profile* out);

#ifdef __cplusplus
}
#endif

#endif
