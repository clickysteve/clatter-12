/*
 * Amalgamation-time stand-in for <math.h>. Copyright (C) 2026 The CLATTER contributors.
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Declares nothing: the functions (expf, sinf, sqrtf, ...) resolve to the C library that the
 * Plinky 12 firmware links (the stock example panels call sqrtf and powf). Only constants the
 * sources expand while being preprocessed are defined here.
 */
#ifndef C12_STUB_MATH_H
#define C12_STUB_MATH_H
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_LN2 0.69314718055994530942
#define M_E 2.7182818284590452354
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#endif
