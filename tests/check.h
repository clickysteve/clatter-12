/* check.h - tiny test helpers. Copyright (C) 2026 The CLATTER contributors. GPL-3.0-only */
#ifndef C12_CHECK_H
#define C12_CHECK_H
#include <stdio.h>
#include <stdint.h>

static int checks_run, checks_failed;
#define CHECK(cond, ...)                                                                     \
	do {                                                                                     \
		checks_run++;                                                                        \
		if (!(cond)) {                                                                       \
			checks_failed++;                                                                 \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);                                      \
			printf(__VA_ARGS__);                                                             \
			printf("\n");                                                                    \
		}                                                                                    \
	} while (0)
#define CHECK_REPORT(name)                                                                    \
	(printf("%s: %d checks, %d failed\n", name, checks_run, checks_failed), checks_failed ? 1 : 0)

/* 16-bit stereo WAV */
static void write_wav(const char* path, const int16_t* lr, unsigned frames, unsigned rate)
{
	FILE* f = fopen(path, "wb");
	if (!f) return;
	uint32_t data = frames * 4u, riff = 36u + data, fmt = 16u, br = rate * 4u;
	uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
	fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
	fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
	fwrite(&br, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(lr, 4, frames, f);
	fclose(f);
}
#endif
