#!/bin/bash
# ENV: board (빌드만) + qemu (CYCCNT 확인)
# BM-A10 — Lima VM `elinux` 안에서 실행. Mac 홈은 VM 에서 읽기 전용 → /tmp/BM-A10_work 로 복사해 빌드.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/BM-A10/board/build_vm.sh [fw|qemu|all]'
#   fw   : STM32F407 측정 펌웨어를 -mcpu=cortex-m4 로 빌드 (보드에서 실행하지 않음), size + ISR 디스어셈블
#   qemu : 같은 dwt_cyccnt.h 로 mps2-an386 에서 CYCCNT 를 읽어 본다 (QEMU 는 사이클을 세지 않는다는 확인)
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-A10_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.h "$SRC"/*.ld "$W"/ 2>/dev/null
cd "$W" || exit 1
CF="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
QEMU="qemu-system-arm -M mps2-an386 -nographic -monitor none -serial none -semihosting-config enable=on,target=native"

fw() {
    arm-none-eabi-gcc $CF -T stm32f407_latency.ld -Wl,-Map=latency_fw.map latency_fw.c -o latency_fw.elf -lgcc || return 1
    echo "== build OK (not run: needs an STM32F407 board) =="
    arm-none-eabi-size latency_fw.elf
    echo "== ISR entry code (naked: first instruction = CYCCNT sample) =="
    for f in EXTI0_IRQHandler EXTI1_IRQHandler EXTI2_IRQHandler; do
        arm-none-eabi-objdump -d --no-show-raw-insn latency_fw.elf | awk -v f="<$f>:" '$2==f{p=1} p&&/^$/{exit} p' | head -5
    done
    echo "== trigger sequences (ldr t0,[r1] / str trigger / dsb / isb [/ ldr resume]) =="
    arm-none-eabi-objdump -d --no-show-raw-insn latency_fw.elf | awk '
        { l3=l2; l2=l1; l1=prev; prev=$0 }
        /isb/ && l1 ~ /dsb/ && n < 3 { print l3; print l2; print l1; print $0; getline; print; print "--"; n++ }'
    echo "== vector table entries 22..24 (EXTI0..2) and 44 (TIM2), 66 (TIM5) =="
    python3 - <<'PY'
import struct, subprocess
subprocess.run(["arm-none-eabi-objcopy","-O","binary","-j",".isr_vector","latency_fw.elf","vec.bin"],check=True)
b = open("vec.bin","rb").read()
v = struct.unpack("<%dI" % (len(b)//4), b)
print("entries=%d  [0]=0x%08x [1]=0x%08x" % (len(v), v[0], v[1]))
for i in (22, 23, 24, 44, 66):
    print("  vec[%d] (IRQ %d) = 0x%08x" % (i, i-16, v[i]))
PY
}
qemu() {
    arm-none-eabi-gcc $CF -T mps2_an386.ld qemu_cyccnt_probe.c -o qemu_cyccnt_probe.elf -lgcc || return 1
    echo "== run 1: plain =="
    timeout 10 $QEMU -kernel qemu_cyccnt_probe.elf; echo "qemu exit=$?"
    echo "== run 2: -d unimp,guest_errors (who handles 0xE000EDFC / 0xE0001000?) =="
    timeout 10 $QEMU -d unimp,guest_errors -kernel qemu_cyccnt_probe.elf 2>&1 | grep -E 'Bad|unassigned' | sort | uniq -c
    echo "== runs 3-5: SysTick delta for the same 200k loop (host-time based) =="
    for i in 1 2 3; do timeout 10 $QEMU -kernel qemu_cyccnt_probe.elf 2>&1 | grep SysTick; done
    echo "== runs 6-7: -icount shift=0 (instruction-count time) =="
    for i in 1 2; do timeout 10 $QEMU -icount shift=0 -kernel qemu_cyccnt_probe.elf 2>&1 | grep -E 'SysTick|CYCCNT d'; done
    return 0
}
case "${1:-all}" in
fw) fw ;;
qemu) qemu ;;
all) fw && qemu ;;
*) echo "usage: $0 [fw|qemu|all]"; exit 2 ;;
esac
