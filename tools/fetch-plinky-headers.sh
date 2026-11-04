#!/bin/sh
# fetch-plinky-headers.sh - download the published Plinky 12 panel API bundle into build/, checked
# against the revision CLATTER 12 was written for. Not vendored: it is Plinky's, with no licence
# stated. Used only by tools/check_api.py and to regenerate tools/plinky_macros.txt.
# Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
set -eu
URL=https://plinky12.com/docs/ide_api/plinky_library.h
SHA=ec52860ee009cf3e6ad08d468794b41baae29ea7c60e87e8a0b18856d07315ba
OUT=build/plinky_library.h
mkdir -p build
curl -fsSL "$URL" -o "$OUT.tmp"
GOT=$(shasum -a 256 "$OUT.tmp" | cut -d' ' -f1)
mv "$OUT.tmp" "$OUT"
if [ "$GOT" != "$SHA" ]; then
  echo "fetch-plinky-headers: the published API changed (sha256 $GOT, expected $SHA)."
  echo "  Review the differences, re-run tools/check_api.py, then update SHA here."
  exit 2
fi
echo "fetch-plinky-headers: $OUT matches the pinned revision"
