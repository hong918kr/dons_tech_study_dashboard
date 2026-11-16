#!/bin/bash
# ARM-A10 gen.sh — (Mac) demo_gen.c 를 분기 보호 옵션별로 빌드·링크하고 디스어셈블 → gen_mac.txt
#   cd lessons/ARM/ARM-A10 && bash gen.sh
# 실행 파일은 /tmp/ARM-A10_gen_* 에 남는다. 각 실행 파일을 한 번 돌려 종료 코드(0 = 정상)도 기록한다.
set -u
cd "$(dirname "$0")"
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
OD=/usr/bin/objdump
FUNCS='/<_(leaf_add|nonleaf|early_exit|tail_call|call_ptr)>:/,/^$/p'
{
for v in "none|-mbranch-protection=none" "standard|-mbranch-protection=standard" \
         "pacret_leaf|-mbranch-protection=pac-ret+leaf" "pacret_bkey|-mbranch-protection=pac-ret+b-key" \
         "pacret_pc|-mbranch-protection=pac-ret+pc" "arm64e|-arch arm64e"; do
    n=${v%%|*}; f=${v#*|}
    # shellcheck disable=SC2086
    cc -O2 $f demo_gen.c demo_gen_main.c -o /tmp/ARM-A10_gen_$n || exit 1
    /tmp/ARM-A10_gen_$n; rc=$?
    echo "##### variant=$n  flags: cc -O2 $f   (run exit=$rc)"
    $OD -d /tmp/ARM-A10_gen_$n | sed -n -E "$FUNCS"
done
echo "##### variant=cortex_m85  flags: clang --target=thumbv8.1m.main-none-eabi -mcpu=cortex-m85 -mfloat-abi=soft -ffreestanding -O2 -mbranch-protection=standard -c"
clang --target=thumbv8.1m.main-none-eabi -mcpu=cortex-m85 -mfloat-abi=soft -ffreestanding -O2 \
      -mbranch-protection=standard -c demo_gen.c -o /tmp/ARM-A10_gen_m85.o || exit 1
$OD -d /tmp/ARM-A10_gen_m85.o | sed -n '/<leaf_add>:/,$p'
} > gen_mac.txt
wc -l gen_mac.txt

# ---- PAC/BTI 실행 결과 (Mac) → mac_forge.txt
{
for v in "arm64 none|-mbranch-protection=none" "arm64 standard|-mbranch-protection=standard" "arm64e|-arch arm64e"; do
    n=${v%%|*}; f=${v#*|}
    # shellcheck disable=SC2086
    cc -std=c11 -O2 $f demo_forge.c -o /tmp/ARM-A10_forge || exit 1
    for m in forge replay replay2; do echo "### $n ($f) $m"; /tmp/ARM-A10_forge $m; done
done
echo "### BTI 표 (Mac arm64, -mbranch-protection=standard)"
cc -O2 -mbranch-protection=standard demo_bti.c demo_bti_pads.S -o /tmp/ARM-A10_bti && /tmp/ARM-A10_bti
} > mac_forge.txt 2>&1
cat mac_forge.txt
