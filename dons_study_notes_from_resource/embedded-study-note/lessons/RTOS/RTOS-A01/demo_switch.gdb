# ENV: qemu
# RTOS-A01 — 스위치아웃된 태스크의 스택 프레임을 gdb 로 본다.
# 사용: run_qemu.sh gdb  (= qemu ... -icount shift=3,align=off,sleep=off -kernel solution.elf -s -S &  gdb-multiarch -q -batch -x demo_switch.gdb solution.elf)
# 멈추는 곳: Phase E (상속 OFF) 에서 M 이 t0+2 에 깨어나 L 을 선점한 직후 gdb_probe().
#   그 순간 L = SysTick→PendSV 로 burn() 도중 쫓겨남(READY), H = mutex_lock 에서 막힘(BLOCKED), M = 실행 중.
set pagination off
set confirm off
target remote :1234
set output-radix 16

hbreak gdb_probe
continue

echo \n=== (1) 지금 CPU: 실행 중 태스크 M, Thread 모드, PSP 사용 ===\n
printf "g_tick=%u  g_cur=%s  (t0=%u)\n", g_tick, g_cur->name, g_t0
printf "SysTick CVR=%u (리로드 24999 에서 0 으로 내려간다. tick 직후라면 24990 근처여야)\n", *(unsigned *)0xE000E018
info registers sp msp psp control basepri primask xpsr
printf "M stack = [0x%08x, 0x%08x)\n", t_m.stack_lo, t_m.stack_hi

echo \n=== (2) 스위치아웃된 L: t_l.sp 에서 16 워드 = r4..r11 | r0 r1 r2 r3 r12 lr pc xPSR ===\n
printf "t_l.sp=0x%08x  state=%d  prio=%u  stack_hi=0x%08x  (frame top = sp + 64 → %d words used)\n", t_l.sp, t_l.state, t_l.prio, t_l.stack_hi, t_l.stack_hi - t_l.sp
x/16wx t_l.sp
printf "stacked pc  = 0x%08x  ", ((unsigned *)t_l.sp)[14]
info symbol ((unsigned *)t_l.sp)[14]
printf "stacked lr  = 0x%08x  ", ((unsigned *)t_l.sp)[13]
info symbol ((unsigned *)t_l.sp)[13]
printf "stacked xPSR= 0x%08x  (T=%d, IPSR=%d)\n", ((unsigned *)t_l.sp)[15], (((unsigned *)t_l.sp)[15] >> 24) & 1, ((unsigned *)t_l.sp)[15] & 0x1ff

echo \n=== (3) 막혀 있는 H: mutex_lock → crit_exit 에서 PendSV 로 나갔다 ===\n
printf "t_h.sp=0x%08x  state=%d  wait_obj=0x%08x (&g_res=0x%08x)\n", t_h.sp, t_h.state, t_h.wait_obj, &g_res
x/16wx t_h.sp
printf "stacked pc  = 0x%08x  ", ((unsigned *)t_h.sp)[14]
info symbol ((unsigned *)t_h.sp)[14]
printf "stacked lr  = 0x%08x  ", ((unsigned *)t_h.sp)[13]
info symbol ((unsigned *)t_h.sp)[13]

delete
continue
