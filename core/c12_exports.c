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
#include "pocket_lxr.h"
#include "pocket_seq.h"
#include "pocket_gen.h"
#include "pocket_kit.h"

static const int c12x_tracks = POCKET_SEQ_TRACKS;
static const int c12x_max_steps = POCKET_SEQ_MAX_STEPS;
static const int c12x_kit_params = C12_KIT_PARAMS;
static const int c12x_kit_characters = POCKET_KIT_CHARACTERS;
static const int c12x_gen_styles = POCKET_GEN_STYLES;
static const int c12x_mutate_medium = POCKET_MUTATE_MEDIUM;
static const int c12x_not_playing = POCKET_SEQ_NOT_PLAYING;
static const int c12x_note_programmed = POCKET_SEQ_NOTE_PROGRAMMED;

static inline uint32_t c12x_cell(int velocity, unsigned tone) { return POCKET_CELL(velocity, tone); }
static inline unsigned c12x_cell_velocity(uint32_t cell) { return POCKET_CELL_VELOCITY(cell); }
static inline unsigned c12x_cell_variation(uint32_t cell) { return POCKET_CELL_VARIATION(cell); }
static inline unsigned c12x_tone_variation(unsigned variation) { return POCKET_TONE_VARIATION(variation); }
static inline int c12x_variation_last(int trig) { return POCKET_VARIATION_LAST(trig); }
