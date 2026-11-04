/*
 * c12_exports.c - the engine's macros, as values and functions the panel can still see.
 *
 * Copyright (C) 2026 The CLATTER contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * tools/amalgamate.py resolves every macro of the C sources at generation time, so the panel
 * glue (C++, after the generated block) cannot use POCKET_CELL and friends. This file restates
 * the ones it needs as constants and inline functions in the same namespace (c12::c12x_*).
 */
#include <stdint.h>
#include "c12_engine.h"
#include "c12_params.h"
#include "c12_kitgen.h"
#include "pocket_lxr.h"
#include "pocket_seq.h"
#include "pocket_gen.h"
#include "pocket_kit.h"
#include "pocket_params.h"

static const int c12x_tracks = POCKET_SEQ_TRACKS;
static const int c12x_max_steps = POCKET_SEQ_MAX_STEPS;
static const int c12x_kit_params = C12_KIT_PARAMS;
static const int c12x_sound_params = POCKET_SOUND_PARAMS;
static const int c12x_kit_characters = POCKET_KIT_CHARACTERS;
static const int c12x_kit_wild_default = POCKET_KIT_WILD_DEFAULT;
static const int c12x_gen_styles = POCKET_GEN_STYLES;
static const int c12x_mutate_strengths = POCKET_MUTATE_STRENGTHS;
static const int c12x_not_playing = POCKET_SEQ_NOT_PLAYING;
static const int c12x_note_programmed = POCKET_SEQ_NOTE_PROGRAMMED;
static const int c12x_conditions = POCKET_CONDITIONS;
static const int c12x_ratchets = C12_RATCHETS;
static const int c12x_roll_shapes = C12_ROLL_SHAPES;
static const int c12x_ramps = C12_RAMPS;
static const int c12x_banks = C12_BANKS;
static const int c12x_bank_rows = C12_BANK_ROWS;
static const int c12x_kind_range = C12_KIND_RANGE;
static const int c12x_kind_bipolar = C12_KIND_BIPOLAR;
static const int c12x_kind_menu = C12_KIND_MENU;
static const int c12x_groups = C12_GROUPS;
static const int c12x_level_unity = C12_LEVEL_UNITY;
static const int c12x_swing_min = POCKET_SEQ_SWING_MIN;
static const int c12x_swing_max = POCKET_SEQ_SWING_MAX;
static const int c12x_bpm_min = POCKET_SEQ_BPM_MIN;
static const int c12x_bpm_max = POCKET_SEQ_BPM_MAX;
static const int c12x_par_voice_decimation_all = POCKET_PAR_VOICE_DECIMATION_ALL;
static const int c12x_pan_par[6] = { POCKET_PAR_PAN1, POCKET_PAR_PAN2, POCKET_PAR_PAN3, POCKET_PAR_PAN4, POCKET_PAR_PAN5, POCKET_PAR_PAN6 };
static const int c12x_vol_par[6] = { POCKET_PAR_VOL1, POCKET_PAR_VOL2, POCKET_PAR_VOL3, POCKET_PAR_VOL4, POCKET_PAR_VOL5, POCKET_PAR_VOL6 };

enum {
	c12x_fx_drive_wet = C12_FX_DRIVE_WET, c12x_fx_drive_type = C12_FX_DRIVE_TYPE, c12x_fx_drive_amount = C12_FX_DRIVE_AMOUNT,
	c12x_fx_drive_tone = C12_FX_DRIVE_TONE, c12x_fx_drive_level = C12_FX_DRIVE_LEVEL, c12x_fx_ring_wet = C12_FX_RING_WET,
	c12x_fx_ring_freq = C12_FX_RING_FREQ, c12x_fx_comp_wet = C12_FX_COMP_WET, c12x_fx_comp_thresh = C12_FX_COMP_THRESH,
	c12x_fx_comp_ratio = C12_FX_COMP_RATIO, c12x_fx_comp_attack = C12_FX_COMP_ATTACK, c12x_fx_comp_release = C12_FX_COMP_RELEASE,
	c12x_fx_comp_gain = C12_FX_COMP_GAIN, c12x_fx_crush_bits = C12_FX_CRUSH_BITS, c12x_fx_rate = C12_FX_RATE,
	c12x_fx_limiter = C12_FX_LIMITER, c12x_fx_params = C12_FX_PARAMS
};

static inline uint32_t c12x_cell(int velocity, unsigned tone) { return POCKET_CELL(velocity, tone); }
static inline unsigned c12x_cell_velocity(uint32_t cell) { return POCKET_CELL_VELOCITY(cell); }
static inline unsigned c12x_cell_variation(uint32_t cell) { return POCKET_CELL_VARIATION(cell); }
static inline unsigned c12x_cell_condition(uint32_t cell) { return POCKET_CELL_CONDITION(cell); }
static inline unsigned c12x_cell_ratchet(uint32_t cell) { return C12_CELL_RATCHET(cell); }
static inline unsigned c12x_cell_roll(uint32_t cell) { return C12_CELL_ROLL(cell); }
static inline unsigned c12x_cell_ramp(uint32_t cell) { return C12_CELL_RAMP(cell); }
static inline uint32_t c12x_cell_with_velocity(uint32_t cell, int v) { return (cell & ~0xffu) | (uint32_t)(v & 0x7f); }
static inline uint32_t c12x_cell_with_variation(uint32_t cell, unsigned variation)
{
	return (cell & ~0xff00u) | (uint32_t)(variation ? POCKET_TONE_VARIATION(variation) : 0u) << 8;
}
static inline uint32_t c12x_cell_with_condition(uint32_t cell, unsigned condition)
{
	return (cell & ~(15u << 16)) | (uint32_t)(condition & 15u) << 16;
}
static inline uint32_t c12x_cell_with_ext(uint32_t cell, unsigned ratchet, unsigned roll, unsigned ramp)
{
	return C12_CELL_WITH_EXT(cell, ratchet, roll, ramp);
}
static inline unsigned c12x_tone_variation(unsigned variation) { return POCKET_TONE_VARIATION(variation); }
static inline int c12x_variation_last(int trig) { return POCKET_VARIATION_LAST(trig); }
