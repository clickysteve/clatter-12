#!/usr/bin/env python3
"""compare_pcm.py A B [min_snr_db] - compare two raw interleaved int16 renders.
Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only

Prints identical samples, the largest difference and the error relative to the signal. Exit status 1
if the files differ in length or the signal-to-difference ratio is below min_snr_db (default: must be
bit-identical)."""
import math, struct, sys

a = open(sys.argv[1], "rb").read()
b = open(sys.argv[2], "rb").read()
need = float(sys.argv[3]) if len(sys.argv) > 3 else None
if len(a) != len(b):
    sys.exit("compare: lengths differ (%d, %d)" % (len(a), len(b)))
x = struct.unpack("<%dh" % (len(a) // 2), a)
y = struct.unpack("<%dh" % (len(b) // 2), b)
same = sum(1 for p, q in zip(x, y) if p == q)
diff = max(abs(p - q) for p, q in zip(x, y))
es = sum(p * p for p in x)
ee = sum((p - q) ** 2 for p, q in zip(x, y))
snr = float("inf") if ee == 0 else 10 * math.log10(es / ee)
print("compare: %d samples, %.3f%% identical, largest difference %d LSB, signal/difference %s dB"
      % (len(x), 100.0 * same / len(x), diff, "inf" if ee == 0 else "%.1f" % snr))
if need is None:
    sys.exit(0 if ee == 0 else 1)
sys.exit(0 if snr >= need else 1)
