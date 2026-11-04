/*
 * c12_params.h - the LXR-02 sound parameters of each voice, as CLATTER 12's SOUND page edits them.
 *
 * Copyright (C) 2026 The CLATTER contributors. Value names and groupings from the LXR firmware
 * (menu/MenuText.h, menu/menu.c parameter_dtypes), Copyright (C) 2012-2026 Erica Synths.
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A C port of CLATTER Web's src/sound/lxrParams.ts (VOICE_PARAMS): every entry is a PAR_* number
 * the firmware's own parameter handler interprets, with the firmware's range and value names.
 * Nothing here invents a parameter. Left out of the grid editor, and said so: the two modulation
 * TARGET parameters of each voice (LFO target, velocity destination), whose values are parameter
 * numbers (0-246), not a scale; the hardware routing parameters (AUDIO_OUT, VOICE_LFO).
 *
 * Portable, control context only. Constant tables in flash.
 */
#ifndef C12_PARAMS_H
#define C12_PARAMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { C12_KIND_RANGE, C12_KIND_BIPOLAR, C12_KIND_MENU };
/* What a parameter is for: its row's colour, and which bank of the SOUND page holds it. */
enum {
	C12_GROUP_OSC, C12_GROUP_NOISE, C12_GROUP_MOD1, C12_GROUP_MOD2, C12_GROUP_FM, C12_GROUP_PITCH,
	C12_GROUP_AMP, C12_GROUP_TRANSIENT, C12_GROUP_FILTER, C12_GROUP_LFO, C12_GROUP_VELOCITY, C12_GROUP_MIX,
	C12_GROUPS
};
/* Banks: SOURCE (oscillators, noise, FM, pitch), SHAPE (amplitude, transient, filter, drive),
 * MOD (LFO, velocity, volume, pan, sample rate). Each holds at most C12_BANK_ROWS parameters. */
#define C12_BANKS 3
#define C12_BANK_ROWS 13

struct c12_param {
	uint8_t par;      /* POCKET_PAR_* */
	uint8_t kind;     /* C12_KIND_* */
	uint8_t max;      /* largest value: 127, or a menu's options - 1 */
	uint8_t group;    /* C12_GROUP_* */
	uint8_t bank;     /* 0..C12_BANKS-1 */
	uint8_t menu;     /* C12_MENU_* for C12_KIND_MENU */
	char label[11];   /* CLATTER's label, group first: "PITCH DECAY" */
};

enum { C12_MENU_NONE, C12_MENU_WAVE, C12_MENU_FILTER, C12_MENU_TRANSIENT, C12_MENU_LFO_WAVE, C12_MENU_LFO_RETRIG,
	C12_MENU_LFO_SYNC, C12_MENU_FM_MODE, C12_MENU_ON_OFF, C12_MENUS };

/* Voice v (0-5: DRUM 1-3, SNARE, CYMBAL, HIHAT): its parameters in editor order, and how many. */
const struct c12_param* c12_voice_params(int voice, int* count);
/* Bank b of voice v: up to C12_BANK_ROWS entries written to out (pointers into the table). */
int c12_bank_params(int voice, int bank, const struct c12_param** out);
/* A menu's value names: option i of menu m, or "" */
const char* c12_menu_option(int menu, int i);
const char* c12_bank_name(int bank);
/* The voice that plays track t (the hats share voice 5). */
static inline int c12_voice_of_track(int track) { return track >= 5 ? 5 : track; }
/* Every parameter number that belongs to voice v (its editor entries plus the two TARGETs),
 * for copying or locking a whole sound. Returns how many were written (at most 48). */
int c12_voice_param_numbers(int voice, uint8_t* out);

#ifdef __cplusplus
}
#endif

#endif
