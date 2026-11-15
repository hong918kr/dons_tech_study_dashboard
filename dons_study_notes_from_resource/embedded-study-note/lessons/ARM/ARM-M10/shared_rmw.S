/* ENV: qemu */
/* ARM-M10 shared_rmw.S — 실험 도구 (TODO 없음, starter/solution/practice 공용).
 *   rmw_inc         : 평범한 read-modify-write (ldr / adds / str) — 보호 없음
 *   rmw_inc_inject  : ldr 와 str 사이에서 IRQ 를 "스스로" pend → 최악의 타이밍을 결정적으로 만든다
 *   primask_get     : PRIMASK 읽기 (테스트용 관찰 도구)
 *   crit_exit_buggy : 일부러 틀린 exit (항상 cpsie i) — 7. 실수 1 / Part B
 *   do_udf          : 정의 안 된 명령 1개 (udf #0, 16비트) → UsageFault/HardFault
 *   HardFault_Handler : MSP/PSP 고르기 → hardfault_c(frame) (ARM-M09/M12 의 축약판)
 */
    .syntax unified
    .cpu    cortex-m3
    .thumb
    .text

    .equ    NVIC_ISPR0, 0xE000E200      // Interrupt Set-Pending Register 0 (IRQ0..31), 1 쓰기 = pend

// void rmw_inc(volatile uint32_t *p)
    .global rmw_inc
    .type   rmw_inc, %function
    .thumb_func
rmw_inc:
    ldr     r1, [r0]            // ① 읽기
    adds    r1, r1, #1          // ② 고치기   ← 여기서 IRQ 가 들어와 *p 를 바꾸면
    str     r1, [r0]            // ③ 쓰기     ← 그 변경을 덮어쓴다 = lost update
    bx      lr
    .size   rmw_inc, . - rmw_inc

// void rmw_inc_inject(volatile uint32_t *p, uint32_t ispr_bits)
    .global rmw_inc_inject
    .type   rmw_inc_inject, %function
    .thumb_func
rmw_inc_inject:
    ldr     r2, [r0]            // ① 읽기 (옛 값)
    ldr     r3, =NVIC_ISPR0
    str     r1, [r3]            //    IRQ pend (마스크 안 됐으면 곧바로 받아들여질 차례)
    dsb                         //    쓰기 완료 보장
    isb                         //    파이프라인 비우기 → 대기 중인 IRQ 는 여기서 받는다
    adds    r2, r2, #1          // ② 고치기
    str     r2, [r0]            // ③ 쓰기 (ISR 가 그사이 올린 값을 덮어씀)
    bx      lr
    .size   rmw_inc_inject, . - rmw_inc_inject

// uint32_t primask_get(void)
    .global primask_get
    .type   primask_get, %function
    .thumb_func
primask_get:
    mrs     r0, primask
    bx      lr
    .size   primask_get, . - primask_get

// void crit_exit_buggy(uint32_t ignored) — 저장값을 무시하고 무조건 켠다 (중첩을 깬다)
    .global crit_exit_buggy
    .type   crit_exit_buggy, %function
    .thumb_func
crit_exit_buggy:
    cpsie   i                   // 바깥 섹션이 아직 진행 중이어도 인터럽트 ON
    bx      lr
    .size   crit_exit_buggy, . - crit_exit_buggy

// void do_udf(void) — 정의 안 된 명령. 핸들러가 stacked PC 를 2 바이트 넘겨서 돌아온다
    .global do_udf
    .global udf_insn
    .type   do_udf, %function
    .thumb_func
do_udf:
udf_insn:
    udf     #0                  // 0xde00 (16비트)
    bx      lr
    .size   do_udf, . - do_udf

// HardFault 진입: EXC_RETURN bit2 로 어느 스택에 프레임이 있는지 고른다 (ARM-M09 에서 자세히)
    .global HardFault_Handler
    .type   HardFault_Handler, %function
    .thumb_func
HardFault_Handler:
    tst     lr, #4
    ite     eq
    mrseq   r0, msp
    mrsne   r0, psp
    b       hardfault_c         // void hardfault_c(uint32_t *frame) — C 로 (꼬리 호출, lr = EXC_RETURN 유지)
    .size   HardFault_Handler, . - HardFault_Handler

    .ltorg
