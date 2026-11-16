# ENV: qemu
# ARM-A01 — QEMU(-s -S) 에 붙어서 첫 PendSV 스위치(A → B)를 한 명령씩 본다 (solution, mps2-an385).
# 사용: run_qemu.sh gdb   (= qemu ... -kernel solution_m3.elf -s -S &  gdb-multiarch -q -batch -x demo_switch.gdb solution_m3.elf)
set pagination off
set confirm off
target remote :1234

define regs8
  printf "  r4=%08x r5=%08x r6=%08x r7=%08x r8=%08x r9=%08x r10=%08x r11=%08x\n", $r4, $r5, $r6, $r7, $r8, $r9, $r10, $r11
end

echo \n=== (1) 첫 PendSV 진입 (A1, A 가 SysTick 에 끊김) ===\n
break PendSV_Handler
continue
printf "  lr(EXC_RETURN)=0x%08x msp=0x%08x psp=0x%08x control=%u g_cur=%c\n", $lr, $msp, $psp, $control, g_cur->name
regs8
echo   A 의 하드웨어 프레임 (r0 r1 r2 r3 r12 lr pc xpsr):\n
x/8wx $psp
printf "  stacked pc -> "
info symbol *(unsigned int *)($psp + 24)
delete

echo \n=== (2) stmdb + str 뒤: A 의 저장 문맥 16 워드 (r4..r11 | 하드웨어 8) ===\n
stepi 5
x/i $pc
printf "  r0(new A sp)=0x%08x  A.sp in TCB=0x%08x\n", $r0, (unsigned int)T[0].sp
x/16wx $r0

echo \n=== (3) bl os_schedule 를 넘기고 B 복원 직전까지 ===\n
stepi 2
nexti
printf "  os_schedule -> r0=0x%08x (&T[1]=0x%08x) g_cur=%c\n", $r0, (unsigned int)&T[1], g_cur->name
stepi 6
x/i $pc
regs8
printf "  psp=0x%08x  lr=0x%08x\n", $psp, $lr
echo   B 의 초기 하드웨어 프레임 (init_stack 이 만든 것):\n
x/8wx $psp

echo \n=== (4) bx lr 한 명령 → B 의 첫 명령 ===\n
stepi
printf "  pc=0x%08x sp=0x%08x psp=0x%08x control=%u r0=0x%08x (&RC[1]=0x%08x) lr=0x%08x\n", $pc, $sp, $psp, $control, $r0, (unsigned int)&RC[1], $lr
info symbol $pc

echo \n=== (5) 첫 main 복귀: os_schedule 가 to_main 으로 (A1 끝) → PendSV 의 main 복귀 경로 ===\n
break shared_os.c:54
continue
delete
finish
x/i $pc
printf "  r0=0x%08x (NULL → main 으로)  g_main_msp=0x%08x msp=0x%08x\n", $r0, g_main_msp, $msp
stepi 6
x/i $pc
printf "  sp=0x%08x lr=0x%08x\n", $sp, $lr
regs8
echo   MSP 위에 남은 svc 하드웨어 프레임 (r0 r1 r2 r3 r12 lr pc xpsr):\n
x/8wx $sp
printf "  frame pc -> "
info symbol *(unsigned int *)($sp + 24)
stepi
printf "  bx lr 뒤: pc=0x%08x sp=0x%08x control=%u ipsr=%u r4=0x%08x r11=0x%08x\n", $pc, $sp, $control, $xpsr & 0x1ff, $r4, $r11
info symbol $pc

echo \n=== (6) 나머지 실행 → 펌웨어가 SYS_EXIT ===\n
continue
