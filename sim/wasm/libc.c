/*
 * libc.c - the few C library functions the panel and the simulator call, for WebAssembly.
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 * memset, memcpy, memmove and the transcendental functions come from CLATTER's libm
 * (third_party/clatter-pocket/lxr/bridge/libm.c), linked beside this file.
 */
#include <stddef.h>
#include <stdarg.h>
#include "include/string.h"
#include "include/stdlib.h"
#include "include/stdio.h"

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *x = a, *y = b;
	for (size_t i = 0; i < n; i++)
		if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
	return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strncmp(const char *a, const char *b, size_t n)
{
	for (size_t i = 0; i < n; i++)
	{
		if (a[i] != b[i]) return (unsigned char)a[i] < (unsigned char)b[i] ? -1 : 1;
		if (!a[i]) return 0;
	}
	return 0;
}
int strcmp(const char *a, const char *b) { return strncmp(a, b, (size_t)-1); }
char *strchr(const char *s, int c)
{
	for (;; s++)
	{
		if (*s == (char)c) return (char *)s;
		if (!*s) return 0;
	}
}
char *strstr(const char *h, const char *n)
{
	size_t k = strlen(n);
	for (; *h; h++)
		if (!strncmp(h, n, k)) return (char *)h;
	return k ? 0 : (char *)h;
}
long strtol(const char *s, char **end, int base)
{
	(void)base;
	long v = 0, sign = 1;
	while (*s == ' ') s++;
	if (*s == '-') { sign = -1; s++; }
	else if (*s == '+') s++;
	while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
	if (end) *end = (char *)s;
	return v * sign;
}

static void out(char *b, size_t n, size_t *at, char c)
{
	if (*at + 1 < n) b[*at] = c;
	(*at)++;
}
int vsnprintf(char *buf, size_t n, const char *f, va_list ap)
{
	size_t at = 0;
	for (; *f; f++)
	{
		if (*f != '%') { out(buf, n, &at, *f); continue; }
		f++;
		while ((*f >= '0' && *f <= '9') || *f == '.' || *f == '-') f++; /* widths are ignored */
		int lng = 0;
		while (*f == 'l' || *f == 'z') { lng = 1; f++; }
		char t[24];
		int k = 0;
		switch (*f)
		{
		case 'd': case 'i': {
			long v = lng ? va_arg(ap, long) : va_arg(ap, int);
			unsigned long u = v < 0 ? (unsigned long)-v : (unsigned long)v;
			do t[k++] = (char)('0' + u % 10); while (u /= 10);
			if (v < 0) t[k++] = '-';
			while (k) out(buf, n, &at, t[--k]);
			break;
		}
		case 'u': case 'x': {
			unsigned long u = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
			unsigned b = *f == 'x' ? 16 : 10;
			do t[k++] = "0123456789abcdef"[u % b]; while (u /= b);
			while (k) out(buf, n, &at, t[--k]);
			break;
		}
		case 'c': out(buf, n, &at, (char)va_arg(ap, int)); break;
		case 's': { const char *s = va_arg(ap, const char *); while (s && *s) out(buf, n, &at, *s++); break; }
		case 'f': { double v = va_arg(ap, double); long w = (long)v; (void)w; out(buf, n, &at, '?'); break; }
		case '%': out(buf, n, &at, '%'); break;
		default: out(buf, n, &at, *f); break;
		}
	}
	if (n) buf[at < n ? at : n - 1] = 0;
	return (int)at;
}
int snprintf(char *buf, size_t n, const char *f, ...)
{
	va_list ap;
	va_start(ap, f);
	int r = vsnprintf(buf, n, f, ap);
	va_end(ap);
	return r;
}
/* printf: the last lines, for the page's console */
char c12w_log[2048];
static size_t logAt;
int printf(const char *f, ...)
{
	char line[256];
	va_list ap;
	va_start(ap, f);
	int r = vsnprintf(line, sizeof line, f, ap);
	va_end(ap);
	size_t k = strlen(line);
	if (logAt + k + 1 >= sizeof c12w_log) logAt = 0;
	memcpy(c12w_log + logAt, line, k + 1);
	logAt += k;
	return r;
}
