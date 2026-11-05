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
	uint32_t strokes;        /* ratchet and roll strokes after a step's first (graces included) */
	uint32_t graces;         /* flam and drag grace notes played ahead of their step */
	uint32_t repeats;        /* note-repeat and beat-repeat hits */
	uint32_t added;          /* hits FILL and DENSITY added */
	uint32_t dropped;        /* hits DENSITY took out */
	uint32_t thinned_strokes;/* ratchets played with fewer strokes (C12_MIN_STROKE_MS) */
	uint32_t space_blocks;   /* LXR blocks in which the delay and reverb ran */
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

/* Spreading (on by default): after making a callback's output, the engine starts on the LXR
 * blocks the next callback will need, up to the callback's average share of the work, so that a
 * callback needing two LXR blocks no longer does them both. The output samples are the same; a
 * control change made between callbacks may reach the engine one LXR block (1.45 ms) later. Off:
 * the first version's timing exactly (every block rendered in the callback that needs it). */
void c12_engine_set_spread(int on);

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
 * sequencer reads nothing above them, so CLATTER 12 keeps a step's articulation there, where it
 * moves, copies, undoes and saves with the step (project version 2):
 *   bits 20-22  ratchet: strokes within the step (C12_RATCHET_*: 1 2 3 4 6 8)
 *   bits 23-25  velocity curve of the strokes (C12_CURVE_*)
 *   bits 26-28  roll: an articulation (C12_ROLL_*), separate from the ratchet
 * A cell without them plays once, exactly as Pocket plays it. Version 1 kept a stroke spacing in
 * 23-24 and a velocity run in 25-26: c12_cell_from_v1 converts. */
enum { C12_RATCHET_1, C12_RATCHET_2, C12_RATCHET_3, C12_RATCHET_4, C12_RATCHET_6, C12_RATCHET_8, C12_RATCHETS };
enum { C12_CURVE_FLAT, C12_CURVE_FADE_IN, C12_CURVE_FADE_OUT, C12_CURVE_ACCENT_FIRST, C12_CURVE_ACCENT_LAST,
       C12_CURVE_ALTERNATE, C12_CURVE_HUMAN, C12_CURVES };
/* flam and drag: grace notes before the hit (played ahead of the step); double and triple
 * strokes within the step; accelerating and decelerating rolls over two steps (an eighth) */
enum { C12_ROLL_NONE, C12_ROLL_FLAM, C12_ROLL_DRAG, C12_ROLL_DOUBLE, C12_ROLL_TRIPLE, C12_ROLL_ACCEL, C12_ROLL_DECEL, C12_ROLLS };
#define C12_CELL_RATCHET(cell) ((unsigned)((cell) >> 20 & 7u))
#define C12_CELL_CURVE(cell) ((unsigned)((cell) >> 23 & 7u))
#define C12_CELL_ROLL(cell) ((unsigned)((cell) >> 26 & 7u))
#define C12_CELL_EXT_MASK (0x1ffu << 20)
#define C12_CELL_WITH_EXT(cell, ratchet, curve, roll) \
	(((cell) & ~C12_CELL_EXT_MASK) | ((uint32_t)(ratchet) & 7u) << 20 | ((uint32_t)(curve) & 7u) << 23 | ((uint32_t)(roll) & 7u) << 26)
/* strokes of a ratchet index */
unsigned c12_ratchet_strokes(unsigned ratchet);
/* a version-1 cell in this layout (spacing ACCEL/DECEL become those rolls; decay and decrescendo
 * FADE OUT, crescendo FADE IN, human HUMAN) */
uint32_t c12_cell_from_v1(uint32_t cell);
/* the velocity of stroke k of n under a curve (u: a random word, for HUMAN), 8..127 */
int c12_curve_velocity(unsigned curve, int velocity, unsigned k, unsigned n, uint32_t u);
/* No two strokes of one ratchet or roll closer than this: the voice's envelope must develop.
 * A ratchet that would be closer at the tempo plays the next smaller count instead (8 6 4 3 2). */
#define C12_MIN_STROKE_MS 12

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
	/* SPACE: CLATTER 12's delay and reverb (c12_space.h), fed by the mixer's sends */
	C12_FX_DLY_TIME, C12_FX_DLY_FEEDBACK, C12_FX_DLY_FILTER, C12_FX_DLY_WET, C12_FX_DLY_PINGPONG,
	C12_FX_VERB_SIZE, C12_FX_VERB_DECAY, C12_FX_VERB_DAMP, C12_FX_VERB_WET,
	/* where the sends go: 0 CLATTER 12's delay and reverb, 1 Plinky's own (mix_buffers_t sends) */
	C12_FX_SPACE_ENGINE,
	C12_FX_PARAMS
};
/* Value ranges: _WET 0-100 (%), _TYPE 0-2 (TUBE, FOLD, RAT), _RATIO 0-5 (1:1 2:1 3:1 4:1 6:1 8:1),
 * CRUSH_BITS 4-16 (16 = off), RATE 0-127 (127 = full rate), LIMITER 0-1, the LXR FX's others 0-127
 * as its P1-P4 (value / 127). DLY_TIME 0-11 (c12_delay_ticks), DLY_FILTER 0-127 (64 open),
 * DLY_PINGPONG and SPACE_ENGINE 0-1, the other SPACE values 0-100. */
void c12_fx_set(int fx, int value);
int c12_fx_get(int fx);
int c12_fx_max(int fx);
int c12_fx_min(int fx);

/* ---- sends to Plinky's own delay and reverb ----------------------------------------------- */
/* With SPACE_ENGINE 1 only (otherwise NULL: CLATTER 12's delay and reverb have them). Mono, at the
 * output rate, for the frames of the last c12_engine_render (int, full scale 32767), ready for
 * mix_buffers_t delaysend / reverbsend. NULL until a send is non-zero. */
const int32_t* c12_engine_send(int which); /* 0 delay, 1 reverb */

/* ---- scenes: a mixer state applied at the next bar --------------------------------------- */
struct c12_mix_state {
	uint8_t level[C12_TRACKS], delay[C12_TRACKS], reverb[C12_TRACKS];
	uint8_t mute, solo, route, master;
};
/* Control context. The audio context takes it at the first step of the next bar (16 steps) as
 * that step starts, so the bar's first hits already sound in it; at once when stopped or when
 * `at_bar` is 0. Returns 0, or -1 while an earlier one is still waiting. */
int c12_mix_schedule(const struct c12_mix_state* s, int at_bar);
int c12_mix_schedule_pending(void);
void c12_mix_get(struct c12_mix_state* out);

/* ---- performance (control context writes; the audio context applies, nothing is stored) -- */
/* Everything here is temporary: it changes what plays, never the pattern. The panel's COMMIT
 * writes a result into the pattern itself (c12_perf_cell gives the same answer the engine plays). */
/* Musical divisions for repeats, in twelfths of a sixteenth. */
enum { C12_DIV_OFF, C12_DIV_BAR, C12_DIV_2, C12_DIV_4, C12_DIV_8, C12_DIV_16, C12_DIV_32, C12_DIV_64,
       C12_DIV_4T, C12_DIV_8T, C12_DIV_16T, C12_DIV_32T, C12_DIV_8D, C12_DIV_16D, C12_DIVS };
unsigned c12_div_twelfths(unsigned div); /* 1/16 = 12 */
void c12_perf_set_throw(unsigned delay_mask, unsigned verb_mask); /* tracks' sends at full */
void c12_perf_set_freeze(int delay, int verb);
/* FILL: CLATTER's fill (rhythm/realize.ts), live: the masked tracks' empty steps sound with a
 * chance rising through the bar (25 % to 100 %), velocities rising, and the bar's last steps
 * ratchet. DENSITY -100..100: below 0 soft hits drop out first (CLATTER's density by strength);
 * above, ghost notes go into the gaps of tracks that play in the bar. */
void c12_perf_set_fill(unsigned mask);
void c12_perf_set_density(int amount);
/* The masked tracks play `cells` ([C12_TRACKS][64], the caller's memory, left unchanged while in
 * use: publish another buffer to change it) instead of the pattern: a temporary mutation. */
void c12_perf_set_alt(const uint32_t* cells, unsigned mask);
/* Beat repeat: the masked tracks repeat the last `div` of the sequence (from the step playing,
 * or the next one if this one is half gone) until set to C12_DIV_OFF; the sequencer runs on
 * underneath and is heard again where it is. shape 0 flat, 1 each repeat softer, 2 building. */
void c12_perf_set_repeat(unsigned div, unsigned mask, unsigned shape);
/* Note repeat: `track` strikes on the `div` grid while div is not C12_DIV_OFF (first at once),
 * at `velocity`, each strike with `chance` percent, shaped by `curve` (C12_CURVE_*). Call as often
 * as the values change (each UI frame while held). */
void c12_perf_note_repeat(int track, unsigned div, int velocity, int chance, unsigned curve);
/* One articulation now (C12_ROLL_*, or C12_ROLL_NONE with a ratchet), at the next step if
 * playing: for auditioning and for playing rolls by hand. */
void c12_perf_play(int track, uint32_t cell);
/* The cell the engine plays for track t at step s on pass `pass` under FILL, DENSITY and the
 * alternative pattern (0: nothing), given the pattern's cell `base`. Pure: the panel's COMMIT
 * uses it. `bar_hits` is whether the track has any hit in that 16-step bar. */
uint32_t c12_perf_cell(int t, int s, unsigned pass, uint32_t base, int bar_hits, unsigned fill_mask, int density);

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
	uint16_t delay_peak, verb_peak;  /* CLATTER 12's delay and reverb returns */
	uint32_t space_us;               /* of mix_us: the delay and reverb */
};
/* Copies and resets (counters restart). */
void c12_engine_profile_read(struct c12_engine_profile* out);

/* The last C12_TRIG_LOG voice triggers the engine made, for tests and displays: the LXR block
 * (counted from c12_engine_init) at whose start each sounded, the track, the velocity, and what
 * made it. Either context; a copy, oldest first. Returns how many (at most C12_TRIG_LOG). */
#define C12_TRIG_LOG 128
enum { C12_TRIG_STEP, C12_TRIG_STROKE, C12_TRIG_GRACE, C12_TRIG_REPEAT, C12_TRIG_HAND };
struct c12_trigger_event { uint32_t block; uint8_t track, velocity, kind, pad; };
unsigned c12_engine_trigger_log(struct c12_trigger_event* out);
uint32_t c12_engine_block_count(void);

#ifdef __cplusplus
}
#endif

#endif
