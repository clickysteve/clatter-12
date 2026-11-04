/*
 * c12_kitgen.h - procedural kits for CLATTER 12: new kits, new sounds, mutation, locks.
 *
 * Copyright (C) 2026 The CLATTER contributors
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Built on CLATTER Pocket's kit designer (seq/pocket_kit.c, a port of CLATTER Web's
 * src/sound/kitgen.ts): a kit is designed from (character, seed, wildness), every voice from an
 * archetype's recipe, and nothing is a stored preset. A new kit draws a new seed; the same
 * arguments always give the same kit. What CLATTER 12 adds:
 *   - locks: locked voices keep every one of their parameters through a new kit or a mutation;
 *   - one voice: a new sound for one voice, of the same family as the kit (pocket_kit_voice);
 *   - mutation: each unlocked voice moves towards another sound the designer draws for it, by
 *     AMOUNT; menus (waveforms, filter and transient types) switch with a chance that grows with
 *     AMOUNT, taking the values that only make sense with them (cutoff with a filter type, pulse
 *     width with a waveform, pitch as a whole: the designer keeps drums on a scale). So a
 *     mutation stays within what the designer would make, as CLATTER Web's mutateKit does.
 *
 * Kits are arrays of C12_KIT_PARAMS bytes indexed by PAR_* number. Control context only.
 */
#ifndef C12_KITGEN_H
#define C12_KITGEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define C12_VOICES 6
#define C12_ALL_VOICES 0x3fu

/* A new kit of `character` (below POCKET_KIT_CHARACTERS) into `kit`; voices whose bit is set in
 * `locks` are copied from `current` instead (which may be `kit` itself). */
void c12_kitgen_new_kit(uint8_t* kit, const uint8_t* current, unsigned character, uint32_t seed,
                        unsigned wildness, unsigned locks);
/* A new sound for `voice` alone, of the family (character, seed) gives; `take` draws another. */
void c12_kitgen_new_voice(uint8_t* kit, unsigned character, uint32_t seed, unsigned wildness,
                          unsigned voice, unsigned take);
/* Mutate the voices in `voices` (bit per voice) that `locks` leaves free, by `amount` (0-100).
 * `salt` makes each mutation differ. Returns how many parameters changed. */
unsigned c12_kitgen_mutate(uint8_t* kit, unsigned character, uint32_t seed, unsigned wildness,
                           unsigned voices, unsigned locks, unsigned amount, uint32_t salt);
/* Copy every parameter of `voice` from `from` to `to`. */
void c12_kitgen_copy_voice(uint8_t* to, const uint8_t* from, unsigned voice);

#ifdef __cplusplus
}
#endif

#endif
