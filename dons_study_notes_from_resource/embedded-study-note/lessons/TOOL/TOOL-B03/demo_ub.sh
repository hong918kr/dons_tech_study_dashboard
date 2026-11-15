#!/usr/bin/env bash
# TOOL-B03 demo_ub.sh — why starter.c changes behaviour with the -O level.
#   bash demo_ub.sh asm     # -O0 vs -O2 disassembly of the 4 functions (starter, solution)
#   bash demo_ub.sh flags   # starter -O2 with -fwrapv / -fno-strict-aliasing; UBSan + ASan reports
#   bash demo_ub.sh vm      # GCC 15 at every level, clang -fsanitize=type, Cortex-M4 asm (Lima VM)
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/TOOL-B03_work/ub
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
OBJDUMP=/usr/bin/objdump
FUNCS="_range_ok _frames_until_wrap _pack_float_word _cal_has_code"
CF="-std=c11 -Wall -Wextra -Werror"

dis() { # $1 = object, rest = symbols
	local o=$1; shift
	for f in "$@"; do
		"$OBJDUMP" -d --no-show-raw-insn --disassemble-symbols="$f" "$o" | sed -n '/^[0-9a-f]* <_/,$p'
	done
}

asm() {
	rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 1
	for v in starter solution; do
		for O in O0 O2; do cc $CF -$O -c "$SRC/$v.c" -o $v.$O.o; done
	done
	echo "### starter -O0: range_ok (the overflow check is really computed)"
	dis starter.O0.o _range_ok
	echo "### starter -O2"
	dis starter.O2.o $FUNCS
	echo "### solution -O2"
	dis solution.O2.o $FUNCS
}

flags() {
	rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 1
	for f in "" "-fwrapv" "-fno-strict-aliasing" "-fwrapv -fno-strict-aliasing"; do
		cc $CF -O2 $f "$SRC/starter.c" -o s
		echo "### starter -O2 ${f:-(no extra flag)}: $(./s | tail -1)"
		./s | grep '^FAIL' | sed 's/^/    /'
	done
	echo "### starter, -g -O1 -fsanitize=undefined (recoverable: every report, run continues)"
	cc -std=c11 -g -O1 -fsanitize=undefined "$SRC/starter.c" -o s_ub
	./s_ub 2>&1 | grep -E 'runtime error|passed' | sed "s#$SRC/##"
	echo "### starter, -g -O1 -fsanitize=address (first report, then abort)"
	cc -std=c11 -g -O1 -fsanitize=address "$SRC/starter.c" -o s_as
	(./s_as >/dev/null 2>asan.txt; exit $?) 2>/dev/null
	echo "exit=$?"
	sed -n '/ERROR: AddressSanitizer/,/^SUMMARY/p' asan.txt | sed "s#$SRC/##"
}

vm() {
	limactl shell elinux -- bash -lc "
		set -u; W=$W; rm -rf \$W && mkdir -p \$W && cd \$W && cp '$SRC/starter.c' .
		echo \"### \$(gcc --version | head -1): starter.c at each level\"
		for O in O0 O1 O2 O3 Os Oz Og; do
			gcc $CF -\$O starter.c -o g_\$O 2>&1 | head -3
			echo \"-\$O: \$(./g_\$O | tail -1)  \$(./g_\$O | grep ^FAIL | sed 's/ *=.*//; s/^FAIL //' | sort -u | tr '\n' ';')\"
		done
		echo \"### \$(clang --version | head -1): -g -O1 -fsanitize=type (TypeSanitizer)\"
		clang -std=c11 -g -O1 -fsanitize=type starter.c -o ty
		./ty 2>&1 | grep -A2 'ERROR: TypeSanitizer' | head -3
		echo \"### \$(arm-none-eabi-gcc --version | head -1): starter.c -mcpu=cortex-m4 -mthumb -O2 -c\"
		arm-none-eabi-gcc -std=c11 -mcpu=cortex-m4 -mthumb -O2 -c starter.c -o m4.o
		arm-none-eabi-objdump -d --no-show-raw-insn m4.o | sed -n '/<range_ok>:/,/<main>:/p' | grep -v -e '^Disassembly' -e '<main>:'
		echo \"### clang (VM) starter at each level\"
		for O in O0 O1 O2 O3 Os Oz; do
			clang $CF -\$O starter.c -o c_\$O
			echo \"-\$O: \$(./c_\$O | tail -1)\"
		done
	"
}

case "${1:-}" in
asm) asm ;;
flags) flags ;;
vm) vm ;;
*) echo "usage: bash demo_ub.sh asm|flags|vm"; exit 2 ;;
esac
