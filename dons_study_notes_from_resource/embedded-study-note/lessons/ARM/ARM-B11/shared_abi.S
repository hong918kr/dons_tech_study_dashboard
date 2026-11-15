/* ARM-B11 공용 테스트 도구 (TODO 아님) — 모든 빌드(starter/solution/practice)에 같이 링크된다.
 *
 * void  clobber_fp_scratch(void)
 *     caller-saved FP/SIMD 레지스터(v0-v7, v16-v31) 전부를 쓰레기(0xA5..)로 덮는다.
 *     콜백 안에서 부르면 "콜백이 caller-saved 를 망가뜨려도 되는" ABI 권리를 실제로 행사하는 셈.
 *
 * float cs_check_call(ema_fn fn, float (*read)(void), float y, float alpha, uint32_t *mask)
 *     d8-d15 (하위 64비트)와 v8-v15 상위 64비트에 센티널을 심고 fn(read, y, alpha)를 부른 뒤
 *     무엇이 바뀌었는지 *mask 에 적는다:  bit k   (k=0..7) = d(8+k) 하위 64비트가 바뀜 → ABI 위반
 *                                         bit 8+k          = v(8+k) 상위 64비트가 바뀜 → 허용(정보)
 *     fn 의 반환값 s0 를 그대로 돌려준다.
 */
#ifdef __APPLE__
#define SYM(x) _##x
#define FTYPE(x)
#define FSIZE(x)
#else
#define SYM(x) x
#define FTYPE(x) .type x, %function
#define FSIZE(x) .size x, .-x
#endif

    .text

    .globl SYM(clobber_fp_scratch)
    FTYPE(clobber_fp_scratch)
    .p2align 2
SYM(clobber_fp_scratch):
    .irp r, 0,1,2,3,4,5,6,7,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    movi    v\r\().16b, #0xa5           // caller-saved → 덮어써도 합법
    .endr
    ret
    FSIZE(clobber_fp_scratch)

// 센티널 = 0xC0DE_0000_0000_000k  (k = 0..7)
.macro SENT_SET reg, k
    movz    x9, #0xc0de, lsl #48
    movk    x9, #\k
    fmov    d\reg, x9                   // 하위 64비트 (이 쓰기는 상위 64비트를 0으로 만든다)
    mov     v\reg\().d[1], x9           // 그래서 상위 64비트는 그 다음에 심는다
.endm

.macro SENT_CHK reg, k
    movz    x9, #0xc0de, lsl #48
    movk    x9, #\k
    fmov    x10, d\reg
    cmp     x10, x9
    cset    w11, ne
    orr     w12, w12, w11, lsl #\k      // bit k   : 하위 64비트 바뀜
    mov     x10, v\reg\().d[1]
    cmp     x10, x9
    cset    w11, ne
    orr     w12, w12, w11, lsl #(\k + 8) // bit k+8 : 상위 64비트 바뀜
.endm

    .globl SYM(cs_check_call)
    FTYPE(cs_check_call)
    .p2align 2
SYM(cs_check_call):
    stp     x29, x30, [sp, #-96]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]         // 나도 callee-saved 를 쓰니 저장
    stp     d8, d9, [sp, #32]
    stp     d10, d11, [sp, #48]
    stp     d12, d13, [sp, #64]
    stp     d14, d15, [sp, #80]
    mov     x19, x2                     // mask 포인터
    mov     x20, x0                     // fn
    mov     x0, x1                      // fn 의 첫 인자 = read (s0=y, s1=alpha 는 그대로)
    SENT_SET 8, 0
    SENT_SET 9, 1
    SENT_SET 10, 2
    SENT_SET 11, 3
    SENT_SET 12, 4
    SENT_SET 13, 5
    SENT_SET 14, 6
    SENT_SET 15, 7
    blr     x20                         // s0 = fn(read, y, alpha)
    mov     w12, #0
    SENT_CHK 8, 0
    SENT_CHK 9, 1
    SENT_CHK 10, 2
    SENT_CHK 11, 3
    SENT_CHK 12, 4
    SENT_CHK 13, 5
    SENT_CHK 14, 6
    SENT_CHK 15, 7
    str     w12, [x19]
    ldp     d14, d15, [sp, #80]
    ldp     d12, d13, [sp, #64]
    ldp     d10, d11, [sp, #48]
    ldp     d8, d9, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #96
    ret                                 // s0 (fn 의 반환값)는 건드리지 않았다
    FSIZE(cs_check_call)

#ifndef __APPLE__
    .section .note.GNU-stack,"",%progbits
#endif
