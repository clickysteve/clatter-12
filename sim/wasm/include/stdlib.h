/* Freestanding <stdlib.h> for the WebAssembly build of the panel. GPL-3.0-only */
#ifndef C12W_STDLIB_H
#define C12W_STDLIB_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
long strtol(const char *s, char **end, int base);
static inline int abs(int x) { return x < 0 ? -x : x; }
static inline long labs(long x) { return x < 0 ? -x : x; }
#ifdef __cplusplus
}
#endif
#endif
