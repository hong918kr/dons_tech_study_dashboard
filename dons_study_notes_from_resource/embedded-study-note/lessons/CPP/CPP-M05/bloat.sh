#!/bin/bash
# CPP-M05 bloat.sh — demo_bloat.cpp 를 MODE×N×최적화로 빌드해 .text/.rodata 를 표로 찍는다 (Lima VM elinux 안에서 실행)
#   limactl shell elinux -- bash -c 'mkdir -p /tmp/CPP-M05 && cp <lesson dir>/* /tmp/CPP-M05/ && bash /tmp/CPP-M05/bloat.sh'
cd "$(dirname "$0")"
F="-std=c++20 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -nostartfiles --specs=nosys.specs -ffunction-sections -fdata-sections -Wl,--gc-sections"
OUT=$(mktemp -d)
for cfg in "-Os" "-O2" "-O2 -DFRAME_NOINLINE"; do
    echo "== $cfg   (.text 바이트)"
    printf "%-34s %8s %8s %8s %8s\n" "mode" "N=1" "N=2" "N=4" "N=8"
    for M in 0 1 2 3; do
        case $M in
        0) name="CRTP, 포트마다 타입" ;;
        1) name="CRTP, 타입 1개+런타임 base" ;;
        2) name="CRTP, 공통 코드 hoist" ;;
        3) name="virtual, 구현 1개" ;;
        esac
        line=""
        for N in 1 2 4 8; do
            arm-none-eabi-g++ $F $cfg -DMODE=$M -DN_INST=$N demo_bloat.cpp -o "$OUT/m$M.n$N.elf" 2>/dev/null || { echo "build fail M=$M N=$N"; exit 1; }
            t=$(arm-none-eabi-size -A "$OUT/m$M.n$N.elf" | awk '$1==".text"{print $2}')
            line="$line $(printf '%8s' "$t")"
        done
        printf "%-34s%s\n" "$name" "$line"
    done
done
echo "== .rodata N=1 -Os (g_payload 16바이트 포함)"
for M in 0 3; do
    arm-none-eabi-g++ $F -Os -DMODE=$M -DN_INST=1 demo_bloat.cpp -o "$OUT/a.elf" 2>/dev/null
    echo "MODE=$M: $(arm-none-eabi-size -A "$OUT/a.elf" | awk '$1==".rodata"{print $2}')"
done
echo "== 함수별 크기 MODE=0 N=4 -O2 -DFRAME_NOINLINE (nm)"
arm-none-eabi-g++ $F -O2 -DFRAME_NOINLINE -DMODE=0 -DN_INST=4 demo_bloat.cpp -o "$OUT/a.elf" 2>/dev/null
arm-none-eabi-nm -C --size-sort -S "$OUT/a.elf" | grep -E ' [TtWw] ' | tail -10
echo "== 함수별 크기 MODE=1 N=4 -O2 -DFRAME_NOINLINE (nm)"
arm-none-eabi-g++ $F -O2 -DFRAME_NOINLINE -DMODE=1 -DN_INST=4 demo_bloat.cpp -o "$OUT/a.elf" 2>/dev/null
arm-none-eabi-nm -C --size-sort -S "$OUT/a.elf" | grep -E ' [TtWw] ' | tail -8
rm -rf "$OUT"
