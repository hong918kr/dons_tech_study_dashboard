#!/bin/bash
# ENV: qemu
# BM-M02 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, gdb-multiarch).
# Mac 홈은 VM 에서 읽기 전용 → 소스를 /tmp/BM-M02_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/BM-M02/run_qemu.sh [solution|starter|practice|gdb|tailchain]'
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-M02_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.ld "$SRC"/*.gdb "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {   # build <src.c> <out.elf>
    arm-none-eabi-gcc $CFLAGS -T bm_m02.ld "$1" -o "$2" -lgcc
}
run() {     # QEMU 는 SYS_EXIT 로 스스로 끝난다. 안전망 timeout 10 s.
    timeout 10 $QEMU -kernel "$1" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
solution|starter)
    build "$1.c" "$1.elf" || exit 1
    arm-none-eabi-size "$1.elf"
    run "$1.elf" ;;
gdb)        # design_applied() 에서 멈춰 NVIC/SCB 레지스터를 디버거로 직접 읽는다
    build solution.c solution.elf || exit 1
    $QEMU -kernel solution.elf -s -S > qemu_gdb.log 2>&1 &
    QPID=$!
    sleep 1
    gdb-multiarch -q -batch -x demo_nvic.gdb solution.elf 2>&1 | grep -v "^warning: No executable"
    wait $QPID; echo "qemu exit=$?" ;;
tailchain)  # S5 만 도는 빌드 + QEMU -d int: RX → ADC 선점 → 복귀 → PendSV 꼬리 물기(tail-chain)
    arm-none-eabi-gcc $CFLAGS -DONLY_S5 -T bm_m02.ld solution.c -o s5.elf -lgcc || exit 1
    timeout 10 $QEMU -kernel s5.elf -d int -D qemu_int.log > /dev/null 2>&1
    rc=$?; echo "qemu exit=$rc   (exc 32 = IRQ16 UART_RX, 31 = IRQ15 ADC, 14 = PendSV)"
    grep -v "Semihosting\|semihosting call\|loading from element\|loaded new PC" qemu_int.log
    exit $rc ;;
practice)   # practice.c = 호스트 C (설계 계산기). VM 의 네이티브 cc 로 빌드·실행
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice_host && ./practice_host
    rc=$?; echo "practice exit=$rc"; exit $rc ;;
*) echo "usage: $0 [solution|starter|practice|gdb|tailchain]"; exit 2 ;;
esac
