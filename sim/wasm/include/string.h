/* Freestanding <string.h> for the WebAssembly build of the panel (sim/wasm/libc.c).
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only */
#ifndef C12W_STRING_H
#define C12W_STRING_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strncmp(const char *a, const char *b, size_t n);
int strcmp(const char *a, const char *b);
char *strchr(const char *s, int c);
char *strstr(const char *h, const char *n);
#ifdef __cplusplus
}
#endif
#endif
