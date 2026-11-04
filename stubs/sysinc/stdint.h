/*
 * Amalgamation-time stand-in for <stdint.h>. Copyright (C) 2026 The CLATTER contributors.
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * tools/amalgamate.py preprocesses the C sources with -nostdinc. The real types come from the
 * headers the Plinky 12 IDE injects in front of every panel (and from the real <stdint.h> in the
 * host and Cortex-M33 checks), so this file declares nothing. It only supplies the macros the
 * sources expand at preprocessing time, which must not survive into the generated file.
 */
#ifndef C12_STUB_STDINT_H
#define C12_STUB_STDINT_H
#define INT8_MIN (-128)
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MIN (-32767 - 1)
#define INT16_MAX 32767
#define UINT16_MAX 65535
#define INT32_MIN (-2147483647 - 1)
#define INT32_MAX 2147483647
#define UINT32_MAX 4294967295u
#endif
