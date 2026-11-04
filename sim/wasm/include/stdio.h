/* Freestanding <stdio.h> for the WebAssembly build of the panel: printf goes to a log the page can
 * read (c12p_log), snprintf formats %d %i %u %x %c %s %% (with l). GPL-3.0-only */
#ifndef C12W_STDIO_H
#define C12W_STDIO_H
#include <stddef.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
int printf(const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
#ifdef __cplusplus
}
#endif
#endif
