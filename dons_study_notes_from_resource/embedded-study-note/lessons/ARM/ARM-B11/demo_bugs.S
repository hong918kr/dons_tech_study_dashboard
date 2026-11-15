/* ARM-B11 demo: ema_step 의 흔한 ABI 버그 2종 (테스트에는 안 들어감, demo_bugs.c 가 실행)
 *   cc -std=c11 -O2 demo_bugs.c demo_bugs.S shared_abi.S -o db && ./db
 */
#ifdef __APPLE__
#define SYM(x) _##x
#else
#define SYM(x) x
#endif
    .text
// 버그 1: y, alpha 를 caller-saved s2/s3 에 두고 콜백을 부른다 → 콜백이 지우면 끝
    .globl SYM(ema_bug_scratch)
    .p2align 2
SYM(ema_bug_scratch):
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    fmov    s2, s0                      // y     → s2 (caller-saved!)
    fmov    s3, s1                      // alpha → s3 (caller-saved!)
    blr     x0
    fsub    s0, s0, s2
    fmadd   s0, s3, s0, s2
    ldp     x29, x30, [sp], #16
    ret

// 버그 2: d8/d9 를 쓰면서 저장/복원을 안 한다 → 값은 맞지만 호출자의 d8/d9 가 망가진다
    .globl SYM(ema_bug_nosave)
    .p2align 2
SYM(ema_bug_nosave):
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    fmov    s8, s0
    fmov    s9, s1
    blr     x0
    fsub    s0, s0, s8
    fmadd   s0, s9, s0, s8
    ldp     x29, x30, [sp], #16
    ret
#ifndef __APPLE__
    .section .note.GNU-stack,"",%progbits
#endif
