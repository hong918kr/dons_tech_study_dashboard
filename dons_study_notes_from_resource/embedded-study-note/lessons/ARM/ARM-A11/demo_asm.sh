#!/usr/bin/env bash
# ARM-A11 demo_asm: md 에 붙인 asm 발췌의 원본 (→ asm_mac.txt)
#   bash demo_asm.sh > asm_mac.txt
# clang -S -fno-discard-value-names: 블록 이름(%vector.memcheck, %vector.body …)이 주석으로 남는다
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/ARM-A11_work/asm; rm -rf "$W"; mkdir -p "$W"
S="-std=c11 -S -fno-asynchronous-unwind-tables -fno-discard-value-names"
cc $S -O2 solution.c -o "$W/sol.O2.s"
cc $S -Os solution.c -o "$W/sol.Os.s"
cc $S -O2 practice.c -o "$W/pra.O2.s"
cc $S -Os demo_pragmas.c -o "$W/prag.Os.s" 2>/dev/null
# fn <file> <func> [from-regex] [to-regex]: 함수 본체 (선택: from 줄부터 to 줄까지만)
fn() {
	awk -v f="_$2:" -v a="${3:-}" -v b="${4:-}" '
		$1 == f { p = 1 } p && /End function/ { exit }
		p && (a == "" || $0 ~ a) { q = 1 }
		p && q && !/\.cfi_|kill:|^\t\.(p2align|globl|loh)|^Lloh|^Lfunc_end/ { print }
		q && b != "" && $0 ~ b { exit }' "$1"
}
echo "### -O2 saxpy_plain (전체)";                fn "$W/sol.O2.s" saxpy_plain
echo; echo "### -O2 saxpy_restrict (진입부)";      fn "$W/sol.O2.s" saxpy_restrict "" "vector.body"
echo; echo "### -O2 csum_f32_if (전체)";           fn "$W/sol.O2.s" csum_f32_if
echo; echo "### -O2 csum_f32_sel vector.body";    fn "$W/sol.O2.s" csum_f32_sel "%vector.body" "b.ne"
echo; echo "### -O2 csum_f32_fast vector.body → middle.block"; fn "$W/sol.O2.s" csum_f32_fast "%vector.body" "faddp.2s"
echo; echo "### -O2 dot_i16 vector.body";          fn "$W/sol.O2.s" dot_i16 "%vector.body" "b.ne"
echo; echo "### -O2 hist_plain (전체)";            fn "$W/sol.O2.s" hist_plain
echo; echo "### -O2 hist4 본 루프 (4개씩)";        fn "$W/sol.O2.s" hist4 "Inner Loop Header" "b.ls|b.hi|b.lo|b.ne"
echo; echo "### -O2 ae_frame_stats vector.body";   fn "$W/sol.O2.s" ae_frame_stats "%vector.body" "b.ne"
echo; echo "### -Os saxpy_restrict (전체)";        fn "$W/sol.Os.s" saxpy_restrict
echo; echo "### -O2 practice sum_ax vector.body";  fn "$W/pra.O2.s" sum_ax "%vector.body" "b.ne"
echo; echo "### -O2 practice sum_stride (진입부: 보폭 1 검사)"; fn "$W/pra.O2.s" sum_stride "" "vector.main.loop.iter.check|vector.ph"
echo; echo "### -Os demo_pragmas saxpy_os_r (vectorize(enable) + restrict: 꼬리 접기)"; fn "$W/prag.Os.s" saxpy_os_r
