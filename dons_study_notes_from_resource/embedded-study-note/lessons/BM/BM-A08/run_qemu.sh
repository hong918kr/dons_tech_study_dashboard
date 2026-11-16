#!/bin/bash
# ENV: qemu
# BM-A08 — Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1).
# Mac 홈은 VM 에서 읽기 전용이라 소스를 /tmp/BM-A08_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/BM-A08/run_qemu.sh [solution|starter|practice|probe|lockup|syms|nullub]'
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-A08_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.ld "$W"/ 2>/dev/null
cd "$W" || exit 1

CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

build() {   # build <src.c> <out.elf> [extra flags...]
    local src=$1 out=$2; shift 2
    arm-none-eabi-gcc $CFLAGS "$@" -T a08.ld -Wl,-Map="${out%.elf}.map" "$src" -o "$out" -lgcc
}
run() {     # run <elf> [qemu flags...]: 한 번 부팅, SYS_EXIT 로 끝난다 (timeout 20 s 안전망)
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
syms)       # 주입 라벨, 배치 주소, 명령 길이 (16/32 비트) 확인
    build solution.c solution.elf || exit 1
    arm-none-eabi-nm -n solution.elf | grep -E ' (g_vectors|_stext|Reset_Handler|Fault_Entry|run_on_psp|task_abort_landing|pc_ldr|pc_str|pc_big|recurse|g_ta|g_sub|g_ram_code|_estack)$'
    for s in pc_ldr pc_str pc_big; do
        a=$(arm-none-eabi-nm solution.elf | awk -v s=$s '$3==s{print $1}')
        arm-none-eabi-objdump -d --start-address=0x$a --stop-address=$(printf '0x%x' $((0x$a + 4))) solution.elf | grep -E '^ +[0-9a-f]+:' | head -1
    done
    echo "--- recurse() 프롤로그 (프레임 크기) ---"
    arm-none-eabi-objdump -d solution.elf | awk '/<recurse>:/{f=1} f&&NR>0{print} /<task_recurse>:/{exit}' | head -8 ;;
probe)      # QEMU 사실 확인: 오정렬 base, 128 B 영역의 SRD, AP=100, RNR 범위, RBAR.VALID (guest_errors 로그 포함)
    build demo_probe.c demo_probe.elf || exit 1
    run demo_probe.elf -d guest_errors ;;
lockup)     # HFNMIENA=1 이면 HardFault 안의 NULL 읽기 → fault 안의 fault
    build solution.c lockup.elf -DHFNMI_LOCKUP || exit 1
    run lockup.elf | grep -E '^T13|fatal|^R12|^XPSR|qemu exit' ;;
nullub)     # C 로 주소 0 을 읽으면 GCC 가 무엇을 만드나 (-fdelete-null-pointer-checks 기본 on)
    printf '#include <stdint.h>\nuint32_t peek0(void) { return *(volatile uint32_t *)0; }\nuint32_t peek0_asm(void) { uint32_t v; __asm__ volatile("ldr %%0, [%%1]" : "=r"(v) : "r"(0u)); return v; }\n' > nullub.c
    for f in "" "-fno-delete-null-pointer-checks"; do
        echo "--- arm-none-eabi-gcc -O2 $f"
        arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -O2 $f -c nullub.c -o nullub.o && arm-none-eabi-objdump -d nullub.o | grep -E '^ +[0-9a-f]+:|>:'
    done ;;
practice)   # practice.c 는 호스트 C — VM 의 gcc 로도 빌드·실행 (Mac 에선 cc 로)
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice_host && ./practice_host
    rc=$?; echo "practice exit=$rc"; exit $rc ;;
*) echo "usage: $0 [solution|starter|practice|probe|lockup|syms|nullub]"; exit 2 ;;
esac
