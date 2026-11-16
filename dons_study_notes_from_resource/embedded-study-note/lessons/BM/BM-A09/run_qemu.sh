#!/bin/bash
# ENV: qemu
# BM-A09 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1, gcc 15, python3).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/BM-A09_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/BM-A09/run_qemu.sh MODE'
# MODE
#   solution | starter : 펌웨어 10 부팅 (UART0 → <mode>_uart.txt) + decode_dump.py --check + 호스트 시험(-DHOST_TEST)
#   practice           : practice.c (호스트 C)
#   syms               : nm / 세그먼트 / 라벨 주소 (solution_nm.txt 를 만든다)
#   naive | naive32 | naiveovf : 결함 주입 — 진입 스텁이 fault 스택으로 안 바꾼다
#                        (naive = BAD_SP, naive32 = 32 B 가드 STACK_OVF, naiveovf = 64 B 가드 STACK_OVF)
#   lockup             : 결함 주입 — 세분 fault 를 끄고 NESTED 단계: HardFault 안의 fault 는?
#   garbage            : demo_garbage.c (검사 폭 8/16/32 비트별 오수용 수, 호스트)
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-A09_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.ld "$SRC"/*.py "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -semihosting-config enable=on,target=native"

build() {   # build <src.c> <out.elf> [extra flags...]
    local src=$1 out=$2; shift 2
    arm-none-eabi-gcc $CFLAGS "$@" -T a09.ld -Wl,-Map="${out%.elf}.map" "$src" -o "$out" -lgcc
}
run() {     # run <elf> <uart file>: 여러 번 리셋해도 QEMU 프로세스 하나. 마지막 부팅의 SYS_EXIT 로 끝.
    rm -f "$2"
    timeout 20 $QEMU -serial file:"$2" -kernel "$1" 2>&1 | cat
    local rc=${PIPESTATUS[0]}
    cat "$2"
    echo "qemu exit=$rc"
    return $rc
}

case "${1:-solution}" in
solution|starter)
    build "$1.c" "$1.elf" || exit 1
    arm-none-eabi-size "$1.elf"
    arm-none-eabi-nm -n "$1.elf" > "$1_nm.txt"
    run "$1.elf" "$1_uart.txt"; q=$?
    echo "--- decode_dump.py --check ---"
    python3 -I decode_dump.py "$1_uart.txt" "$1_nm.txt" --check; d=$?
    echo "decode exit=$d"
    echo "--- host test (cc -DHOST_TEST) ---"
    cc -std=c11 -Wall -Wextra -Werror -O2 -DHOST_TEST "$1.c" -o "$1_host" && ./"$1_host"; h=$?
    echo "host exit=$h"
    [ $q -eq 0 ] && [ $d -eq 0 ] && [ $h -eq 0 ] && { echo "ALL PASS"; exit 0; }
    echo "FAIL (qemu=$q decode=$d host=$h)"; exit 1 ;;
practice)
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice_host && ./practice_host
    rc=$?; echo "practice exit=$rc"; exit $rc ;;
syms)
    build solution.c solution.elf || exit 1
    arm-none-eabi-nm -n solution.elf > solution_nm.txt
    grep -E ' (pc_[a-z0-9]+|inject_[a-z_]+|task_div0|run_on_psp|recurse|Irq7_Handler|Fault_Entry|HardFault_Handler|fault_capture|fault_nested|main|g_dump|g_nested|g_plan|g_bootctr|g_fault_active|g_fault_r4_r11|_sfault_stack|_fault_stack_top|_sstack_guard|_estack_guard|_estack|_snoinit|_enoinit)$' solution_nm.txt
    arm-none-eabi-readelf -lW solution.elf | sed -n '/Program Headers/,/^$/p'
    echo "--- Fault_Entry (네 fault 벡터의 alias — objdump 는 그중 한 이름으로 표시) ---"
    a=$(awk '$3=="Fault_Entry"{print $1}' solution_nm.txt); e=$(awk '$3=="inject_udf_regs"{print $1}' solution_nm.txt)
    arm-none-eabi-objdump -d --start-address=0x$a --stop-address=0x$e solution.elf | grep -E '^ +[0-9a-f]+:' ;;
naive|naive32|naiveovf)   # 모두 스택 전환 없는 진입. naive: 64 B 가드 BAD_SP, naive32: 32 B 가드 STACK_OVF, naiveovf: 64 B 가드 STACK_OVF
    g=64; f=5; [ "$1" = naive32 ] && { g=32; f=4; }; [ "$1" = naiveovf ] && f=4
    build solution.c "$1.elf" -DNAIVE_ENTRY -DFIRST_STEP=${f}u -DLAST_STEP=$((f+1))u -Wl,--defsym=_Guard_Size=$g || exit 1
    arm-none-eabi-nm -n "$1.elf" | grep -E " (_sfault_stack|_fault_stack_top|_sstack_guard|_estack_guard|_estack)$"
    run "$1.elf" "$1_uart.txt" ;;
lockup)
    build solution.c lockup.elf -DFIRST_STEP=6u -DLAST_STEP=7u -DNO_FAULT_ENABLE || exit 1
    run lockup.elf lockup_uart.txt ;;
garbage)
    cc -std=c11 -Wall -Wextra -Werror -O2 demo_garbage.c -o demo_garbage && ./demo_garbage ;;
*) echo "usage: $0 [solution|starter|practice|syms|naive|naive32|naiveovf|lockup|garbage]"; exit 2 ;;
esac
