#!/usr/bin/env bash
# ARM-A11 demo_flags: demo_csum.c 의 함수 5개 × FP 플래그 5가지 → "VFxIC / 순서" 표 (실측 → flags_mac.txt)
#   ord  = 벡터 본문에 lane 별 fadd.4s 가 없다 (스칼라 fadd 사슬, 순서 유지 = 결과 비트가 스칼라와 같다)
#   re   = 벡터 본문에 fadd.4s 가 있다 (lane 별 누산 = 재결합, 비트가 달라질 수 있다)
#   no   = 벡터화 안 됨
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/ARM-A11_work/flags; rm -rf "$W"; mkdir -p "$W"
FUNCS="v_if v_sel v_if_ra v_sel_ra v_sum"
printf '%-46s' "flags \\ function"; for f in $FUNCS; do printf '%-11s' "$f"; done; echo
i=0
while IFS= read -r FL; do
	i=$((i + 1))
	cc -std=c11 -O2 $FL -c demo_csum.c -o "$W/o$i.o" -fsave-optimization-record \
		-foptimization-record-file="$W/r$i.yaml" -foptimization-record-passes=loop-vectorize 2>/dev/null
	cc -std=c11 -O2 $FL -S -fno-discard-value-names demo_csum.c -o "$W/s$i.s" 2>/dev/null
	printf '%-46s' "-O2 ${FL:-(none)}"
	for f in $FUNCS; do
		vf=$(awk -v f="$f" '/^--- !/{k=$2} /^Function:/{fn=$2} /VectorizationFactor:/{v=$3} /InterleaveCount:/{c=$3}
			/^\.\.\.$/{ if (fn==f && k=="!Passed") { gsub("\047","",v); gsub("\047","",c); print v "x" c }; v=c="" }' "$W/r$i.yaml")
		if [ -z "$vf" ]; then printf '%-11s' "no"; continue; fi
		if awk -v f="_$f:" '$1==f{p=1} p&&/vector.body/{b=1} b&&/fadd\.4s/{x=1} p&&/End function/{exit} END{exit !x}' "$W/s$i.s"
		then printf '%-11s' "$vf re"; else printf '%-11s' "$vf ord"; fi
	done
	echo
done <<'FLAGS'

-fassociative-math
-fassociative-math -fno-signed-zeros
-ffinite-math-only
-ffast-math
FLAGS
