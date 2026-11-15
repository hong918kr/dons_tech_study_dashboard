#!/bin/bash
# ENV: qemu
# BM-A01 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/BM-A01_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/BM-A01/run_qemu.sh [solution|starter|practice|probe|order|disasm]'
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-A01_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.ld "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an386 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {   # build <src.c> <out.elf> [extra flags...]
    local src=$1 out=$2; shift 2
    arm-none-eabi-gcc $CFLAGS "$@" -T a01.ld -Wl,-Map="${out%.elf}.map" "$src" -o "$out" -lgcc
}
run() {     # run <elf> [qemu flags...]
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
    run "$1.elf" ;;
probe)
    build demo_probe.c demo_probe.elf || exit 1
    run demo_probe.elf ;;
*) echo "usage: $0 [solution|starter|practice|probe|order|disasm]"; exit 2 ;;
esac
