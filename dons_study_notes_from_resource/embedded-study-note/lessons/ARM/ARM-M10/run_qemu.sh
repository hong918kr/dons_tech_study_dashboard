#!/bin/bash
# ENV: qemu
# ARM-M10 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/ARM-M10_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-M10/run_qemu.sh [starter|solution|practice|modes|clobber|objdump|m0]'
# 소스 규칙: <v>.c (없으면 harness.c) + <v>.S + shared_*.S + shared_*.c  (+ arm_m10.ld)
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-M10_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.S "$SRC"/*.ld "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"
# 채점 실행은 icount 모드: 가상 시간 = 명령 수 → SysTick 이 매번 같은 명령에 떨어진다 (결정적)
ACCEL="-icount shift=0,align=off,sleep=off"

build() {   # build <variant> → <variant>.elf
    local v=$1; shift
    local main=harness.c
    [ -f "$v.c" ] && main="$v.c"
    arm-none-eabi-gcc $CFLAGS "$@" -T arm_m10.ld "$main" "$v.S" shared_*.S shared_*.c -o "$v.elf" -lgcc
}
run() {     # run <elf> [qemu accel opts]: 펌웨어가 SYS_EXIT 로 QEMU 를 끝낸다. 안전망 timeout 20 s.
    local elf=$1; shift
    timeout 20 $QEMU "$@" -kernel "$elf" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
starter|solution|practice)
    build "$1" || exit 2
    run "$1.elf" $ACCEL ;;
modes)      # 같은 solution.elf 를 세 가지 QEMU 실행 모드로: 스트레스 lost 가 어떻게 달라지나
    build solution || exit 2
    for m in "default TCG|" "one-insn-per-tb|-accel tcg,one-insn-per-tb=on" "icount|$ACCEL"; do
        name=${m%%|*}; opts=${m#*|}
        for r in 1 2 3; do
            echo "--- $name run $r ---"
            run solution.elf $opts | grep -E "INFO stress|passed|qemu exit"
        done
    done ;;
clobber)    # demo_clobber.c: "memory" clobber 없는 inline asm 크리티컬 섹션의 -O2 코드
    arm-none-eabi-gcc $CFLAGS -c demo_clobber.c -o demo_clobber.o || exit 2
    arm-none-eabi-objdump -d --no-show-raw-insn demo_clobber.o | sed -n '/<add_n_noclob>:/,$p' ;;
objdump)    # solution.S 프리미티브의 실제 인코딩
    build solution || exit 2
    arm-none-eabi-objdump -d solution.elf | sed -n '/<crit_enter>:/,/<rmw_inc>:/p' | grep -v '^$' | sed '$d' ;;
m0)         # ARMv6-M 에는 BASEPRI/FAULTMASK 가 없다: 어셈블러는 통과, QEMU microbit(M0) 은 무시
    sed -e 's/LENGTH = 64K/LENGTH = 16K/' arm_m10.ld > m0.ld        # nRF51 RAM 16 KB
    arm-none-eabi-gcc -mcpu=cortex-m0 -mthumb -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -nostdlib \
        -T m0.ld demo_m0_basepri.c shared_startup.c -o m0.elf -lgcc || exit 2
    timeout 10 qemu-system-arm -M microbit -nographic -monitor none -serial none \
        -semihosting-config enable=on,target=native -d unimp,guest_errors -kernel m0.elf 2>&1 | cat
    echo "qemu exit=${PIPESTATUS[0]}" ;;
*) echo "usage: $0 [starter|solution|practice|modes|clobber|objdump|m0]"; exit 2 ;;
esac
