#!/usr/bin/env bash
# ARM-A11 demo_gcc_vm: 같은 solution.c 를 리눅스 VM 의 gcc 로 → 어느 커널이 벡터화되나 (→ gcc_vm.txt)
#   Mac 에서:  limactl shell elinux -- bash -lc 'bash <이 디렉터리>/demo_gcc_vm.sh' > gcc_vm.txt
# 맥 홈은 VM 에서 읽기 전용 → 출력은 /tmp 로. gcc 는 '#pragma clang …' 을 모른다 (-Wunknown-pragmas, 무시됨).
set -u
cd "$(dirname "$0")" || exit 2
LAST=$(grep -n '= 테스트 하네스 =' solution.c | head -1 | cut -d: -f1)
echo "# $(gcc --version | head -1)   (kernel lines < $LAST; '#pragma clang' ignored by gcc)"
for O in O2 O3 Os; do
	echo "=== gcc -$O -fopt-info-vec-optimized"
	gcc -std=c11 -$O -c solution.c -o /tmp/ARM-A11_gcc.o -fopt-info-vec-optimized 2>&1 |
		awk -F: -v last="$LAST" '$1 == "solution.c" && $2 < last' | sort -t: -k2,2n | uniq
done
echo "=== gcc -O2 -fopt-info-vec-missed (first 'not vectorized' reason per kernel line)"
gcc -std=c11 -O2 -c solution.c -o /tmp/ARM-A11_gcc.o -fopt-info-vec-missed 2>&1 |
	awk -F: -v last="$LAST" '$1 == "solution.c" && $2 < last && /not vectorized|cost model|versioning/' | sort -t: -k2,2n | uniq
echo "=== gcc -O3 -S: float 합이 lane 별(fadd v.4s)인가, 순서 유지(스칼라 fadd s)인가"
gcc -std=c11 -O3 -S solution.c -o /tmp/ARM-A11_gcc.s 2>/dev/null
for f in csum_f32_sel csum_f32_fast; do
	b=$(awk -v f="$f:" '$1 == f { p = 1 } p { print } p && /\.size/ { exit }' /tmp/ARM-A11_gcc.s)
	echo "$f: fadd v.4s = $(grep -cE 'fadd[[:space:]]+v[0-9]+\.4s' <<<"$b"), scalar fadd s = $(grep -cE 'fadd[[:space:]]+s[0-9]+' <<<"$b")"
done
