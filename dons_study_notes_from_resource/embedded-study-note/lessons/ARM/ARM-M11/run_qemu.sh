#!/bin/bash
# ENV: qemu
# ARM-M11 — Lima VM `elinux` 안에서 실행 (qemu-system-arm 10.2.1, arm-none-eabi-gcc 14.2, gdb-multiarch 17.1).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/ARM-M11_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-M11/run_qemu.sh [build|run|run1|gdb|m0]'
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-M11_work
mkdir -p "$W"
cp "$SRC"/demo_* "$W"/
cd "$W" || exit 1
CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {
    arm-none-eabi-gcc $CFLAGS -T demo_m3.ld demo_m3_main.c demo_m3_prims.S -o m3.elf -lgcc || exit 1
    arm-none-eabi-size m3.elf
}
case "${1:-run}" in
build) build ;;
run)   build; $QEMU -kernel m3.elf; echo "qemu exit=$?" ;;
run1)  build; $QEMU -accel tcg,one-insn-per-tb=on -kernel m3.elf; echo "qemu exit=$?" ;;   # IRQ 이 명령 사이 어디든 들어오게
gdb)   build
       $QEMU -kernel m3.elf -s -S > qemu_gdb.log 2>&1 &
       QPID=$!
       sleep 1
       gdb-multiarch -q -batch -x demo_gdb_isr.gdb m3.elf
       wait $QPID; echo "qemu exit=$?"
       echo "--- firmware output (semihosting) ---"; cat qemu_gdb.log ;;
m0)    echo "--- (1) demo_m3_prims.S assembled for cortex-m0 (ARMv6-M) ---"
       arm-none-eabi-gcc -mcpu=cortex-m0 -mthumb -c demo_m3_prims.S -o /dev/null
       M0FLAGS="-mcpu=cortex-m0 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns -T demo_m0.ld"
       echo "--- (2) C11 atomic_fetch_add on cortex-m0 without our __atomic_fetch_add_4 ---"
       arm-none-eabi-gcc $M0FLAGS -DNO_OWN_LIBCALL demo_m0_primask.c -o m0_nolib.elf -lgcc 2>&1 | grep -m2 "undefined reference"
       echo "--- (3) with PRIMASK __atomic_fetch_add_4, run on -M microbit (Cortex-M0) ---"
       arm-none-eabi-gcc $M0FLAGS demo_m0_primask.c -o m0.elf -lgcc || exit 1
       arm-none-eabi-objdump -d m0.elf | sed -n '/<__atomic_fetch_add_4>:/,/^$/p'
       qemu-system-arm -M microbit -nographic -monitor none -serial none \
           -semihosting-config enable=on,target=native -kernel m0.elf; echo "qemu exit=$?" ;;
esac
