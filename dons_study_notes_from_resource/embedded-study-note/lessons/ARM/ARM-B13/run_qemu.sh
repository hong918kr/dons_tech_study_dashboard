#!/bin/bash
# ENV: qemu
# ARM-B13 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, gdb-multiarch).
# Mac 홈은 VM 에서 읽기 전용 → 소스를 /tmp/ARM-B13_work 로 복사해서 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-B13/run_qemu.sh [starter|solution|practice|gdb|dis|cc|itrules|bugs]'
# 변형 v 의 소스 = (v.c 가 있으면 v.c, 없으면 harness.c) + v.S + shared_*.c + shared_*.S, 링커 스크립트 arm_b13.ld
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-B13_work
rm -rf "$W"; mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.S "$SRC"/*.ld "$SRC"/*.gdb "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {   # build <variant> [extra flags...]  → <variant>.elf
    local v=$1; shift
    local main=harness.c
    [ -f "$v.c" ] && main="$v.c"
    arm-none-eabi-gcc $CFLAGS "$@" -T arm_b13.ld "$main" "$v.S" shared_*.c shared_*.S -o "$v.elf" -lgcc
}
run() {     # run <elf>: 펌웨어가 SYS_EXIT 로 스스로 끝낸다. 안전장치 timeout 20 s.
    timeout 20 $QEMU -kernel "$1" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
starter|solution|practice)
    build "$1" || exit 1
    run "$1.elf" ;;
gdb)        # IT 블록을 한 명령씩: xPSR 의 IT 비트와 플래그를 본다 (demo_it.gdb)
    build solution || exit 1
    $QEMU -kernel solution.elf -s -S > qemu_gdb.log 2>&1 &
    QPID=$!
    sleep 1
    timeout 60 gdb-multiarch -q -batch -x demo_it.gdb solution.elf
    kill $QPID 2>/dev/null; wait $QPID 2>/dev/null
    echo "--- firmware output (semihosting, 마지막 3줄) ---"; tail -n 3 qemu_gdb.log ;;
dis)        # 16비트/32비트 인코딩 확인 (objdump 는 .n/.w 로 표시)
    arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -c solution.S -o solution.o || exit 1
    arm-none-eabi-objdump -d solution.o | tail -n +7
    echo "--- arm-none-eabi-size ---"; arm-none-eabi-size solution.o
    echo "--- demo_encodings.S (IT 안/밖의 같은 명령) ---"
    arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -c demo_encodings.S -o demo_encodings.o || exit 1
    arm-none-eabi-objdump -d demo_encodings.o | tail -n +7 ;;
cc)         # 같은 함수를 C 로 쓰면 GCC -O2 가 내는 코드
    arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -O2 -c demo_cc.c -o demo_cc.o || exit 1
    arm-none-eabi-objdump -d demo_cc.o | tail -n +7
    for m in "-march=armv8-a" "-march=armv8-a -mno-restrict-it"; do
        echo "--- c_add_if_gt with $m -mthumb -O2 ---"
        arm-none-eabi-gcc $m -mthumb -O2 -c demo_cc.c -o demo_cc8.o || exit 1
        arm-none-eabi-objdump -d demo_cc8.o | sed -n '/<c_add_if_gt>:/,$p' | tail -n +2
    done ;;
itrules)    # IT 규칙 위반을 어셈블러가 어떻게 거절하는지 (demo_it_rules.S, 케이스별로 따로 어셈블)
    for c in 1 2 3 4 5 6 7; do
        echo "### CASE $c"
        arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -c -DCASE=$c demo_it_rules.S -o rule$c.o 2>&1 | grep -v "^Assembler messages\|In function" | sed 's|^.*demo_it_rules.S|demo_it_rules.S|'
        [ -f rule$c.o ] && arm-none-eabi-objdump -d rule$c.o | sed -n '/<case>:/,$p'
    done
    echo "### CASE 1 again with -Wa,-mimplicit-it=always (어셈블러가 IT 를 몰래 넣는다)"
    arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -Wa,-mimplicit-it=always -c -DCASE=1 demo_it_rules.S -o rule1i.o || exit 1
    arm-none-eabi-objdump -d rule1i.o | sed -n '/<case>:/,$p'
    echo "### CASE 7 을 ARMv8-A AArch32 (T32) 로 + -mwarn-restrict-it — 폐기(deprecated) 경고"
    arm-none-eabi-gcc -march=armv8-a -mthumb -Wa,-mwarn-restrict-it -c -DCASE=7 demo_it_rules.S -o rule7.o 2>&1 | grep -v "^Assembler messages" | sed 's|^.*demo_it_rules.S|demo_it_rules.S|'
    true ;;
bugs)       # 일부러 틀린 함수들 (demo_bugs.c + demo_bugs.S): BUG=1..7 을 따로 빌드·실행
    for b in 1 2 3 4 5 7 6; do
        echo "### BUG $b"
        arm-none-eabi-gcc $CFLAGS -DBUG=$b -T arm_b13.ld demo_bugs.c demo_bugs.S solution.S shared_*.c shared_*.S -o bug$b.elf -lgcc 2>&1 | sed "s|^/usr/lib/gcc/.*/bin/ld: ||" || true
        [ -f bug$b.elf ] || { echo "(link failed — 실행 안 함)"; continue; }
        [ $b = 5 ] && arm-none-eabi-nm bug5.elf | grep -E " (max_s32|max_nothumb)$" && arm-none-eabi-readelf -s bug5.elf | grep -E " (max_s32|max_nothumb)$"
        [ $b = 6 ] && arm-none-eabi-objdump -d bug6.elf | grep -E "bl.*<max_nothumb>"
        timeout 5 $QEMU -kernel bug$b.elf 2>&1 | cat
        echo "qemu exit=${PIPESTATUS[0]}"
    done
    true ;;
*) echo "usage: $0 [starter|solution|practice|gdb|dis|cc|itrules|bugs]"; exit 2 ;;
esac
