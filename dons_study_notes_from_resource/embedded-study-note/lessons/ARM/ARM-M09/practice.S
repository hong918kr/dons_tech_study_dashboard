/* ENV: qemu */
/* ARM-M09 practice.S — solution.S 전체 + 추가 연습용 asm (PendSV 스텁, svc #4/#5/#6 래퍼, 프로파일 대상 루프). */
#include "solution.S"

    .text
    .thumb

// PendSV_Handler: SVC 와 같은 진입 스텁 → pendsv_c(frame, exc_return)
    .global PendSV_Handler
    .type   PendSV_Handler, %function
    .thumb_func
PendSV_Handler:
    tst     lr, #4
    ite     eq
    mrseq   r0, msp
    mrsne   r0, psp
    mov     r1, lr
    b       pendsv_c
    .size   PendSV_Handler, . - PendSV_Handler

// uint64_t sys_mul64(uint32_t a, uint32_t b) — svc #4, 결과는 r0(하위):r1(상위)
    .global sys_mul64
    .type   sys_mul64, %function
    .thumb_func
sys_mul64:
    svc     #4
    bx      lr
    .size   sys_mul64, . - sys_mul64

// void sys_inject(void (*fn)(uint32_t), uint32_t arg) — svc #5: 예외 복귀 때 fn(arg) 를 "끼워 넣는다"
//   스택된 LR 이 덮어써지므로 래퍼는 자기 lr 을 먼저 push 한다 (8 B → 8 정렬 유지)
    .global sys_inject
    .type   sys_inject, %function
    .thumb_func
sys_inject:
    push    {r4, lr}
    svc     #5
    .global sys_inject_ret
sys_inject_ret:
    pop     {r4, pc}
    .size   sys_inject, . - sys_inject

// void sys_pend_pendsv(void) — svc #6: 핸들러 안에서 PendSV 를 pend
    .global sys_pend_pendsv
    .type   sys_pend_pendsv, %function
    .thumb_func
sys_pend_pendsv:
    svc     #6
    bx      lr
    .size   sys_pend_pendsv, . - sys_pend_pendsv

// void busy_a(uint32_t n) / busy_b(uint32_t n) — PC 샘플링 대상 (leaf, 루프만)
    .global busy_a
    .type   busy_a, %function
    .thumb_func
busy_a:
1:  subs    r0, r0, #1
    bne     1b
    bx      lr
    .global busy_a_end
busy_a_end:
    .size   busy_a, . - busy_a

    .global busy_b
    .type   busy_b, %function
    .thumb_func
busy_b:
1:  nop
    subs    r0, r0, #1
    bne     1b
    bx      lr
    .global busy_b_end
busy_b_end:
    .size   busy_b, . - busy_b
