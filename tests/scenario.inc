/* scenario.inc - one deterministic session, compiled twice: as C against the native unity
 * build and as C++ against the generated panel engine (namespace c12). test_equiv compares the
 * two renders bit for bit, which shows the C++ amalgamation computes what the C code computes.
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only */
static unsigned scenario_render(int16_t* out, unsigned frames)
{
	static uint8_t kit[256];
	static uint32_t tables[C12_TABLE_BYTES / 4];
	c12_tables_bind(tables);
	if (c12_engine_init(32000u, 1234u) != 0) return 0;
	struct pocket_gen_settings g;
	g.seed = 42; g.density = 60; g.complexity = 50; g.style = 2; g.locks = 0; g.last_page = 1;
	pocket_gen_generate(pocket_seq_edit_begin(), &g);
	pocket_seq_edit_commit();
	pocket_seq_set_last_page(1);
	pocket_seq_set_tempo(128);
	pocket_lxr_set_bpm(128);
	pocket_seq_play(1);
	unsigned i;
	for (i = 0; i + 64 <= frames; i += 64)
	{
		if (i == 32000u * 2)
		{
			c12_kit_design(kit, 5, 77, 60);
			c12_kit_set_target(kit, 256);
		}
		if (i == 32000u * 3)
		{
			pocket_gen_mutate(pocket_seq_edit_begin(), 99, 2, 0);
			pocket_seq_edit_commit();
		}
		if (i % 128 == 0) c12_kit_pump(48);
		c12_engine_render(&out[2 * i], 64);
	}
	return i;
}
