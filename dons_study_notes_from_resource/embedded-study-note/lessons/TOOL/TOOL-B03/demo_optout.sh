#!/usr/bin/env bash
# TOOL-B03 demo_optout.sh — the same breakpoint at -O0 / -O1 / -Og / -O2.
#   bash demo_optout.sh mac   # Apple clang + lldb batch (demo_optout.lldb)
#   bash demo_optout.sh vm    # Lima VM: gcc -static + qemu-aarch64 -g + gdb-multiarch
#                             # (native gdb in the VM fails: "Unable to fetch SVE/SSVE vector length")
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/TOOL-B03_work/optout

mac() {
	export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
	rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 1
	cp "$SRC/demo_optout.c" .
	for O in O0 O1 Og O2; do
		cc -std=c11 -g -$O demo_optout.c -o optout_$O
		echo "=========== clang -g -$O"
		# command source -e false: keep going after a failed 'expression'
		lldb -b -o "command source -e false $SRC/demo_optout.lldb" ./optout_$O 2>&1 |
			sed -n '/^(lldb) breakpoint set/,$p'
	done
	echo "=========== DWARF location lists, clang -g -O2 -c (dwarfdump --debug-info)"
	cc -std=c11 -g -O2 -c demo_optout.c -o o2.o
	dwarfdump --debug-info o2.o | sed -n '/DW_AT_name\t("filter_block")/,/DW_TAG_lexical_block/p' |
		grep -E 'DW_TAG|DW_AT_location|DW_AT_name|^ +\[0x'
	echo "=========== -O2 filter_block tail (objdump -d, .o addresses)"
	/usr/bin/objdump -d --no-show-raw-insn --disassemble-symbols=_filter_block o2.o | tail -16
}

vm() {
	limactl shell elinux -- bash -lc "
		set -u; W=$W; rm -rf \$W && mkdir -p \$W && cd \$W && cp '$SRC/demo_optout.c' .
		for O in O0 Og O2; do
			gcc -std=c11 -g -\$O -static demo_optout.c -o optout_\$O
			echo \"=========== gcc -g -\$O  (qemu-aarch64 -g + gdb-multiarch)\"
			qemu-aarch64 -g 23403 ./optout_\$O >/dev/null 2>&1 &
			sleep 0.5
			gdb-multiarch -q -batch -nx -ex 'set debuginfod enabled off' \
				-ex 'target remote :23403' -ex 'break demo_optout.c:25' -ex 'continue' \
				-ex 'info args' -ex 'info locals' -ex 'print sum + peak' -ex 'finish' \
				-ex 'kill' ./optout_\$O 2>&1 | grep -v -e '^\\[' -e '_start ()' -e '^Kill the program'
			wait
		done
	"
}

case "${1:-}" in
mac) mac ;;
vm) vm ;;
*) echo "usage: bash demo_optout.sh mac|vm"; exit 2 ;;
esac
