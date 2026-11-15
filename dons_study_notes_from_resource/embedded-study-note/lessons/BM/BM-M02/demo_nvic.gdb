# BM-M02 — design_applied() 에서 멈춰 NVIC/SCB 를 디버거로 직접 읽는다 (run_qemu.sh gdb)
set pagination off
set confirm off
target remote :1234
break design_applied
continue
printf "AIRCR  = 0x%08x  (VECTKEYSTAT 0xFA05, PRIGROUP [10:8])\n", *(unsigned int *)0xE000ED0C
printf "SHPR3  = 0x%08x  (SysTick [31:24], PendSV [23:16])\n", *(unsigned int *)0xE000ED20
printf "ISER0  = 0x%08x\n", *(unsigned int *)0xE000E100
x/4xb 0xE000E40E
break MotorPwm_IRQHandler
continue
printf "IABR0  = 0x%08x  (active IRQs at PWM entry in S1)\n", *(unsigned int *)0xE000E300
printf "ICSR   = 0x%08x  (VECTACTIVE [8:0])\n", *(unsigned int *)0xE000ED04
p/x $xpsr
bt
kill
quit
