/*
 * c12_kitgen.c - see c12_kitgen.h.
 *
 * Copyright (C) 2026 The CLATTER contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdint.h>
#include <string.h>
#include "c12_kitgen.h"
#include "c12_params.h"
#include "c12_engine.h"
#include "pocket_kit.h"
#include "pocket_gen.h"
#include "pocket_params.h"

static uint8_t scratch[C12_KIT_PARAMS];

void c12_kitgen_copy_voice(uint8_t* to, const uint8_t* from, unsigned voice)
{
	uint8_t pars[64];
	const int n = c12_voice_param_numbers((int)voice, pars);
	for (int i = 0; i < n; i++) to[pars[i]] = from[pars[i]];
}

void c12_kitgen_new_kit(uint8_t* kit, const uint8_t* current, unsigned character, uint32_t seed,
                        unsigned wildness, unsigned locks)
{
	/* locked voices first, in case current is kit */
	if (locks & C12_ALL_VOICES) memcpy(scratch, current, C12_KIT_PARAMS);
	c12_kit_design(kit, (int)(character % POCKET_KIT_CHARACTERS), seed, wildness > 100u ? 100u : wildness);
	for (unsigned v = 0; v < C12_VOICES; v++)
		if (locks >> v & 1u) c12_kitgen_copy_voice(kit, scratch, v);
}

void c12_kitgen_new_voice(uint8_t* kit, unsigned character, uint32_t seed, unsigned wildness,
                          unsigned voice, unsigned take)
{
	if (voice >= C12_VOICES) return;
	pocket_kit_voice(kit, character % POCKET_KIT_CHARACTERS, seed, wildness > 100u ? 100u : wildness, voice, take);
}

/* the parameters that only mean something together with a menu: taken with it */
static int companion(uint8_t menuPar, uint8_t par)
{
	/* a filter type brings its cutoff, resonance and filter drive */
	for (int n = 0; n < 6; n++)
		if (menuPar == POCKET_PAR_FILTER_TYPE_1 + n)
			return par == POCKET_PAR_FILTER_FREQ_1 + n || par == POCKET_PAR_RESO_1 + n || par == POCKET_PAR_FILTER_DRIVE_1 + n;
	/* a main waveform brings its pulse width */
	static const uint8_t wave[6] = { POCKET_PAR_OSC_WAVE_DRUM1, POCKET_PAR_OSC_WAVE_DRUM2, POCKET_PAR_OSC_WAVE_DRUM3,
	                                 POCKET_PAR_OSC_WAVE_SNARE, POCKET_PAR_WAVE1_CYM, POCKET_PAR_WAVE1_HH };
	for (int n = 0; n < 6; n++)
		if (menuPar == wave[n]) return par == POCKET_PAR_PWM_1 + n;
	return 0;
}

/* pitch moves in whole designs: the designer keeps the drums of a kit on one scale */
static int is_pitch(uint8_t par)
{
	return par == POCKET_PAR_COARSE1 || par == POCKET_PAR_COARSE2 || par == POCKET_PAR_COARSE3 || par == POCKET_PAR_COARSE4 ||
	       par == POCKET_PAR_COARSE5 || par == POCKET_PAR_COARSE6;
}

unsigned c12_kitgen_mutate(uint8_t* kit, unsigned character, uint32_t seed, unsigned wildness,
                           unsigned voices, unsigned locks, unsigned amount, uint32_t salt)
{
	if (amount > 100u) amount = 100u;
	if (wildness > 100u) wildness = 100u;
	unsigned changed = 0;
	for (unsigned v = 0; v < C12_VOICES; v++)
	{
		if (!(voices >> v & 1u) || (locks >> v & 1u)) continue;
		/* where this voice could go: another sound the designer draws for it */
		memcpy(scratch, kit, C12_KIT_PARAMS);
		pocket_kit_voice(scratch, character % POCKET_KIT_CHARACTERS, seed, wildness, v, 0x4000u + (salt & 0x3fffu));
		uint32_t rng = pocket_gen_mix(pocket_gen_mix(seed ^ 0x6d757461u, salt), v);
		int count;
		const struct c12_param* p = c12_voice_params((int)v, &count);
		uint8_t switched[64];
		int nSwitched = 0;
		/* menus and pitch first: a switch decides its companions */
		for (int i = 0; i < count; i++)
		{
			const uint8_t par = p[i].par;
			if (p[i].kind != C12_KIND_MENU && !is_pitch(par)) continue;
			if (kit[par] == scratch[par]) continue;
			/* at AMOUNT 100 about half the menus switch, as a new draw would */
			if (pocket_gen_below(&rng, 200u) < amount)
			{
				kit[par] = scratch[par];
				changed++;
				if (p[i].kind == C12_KIND_MENU) switched[nSwitched++] = par;
			}
		}
		for (int i = 0; i < count; i++)
		{
			const uint8_t par = p[i].par;
			if (p[i].kind == C12_KIND_MENU || is_pitch(par)) continue;
			int take = 0;
			for (int s = 0; s < nSwitched; s++) take |= companion(switched[s], par);
			const int from = kit[par], to = scratch[par];
			int next = take ? to : from + ((to - from) * (int)amount + (to > from ? 50 : -50)) / 100;
			if (next < 0) next = 0;
			if (next > p[i].max) next = p[i].max;
			if (next != from)
			{
				kit[par] = (uint8_t)next;
				changed++;
			}
		}
	}
	return changed;
}
