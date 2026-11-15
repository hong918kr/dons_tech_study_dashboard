# ENV: qemu
# ARM-B14 — QEMU(-s -S) 에 붙어서 MSP/PSP/CONTROL/xPSR 이 바뀌는 순간을 본다.
# 사용: run_qemu.sh gdb   (= qemu ... -kernel solution.elf -s -S &  gdb-multiarch -q -batch -x demo_regs.gdb solution.elf)
# gdb-multiarch 17.1 + QEMU 10.2 의 M-profile 레지스터 이름: msp psp primask basepri faultmask control (+ xpsr)
set pagination off
set confirm off
target remote :1234

define regs
  printf "  pc=0x%08x sp=0x%08x msp=0x%08x psp=0x%08x control=%u xpsr=0x%08x ipsr=%u\n", $pc, $sp, $msp, $psp, $control, $xpsr, $xpsr & 0x1ff
end
define step1
  x/i $pc
  stepi
  regs
end

echo \n=== (1) reset: 첫 명령 직전 — 하드웨어가 벡터 [0] 을 MSP 에 넣었다 ===\n
info registers
printf "  vector[0]=0x%08x vector[1]=0x%08x\n", *(unsigned int *)0, *(unsigned int *)4

echo \n=== (2) run_on_psp (1회째: task_probe) 를 한 명령씩 ===\n
break run_on_psp
continue
regs
step1
step1
step1
step1
step1
step1
echo   (여기가 psp_switched: sp 가 PSP 값으로 바뀌었다)\n
p/x $msp
p/x $psp
p $control

echo \n=== (3) SVC_Handler 첫 명령 (task_probe 안의 svc #0) ===\n
break SVC_Handler
continue
regs
printf "  lr (EXC_RETURN) = 0x%08x\n", $lr
printf "  thread sp before svc = 0x%08x  (psp 와의 차 = %d bytes)\n", g_sp_before_svc, g_sp_before_svc - $psp

echo \n=== (4) PendSV_Handler 첫 명령 (main 이 ICSR.PENDSVSET) ===\n
break PendSV_Handler
continue
regs
printf "  lr (EXC_RETURN) = 0x%08x\n", $lr

echo \n=== (5) 비특권 태스크가 svc_call 에 들어온 순간 (CONTROL=3) ===\n
delete
break svc_call if $control == 3
continue
regs
delete
break SVC_Handler
continue
echo   (SVC_Handler 진입 — Handler 는 항상 특권 + MSP)\n
regs
printf "  lr (EXC_RETURN) = 0x%08x\n", $lr

delete
echo \n=== (6) 나머지 실행 → 펌웨어가 SYS_EXIT ===\n
continue
