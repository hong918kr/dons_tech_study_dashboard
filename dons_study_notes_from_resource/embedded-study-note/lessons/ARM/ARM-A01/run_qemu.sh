#!/bin/bash
# ENV: qemu
# ARM-A01 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, gdb-multiarch).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/ARM-A01_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-A01/run_qemu.sh [starter|solution|practice|fpu|bugs|gdb|objdump]'
# starter/solution: 같은 펌웨어를 두 번 — mps2-an385 (Cortex-M3) + mps2-an386 (Cortex-M4F, -DHAS_FPU).
# fpu: an386 판만.  소스 규칙: <v>.c (harness.c 를 #include) + <v>.S + shared_*.c + shared_*.S (+ arm_a01.ld)
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-A01_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.h "$SRC"/*.S "$SRC"/*.ld "$SRC"/*.gdb "$W"/ 2>/dev/null
cd "$W" || exit 1

COMMON="-mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
M3="-mcpu=cortex-m3"
M4F="-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -DHAS_FPU"
QEMU="qemu-system-arm -nographic -monitor none -serial none -semihosting-config enable=on,target=native"
# icount: 가상 시간 = 명령 수 (shift=0 → 1 명령 = 1 ns). SysTick 이 매번 같은 명령에 떨어져 출력이 결정적이다.
ICOUNT="-icount shift=0,align=off,sleep=off"

build() {   # build <variant> <out.elf> [cpu flags...]
    local v=$1 out=$2; shift 2
    arm-none-eabi-gcc $COMMON "$@" -T arm_a01.ld "$v.c" "$v.S" shared_*.c shared_*.S -o "$out" -lgcc
}
run() {     # run <machine> <elf>: 펌웨어가 SYS_EXIT 로 QEMU 를 끝낸다. 안전망 timeout 20 s.
    timeout 20 $QEMU $ICOUNT -M "$1" -kernel "$2" 2>&1 | cat
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
fpu)        # Part C 만 보고 싶을 때: an386 (Cortex-M4F) 판
    build solution solution_m4f.elf $M4F || exit 2
    run mps2-an386 solution_m4f.elf; rc=$?
    echo "qemu exit=$rc"
    exit $rc ;;
practice)
    build practice practice.elf $M3 || exit 2
    run mps2-an385 practice.elf; rc=$?
    echo "qemu exit=$rc"
    exit $rc ;;
bugs)       # Part B 결함 주입: 잘못된 핸들러/우선순위를 끼우고 "기대한 고장" 이 났는지 확인 (다 났으면 exit 0)
    rc=0
    for sel in 1 2 3 5 4; do
        if [ $sel -eq 4 ]; then cpu="$M4F"; mach=mps2-an386; else cpu="$M3"; mach=mps2-an385; fi
        arm-none-eabi-gcc $COMMON $cpu -DBUGS -DBUGSEL=$sel -T arm_a01.ld solution.c solution.S demo_bugs.S \
            shared_*.c shared_*.S -o bugs$sel.elf -lgcc || exit 2
        run $mach bugs$sel.elf; r=$?
        [ $rc -eq 0 ] && rc=$r
    done
    echo "qemu exit=$rc"
    exit $rc ;;
objdump)    # 스위처의 실제 기계어
    build solution solution_m3.elf $M3 || exit 2
    build solution solution_m4f.elf $M4F || exit 2
    arm-none-eabi-objdump -d solution_m3.elf | sed -n '/<SVC_Handler>:/,/^$/p;/<PendSV_Handler>:/,/^$/p'
    echo "---- M4F ----"
    arm-none-eabi-objdump -d solution_m4f.elf | sed -n '/<PendSV_Handler>:/,/^$/p'
    arm-none-eabi-size solution_m3.elf solution_m4f.elf ;;
gdb)        # -s -S 로 멈춘 QEMU 에 gdb-multiarch 를 붙여 PendSV 앞뒤를 본다 (an385)
    # icount 를 빼고 돈다: icount(sleep=off) 는 gdb 가 멈춘 동안 가상 시계를 다음 타이머로 계속 건너뛰어 틱이 몰린다 (실측)
    build solution solution_m3.elf $M3 || exit 2
    $QEMU -M mps2-an385 -kernel solution_m3.elf -s -S > qemu_gdb.log 2>&1 &
    QPID=$!
    sleep 1
    timeout 60 gdb-multiarch -q -batch -x demo_switch.gdb solution_m3.elf
    timeout 20 tail --pid=$QPID -f /dev/null; wait $QPID; echo "qemu exit=$? (디버거로 멈춘 동안에도 호스트 시간이 흘러 틱이 밀린다 → 이 레인의 PASS 수는 의미 없음)"
    echo "--- firmware output (semihosting, last 2 lines) ---"; tail -2 qemu_gdb.log
    exit 0 ;;
*) echo "usage: $0 [starter|solution|practice|fpu|bugs|gdb|objdump]"; exit 2 ;;
esac
