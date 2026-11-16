#!/usr/bin/env bash
# ARM-A11 demo_bench: 같은 solution.c 커널을 -O2 와 -Os 로 컴파일해 ns/원소 비교 (→ bench_mac.txt)
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/ARM-A11_work/bench; rm -rf "$W"; mkdir -p "$W"
echo "# $(sysctl -n machdep.cpu.brand_string), $(cc --version | head -1)"
echo "# load before: $(uptime | sed 's/.*load averages*: //')"
for O in O2 Os; do
	cc -std=c11 -$O -c solution.c -Dmain=a11_solution_main -o "$W/sol.$O.o" || exit 1
	cc -std=c11 -O2 demo_bench.c "$W/sol.$O.o" -o "$W/bench.$O" || exit 1
	"$W/bench.$O" "kernels built with -$O"
done
echo "# load after: $(uptime | sed 's/.*load averages*: //')"
