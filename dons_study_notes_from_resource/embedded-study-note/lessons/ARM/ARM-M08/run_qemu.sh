#!/bin/bash
# ENV: qemu
# ARM-M08 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, gdb-multiarch).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/ARM-M08_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-M08/run_qemu.sh [starter|solution|practice|gdb|objdump|thumbfunc]'
# 소스 규칙: <v>.c (없으면 harness.c) + <v>.S + shared_*.c   (+ arm_m08.ld)
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-M08_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.S "$SRC"/*.ld "$SRC"/*.gdb "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {   # build <variant> [extra flags...]  → <variant>.elf
    local v=$1; shift
    local main=harness.c
    [ -f "$v.c" ] && main="$v.c"
    arm-none-eabi-gcc $CFLAGS "$@" -T arm_m08.ld "$main" "$v.S" shared_*.c -o "$v.elf" -lgcc
}
run() {     # run <elf>: 펌웨어가 SYS_EXIT 로 QEMU 를 끝낸다. 혹시 몰라 timeout 20 s.
    timeout 20 $QEMU -kernel "$1" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
starter|solution|practice)
    build "$1" || exit 2
    run "$1.elf" ;;
objdump)    # 테이블의 실제 워드(전부 홀수) + 핸들러 심볼 주소(짝수) + 섹션 배치
    build solution || exit 2
    arm-none-eabi-objdump -h solution.elf | grep -E 'isr_vector|\.text|\.data|\.bss'
    arm-none-eabi-objdump -s -j .isr_vector solution.elf | sed -n '4,20p'
    arm-none-eabi-nm -n solution.elf | grep -E ' (g_vectors|Reset_Handler|Default_Handler|NMI_Handler|HardFault_Handler|UsageFault_Handler|SVC_Handler|SysTick_Handler2?|Irq6_Handler|bx_target|call_blx_ret|g_ram_vectors)$'
    arm-none-eabi-readelf -sW solution.elf | grep -E ' (Reset_Handler|NMI_Handler|UsageFault_Handler|SVC_Handler|call_blx_ret|g_vectors)$' ;;
thumbfunc)  # .thumb_func / .type 유무에 따른 심볼 bit0 (demo_thumbfunc.S)
    arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -c demo_thumbfunc.S -o demo_thumbfunc.o || exit 2
    arm-none-eabi-ld -Ttext=0x100 demo_thumbfunc.o -o demo_thumbfunc.elf 2>&1 | grep -v 'cannot find entry'
    arm-none-eabi-readelf -sW demo_thumbfunc.elf | grep -E 'lbl_|tbl'
    arm-none-eabi-objdump -s -j .data demo_thumbfunc.elf | tail -n +4 ;;
gdb)        # -s -S 로 멈춘 QEMU 에 gdb-multiarch 를 붙여 리셋 순간과 INVSTATE 를 본다
    build solution || exit 2
    $QEMU -kernel solution.elf -s -S > qemu_gdb.log 2>&1 &
    QPID=$!
    sleep 1
    timeout 60 gdb-multiarch -q -batch -x demo_vectors.gdb solution.elf
    wait $QPID; echo "qemu exit=$?"
    echo "--- firmware output (semihosting, last 3 lines) ---"; tail -3 qemu_gdb.log ;;
*) echo "usage: $0 [starter|solution|practice|gdb|objdump|thumbfunc]"; exit 2 ;;
esac
