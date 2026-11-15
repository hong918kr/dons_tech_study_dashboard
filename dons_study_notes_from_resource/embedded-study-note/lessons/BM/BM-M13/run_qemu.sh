#!/bin/bash
# ENV: qemu
# BM-M13 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/BM-M13_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/BM-M13/run_qemu.sh [solution|starter|practice|probe|noreboot|bssbug|syms]'
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-M13_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.ld "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {   # build <src.c> <out.elf> [extra flags...]
    local src=$1 out=$2; shift 2
    arm-none-eabi-gcc $CFLAGS "$@" -T m13.ld -Wl,-Map="${out%.elf}.map" "$src" -o "$out" -lgcc
}
run() {     # run <elf> [qemu flags...]: 여러 번 리셋해도 한 QEMU 프로세스. 마지막 부팅의 SYS_EXIT 로 끝난다.
    local elf=$1; shift
    timeout 20 $QEMU "$@" -kernel "$elf" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
solution|starter)
    build "$1.c" "$1.elf" || exit 1
    arm-none-eabi-size "$1.elf"
    run "$1.elf" ;;
syms)       # 주입 라벨 주소, .noinit 배치, ELF 세그먼트 (NOLOAD → FileSiz 0)
    build solution.c solution.elf || exit 1
    arm-none-eabi-nm -n solution.elf | grep -E ' (pc_[a-z0-9]+|g_guard|g_crash|g_plan|_snoinit|_enoinit|_estack|Fault_Entry|HardFault_Handler|UsageFault_Handler|Irq7_Handler)$'
    arm-none-eabi-readelf -lW solution.elf | sed -n '/Program Headers/,/^$/p;/Section to Segment/,$p'
    echo "--- 주입 명령 (라벨 주소의 명령 한 개) ---"
    for s in pc_div0 pc_unal pc_bus pc_udf pc_mpu pc_esc pc_isr; do
        a=$(arm-none-eabi-nm solution.elf | awk -v s=$s '$3==s{print $1}')
        printf '%-8s ' "$s"
        arm-none-eabi-objdump -d --start-address=0x$a --stop-address=$(printf '0x%x' $((0x$a + 4))) solution.elf | grep -E '^ +[0-9a-f]+:' | head -1
    done ;;
probe)      # QEMU 사실 확인: 주소별 BusFault 여부, 트랩 on/off, 리셋 뒤 무엇이 남나
    build demo_probe.c demo_probe.elf || exit 1
    run demo_probe.elf ;;
noreboot)   # -no-reboot: 게스트 리셋 요청 = QEMU 종료
    build solution.c solution.elf || exit 1
    run solution.elf -no-reboot ;;
bssbug)     # 결함 주입: 기록을 .noinit 대신 .bss 에 → reset_init 이 매 부팅 0 으로 → 무한 리셋 (timeout 3 s 로 끊음)
    build solution.c bssbug.elf -DPERSIST_IN_BSS || exit 1
    timeout 3 $QEMU -kernel bssbug.elf > bssbug.log 2>&1
    rc=$?
    head -6 bssbug.log
    echo "... (3 초 동안) '== boot #' 줄 수: $(grep -c '== boot #' bssbug.log), 'cold boot' 줄 수: $(grep -c 'cold boot' bssbug.log)"
    echo "    '== boot #2' 가 나온 횟수: $(grep -c '== boot #2 ' bssbug.log)"
    echo "qemu exit=$rc (124 = timeout 이 죽임)" ;;
practice)   # practice.c 는 호스트 C — VM 의 gcc 로도 빌드·실행 (Mac 에선 cc 로)
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice_host && ./practice_host
    rc=$?; echo "practice exit=$rc"; exit $rc ;;
*) echo "usage: $0 [solution|starter|practice|probe|noreboot|bssbug|syms]"; exit 2 ;;
esac
