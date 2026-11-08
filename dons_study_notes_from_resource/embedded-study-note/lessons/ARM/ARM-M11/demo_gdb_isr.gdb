# ENV: qemu
# ARM-M11 — LDREX 와 STREX 사이에 gdb 로 멈춰서 예외/외부 쓰기를 끼워 넣는다.
# 사용: qemu-system-arm ... -kernel m3.elf -s -S &   gdb-multiarch -q -batch -x demo_gdb_isr.gdb m3.elf
# 조건 ($xpsr & 0x1ff) == 0  = IPSR 0 = Thread 모드(main) 일 때만 멈춘다. ISR 안의 m_atomic_inc 는 그냥 지나간다.
set pagination off
set confirm off
target remote :1234

break scenario_begin
continue
echo \n=== scenario_begin reached (stress done, SysTick off) ===\n
break inc_after_ldrex if ($xpsr & 0x1ff) == 0
break inc_after_strex if ($xpsr & 0x1ff) == 0
break PendSV_Handler
break IRQ30_Handler

echo \n##### R1: pend PendSV (SCB->ICSR.PENDSVSET) between LDREX and STREX #####\n
continue
printf "[main] after LDREX: r2(loaded)=%u  g_counter=%u  xpsr=0x%08x\n", $r2, g_counter, $xpsr
set *(unsigned int *)0xE000ED04 = 0x10000000
printf "[gdb ] ICSR=0x%08x (bit28 PENDSVSET written)\n", *(unsigned int *)0xE000ED04
continue
printf "[isr ] stopped in handler: IPSR=%u\n", $xpsr & 0x1ff
continue
printf "[main] after STREX: r12(status)=%u  g_counter=%u  r3(attempt)=%u\n", $r12, g_counter, $r3
continue
printf "[main] retry LDREX: r2(loaded)=%u  r3(attempt)=%u\n", $r2, $r3
continue
printf "[main] after STREX: r12(status)=%u  g_counter=%u\n", $r12, g_counter

echo \n##### R2: pend IRQ30 (NVIC->ISPR0 bit30); ISR does NOT touch g_counter #####\n
continue
printf "[main] after LDREX: r2=%u g_counter=%u\n", $r2, g_counter
set *(unsigned int *)0xE000E200 = 0x40000000
continue
printf "[isr ] stopped in handler: IPSR=%u (16+30=46), g_isr_mode=%u\n", $xpsr & 0x1ff, g_isr_mode
continue
printf "[main] after STREX: r12(status)=%u  g_counter=%u (unchanged by ISR)\n", $r12, g_counter
continue
printf "[main] retry LDREX: r2=%u r3(attempt)=%u\n", $r2, $r3
continue
printf "[main] after STREX: r12(status)=%u  g_counter=%u\n", $r12, g_counter

echo \n##### R3: no exception; gdb (an 'external master') writes g_counter += 100 #####\n
continue
printf "[main] after LDREX: r2=%u g_counter=%u\n", $r2, g_counter
set var g_counter = g_counter + 100
printf "[gdb ] g_counter now %u\n", g_counter
continue
printf "[main] after STREX: r12(status)=%u  g_counter=%u\n", $r12, g_counter
continue
printf "[main] retry LDREX: r2=%u r3(attempt)=%u\n", $r2, $r3
continue
printf "[main] after STREX: r12(status)=%u  g_counter=%u\n", $r12, g_counter

echo \n##### R4: no exception; gdb writes the SAME value back #####\n
continue
printf "[main] after LDREX: r2=%u g_counter=%u\n", $r2, g_counter
set var g_counter = g_counter
continue
printf "[main] after STREX: r12(status)=%u  g_counter=%u\n", $r12, g_counter

echo \n##### R5: control — nothing injected #####\n
continue
printf "[main] after LDREX: r2=%u\n", $r2
continue
printf "[main] after STREX: r12(status)=%u  g_counter=%u\n", $r12, g_counter

delete
continue
