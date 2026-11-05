#!/usr/bin/env python3
"""compare_envelope.py A B [max_db] - two raw interleaved int16 stereo renders of the same session
compared as sound rather than as samples: the level of each 10 ms window (both channels), in dB.

For renders that should sound the same but need not match sample for sample: noise voices and free
oscillators decorrelate when anything moves by a block. Prints the median, 95th percentile and
largest difference over windows louder than -50 dBFS in either, and exits 1 if the 95th percentile
exceeds max_db (default 1.0).
Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only"""
import math, struct, sys

def windows(path, n=320 * 2):
    d = open(path, "rb").read()
    x = struct.unpack("<%dh" % (len(d) // 2), d)
    out = []
    for i in range(0, len(x) - n + 1, n):
        e = sum(v * v for v in x[i:i + n]) / n
        out.append(10 * math.log10(e / (32768.0 ** 2)) if e > 0 else -200.0)
    return out

a, b = windows(sys.argv[1]), windows(sys.argv[2])
lim = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
d = sorted(abs(p - q) for p, q in zip(a, b) if max(p, q) > -50)
if not d:
    sys.exit("compare_envelope: silent")
med, p95, top = d[len(d) // 2], d[int(len(d) * 0.95)], d[-1]
print("envelope: %d windows of 10 ms, level difference median %.2f dB, 95%% %.2f dB, largest %.2f dB" % (len(d), med, p95, top))
sys.exit(0 if p95 <= lim else 1)
