#!/usr/bin/env bash
# ARM-A11 check_vec: "이 함수는 벡터화됐나, 왜 안 됐나"를 함수별로 확인한다.
#   bash check_vec.sh                 # solution.c + practice.c (체커가 돌리는 기본)
#   bash check_vec.sh solution|practice|starter
# 방법: 파일을 -O2 / -O3 / -Os 로 다시 컴파일하면서
#   (1) -fsave-optimization-record (YAML) → 함수 이름이 붙은 loop-vectorize 리마크 (Passed/Missed/Analysis, VF, IC)
#   (2) -S -fno-discard-value-names → 블록 이름(vector.memcheck, vector.body …)이 남은 asm
# 을 만들고, 기대값과 비교한다. 기대값은 Apple clang 21 (clang-2100.0.123.102) 에서 실측한 것이다.
# 다른 버전의 clang 에서는 VF/IC 가 달라질 수 있다 → FAIL 줄이 "무엇이 바뀌었나"를 알려 준다.
# 쓰는 곳: /tmp/ARM-A11_work/check_vec 만.
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk # 27.2 SDK link is broken

W=/tmp/ARM-A11_work/check_vec
rm -rf "$W" && mkdir -p "$W" || exit 2
PASS=0
TOTAL=0
ok() { PASS=$((PASS + 1)); TOTAL=$((TOTAL + 1)); echo "PASS $*"; }
bad() { TOTAL=$((TOTAL + 1)); echo "FAIL $*"; }

echo "# $(cc --version | head -1)"

# build <file.c> <O>  →  $W/<base>.<O>.yaml, $W/<base>.<O>.s, $W/<base>.<O>.txt (함수 kind name vf ic)
build() {
	local f=$1 O=$2 b=${1%.c}
	cc -std=c11 -"$O" -c "$f" -o "$W/$b.$O.o" -fsave-optimization-record \
		-foptimization-record-file="$W/$b.$O.yaml" -foptimization-record-passes=loop-vectorize \
		2>"$W/$b.$O.log" || { echo "build failed: $f -$O"; cat "$W/$b.$O.log"; return 1; }
	cc -std=c11 -"$O" -S -fno-asynchronous-unwind-tables -fno-discard-value-names "$f" \
		-o "$W/$b.$O.s" 2>>"$W/$b.$O.log" || return 1
	awk '/^--- !/ { k = $2; n = f = vf = ic = "" }
	     /^Name:/ { n = $2 }  /^Function:/ { f = $2 }
	     /VectorizationFactor:/ { vf = $3; gsub("\047", "", vf) }
	     /InterleaveCount:/ { ic = $3; gsub("\047", "", ic) }
	     /^\.\.\.$/ { print f, k, n, (vf == "" ? "-" : vf), (ic == "" ? "-" : ic) }' \
		"$W/$b.$O.yaml" >"$W/$b.$O.txt"
}

# vec <base> <O> <func> <VFxIC>: 그 함수에 "vectorized loop (width VF, interleave IC)" 기록이 있다
vec() {
	local want=$4 got
	got=$(awk -v f="$3" '$1 == f && $2 == "!Passed" { printf "%s%sx%s", sep, $4, $5; sep = "," }' "$W/$1.$2.txt")
	if [[ ",$got," == *",$want,"* ]]; then ok "-$2 $3: vectorized $want"
	else bad "-$2 $3: expected vectorized $want, got '${got:-none}'"; fi
}
# novec <base> <O> <func> <Name>: Passed 기록이 하나도 없고, 이유(Name) 기록이 있다
novec() {
	local got why
	got=$(awk -v f="$3" '$1 == f && $2 == "!Passed"' "$W/$1.$2.txt" | wc -l | tr -d ' ')
	why=$(awk -v f="$3" '$1 == f && $2 != "!Passed" { printf "%s%s", sep, $3; sep = "," }' "$W/$1.$2.txt")
	if [ "$got" = 0 ] && [[ ",$why," == *",$4,"* ]]; then ok "-$2 $3: not vectorized ($4)"
	else bad "-$2 $3: expected not vectorized ($4), got $got passed, reasons '${why:-none}'"; fi
}
# reason <base> <O> <func> <Name>: (벡터화 여부와 상관없이) 그 이유 기록이 있다
reason() {
	if awk -v f="$3" -v n="$4" '$1 == f && $3 == n { found = 1 } END { exit !found }' "$W/$1.$2.txt"
	then ok "-$2 $3: remark $4"; else bad "-$2 $3: no remark $4"; fi
}
# body <base> <O> <func>: asm 에서 그 함수 본체만
body() { awk -v f="_$3:" '$1 == f { p = 1 } p { print } p && /End function/ { exit }' "$W/$1.$2.s"; }
asm_has() {
	if body "$1" "$2" "$3" | grep -Eq "$4"; then ok "-$2 $3: asm has /$4/"
	else bad "-$2 $3: asm lacks /$4/"; fi
}
asm_not() {
	if body "$1" "$2" "$3" | grep -Eq "$4"; then bad "-$2 $3: asm unexpectedly has /$4/"
	else ok "-$2 $3: asm has no /$4/"; fi
}

check_solution() { # $1 = solution | starter
	local b=$1 O
	for O in O2 O3 Os; do build "$b.c" "$O" || { bad "build $b -$O"; return; }; done
	for O in O2 O3; do
		# 주어진 커널
		vec "$b" $O saxpy_plain 4x4
		asm_has "$b" $O saxpy_plain 'vector\.memcheck'        # 겹침 검사 블록
		novec "$b" $O hist_plain CantIdentifyArrayBounds
		novec "$b" $O csum_f32_if NonReductionValueUsedOutsideLoop
		vec "$b" $O csum_i32 4x4
		asm_has "$b" $O csum_i32 'cmgt\.4s'
		vec "$b" $O dot_i16 8x4
		asm_has "$b" $O dot_i16 'smlal2\.4s'
		novec "$b" $O ae_fused CantIdentifyArrayBounds
		# TODO 커널
		vec "$b" $O saxpy_restrict 4x4
		asm_not "$b" $O saxpy_restrict 'vector\.memcheck'    # restrict → 검사 사라짐
		vec "$b" $O csum_f32_sel 4x4
		asm_not "$b" $O csum_f32_sel 'fadd\.4s'              # 순서 유지(ordered) 리덕션: 스칼라 fadd 사슬
		vec "$b" $O csum_f32_fast 4x4
		asm_has "$b" $O csum_f32_fast 'fadd\.4s'             # 재결합: lane 별 누산
		reason "$b" $O hist4 UnsafeDep                        # 본 루프: 간접 의존
		vec "$b" $O hist4 4x4                                 # 병합 루프만 벡터
		vec "$b" $O gain_inplace 4x4
		asm_not "$b" $O gain_inplace 'vector\.memcheck'
		vec "$b" $O ae_frame_stats 16x1
	done
	# -Os: 꼬리를 스칼라 루프로 둘 수 없다 → NEON 에서는 "이득 없음"
	novec "$b" Os saxpy_plain CantVersionLoopWithOptForSize
	novec "$b" Os saxpy_restrict VectorizationNotBeneficial
	novec "$b" Os dot_i16 VectorizationNotBeneficial
	novec "$b" Os csum_f32_fast VectorizationNotBeneficial
	vec "$b" Os hist4 4x1                                     # 병합 루프: 반복 256 회 고정 → 꼬리 없음
	asm_not "$b" Os csum_i32 'cmgt\.4s'
}

check_practice() {
	local b=practice O
	for O in O2 O3 Os; do build "$b.c" "$O" || { bad "build $b -$O"; return; }; done
	for O in O2 O3; do
		vec $b $O dot_i16_w4 4x1                                # pragma 가 폭·인터리브를 고정
		reason $b $O dot_i16_w4 InterleavingBeneficialButDisabled
		vec $b $O sum_ax 8x4
		asm_has $b $O sum_ax 'ld4\.8h'                         # AoS 보폭 4 → ld4
		vec $b $O sum_stride 4x4
		asm_has $b $O sum_stride 'cmp[[:space:]]+x2, #1$'      # st == 1 일 때만 벡터 경로
		novec $b $O prefix_sum UnsafeDep
		reason $b $O hist4_u16 UnsafeDep
	done
	novec $b Os sum_ax VectorizationNotBeneficial
}

case "${1:-all}" in
solution | starter) check_solution "$1" ;;
practice) check_practice ;;
all) check_solution solution; check_practice ;;
*) echo "usage: bash check_vec.sh [solution|practice|starter]"; exit 2 ;;
esac

echo "$PASS/$TOTAL passed"
[ "$PASS" -eq "$TOTAL" ]
