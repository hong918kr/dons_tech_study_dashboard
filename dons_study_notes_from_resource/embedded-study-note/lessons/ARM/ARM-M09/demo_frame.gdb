# ENV: qemu
# ARM-M09 — QEMU(-s -S) 에 붙어서 예외 진입 순간의 스택 프레임을 본다 (solution, mps2-an385).
# 사용: run_qemu.sh gdb   (= qemu ... -kernel solution_m3.elf -s -S &  gdb-multiarch -q -batch -x demo_frame.gdb solution_m3.elf)
set pagination off
set confirm off
target remote :1234

define frame8
  printf "  sp=0x%08x lr(EXC_RETURN)=0x%08x ipsr=%u msp=0x%08x psp=0x%08x\n", $sp, $lr, $xpsr & 0x1ff, $msp, $psp
  x/8wx $sp
  printf "  stacked pc=0x%08x -> ", *(unsigned int *)($sp + 24)
  info symbol *(unsigned int *)($sp + 24)
end

echo \n=== (1) A1: svc #1 직전 (Thread, MSP) ===\n
break svc_sentinel_svc
continue
x/i $pc
printf "  r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x r12=0x%08x lr=0x%08x sp=0x%08x\n", $r0, $r1, $r2, $r3, $r12, $lr, $sp

echo \n=== (2) stepi 한 번 → SVC_Handler 첫 명령. 하드웨어가 8 워드를 쌓았다 ===\n
stepi
x/i $pc
frame8
printf "  [stacked pc - 2] = 0x%04x  (svc #imm8 = 0xDFnn)\n", *(unsigned short *)(*(unsigned int *)($sp + 24) - 2)

echo \n=== (3) 스텁 5 명령: tst / ite / mrseq / mrsne(건너뜀) / mov ===\n
stepi
stepi
stepi
stepi
stepi
x/i $pc
printf "  r0(frame)=0x%08x r1(EXC_RETURN)=0x%08x\n", $r0, $r1
delete

echo \n=== (4) A3: SysTick 이 spin 루프를 끊은 순간 ===\n
break SysTick_Handler
continue
frame8
delete

echo \n=== (5) B5: SVC 핸들러 안에서 중첩된 SysTick (lr == 0xfffffff1) ===\n
break SysTick_Handler if $lr == 0xfffffff1
continue
frame8
delete

echo \n=== (6) B6: SysTick 핸들러 안 svc → HardFault ===\n
break HardFault_Handler
continue
frame8
printf "  HFSR=0x%08x\n", *(unsigned int *)0xE000ED2C
delete

echo \n=== (7) C1: 비특권 PSP 태스크의 svc (lr == 0xfffffffd, CONTROL=3 이었던 쪽) ===\n
break SVC_Handler if $lr == 0xfffffffd && $psp < 0x20010000 && *(unsigned short *)(*(unsigned int *)($psp + 24) - 2) == 0xdf01
continue
printf "  sp(MSP)=0x%08x lr=0x%08x psp=0x%08x control=%u\n", $sp, $lr, $psp, $control
x/8wx $psp
delete

echo \n=== (8) 나머지 실행 → 펌웨어가 SYS_EXIT ===\n
continue
