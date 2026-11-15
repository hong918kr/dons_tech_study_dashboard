#!/usr/bin/env bash
# ARM-B02: prove shared_mystery.S + practice.S contain exactly the instructions clang -O2 emits
# for demo_mystery_src.c (only comments/directives/symbol macros were changed).
#   bash check_mystery_sync.sh        -> "IDENTICAL: N instruction lines" or a diff, exit 0/1
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-B02_sync; mkdir -p "$W"
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}

clang -S -O2 -fno-vectorize -fno-slp-vectorize -fno-asynchronous-unwind-tables \
      -o "$W/raw.s" "$HERE/demo_mystery_src.c"
# preprocess our .S files (expands SYM/PAGE/... for __APPLE__)
cat "$HERE/shared_mystery.S" "$HERE/practice.S" > "$W/ours.S"
clang -E -x assembler-with-cpp "$W/ours.S" -o "$W/ours.i"

norm() {  # keep only instruction lines: drop comments, labels, directives, blanks
  sed -e 's@//.*@@' -e 's/;.*//' -e 's/[[:space:]]\{1,\}/ /g' -e 's/^ //' -e 's/ $//' \
      -e 's/, /,/g' "$1" |
  grep -v -e '^$' -e '^\.' -e '^#' -e ':$' -e '^[A-Za-z_0-9]*: ' -e '^/\*' -e '^\*' || true
}
norm "$W/raw.s"  > "$W/raw.ins"
norm "$W/ours.i" > "$W/ours.ins"
if diff -u "$W/raw.ins" "$W/ours.ins" > "$W/diff.txt"; then
  echo "IDENTICAL: $(wc -l < "$W/raw.ins" | tr -d ' ') instruction lines (clang -O2 vs shared_mystery.S + practice.S)"
else
  cat "$W/diff.txt"; exit 1
fi
