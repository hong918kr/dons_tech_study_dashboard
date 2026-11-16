#!/usr/bin/env bash
# TOOL-M07 demo_vm_gdb.sh — Lima VM `elinux` 에서 gdb 로 같은 사냥 → demo_vm_gdb.txt, demo_cm.txt
#   (1) aarch64 Linux 사용자 모드: qemu-aarch64 -g + gdb-multiarch (VM 의 네이티브 gdb 는 SVE 문제로 run 불가)
#   (2) Cortex-M3: arm-none-eabi-gcc + qemu-system-arm -M mps2-an385 -gdb + gdb-multiarch
set -u
cd "$(dirname "$0")" || exit 2
D=$(pwd)
limactl shell elinux -- bash -lc "
set -u
V=/tmp/TOOL-M07_work/vm; rm -rf \$V; mkdir -p \$V; cd \$V
cp '$D'/buggy.c '$D'/ringbuf_gdb.py '$D'/demo_vm_hw.gdb '$D'/demo_vm_hunt.gdb '$D'/cm_start.c '$D'/cm.ld '$D'/demo_cm.gdb .
gdb-multiarch --version | head -1; qemu-aarch64 --version | head -1
gcc -std=c11 -g -O0 -static buggy.c -o buggy_g
echo '### (a) 하드웨어 워치포인트 요청'
qemu-aarch64 -g 23471 ./buggy_g > q1.txt 2>&1 &
sleep 1
gdb-multiarch -q -batch -x demo_vm_hw.gdb ./buggy_g 2>&1
wait
echo '### (b) 조건부 BP → 소프트웨어 워치포인트 → commands 로깅'
qemu-aarch64 -g 23472 ./buggy_g > q2.txt 2>&1 &
sleep 1
gdb-multiarch -q -batch -x demo_vm_hunt.gdb ./buggy_g 2>&1
wait
echo '### 프로그램 출력 (qemu stdout)'; cat q2.txt
" > "$D/demo_vm_gdb.txt" 2>&1
echo "== demo_vm_gdb.txt (rc=$?)"
limactl shell elinux -- bash -lc "
set -u
V=/tmp/TOOL-M07_work/vm; cd \$V
arm-none-eabi-gcc --version | head -1; qemu-system-arm --version | head -1
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -g -O0 -nostdlib -Dprintf=cm_printf -T cm.ld cm_start.c buggy.c -o buggy_cm.elf
arm-none-eabi-size buggy_cm.elf
arm-none-eabi-nm buggy_cm.elf | grep -E ' (dev|g_tick)\$'
qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none -kernel buggy_cm.elf -gdb tcp::23474 -S > qcm.txt 2>&1 &
sleep 1
start=\$(date +%s.%N)
gdb-multiarch -q -batch -x demo_cm.gdb buggy_cm.elf 2>&1
end=\$(date +%s.%N)
wait
echo \"gdb session wall time: \$(echo \"\$end - \$start\" | bc) s\"
" > "$D/demo_cm.txt" 2>&1
echo "== demo_cm.txt (rc=$?)"
