/*
 * Amalgamation-time stand-in for <stddef.h>: declares nothing (see stdint.h here).
 * Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef C12_STUB_STDDEF_H
#define C12_STUB_STDDEF_H
#define NULL 0
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif
