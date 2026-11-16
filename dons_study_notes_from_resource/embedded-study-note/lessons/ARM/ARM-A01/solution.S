/* ENV: qemu */
/* ARM-A01 solution.S — 순수 asm 컨텍스트 스위처 (Cortex-M3 / -DHAS_FPU 면 Cortex-M4F).
 *  SVC_Handler    : svc #0 = "스케줄러 시작". main 문맥(r4–r11, EXC_RETURN [+s16–s31])을 MSP 에 쌓고
 *                   g_main_msp 에 기록 → g_cur 의 저장 문맥을 풀어 PSP 로 첫 태스크에 진입.
 *  PendSV_Handler : 나가는 태스크의 r4–r11 [+s16–s31] 을 PSP 에 쌓고 cur->sp 저장 → os_schedule(EXC_RETURN)
 *                   → 다음 TCB 의 r4–r11 [+s16–s31] 복원, PSP 교체, EXC_RETURN 으로 복귀.
 *                   os_schedule 가 NULL 이면 main 문맥을 MSP 에서 꺼내 svc 다음 명령으로 돌아간다.
 * 하드웨어가 이미 r0–r3, r12, lr, pc, xPSR (FP 문맥이면 + s0–s15, FPSCR) 를 PSP 에 쌓아 두었다.
 */
    .syntax unified
#ifdef HAS_FPU
    .cpu    cortex-m4
    .fpu    fpv4-sp-d16
#else
    .cpu    cortex-m3
#endif
    .thumb
    .text

    .equ    TCB_SP, 0           // struct tcb 오프셋 (a01.h 의 _Static_assert 와 같아야 한다)
    .equ    TCB_EXCRET, 8

/* ======================= TODO 1: SVC_Handler (첫 태스크 시작) ======================= */
    .global SVC_Handler
    .type   SVC_Handler, %function
    .thumb_func
SVC_Handler:
#ifdef HAS_FPU
    tst     lr, #0x10           // EXC_RETURN bit4 = 0 → main 이 FP 문맥을 갖고 있다
    it      eq
    vpusheq {s16-s31}           // 그러면 callee-saved FP 레지스터도 main 몫으로 보관
#endif
    push    {r3-r11, lr}        // main 의 r4–r11 + EXC_RETURN (r3 은 8 정렬용 짝)
    ldr     r0, =g_main_msp
    mov     r1, sp
    str     r1, [r0]            // PendSV 가 main 으로 돌아갈 때 여기서 꺼낸다
    ldr     r0, =g_cur
    ldr     r0, [r0]            // 첫 태스크 TCB
    ldr     r1, [r0, #TCB_SP]   // init_stack 이 만든 16 워드 프레임의 바닥
    ldmia   r1!, {r4-r11}       // 소프트웨어 부분 (r4–r11)
    msr     psp, r1             // 남은 8 워드 = 하드웨어 프레임 → 예외 복귀가 PSP 에서 꺼낸다
    movs    r2, #0
    msr     basepri, r2         // os_run 이 막아 둔 SysTick/PendSV 를 연다 (SVC 는 우선순위 0 이라 여기서 안 끊긴다)
    isb
#ifdef HAS_FPU
    ldr     lr, [r0, #TCB_EXCRET]
#else
    ldr     lr, =0xFFFFFFFD     // Thread 모드, PSP, 기본 프레임
#endif
    bx      lr
    .size   SVC_Handler, . - SVC_Handler
    .ltorg

/* ======================= TODO 2: PendSV_Handler (컨텍스트 스위치) ======================= */
    .global PendSV_Handler
    .type   PendSV_Handler, %function
    .thumb_func
PendSV_Handler:
    mrs     r0, psp             // 나가는 태스크의 PSP (하드웨어 프레임 바로 아래)
#ifdef HAS_FPU
    tst     lr, #0x10
    it      eq
    vstmdbeq r0!, {s16-s31}     // FP 문맥이 있으면 s16–s31 먼저 (이 FP 명령이 lazy 저장된 s0–s15 도 채운다)
#endif
    stmdb   r0!, {r4-r11}       // r4–r11
    ldr     r3, =g_cur
    ldr     r2, [r3]
    str     r0, [r2, #TCB_SP]   // cur->sp = 쌓은 뒤의 PSP
#ifdef HAS_FPU
    str     lr, [r2, #TCB_EXCRET]   // 이 태스크가 FP 프레임이었는지 기억
#endif
    mov     r0, lr              // os_schedule(EXC_RETURN)
    push    {r3, lr}            // lr(EXC_RETURN) 보관 + 8 정렬
    bl      os_schedule         // r0 = 다음 TCB (g_cur 갱신됨) 또는 NULL
    pop     {r3, lr}
    cbz     r0, 1f
#ifdef HAS_FPU
    ldr     lr, [r0, #TCB_EXCRET]   // 들어오는 태스크의 EXC_RETURN (0xFFFFFFFD 또는 0xFFFFFFED)
#endif
    ldr     r1, [r0, #TCB_SP]
    ldmia   r1!, {r4-r11}
#ifdef HAS_FPU
    tst     lr, #0x10
    it      eq
    vldmiaeq r1!, {s16-s31}
#endif
    msr     psp, r1
    isb
    bx      lr                  // 0xFFFFFFFD/0xFFFFFFED → 하드웨어가 새 PSP 에서 r0..xPSR 를 꺼낸다
1:  ldr     r0, =g_main_msp     // ---- main 으로 복귀 ----
    ldr     r0, [r0]
    mov     sp, r0              // (PendSV 는 Thread 에서만 들어오므로 이미 같은 값이다)
    pop     {r3-r11, lr}        // main 의 r4–r11 + main 의 EXC_RETURN (0xFFFFFFF9 / 0xFFFFFFE9)
#ifdef HAS_FPU
    tst     lr, #0x10
    it      eq
    vpopeq  {s16-s31}
#endif
    bx      lr                  // → os_start 의 svc 다음 명령, MSP, CONTROL.SPSEL = 0
    .size   PendSV_Handler, . - PendSV_Handler
    .ltorg
