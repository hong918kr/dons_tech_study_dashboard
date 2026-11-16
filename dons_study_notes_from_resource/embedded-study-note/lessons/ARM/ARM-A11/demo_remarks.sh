#!/usr/bin/env bash
# ARM-A11 demo_remarks: 실제 -Rpass 리마크와 레벨별 asm 크기 (→ remarks_mac.txt)
#   bash demo_remarks.sh > remarks_mac.txt
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/ARM-A11_work/remarks; rm -rf "$W"; mkdir -p "$W"
R="-Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize"
LAST=$(grep -n '= 테스트 하네스 =' solution.c | head -1 | cut -d: -f1)   # 커널 줄만 (하네스 리마크는 뺀다)
echo "# $(cc --version | head -1)"
for O in O2 O3 Os; do
	echo "=== solution.c -$O  ($R)  — kernel lines < $LAST"
	cc -std=c11 -$O -c solution.c -o /dev/null $R 2>&1 | grep -E '^solution\.c:[0-9]+:[0-9]+: remark' |
		awk -F: -v last="$LAST" '$2 < last' | sed 's/ \[-Rpass[^]]*\]$//'
done
echo
echo "=== instructions per function (objdump), O2 vs O3 vs Os; O2==O3 compares the disassembly text"
printf '%-16s %6s %6s %6s  %s\n' function O2 O3 Os "O2 asm == O3 asm?"
for O in O2 O3 Os; do
	cc -std=c11 -$O -c solution.c -o "$W/s.$O.o"
done
for f in saxpy_plain saxpy_restrict hist_plain hist4 csum_f32_if csum_f32_sel csum_f32_fast csum_i32 dot_i16 gain_inplace ae_fused ae_frame_stats; do
	row="$f"
	for O in O2 O3 Os; do
		/usr/bin/objdump -d --no-show-raw-insn --no-leading-addr --disassemble-symbols="_$f" "$W/s.$O.o" |
			awk '/^<_/ { p = 1; next } p && NF' | sed 's/0x[0-9a-f]* </</' >"$W/$f.$O.txt"   # 절대 주소 → 함수 상대
		row="$row $(wc -l <"$W/$f.$O.txt" | tr -d ' ')"
	done
	cmp -s "$W/$f.O2.txt" "$W/$f.O3.txt" && same=yes || same=no
	printf '%-16s %6s %6s %6s  %s\n' $row $same
done
echo
for O in O2 Os; do
	echo "=== demo_pragmas.c -$O (-Rpass='loop-vectorize|loop-unroll' + missed/analysis), remarks + warnings"
	cc -std=c11 -$O -c demo_pragmas.c -o /dev/null $R -Rpass='loop-vectorize|loop-unroll' 2>&1 |
		grep -E '^demo_pragmas\.c:[0-9]+:[0-9]+: (remark|warning)' | sed 's/ \[-[RW][^]]*\]$//'
done
