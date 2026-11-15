#!/usr/bin/env bash
# TOOL-B03 demo_bench.sh — size + speed of demo_bench.c at every -O level.
#   bash demo_bench.sh mac    # Apple clang: arm64 Mach-O sizes/times + thumbv7em .text
#   bash demo_bench.sh vm     # Lima VM: arm-none-eabi-gcc Cortex-M4 sizes + gcc aarch64 times
# Work dir: /tmp/TOOL-B03_work/bench (Mac) and the same path inside the VM.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/TOOL-B03_work/bench
LEVELS="O0 O1 O2 O3 Os Oz Og"
CF="-std=c11 -Wall -Wextra -Werror"

median5() { # run "$@" 5 times, print the median of the ms value it prints on stderr
	for k in 1 2 3 4 5; do "$@" 2>&1 >/dev/null; done | sort -n | sed -n 3p
}

mac() {
	export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
	OBJDUMP=/usr/bin/objdump
	LLVM_SIZE=/Library/Developer/CommandLineTools/usr/bin/llvm-size
	rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 1
	echo "# Apple clang $(cc --version | head -1 | sed 's/.*version //'), $(uname -m), reps=400"
	printf '%-4s %12s %12s %14s %10s %8s  %s\n' level kernel.o exe.__text thumbv7em.o median_ms speedup checksum
	base=""
	for O in $LEVELS; do
		cc $CF -$O -DBENCH_KERNEL_ONLY -c "$SRC/demo_bench.c" -o k.$O.o
		cc $CF -$O "$SRC/demo_bench.c" -o bench.$O
		clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -mfloat-abi=hard \
			-ffreestanding $CF -$O -DBENCH_KERNEL_ONLY -c "$SRC/demo_bench.c" -o t.$O.o
		ko=$((16#$("$OBJDUMP" -h k.$O.o | awk '$2 == "__text" { print $3 }')))
		ex=$(size -m bench.$O | awk '/Section __text:/ { print $3 }')
		to=$("$LLVM_SIZE" -A t.$O.o | awk '$1 == ".text" { print $2 }')
		ms=$(median5 ./bench.$O)
		sum=$(./bench.$O 2>/dev/null)
		[ -n "$base" ] || base=$ms
		sp=$(awk -v b="$base" -v m="$ms" 'BEGIN { printf "%.1fx", b / m }')
		printf '%-4s %12s %12s %14s %10s %8s  %s\n' -$O "$ko" "$ex" "$to" "$ms" "$sp" "${sum%% *}"
	done
	echo "# per-function bytes, clang thumbv7em .o (llvm-nm --print-size)"
	for O in $LEVELS; do
		printf '%-4s ' -$O
		/Library/Developer/CommandLineTools/usr/bin/llvm-nm --print-size --radix=d t.$O.o |
			awk '$3 ~ /[Tt]/ { printf "%s=%d ", $4, $2 } END { print "" }'
	done
}

vm() {
	limactl shell elinux -- bash -lc "
		set -u; rm -rf $W && mkdir -p $W && cd $W && cp '$SRC/demo_bench.c' .
		echo \"# \$(arm-none-eabi-gcc --version | head -1)  (-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -c)\"
		echo \"# \$(gcc --version | head -1)  aarch64 Linux (VM), reps=400\"
		printf '%-4s %8s %8s %8s %12s %10s %8s  %s\n' level text data bss aarch64.text median_ms speedup checksum
		base=''
		for O in $LEVELS; do
			arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
				$CF -\$O -DBENCH_KERNEL_ONLY -c demo_bench.c -o m4.\$O.o
			gcc $CF -\$O -DBENCH_KERNEL_ONLY -c demo_bench.c -o a64.\$O.o
			gcc $CF -\$O demo_bench.c -o bench.\$O
			read t d b rest < <(arm-none-eabi-size m4.\$O.o | tail -1)
			a=\$(size -A a64.\$O.o | awk '\$1 == \".text\" { print \$2 }')
			ms=\$(for k in 1 2 3 4 5; do ./bench.\$O 2>&1 >/dev/null; done | sort -n | sed -n 3p)
			sum=\$(./bench.\$O 2>/dev/null)
			[ -n \"\$base\" ] || base=\$ms
			sp=\$(awk -v b=\"\$base\" -v m=\"\$ms\" 'BEGIN { printf \"%.1fx\", b / m }')
			printf '%-4s %8s %8s %8s %12s %10s %8s  %s\n' -\$O \$t \$d \$b \$a \$ms \$sp \${sum%% *}
		done
		echo '# per-function bytes, arm-none-eabi-gcc Cortex-M4 .o (arm-none-eabi-nm --print-size)'
		for O in $LEVELS; do
			printf '%-4s ' -\$O
			arm-none-eabi-nm --print-size --radix=d m4.\$O.o |
				awk '\$3 ~ /[Tt]/ { printf \"%s=%d \", \$4, \$2 } END { print \"\" }'
		done
	"
}

case "${1:-}" in
mac) mac ;;
vm) vm ;;
*) echo "usage: bash demo_bench.sh mac|vm"; exit 2 ;;
esac
