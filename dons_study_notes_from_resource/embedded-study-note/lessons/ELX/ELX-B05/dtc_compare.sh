#!/bin/bash
# ELX-B05: 내 파서 출력 vs `dtc -I dtb -O dts` 를 여러 진짜 DTB 로 바이트 단위 비교 (Mac)
# 사용: bash dtc_compare.sh [solution|practice|starter]     (기본 solution)
set -u
V=${1:-solution}
D=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d /tmp/ELX-B05_cmp.XXXXXX)
trap 'rm -rf "$W"' EXIT
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
cc -std=c11 -Wall -Wextra -Werror -O2 "$D/$V.c" -o "$W/p" || exit 1
pass=0 total=0
ok()   { pass=$((pass + 1)); total=$((total + 1)); echo "PASS $*"; }
bad()  { total=$((total + 1)); echo "FAIL $*"; }
same() {  # $1 = 이름, $2 = dtb
	"$W/p" "$2" > "$W/mine.dts" 2> "$W/mine.err"
	dtc -q -I dtb -O dts "$2" > "$W/ref.dts" 2> /dev/null
	if cmp -s "$W/mine.dts" "$W/ref.dts"; then ok "$1 ($(wc -c < "$2" | tr -d ' ') B, $(wc -l < "$W/ref.dts" | tr -d ' ') lines) == dtc"
	else bad "$1 differs from dtc"; diff "$W/mine.dts" "$W/ref.dts" | head -5; fi
}
same_nolabel() {  # dtc 1.8 은 __symbols__ 를 보고 "gic: " 같은 라벨을 되살린다 → 라벨만 지우고 비교
	"$W/p" "$2" > "$W/mine.dts" 2> "$W/mine.err"
	dtc -q -I dtb -O dts "$2" > "$W/ref.dts" 2> /dev/null
	n=$(grep -cE '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*: ' "$W/ref.dts")
	sed -E 's/^([[:space:]]*)[A-Za-z_][A-Za-z0-9_]*: /\1/' "$W/ref.dts" > "$W/ref2.dts"
	if cmp -s "$W/mine.dts" "$W/ref2.dts"; then ok "$1 == dtc after stripping $n labels dtc restored from __symbols__"
	else bad "$1 differs from dtc (labels stripped)"; diff "$W/mine.dts" "$W/ref2.dts" | head -5; fi
}
reject() {  # $1 = 이름, $2 = dtb — dtc 도 내 파서도 거부해야 한다
	"$W/p" "$2" > /dev/null 2> "$W/mine.err"; m=$?
	dtc -q -I dtb -O dts "$2" > /dev/null 2> "$W/ref.err"; r=$?
	if [ $m -ne 0 ] && [ $r -ne 0 ]; then ok "$1 rejected: mine='$(cat "$W/mine.err")' dtc='$(head -1 "$W/ref.err")'"
	else bad "$1 mine=$m dtc=$r"; fi
}

same board.dtb "$D/board.dtb"
dtc -q -p 4096 -I dts -O dtb -o "$W/pad.dtb" "$D/board.dts";            same "board -p 4096 (padding)" "$W/pad.dtb"
dtc -q -@ -I dts -O dtb -o "$W/sym.dtb" "$D/board.dts";                 same_nolabel "board -@ (__symbols__)" "$W/sym.dtb"
cp "$D/board.dtb" "$W/put.dtb"; fdtput -t s "$W/put.dtb" /soc@0/ethernet@9050000 status okay
fdtput -t x "$W/put.dtb" /soc@0/i2c@9040000/temp-sensor@48 interrupts 6 1; same "fdtput (libfdt edit)" "$W/put.dtb"
if command -v qemu-system-aarch64 > /dev/null; then
	for m in virt virt,gic-version=3 virt,secure=on; do
		qemu-system-aarch64 -machine "$m,dumpdtb=$W/q.dtb" -cpu cortex-a57 -smp 2 -m 512M -nographic > /dev/null 2>&1
		same "QEMU $m dumpdtb" "$W/q.dtb"
	done
fi
head -c 1000 "$D/board.dtb" > "$W/trunc.dtb";                          reject "truncated 1000 B" "$W/trunc.dtb"
cp "$D/board.dtb" "$W/magic.dtb"; printf '\xd0\x0d\xfe\xee' | dd of="$W/magic.dtb" conv=notrunc 2> /dev/null
reject "bad magic" "$W/magic.dtb"
echo "$pass/$total passed"
[ $pass -eq $total ] && echo "RESULT: PASS ($V)" || { echo "RESULT: FAIL ($V)"; exit 1; }
