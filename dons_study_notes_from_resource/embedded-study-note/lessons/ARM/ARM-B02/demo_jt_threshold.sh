#!/usr/bin/env bash
# ARM-B02: at which case count does Apple clang -O2 switch from a compare tree to a jump table?
# Generates switch(op){ case 0..N-1: return f(a*k+i); } and greps the asm for a jump table (lJTI).
set -euo pipefail
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
W=/tmp/ARM-B02_jt; mkdir -p "$W"
for opt in O0 O2; do
  line="-$opt:"
  for n in 3 4 5 6 7 8 9 10 11 12; do
    { echo "int f(int);"; echo "int g(int op, int a) { switch (op) {"
      for ((i = 0; i < n; i++)); do echo "case $i: return f(a * $((i + 3)) + $i);"; done
      echo "default: return -22; } }"; } > "$W/sw$n.c"
    clang -S -$opt -o "$W/sw$n.s" "$W/sw$n.c"
    if grep -q 'lJTI' "$W/sw$n.s"; then line="$line $n=JT"; else line="$line $n=cmp"; fi
  done
  echo "$line"
done
