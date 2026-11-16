#!/bin/bash
# ENV: qemu
# ARM-A07 — Thumb-2 DSP 레인. Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/ARM-A07_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-A07/run_qemu.sh [m4|m4dis|m3probe]'
#   m4      : m4_harness.c + m4_fir.S + m4_startup.c 를 Cortex-M4 (mps2-an386) 에서 실행. 전부 PASS 면 exit 0
#   m4dis   : 핵심 루프의 실제 인코딩 (objdump)
#   m3probe : 같은 DSP 명령(QADD)을 Cortex-M3 (mps2-an385) 에서 실행 → UNDEFINSTR UsageFault 를 보면 exit 0
# (starter/solution/practice 는 맥 레인 — 이 스크립트는 M4 부분만)
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-A07_work
mkdir -p "$W"
cp "$SRC"/m4_*.c "$SRC"/m4_*.S "$SRC"/m3probe.c "$SRC"/m4.ld "$W"/ 2>/dev/null
cd "$W" || exit 1

COMMON="-mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns -mfloat-abi=soft"
QEMU="qemu-system-arm -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build_m4() {
    arm-none-eabi-gcc $COMMON -mcpu=cortex-m4 -T m4.ld m4_harness.c m4_fir.S m4_startup.c -o m4.elf -lgcc
}

case "${1:-m4}" in
m4)
    build_m4 || exit 2
    timeout 20 $QEMU -M mps2-an386 -kernel m4.elf 2>&1 | cat
    rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    exit $rc ;;
m4dis)
    build_m4 || exit 2
    arm-none-eabi-objdump -d m4.elf | sed -n '/<fir_m4_q31>:/,/<q_get>:/p' | grep -v '^$' | sed '$d'
    echo "--- fir_m4_smlad (gcc -O2, ACLE __smlad/__ssat): 안쪽 루프 + 출력 ---"
    arm-none-eabi-objdump -d m4.elf | sed -n '/<fir_m4_smlad>:/,/^$/p' | grep -B4 -A10 'smlad' ;;
m3probe)
    arm-none-eabi-gcc $COMMON -mcpu=cortex-m3 -T m4.ld m3probe.c m4_startup.c -o m3.elf -lgcc || exit 2
    timeout 20 $QEMU -M mps2-an385 -kernel m3.elf 2>&1 | cat
    rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    exit $rc ;;
*) echo "usage: $0 [m4|m4dis|m3probe]"; exit 2 ;;
esac
