#!/bin/sh
# compare_builds.sh REV [nospread] - CLATTER 12's audio from git revision REV against this working
# tree's, sample for sample, over the sessions in tests/golden.c (steady, live, colour, apart).
#
# Builds REV's engine (its own tools/amalgamate.py, the submodule as checked out here) in a temporary
# git worktree, links this tree's tests/golden.c against both engines, renders, and compares with
# tools/compare_pcm.py. `nospread` renders this tree's engine with spreading off
# (c12_engine_set_spread(0)): every LXR block in the callback that needs it, as before; then the
# sessions' control changes reach both engines at the same LXR block, and a pure optimisation must
# compare bit-identical. With spreading on (the default), the steady and colour sessions must still
# be identical where nothing is touched after PLAY; live and apart move control changes by up to
# one LXR block, and differ by that.
# Linux (weak symbols). Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
set -eu
REV=${1:?usage: compare_builds.sh REV [nospread]}
MODE=${2:-}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-cc}
OUT="$ROOT/build/compare"
WT=$(mktemp -d "${TMPDIR:-/tmp}/c12-compare.XXXXXX")
cleanup() { git -C "$ROOT" worktree remove --force "$WT" >/dev/null 2>&1 || true; rm -rf "$WT"; }
trap cleanup EXIT
git -C "$ROOT" worktree add --detach "$WT" "$REV" >/dev/null 2>&1
want=$(git -C "$WT" ls-tree HEAD third_party/clatter-pocket | awk '{print $3}')
have=$(git -C "$ROOT/third_party/clatter-pocket" rev-parse HEAD)
[ "$want" = "$have" ] || echo "compare_builds: $REV pins clatter-pocket $want, this tree has $have (using this tree's)"
rmdir "$WT/third_party/clatter-pocket" 2>/dev/null || true
ln -s "$ROOT/third_party/clatter-pocket" "$WT/third_party/clatter-pocket"
make -C "$WT" CC="$CC" build/c12_unity.o >/dev/null
make -C "$ROOT" CC="$CC" build/c12_unity.o >/dev/null
mkdir -p "$OUT/old" "$OUT/new"
FLAGS="-std=gnu11 -O2 -w -Itests -I$ROOT/third_party/clatter-pocket/engine -I$ROOT/third_party/clatter-pocket/seq"
$CC $FLAGS -I"$WT/core" "$ROOT/tests/golden.c" "$WT/build/c12_unity.o" -lm -o "$OUT/golden_old"
$CC $FLAGS -I"$ROOT/core" "$ROOT/tests/golden.c" "$ROOT/build/c12_unity.o" -lm -o "$OUT/golden_new"
echo "compare_builds: $REV ($(git -C "$WT" rev-parse --short HEAD)) against this tree${MODE:+ ($MODE)}"
"$OUT/golden_old" "$OUT/old" nospread
"$OUT/golden_new" "$OUT/new" $MODE
status=0
for s in steady live colour apart; do
  printf '  %-7s ' "$s"
  python3 "$ROOT/tools/compare_pcm.py" "$OUT/old/$s.raw" "$OUT/new/$s.raw" || status=1
done
exit $status
