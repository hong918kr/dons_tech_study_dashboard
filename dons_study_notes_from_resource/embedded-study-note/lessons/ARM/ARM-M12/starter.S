/* ENV: qemu */
/* ARM-M12 solution.S — fault 4 종(HardFault/MemManage/BusFault/UsageFault)의 공통 어셈블리 진입부.
 * 하는 일: 하드웨어가 쌓은 8 워드 프레임 {r0,r1,r2,r3,r12,lr,pc,xpsr} 이 MSP 와 PSP 중 어디 있는지
 *          EXC_RETURN (진입 시 LR) 의 bit2 로 고르고, C 분석기 hardfault_c(frame, exc_return) 로 넘긴다.
 * 왜 어셈블리인가: C 함수는 프롤로그에서 push 를 해 sp 를 바꾼다 → C 안에서 "진입 순간의 sp" 를 알 수 없다.
 */
    .syntax unified
    .cpu    cortex-m3
    .thumb
    .text

// void Fault_Entry(void) — 벡터 [3]..[6] 이 모두 여기를 가리킨다 (shared_startup.c)
    .global Fault_Entry
    .type   Fault_Entry, %function
    .thumb_func
Fault_Entry:
    mrs     r0, msp             // TODO 1: 지금은 항상 MSP. EXC_RETURN bit2 를 보고 PSP 도 고를 것
    mov     r1, lr              // r1 = EXC_RETURN (0xFFFFFFF1 / F9 / FD)
    b       hardfault_c         // bl 이 아니라 b: LR(EXC_RETURN) 그대로 → C 의 return 이 곧 예외 복귀
    .size   Fault_Entry, . - Fault_Entry
