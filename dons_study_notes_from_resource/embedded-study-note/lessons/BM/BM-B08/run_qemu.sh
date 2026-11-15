#!/bin/bash
# ENV: qemu
# BM-B08 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, gdb-multiarch 17.1).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/BM-B08_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/BM-B08/run_qemu.sh [solution|starter|practice|gdb|faults|map|nopoison|addr0]'
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-B08_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.ld "$SRC"/*.gdb "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {   # build <src.c> <out.elf> [extra flags...]
    local src=$1 out=$2; shift 2
    arm-none-eabi-gcc $CFLAGS "$@" -T bm_b08.ld -Wl,-Map="${out%.elf}.map" "$src" handlers.c -o "$out" -lgcc
}
run() {     # run <elf>: QEMU 는 SYS_EXIT 로 스스로 끝난다. 혹시 몰라 timeout 10 s.
    timeout 10 $QEMU -kernel "$1" 2>&1 | cat     # 파이프: QEMU abort(Lockup) 때 셸의 "Aborted" 잡음 방지
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
solution|starter)
    build "$1.c" "$1.elf" || exit 1
    arm-none-eabi-size "$1.elf"
    run "$1.elf" ;;
nopoison)   # starter 를 ram_poison 없이: QEMU RAM 이 0 으로 시작해서 생기는 "가짜 PASS"
    build starter.c starter_np.elf -DNO_POISON || exit 1
    run starter_np.elf ;;
gdb)
    build solution.c solution.elf || exit 1
    $QEMU -kernel solution.elf -s -S > qemu_gdb.log 2>&1 &
    QPID=$!
    sleep 1
    gdb-multiarch -q -batch -x demo_boot.gdb solution.elf
    wait $QPID; echo "qemu exit=$?"
    echo "--- firmware output (semihosting) ---"; cat qemu_gdb.log ;;
map)
    build solution.c solution.elf || exit 1
    echo "--- arm-none-eabi-size -A ---";  arm-none-eabi-size -A solution.elf | grep -v '^\.debug\|^\.ARM.attr\|^$'
    echo "--- readelf -l (LMA = PhysAddr, VMA = VirtAddr) ---"
    arm-none-eabi-readelf -lW solution.elf | sed -n '/Program Headers/,/Section to Segment/p'
    echo "--- vector table (objdump -s -j .isr_vector, first 64 bytes) ---"
    arm-none-eabi-objdump -s -j .isr_vector solution.elf | sed -n '5,8p'
    echo "--- symbols ---"
    arm-none-eabi-nm -n solution.elf | grep -E ' (_sidata|_sdata|_edata|_sbss|_ebss|_estack|__init_array_start|__init_array_end|g_vectors|Reset_Handler|Default_Handler|NMI_Handler|SysTick_Handler|HardFault_Handler|reset_init|main|g_magic|g_table|g_zero)$' ;;
faults)
    echo "##### F1: Reset 벡터 Thumb 비트 없음 (-DFAULT_THUMB) #####"
    build solution.c f_thumb.elf -DFAULT_THUMB || exit 1
    arm-none-eabi-objdump -s -j .isr_vector f_thumb.elf | sed -n '5p'
    run f_thumb.elf
    echo "##### F2: _estack = ORIGIN(RAM) (LENGTH 더하기를 잊음) #####"
    sed 's/^_estack = ORIGIN(RAM) + LENGTH(RAM);/_estack = ORIGIN(RAM);/' bm_b08.ld > f_estack1.ld
    arm-none-eabi-gcc $CFLAGS -T f_estack1.ld solution.c handlers.c -o f_estack1.elf -lgcc 2>&1 | grep -m1 "BM-B08" | sed "s|^.*/bin/ld[^:]*: ||"
    sed -e 's/^_estack = ORIGIN(RAM) + LENGTH(RAM);/_estack = 0x30000000;/' bm_b08.ld > f_estack2.ld
    echo "##### F3: _estack = 0x30000000 (아무것도 없는 주소) #####"
    arm-none-eabi-gcc $CFLAGS -T f_estack2.ld solution.c handlers.c -o f_estack2.elf -lgcc || exit 1
    run f_estack2.elf
    echo "##### F4: RAM LENGTH = 8M (보드는 4 MB) → _estack 0x20800000 #####"
    sed -e 's/LENGTH = 64K/LENGTH = 8M/' bm_b08.ld > f_estack3.ld
    arm-none-eabi-gcc $CFLAGS -T f_estack3.ld solution.c handlers.c -o f_estack3.elf -lgcc || exit 1
    run f_estack3.elf
    echo "##### F5: -ffreestanding · -fno-tree-loop-distribute-patterns 없이 -nostdlib 링크 #####"
    arm-none-eabi-gcc ${CFLAGS/-ffreestanding -nostdlib -fno-tree-loop-distribute-patterns/-nostdlib} -T bm_b08.ld solution.c handlers.c -o f_memcpy.elf -lgcc 2>&1 | grep "undefined reference" | sed "s|.*/||"
    true ;;
addr0)      # 주소 0 의 객체와 0 비교 → GCC 가 상수 0 으로 접는다 (7. 실수 4)
    arm-none-eabi-gcc $CFLAGS -c demo_addr0.c -o demo_addr0.o || exit 1
    arm-none-eabi-objdump -d demo_addr0.o | sed -n '/<table_at_zero_naive>:/,$p' ;;
practice)
    build solution.c solution.elf || exit 1
    echo "--- live dump of the fresh solution.elf (compare with the table inside practice.c) ---"
    arm-none-eabi-objdump -s -j .isr_vector solution.elf | sed -n '5,8p'
    arm-none-eabi-nm -n solution.elf | grep -E ' (_sidata|_sdata|_edata|_sbss|_ebss|_estack|Default_Handler)$'
    echo "--- practice.c (host C, built natively in the VM) ---"
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice_host && ./practice_host
    echo "practice exit=$?" ;;
*) echo "usage: $0 [solution|starter|practice|gdb|faults|map|nopoison|addr0]"; exit 2 ;;
esac
