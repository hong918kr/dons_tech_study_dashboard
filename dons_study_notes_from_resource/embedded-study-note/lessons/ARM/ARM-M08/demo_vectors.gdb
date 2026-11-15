# ENV: qemu
# ARM-M08 — QEMU(-s -S) 에 붙어서 (1) 리셋 순간 하드웨어가 벡터 [0]/[1] 을 쓴 결과, (2) Thumb 비트 없는 벡터로
#           들어간 직후의 UsageFault 프레임을 본다.
# 사용: run_qemu.sh gdb   (= qemu ... -kernel solution.elf -s -S &  gdb-multiarch -q -batch -x demo_vectors.gdb solution.elf)
set pagination off
set confirm off
target remote :1234

echo \n=== (1) reset: 첫 명령 직전 ===\n
printf "  vector[0]=0x%08x  vector[1]=0x%08x\n", *(unsigned int *)0, *(unsigned int *)4
printf "  pc=0x%08x  msp=0x%08x  sp=0x%08x  xpsr=0x%08x  (T = bit24 = %u)\n", $pc, $msp, $sp, $xpsr, ($xpsr >> 24) & 1
info symbol $pc

echo \n=== (2) B1: SVC 엔트리 bit0 = 0 → UsageFault_Handler(= HardFault_Handler) 첫 명령 ===\n
break HardFault_Handler
continue
printf "  ipsr=%u  lr(EXC_RETURN)=0x%08x  CFSR=0x%08x  VTOR=0x%08x\n", $xpsr & 0x1ff, $lr, *(unsigned int *)0xE000ED28, *(unsigned int *)0xE000ED08
printf "  RAM vector[11]=0x%08x\n", *(unsigned int *)(*(unsigned int *)0xE000ED08 + 44)
echo   stacked frame (r0 r1 r2 r3 r12 lr pc xpsr):\n
x/8wx $sp
info symbol *(unsigned int *)($sp + 24)

echo \n=== (3) B2, B3 는 HardFault — 건너뛰고 B4: Thread 에서 blx 짝수 ===\n
continue
printf "  B2 ipsr=%u HFSR=0x%08x\n", $xpsr & 0x1ff, *(unsigned int *)0xE000ED2C
continue
printf "  B3 ipsr=%u HFSR=0x%08x\n", $xpsr & 0x1ff, *(unsigned int *)0xE000ED2C
continue
printf "  B4 ipsr=%u  lr(EXC_RETURN)=0x%08x\n", $xpsr & 0x1ff, $lr
x/8wx $sp
info symbol *(unsigned int *)($sp + 24)
info symbol *(unsigned int *)($sp + 20)

delete
echo \n=== (4) 나머지 실행 → 펌웨어가 SYS_EXIT ===\n
continue
