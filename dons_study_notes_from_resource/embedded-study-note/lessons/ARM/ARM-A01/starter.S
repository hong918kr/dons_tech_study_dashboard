/* ENV: qemu */
/* ARM-A01 starter.S — 순수 asm 컨텍스트 스위처 (Cortex-M3 / -DHAS_FPU 면 Cortex-M4F).
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
    // TODO (FPU): EXC_RETURN bit4 == 0 이면 s16–s31 도 main 몫으로 보관
    //
    //
#endif
    // TODO: main 의 r4–r11 + EXC_RETURN 을 MSP 에 push (8 정렬 유지)
    // TODO: g_main_msp = sp
    //
    //
    // TODO: r0 = g_cur (첫 태스크 TCB)
    //
    // TODO: r1 = r0->sp, r4–r11 복원
    //
    // TODO: PSP = r1
    // TODO: BASEPRI = 0 (os_run 이 0x40 으로 올려 두었다)
    //
    // TODO: isb
#ifdef HAS_FPU
    // TODO (FPU): lr = r0->exc_return
#else
    // TODO: lr = Thread + PSP 로 돌아가는 EXC_RETURN
#endif
    bx      lr                  // 스텁: main 으로 그냥 돌아간다 (태스크는 안 돈다)
    .size   SVC_Handler, . - SVC_Handler
    .ltorg

/* ======================= TODO 2: PendSV_Handler (컨텍스트 스위치) ======================= */
    .global PendSV_Handler
    .type   PendSV_Handler, %function
    .thumb_func
PendSV_Handler:
    // TODO: r0 = 나가는 태스크의 PSP
#ifdef HAS_FPU
    // TODO (FPU): EXC_RETURN bit4 == 0 이면 s16–s31 을 r0 아래로 (stmdb 보다 먼저)
    //
    //
#endif
    // TODO: r4–r11 을 r0 아래로
    // TODO: g_cur->sp = r0
    //
    //
#ifdef HAS_FPU
    // TODO (FPU): g_cur->exc_return = lr
#endif
    // TODO: os_schedule(EXC_RETURN) 호출 — lr 은 보존해야 한다
    //
    //
    //
    // TODO: r0 == NULL 이면 아래 "main 으로" 로
#ifdef HAS_FPU
    // TODO (FPU): lr = 다음 TCB 의 exc_return
#endif
    // TODO: r1 = next->sp, r4–r11 복원
    //
#ifdef HAS_FPU
    // TODO (FPU): bit4 == 0 이면 s16–s31 복원
    //
    //
#endif
    // TODO: PSP = r1, isb
    //
    bx      lr                  // 스텁: 아무것도 안 바꾸고 돌아간다
    // TODO: main 으로 — g_main_msp 에서 sp 복원, SVC 가 쌓은 것을 pop, bx lr
    //
    //
    //
#ifdef HAS_FPU
    // TODO (FPU): bit4 == 0 이면 s16–s31 pop
    //
    //
#endif
    //
    .size   PendSV_Handler, . - PendSV_Handler
    .ltorg
