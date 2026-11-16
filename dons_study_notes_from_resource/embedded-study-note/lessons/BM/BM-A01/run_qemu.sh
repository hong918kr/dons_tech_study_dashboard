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
order)      # 결함 주입: Reset_Handler 순서를 바꾼 두 빌드. 둘 다 "잡혀야" (exit ≠ 0) 통과.
    ok=0
    echo "##### O1: fpu_enable() 을 생성자 뒤로 (-DINJECT_FPU_LATE) #####"
    build solution.c o_fpu_late.elf -DINJECT_FPU_LATE || exit 1
    run o_fpu_late.elf | grep -E "boot|FAIL|passed|qemu exit" > o1.log
    cat o1.log
    if grep -q "qemu exit=1" o1.log && grep -q "CFSR=0x00080000 HFSR=0x40000000" o1.log; then echo "O1 caught: NOCP at boot (HardFault FORCED)"; ok=$((ok+1)); fi
    echo "##### O2: vtor_relocate() 를 .bss 0 채우기 앞으로 (-DINJECT_VTOR_EARLY) #####"
    build solution.c o_vtor_early.elf -DINJECT_VTOR_EARLY || exit 1
    run o_vtor_early.elf > o2.log 2>&1
    grep -E "boot|FAIL|passed|Lockup|qemu exit|unexpected" o2.log | head -12
    if ! grep -q "qemu exit=0" o2.log; then echo "O2 caught: table wiped by .bss zeroing"; ok=$((ok+1)); fi
    echo "order injections caught: $ok/2"
    [ $ok -eq 2 ] ;;
disasm)     # 어떤 함수가 VFP 명령을 쓰나? (Reset_Handler / fault 경로에는 없어야 한다)
    build solution.c solution.elf || exit 1
    arm-none-eabi-objdump -d solution.elf > sol.dis
    awk '/^[0-9a-f]+ <.*>:$/{fn=$2} /\tv[a-z]+(\.[a-z0-9]+)*\t/{c[fn]++} END{for(f in c) printf "%-28s %d VFP insns\n", f, c[f]}' sol.dis | sort
    for f in Reset_Handler fault_c Fault_Entry; do
        n=$(awk -v F="<$f>:" '$2==F{on=1;next} /^[0-9a-f]+ <.*>:$/{on=0} on && /\tv[a-z]+/' sol.dis | wc -l)
        echo "$f: $n VFP instructions"
    done
    echo "--- ctor_gain_table ---"
    awk '/<ctor_gain_table>:$/{on=1} on&&/^$/{exit} on' sol.dis | head -16
    echo "--- symbols ---"
    arm-none-eabi-nm -n solution.elf | grep -E ' (g_vectors|g_ram_vectors|g_vt_scratch|ctor_gain_table|g_ctor_scale|_sbss|_ebss|__init_array_start|pc_b1|pc_b2)$' ;;
practice)   # practice.c 는 호스트 C — VM 의 cc 로 빌드·실행 (Mac 에선 cc 로)
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice_host && ./practice_host
    rc=$?; echo "practice exit=$rc"; exit $rc ;;
*) echo "usage: $0 [solution|starter|practice|probe|order|disasm]"; exit 2 ;;
esac
