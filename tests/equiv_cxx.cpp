// equiv_cxx.cpp - the scenario against the generated C++ engine (build/c12_engine.inc), with the
// C library headers in front of it as the Plinky IDE puts its own. Writes raw int16 to argv[1].
// Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define C12_LXR_CODE
#define C12_LXR_DATA
#include "c12_engine.inc"
#define C12_TABLE_BYTES 102240u // core/c12_engine.h (macros do not survive amalgamation)

namespace c12 {
#include "scenario.inc"
}

static int16_t pcm[32000 * 5 * 2];
int main(int argc, char **argv) {
  unsigned n = c12::scenario_render(pcm, 32000 * 5);
  FILE *f = fopen(argc > 1 ? argv[1] : "build/test/equiv_cxx.raw", "wb");
  fwrite(pcm, 4, n, f);
  fclose(f);
  printf("equiv (C++ amalgamation): %u frames\n", n);
  return n ? 0 : 1;
}
