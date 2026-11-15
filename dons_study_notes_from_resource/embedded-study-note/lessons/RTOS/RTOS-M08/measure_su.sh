#!/bin/bash
# RTOS-M08: fw_graph.c 를 세 가지로 컴파일해 함수별 스택(.su)과 호출 그래프를 뽑는다.
#   lesson 디렉터리에서: bash measure_su.sh            (결과: /tmp/RTOS-M08_su/{m4,m4o0,a64}/fw_graph.{o,su}, *.edges)
# -fstack-usage 는 .su 를 "-o 로 준 객체 파일 이름.su" 로 쓴다 → 타깃마다 디렉터리를 나눈다.
set -e
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
O=${O:-/tmp/RTOS-M08_su}
mkdir -p $O/m4 $O/m4o0 $O/a64
M4="clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -Wall -Wextra -Werror"
$M4 -O2 -fstack-usage -c fw_graph.c -o $O/m4/fw_graph.o            # Cortex-M4F, 출시 빌드
$M4 -O0 -fstack-usage -c fw_graph.c -o $O/m4o0/fw_graph.o          # Cortex-M4F, 디버그 빌드 (여유율 근거)
cc -O2 -Wall -Wextra -Werror -fstack-usage -c fw_graph.c -o $O/a64/fw_graph.o   # 맥 arm64 (실측 데모와 같은 객체)
for t in m4 m4o0 a64; do bash callgraph.sh $O/$t/fw_graph.o > $O/$t/fw_graph.edges; done
printf '%-22s %8s %8s %8s\n' function "M4 -O2" "M4 -O0" "arm64"
paste $O/m4/fw_graph.su $O/m4o0/fw_graph.su $O/a64/fw_graph.su |
  awk -F'\t' '{ n = $1; sub(/.*:/, "", n); printf "%-22s %8s %8s %8s\n", n, $2, $5, $8 }'
echo "-- edges (M4 -O2)";  tr '\n' ';' < $O/m4/fw_graph.edges; echo
echo "-- edges only in arm64:"; comm -13 $O/m4/fw_graph.edges $O/a64/fw_graph.edges | tr '\n' ';'; echo
