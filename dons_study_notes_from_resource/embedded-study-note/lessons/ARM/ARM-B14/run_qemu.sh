#!/bin/bash
# ENV: qemu
# ARM-B14 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, gdb-multiarch 17.1).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/ARM-B14_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-B14/run_qemu.sh [starter|solution|practice|gdb|noisb|objdump|spselbug]'
# 소스 규칙: <v>.c (없으면 harness.c) + <v>.S + shared_*.c   (+ arm_b14.ld)
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-B14_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.S "$SRC"/*.ld "$SRC"/*.gdb "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {   # build <variant> [extra flags...]  → <variant>.elf
    local v=$1; shift
    local main=harness.c
    [ -f "$v.c" ] && main="$v.c"
    arm-none-eabi-gcc $CFLAGS "$@" -T arm_b14.ld "$main" "$v.S" shared_*.c -o "$v.elf" -lgcc
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
noisb)      # run_on_psp 에서 ISB 를 뺀 판 — QEMU 에서는 차이가 안 보인다 (7. 실수 참고)
    build solution -DNO_ISB || exit 2
    arm-none-eabi-objdump -d solution.elf | sed -n '/<run_on_psp>:/,/<SVC_Handler>:/p' | grep -v '^$' | sed '$d'
    run solution.elf | tail -2 ;;
spselbug)   # 7. 실수 1: PSP 를 안 정하고 C 에서 set_control(SPSEL) → sp 가 PSP(=0) 로 바뀐다
    arm-none-eabi-gcc $CFLAGS -T arm_b14.ld demo_spsel_bug.c solution.S shared_*.c -o spselbug.elf -lgcc || exit 2
    run spselbug.elf ;;
objdump)    # 헬퍼들의 실제 인코딩 (MRS/MSR = 32 비트 Thumb-2)
    build solution || exit 2
    arm-none-eabi-objdump -d solution.elf | sed -n '/<get_msp>:/,/<sh_puts>:/p' | grep -v '^$' | sed '$d' ;;
gdb)        # -s -S 로 멈춘 QEMU 에 gdb-multiarch 를 붙여 레지스터 변화를 본다
    build solution || exit 2
    $QEMU -kernel solution.elf -s -S > qemu_gdb.log 2>&1 &
    QPID=$!
    sleep 1
    timeout 60 gdb-multiarch -q -batch -x demo_regs.gdb solution.elf
    wait $QPID; echo "qemu exit=$?"
    echo "--- firmware output (semihosting) ---"; tail -3 qemu_gdb.log ;;
*) echo "usage: $0 [starter|solution|practice|gdb|noisb|objdump|spselbug]"; exit 2 ;;
esac
