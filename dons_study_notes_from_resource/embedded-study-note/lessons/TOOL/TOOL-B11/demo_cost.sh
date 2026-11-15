#!/usr/bin/env bash
# TOOL-B11 demo — ASan cost: median of 5 runs, max RSS, binary size.  bash demo_cost.sh
set -u
cd "$(dirname "$0")" || exit 1
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=/tmp/TOOL-B11_work/cost; rm -rf "$D"; mkdir -p "$D"
cc -std=c11 -O1 -g demo_cost.c solution.c -o "$D/plain"
cc -std=c11 -O1 -g -fsanitize=address demo_cost.c solution.c -o "$D/asan"
med() { sort -n | sed -n 3p; }   # 5 samples -> 3rd
for b in plain asan; do
	for i in 1 2 3 4 5; do /usr/bin/time -l "$D/$b" >"$D/$b.$i" 2>&1; done
	a=$(cat "$D/$b".? | sed -nE 's/.*alloc_ms=([0-9.]+).*/\1/p' | med)
	c=$(cat "$D/$b".? | sed -nE 's/.*crc_ms=([0-9.]+).*/\1/p' | med)
	h=$(cat "$D/$b".? | sed -nE 's/.*hist_ms=([0-9.]+).*/\1/p' | med)
	r=$(cat "$D/$b".? | awk '/maximum resident/ {print $1}' | med)
	echo "$b: alloc_ms=$a crc_ms=$c hist_ms=$h maxrss_KiB=$((r / 1024))"
done
echo "--- size -m (Mach-O segments) of the two executables"
for b in plain asan; do /usr/bin/size -m "$D/$b" | grep -E 'Segment __TEXT:|Section __text:' | sed "s/^/$b /"; done
echo "--- ASan runtime is a separate dylib:"
otool -L "$D/asan" | grep -i asan
