#!/bin/bash
# ENV: qemu
# ARM-M09 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, gdb-multiarch).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/ARM-M09_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-M09/run_qemu.sh [starter|solution|practice|gdb|objdump|bugs]'
# starter/solution: 같은 펌웨어를 두 번 돈다 — mps2-an385 (Cortex-M3) + mps2-an386 (Cortex-M4F, -DHAS_FPU).
# 소스 규칙: <v>.c (harness.c 를 #include) + <v>.S + shared_*.c (+ arm_m09.ld)
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-M09_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.S "$SRC"/*.ld "$SRC"/*.gdb "$W"/ 2>/dev/null
cd "$W" || exit 1

COMMON="-mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
M3="-mcpu=cortex-m3"
M4F="-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DHAS_FPU"
QEMU="qemu-system-arm -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {   # build <variant> <out.elf> [cpu flags...]
    local v=$1 out=$2; shift 2
    arm-none-eabi-gcc $COMMON "$@" -T arm_m09.ld "$v.c" "$v.S" shared_*.c -o "$out" -lgcc
}
run() {     # run <machine> <elf>: 펌웨어가 SYS_EXIT 로 QEMU 를 끝낸다. 혹시 몰라 timeout 20 s.
    timeout 20 $QEMU -M "$1" -kernel "$2" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "[$1] qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
starter|solution)
    build "$1" "$1_m3.elf" $M3 || exit 2
    build "$1" "$1_m4f.elf" $M4F || exit 2
    run mps2-an385 "$1_m3.elf"; rc1=$?
    run mps2-an386 "$1_m4f.elf"; rc2=$?
    rc=$rc1; [ $rc -eq 0 ] && rc=$rc2
    echo "qemu exit=$rc"
    exit $rc ;;
practice)
    build practice practice.elf $M3 || exit 2
    run mps2-an385 practice.elf; rc=$?
    echo "qemu exit=$rc"
    exit $rc ;;
objdump)    # 진입 스텁의 실제 인코딩
    build solution solution_m3.elf $M3 || exit 2
    arm-none-eabi-objdump -d solution_m3.elf | sed -n '/<SVC_Handler>:/,/<sys_add>:/p' | grep -v '^$' | sed '$d'
    arm-none-eabi-objdump -d solution_m3.elf | sed -n '/<svc_sentinel>:/,/<svc_under_primask>:/p' | grep -v '^$' | sed '$d' ;;
bugs)       # 7. 자주 하는 실수 재현 (timeout 5 s)
    for b in 1 2; do
        echo "--- BUG=$b ---"
        arm-none-eabi-gcc $COMMON $M3 -DBUG=$b -T arm_m09.ld solution.c demo_bugs.S shared_*.c -o bug$b.elf -lgcc || exit 2
        timeout 5 $QEMU -M mps2-an385 -kernel bug$b.elf 2>&1 | grep -v '^PASS' | head -${2:-30}
        echo "qemu exit=${PIPESTATUS[0]}"
    done ;;
gdb)        # -s -S 로 멈춘 QEMU 에 gdb-multiarch 를 붙여 핸들러 첫 명령에서 프레임을 덤프한다
    build solution solution_m3.elf $M3 || exit 2
    $QEMU -M mps2-an385 -kernel solution_m3.elf -s -S > qemu_gdb.log 2>&1 &
    QPID=$!
    sleep 1
    timeout 60 gdb-multiarch -q -batch -x demo_frame.gdb solution_m3.elf
    timeout 20 tail --pid=$QPID -f /dev/null; wait $QPID; echo "qemu exit=$?"
    echo "--- firmware output (semihosting, last 3 lines) ---"; tail -3 qemu_gdb.log ;;
*) echo "usage: $0 [starter|solution|practice|gdb|objdump|bugs]"; exit 2 ;;
esac
