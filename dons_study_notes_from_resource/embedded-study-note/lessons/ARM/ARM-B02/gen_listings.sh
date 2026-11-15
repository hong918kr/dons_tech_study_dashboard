#!/usr/bin/env bash
# ARM-B02: regenerate the -O0 / -O2 listings of demo_six.c and print a per-function summary.
#   bash gen_listings.sh            (writes demo_six_O0.s, demo_six_O2.s next to this script)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
W=/tmp/ARM-B02_listings; mkdir -p "$W"

clang --version | head -1
for o in O0 O2; do
  clang -S -$o -fno-asynchronous-unwind-tables -o "$W/six_$o.s" "$HERE/demo_six.c"
  echo "-$o: $(grep -c '\.cfi_' "$W/six_$o.s") .cfi lines even with -fno-asynchronous-unwind-tables (Apple keeps unwind info)"
  grep -v '\.cfi_' "$W/six_$o.s" > "$HERE/demo_six_$o.s"     # trimmed copy kept in the lesson
done

# instruction count per function = tab-indented lines that are not directives
count() {
  awk '/^_[a-z_]+:/ { f=$1; sub(":","",f) }
       /^\t[a-z]/ && f != "" { n[f]++ }
       /End function/ { if (f != "") order[++k]=f; f="" }
       END { for (i=1;i<=k;i++) printf "%s=%d ", order[i], n[order[i]]; print "" }' "$1"
}
echo "insns -O0: $(count "$HERE/demo_six_O0.s")"
echo "insns -O2: $(count "$HERE/demo_six_O2.s")"

clang -S -O2 -fno-vectorize -fno-asynchronous-unwind-tables -o "$W/six_novec.s" "$HERE/demo_six.c"
echo "insns -O2 -fno-vectorize: $(count "$W/six_novec.s")"
echo "--- sum_loop at -O2 -fno-vectorize ---"
grep -v '\.cfi_' "$W/six_novec.s" | sed -n '/^_sum_loop:/,/End function/p'
