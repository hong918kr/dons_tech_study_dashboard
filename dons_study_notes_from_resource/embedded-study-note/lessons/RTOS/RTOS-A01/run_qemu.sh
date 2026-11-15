#!/bin/bash
# ENV: qemu
# RTOS-A01 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, gdb-multiarch 17.1).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/RTOS-A01_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/RTOS/RTOS-A01/run_qemu.sh [solution|starter|practice|gdb|faults|size|noicount]'
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/RTOS-A01_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.ld "$SRC"/*.gdb "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
# -icount: 가상 시간 = 명령어 수 (1 명령 = 2^3 ns). tick 경계가 매번 같은 명령에서 떨어져 결과가 결정적이다.
# sleep=off: idle 의 WFI 동안 가상 시계를 다음 타이머까지 건너뛴다 (실시간으로 기다리지 않음).
ICOUNT="-icount shift=3,align=off,sleep=off"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {   # build <src.c> <out.elf> [extra flags...]
    local src=$1 out=$2; shift 2
    arm-none-eabi-gcc $CFLAGS "$@" -T rtos_a01.ld -Wl,-Map="${out%.elf}.map" "$src" -o "$out" -lgcc
}
run() {     # run <elf> [qemu extra...]: 펌웨어가 SYS_EXIT 로 끝낸다. 안전망 timeout 15 s.
    local elf=$1; shift
    timeout 15 $QEMU "$@" -kernel "$elf" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
solution|starter)
    build "$1.c" "$1.elf" || exit 1
    arm-none-eabi-size "$1.elf"
    run "$1.elf" $ICOUNT ;;
noicount)   # 같은 solution 을 -icount 없이 (가상 시간 = 호스트 실시간)
    build solution.c solution.elf || exit 1
    run solution.elf ;;
size)
    build solution.c solution.elf || exit 1
    arm-none-eabi-size -A solution.elf | grep -v '^\.debug\|^\.ARM.attr\|^$'
    arm-none-eabi-nm -S --size-sort solution.elf | grep -iE ' (PendSV_Handler|SVC_Handler|SysTick_Handler|kernel_switch_context|kernel_tick|task_stack_init|start_first_task|mutex_lock|mutex_unlock|sem_take|crit_enter|crit_exit)$'
    echo "--- PendSV_Handler / SVC_Handler / start_first_task disassembly ---"
    arm-none-eabi-objdump -d solution.elf | sed -n '/<PendSV_Handler>:/,/^$/p;/<SVC_Handler>:/,/^$/p;/<start_first_task>:/,/^$/p' ;;
gdb)
    build solution.c solution.elf || exit 1
    $QEMU $ICOUNT -kernel solution.elf -s -S > qemu_gdb.log 2>&1 &
    QPID=$!
    sleep 1
    gdb-multiarch -q -batch -x demo_switch.gdb solution.elf
    wait $QPID; echo "qemu exit=$?"
    echo "--- firmware output (semihosting) ---"; cat qemu_gdb.log ;;
faults)     # solution.c 를 sed 로 한 줄씩 망가뜨린 사본 f_*.c 를 돌린다
    echo "##### F1: 초기 xPSR = 0 (T 비트 없음) #####"
    sed 's/\*--sp = 0x01000000u;/*--sp = 0u;/' solution.c > f_xpsr.c
    build f_xpsr.c f_xpsr.elf || exit 1
    run f_xpsr.elf $ICOUNT
    echo "##### F2: 초기 PC 칸에 bit0 = 1 을 그대로 (& ~1u 빠뜨림) #####"
    sed 's/(uint32_t)(uintptr_t)fn & ~1u;/(uint32_t)(uintptr_t)fn;/' solution.c > f_pc1.c
    build f_pc1.c f_pc1.elf || exit 1
    run f_pc1.elf $ICOUNT -d guest_errors
    echo "##### F3: PendSV 우선순위 = 0x00 (최고, BASEPRI 로도 안 막힘) #####"
    sed 's/(PRIO_LOWEST << 16) | (PRIO_LOWEST << 24)/(PRIO_LOWEST << 24)/' solution.c > f_pendsv.c
    build f_pendsv.c f_pendsv.elf || exit 1
    run f_pendsv.elf $ICOUNT
    echo "##### F4: TCB 첫 멤버가 sp 가 아님 (name 과 순서 바뀜) #####"
    awk '/uint32_t \*sp;  /{hold=$0; next} /const char \*name;/{print; print hold; next} {print}' solution.c > f_tcb.c
    build f_tcb.c f_tcb.elf || exit 1
    run f_tcb.elf $ICOUNT
    true ;;
practice)
    echo "--- practice.c (host C, built natively in the VM) ---"
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice_host || exit 1
    ./practice_host
    rc=$?
    echo "practice exit=$rc"
    exit $rc ;;
*) echo "usage: $0 [solution|starter|practice|gdb|faults|size|noicount]"; exit 2 ;;
esac
