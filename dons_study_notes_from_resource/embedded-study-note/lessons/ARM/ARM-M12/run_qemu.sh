#!/bin/bash
# ENV: qemu
# ARM-M12 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, gdb-multiarch).
# Mac 홈은 VM 에서 읽기 전용 → 소스를 /tmp/ARM-M12_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-M12/run_qemu.sh [starter|solution|practice|gdb|syms|skip2bug]'
# 소스 규칙: <v>.c (없으면 harness.c) + <v>.S + shared_*.c + shared_*.S  (+ arm_m12.ld)
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-M12_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.S "$SRC"/*.ld "$SRC"/*.gdb "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {   # build <variant> [extra flags...]  → <variant>.elf
    local v=$1; shift
    local main=harness.c
    [ -f "$v.c" ] && main="$v.c"
    arm-none-eabi-gcc $CFLAGS "$@" -T arm_m12.ld "$main" "$v.S" shared_*.c shared_*.S -o "$v.elf" -lgcc
}
run() {     # 펌웨어가 SYS_EXIT 로 QEMU 를 끝낸다. 안전망 timeout 20 s.
    timeout 20 $QEMU -kernel "$1" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
starter|solution|practice)
    build "$1" || exit 2
    run "$1.elf" ;;
skip2bug)   # 7. 실수 2: 쌓인 PC 를 항상 +2 → 32 비트 명령 한가운데로 복귀하면?
    arm-none-eabi-gcc $CFLAGS -DDEMO_SKIP2 -T arm_m12.ld \
        solution.c solution.S shared_*.c shared_*.S -o skip2.elf -lgcc || exit 2
    run skip2.elf ;;
syms)       # 라벨 주소와 실제 인코딩 (쌓인 PC 와 대조용)
    build solution || exit 2
    arm-none-eabi-nm -n solution.elf | grep -E ' (fault_|trigger_ret|run_trigger|Fault_Entry|hardfault_c|task_stack)'
    arm-none-eabi-objdump -d solution.elf | sed -n '/<run_trigger>:/,/<run_on_psp>:/p' | grep -v '^$' | sed '$d'
    arm-none-eabi-objdump -d solution.elf | sed -n '/<Fault_Entry>:/,/^$/p' ;;
gdb)        # -S 로 멈춘 QEMU 에 gdb-multiarch: 핸들러 안에서 bt 와 PC 복원
    build solution || exit 2
    $QEMU -kernel solution.elf -S -gdb tcp::3412 > qemu_gdb.log 2>&1 &    # 포트 3412: 다른 실습과 1234 충돌 방지
    QPID=$!
    sleep 1
    timeout 60 gdb-multiarch -q -batch -x demo_fault.gdb solution.elf
    kill $QPID 2>/dev/null; wait $QPID 2>/dev/null
    echo "--- addr2line on the stacked PC/LR (host side) ---"
    arm-none-eabi-addr2line -f -e solution.elf $(arm-none-eabi-nm solution.elf | awk '/ fault_div_insn$/{print "0x"$1}') \
        $(arm-none-eabi-nm solution.elf | awk '/ trigger_ret$/{print "0x"$1}') ;;
*) echo "usage: $0 [starter|solution|practice|gdb|syms|skip2bug]"; exit 2 ;;
esac
