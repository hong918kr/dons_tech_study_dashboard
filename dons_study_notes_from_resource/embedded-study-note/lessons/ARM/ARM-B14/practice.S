/* ENV: qemu */
/* ARM-B14 practice.S — 추가 연습 답. 본문 헬퍼(solution.S)를 그대로 포함하고 아래만 더한다. */
#include "solution.S"

// 연습 1: uint32_t nzcv_after_cmp(uint32_t a, uint32_t b) — CMP 직후 APSR[31:28] = N Z C V
    .global nzcv_after_cmp
    .type   nzcv_after_cmp, %function
    .thumb_func
nzcv_after_cmp:
    cmp     r0, r1              // a - b 로 플래그만 갱신
    mrs     r0, apsr            // APSR 보기 (IPSR/EPSR 비트는 0)
    lsrs    r0, r0, #28         // r0 = NZCV 니블 (lsrs 의 플래그 변경은 상관없다: 이미 읽었다)
    bx      lr
    .size   nzcv_after_cmp, . - nzcv_after_cmp

// 연습 1: uint32_t q_after_usat8(int32_t x) — Q(포화) 플래그를 지우고 USAT #8 후 Q 를 돌려준다
    .global q_after_usat8
    .type   q_after_usat8, %function
    .thumb_func
q_after_usat8:
    movs    r1, #0
    msr     apsr_nzcvq, r1      // N Z C V Q 를 모두 0 으로 (Q 는 스스로 안 지워지는 sticky 비트)
    usat    r0, #8, r0          // 0..255 로 포화. 잘렸으면 Q = 1
    mrs     r0, apsr
    ubfx    r0, r0, #27, #1     // bit27 = Q
    bx      lr
    .size   q_after_usat8, . - q_after_usat8

// 연습 4: SysTick_Handler — shared_startup.c 의 weak 기본 핸들러를 덮어쓴다 (같은 트램펄린)
    .global SysTick_Handler
    .type   SysTick_Handler, %function
    .thumb_func
SysTick_Handler:
    mov     r0, lr
    mov     r1, sp
    mrs     r2, msp
    mrs     r3, psp
    b       exc_observe
    .size   SysTick_Handler, . - SysTick_Handler
