/*
 * c12_params.c - see c12_params.h. A port of CLATTER Web's src/sound/lxrParams.ts.
 *
 * Copyright (C) 2026 The CLATTER contributors. Value names and groupings from the LXR firmware,
 * Copyright (C) 2012-2026 Erica Synths. SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdint.h>
#include "c12_params.h"
#include "pocket_params.h"

#define PP(n) POCKET_PAR_##n
#define SRC 0
#define SHP 1
#define MOD 2
#define G(g) C12_GROUP_##g
#define R(par, g, bank, label) { PP(par), C12_KIND_RANGE, 127, G(g), bank, C12_MENU_NONE, label }
#define B(par, g, bank, label) { PP(par), C12_KIND_BIPOLAR, 127, G(g), bank, C12_MENU_NONE, label }
#define M(par, g, bank, menu, n, label) { PP(par), C12_KIND_MENU, (n) - 1, G(g), bank, C12_MENU_##menu, label }

/* lxrParams.ts shared(v, n): every voice type has these, in this order */
#define SHARED(n) \
	M(TRANS##n##_WAVE, TRANSIENT, SHP, TRANSIENT, 14, "TRANS WAVE"), \
	R(TRANS##n##_VOL, TRANSIENT, SHP, "TRANS LEVL"), \
	R(TRANS##n##_FREQ, TRANSIENT, SHP, "TRANS PTCH"), \
	R(FILTER_FREQ_##n, FILTER, SHP, "CUTOFF"), \
	R(RESO_##n, FILTER, SHP, "RESONANCE"), \
	M(FILTER_TYPE_##n, FILTER, SHP, FILTER, 8, "FILT TYPE"), \
	R(FILTER_DRIVE_##n, FILTER, SHP, "FILT DRIVE"), \
	R(FREQ_LFO##n, LFO, MOD, "LFO RATE"), \
	M(SYNC_LFO##n, LFO, MOD, LFO_SYNC, 12, "LFO SYNC"), \
	R(AMOUNT_LFO##n, LFO, MOD, "LFO DEPTH"), \
	M(WAVE_LFO##n, LFO, MOD, LFO_WAVE, 8, "LFO WAVE"), \
	M(RETRIGGER_LFO##n, LFO, MOD, LFO_RETRIG, 7, "LFO RETRIG"), \
	R(OFFSET_LFO##n, LFO, MOD, "LFO PHASE"), \
	R(VELO_MOD_AMT_##n, VELOCITY, MOD, "VEL AMOUNT"), \
	M(VOLUME_MOD_ON_OFF##n, VELOCITY, MOD, ON_OFF, 2, "VEL > VOL"), \
	R(VOL##n, MIX, MOD, "VOLUME"), \
	B(PAN##n, MIX, MOD, "PAN"), \
	R(VOICE_DECIMATION##n, MIX, MOD, "SAMPLERATE")

#define DRUM(n) \
	M(OSC_WAVE_DRUM##n, OSC, SRC, WAVE, 6, "OSC WAVE"), \
	B(COARSE##n, OSC, SRC, "PITCH"), \
	B(FINE##n, OSC, SRC, "FINE TUNE"), \
	R(PWM_##n, OSC, SRC, "PULSEWIDTH"), \
	R(FMAMNT##n, FM, SRC, "FM AMOUNT"), \
	B(FM_FREQ##n, FM, SRC, "FM FREQ"), \
	M(MOD_WAVE_DRUM##n, FM, SRC, WAVE, 6, "FM WAVE"), \
	M(MIX_MOD_##n, FM, SRC, FM_MODE, 2, "FM MODE"), \
	R(MOD_EG##n, PITCH, SRC, "PTCH DECAY"), \
	R(PITCH_SLOPE##n, PITCH, SRC, "PTCH SLOPE"), \
	R(MODAMNT##n, PITCH, SRC, "PTCH AMNT"), \
	R(VELOA##n, AMP, SHP, "ATTACK"), \
	R(VELOD##n, AMP, SHP, "DECAY"), \
	R(VOL_SLOPE##n, AMP, SHP, "AMP SLOPE"), \
	R(DRIVE##n, AMP, SHP, "DRIVE"), \
	SHARED(n)

static const struct c12_param drum1[] = { DRUM(1) };
static const struct c12_param drum2[] = { DRUM(2) };
static const struct c12_param drum3[] = { DRUM(3) };
static const struct c12_param snare[] = {
	M(OSC_WAVE_SNARE, OSC, SRC, WAVE, 6, "OSC WAVE"),
	B(COARSE4, OSC, SRC, "PITCH"),
	B(FINE4, OSC, SRC, "FINE TUNE"),
	R(PWM_4, OSC, SRC, "PULSEWIDTH"),
	R(NOISE_FREQ1, NOISE, SRC, "NOISE FREQ"),
	R(MIX1, NOISE, SRC, "NOISE MIX"),
	R(MOD_EG4, PITCH, SRC, "PTCH DECAY"),
	R(PITCH_SLOPE4, PITCH, SRC, "PTCH SLOPE"),
	R(MODAMNT4, PITCH, SRC, "PTCH AMNT"),
	R(VELOA4, AMP, SHP, "ATTACK"),
	R(VELOD4, AMP, SHP, "DECAY"),
	R(VOL_SLOPE4, AMP, SHP, "AMP SLOPE"),
	R(REPEAT4, AMP, SHP, "REPEAT"),
	R(SNARE_DISTORTION, AMP, SHP, "DRIVE"),
	SHARED(4)
};
static const struct c12_param cymbal[] = {
	M(WAVE1_CYM, OSC, SRC, WAVE, 6, "OSC WAVE"),
	B(COARSE5, OSC, SRC, "PITCH"),
	B(FINE5, OSC, SRC, "FINE TUNE"),
	R(PWM_5, OSC, SRC, "PULSEWIDTH"),
	M(WAVE2_CYM, MOD1, SRC, WAVE, 6, "MOD1 WAVE"),
	B(MOD_OSC_F1_CYM, MOD1, SRC, "MOD1 FREQ"),
	R(MOD_OSC_GAIN1_CYM, MOD1, SRC, "MOD1 AMNT"),
	M(WAVE3_CYM, MOD2, SRC, WAVE, 6, "MOD2 WAVE"),
	B(MOD_OSC_F2_CYM, MOD2, SRC, "MOD2 FREQ"),
	R(MOD_OSC_GAIN2_CYM, MOD2, SRC, "MOD2 AMNT"),
	R(VELOA5, AMP, SHP, "ATTACK"),
	R(VELOD5, AMP, SHP, "DECAY"),
	R(VOL_SLOPE5, AMP, SHP, "AMP SLOPE"),
	R(REPEAT5, AMP, SHP, "REPEAT"),
	R(CYMBAL_DISTORTION, AMP, SHP, "DRIVE"),
	SHARED(5)
};
static const struct c12_param hihat[] = {
	M(WAVE1_HH, OSC, SRC, WAVE, 6, "OSC WAVE"),
	B(COARSE6, OSC, SRC, "PITCH"),
	B(FINE6, OSC, SRC, "FINE TUNE"),
	R(PWM_6, OSC, SRC, "PULSEWIDTH"),
	M(WAVE2_HH, MOD1, SRC, WAVE, 6, "MOD1 WAVE"),
	B(MOD_OSC_F1, MOD1, SRC, "MOD1 FREQ"),
	R(MOD_OSC_GAIN1, MOD1, SRC, "MOD1 AMNT"),
	M(WAVE3_HH, MOD2, SRC, WAVE, 6, "MOD2 WAVE"),
	B(MOD_OSC_F2, MOD2, SRC, "MOD2 FREQ"),
	R(MOD_OSC_GAIN2, MOD2, SRC, "MOD2 AMNT"),
	R(VELOA6, AMP, SHP, "ATTACK"),
	R(VELOD6_CLOSED, AMP, SHP, "CLOSED DEC"),
	R(VELOD6_OPEN, AMP, SHP, "OPEN DECAY"),
	R(VOL_SLOPE6, AMP, SHP, "AMP SLOPE"),
	R(HAT_DISTORTION, AMP, SHP, "DRIVE"),
	SHARED(6)
};

#define N(a) (int)(sizeof a / sizeof a[0])
static const struct c12_param* const voices[6] = { drum1, drum2, drum3, snare, cymbal, hihat };
static const uint8_t counts[6] = { N(drum1), N(drum2), N(drum3), N(snare), N(cymbal), N(hihat) };
/* the two TARGET parameters of each voice (not on the grid: their values name parameters) */
static const uint8_t targetLfo[6] = { PP(TARGET_LFO1), PP(TARGET_LFO2), PP(TARGET_LFO3), PP(TARGET_LFO4), PP(TARGET_LFO5), PP(TARGET_LFO6) };
static const uint8_t targetVel[6] = { PP(VEL_DEST_1), PP(VEL_DEST_2), PP(VEL_DEST_3), PP(VEL_DEST_4), PP(VEL_DEST_5), PP(VEL_DEST_6) };

const struct c12_param* c12_voice_params(int voice, int* count)
{
	if (voice < 0 || voice > 5) voice = 0;
	if (count) *count = counts[voice];
	return voices[voice];
}

int c12_bank_params(int voice, int bank, const struct c12_param** out)
{
	int n = 0, count;
	const struct c12_param* p = c12_voice_params(voice, &count);
	for (int i = 0; i < count && n < C12_BANK_ROWS; i++)
		if (p[i].bank == bank) out[n++] = &p[i];
	return n;
}

int c12_voice_param_numbers(int voice, uint8_t* out)
{
	int count;
	const struct c12_param* p = c12_voice_params(voice, &count);
	int n = 0;
	for (int i = 0; i < count; i++) out[n++] = p[i].par;
	out[n++] = targetLfo[voice < 0 || voice > 5 ? 0 : voice];
	out[n++] = targetVel[voice < 0 || voice > 5 ? 0 : voice];
	return n;
}

/* menu/MenuText.h, as lxrParams.ts spells them out */
static const char* const waves[] = { "SINE", "TRI", "SAW", "REC", "NOISE", "PWM" };
static const char* const filters[] = { "LP", "HP", "BP", "UBP", "NOTCH", "PEAK", "LP2", "OFF" };
static const char* const transients[] = { "SNAP", "OFFSET", "CLICK", "CLICK 2", "TICK", "KICK", "RIM", "DROP", "HAT", "CLAP", "KICK 2", "SNARE", "TOM", "SNAP 2" };
static const char* const lfoWaves[] = { "SINE", "TRI", "SAW UP", "SAW DN", "SQUARE", "RANDOM", "EXP UP", "EXP DN" };
static const char* const lfoRetrig[] = { "OFF", "V1", "V2", "V3", "V4", "V5", "V6" };
static const char* const lfoSync[] = { "OFF", "4/1", "2/1", "1/1", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16", "1/32" };
static const char* const fmModes[] = { "FM", "MIX" };
static const char* const onOff[] = { "OFF", "ON" };

const char* c12_menu_option(int menu, int i)
{
	const char* const* t = 0;
	int n = 0;
	switch (menu)
	{
	case C12_MENU_WAVE: t = waves; n = N(waves); break;
	case C12_MENU_FILTER: t = filters; n = N(filters); break;
	case C12_MENU_TRANSIENT: t = transients; n = N(transients); break;
	case C12_MENU_LFO_WAVE: t = lfoWaves; n = N(lfoWaves); break;
	case C12_MENU_LFO_RETRIG: t = lfoRetrig; n = N(lfoRetrig); break;
	case C12_MENU_LFO_SYNC: t = lfoSync; n = N(lfoSync); break;
	case C12_MENU_FM_MODE: t = fmModes; n = N(fmModes); break;
	case C12_MENU_ON_OFF: t = onOff; n = N(onOff); break;
	default: break;
	}
	return t && i >= 0 && i < n ? t[i] : "";
}

const char* c12_bank_name(int bank)
{
	return bank == 0 ? "SOURCE" : bank == 1 ? "SHAPE" : bank == 2 ? "MOD" : "";
}
