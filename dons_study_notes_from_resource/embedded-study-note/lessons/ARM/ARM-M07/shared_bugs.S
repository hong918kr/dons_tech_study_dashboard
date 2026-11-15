/* ARM-M07 shared_bugs.S -- six INJECTED BUGS (Part B). Linked into every variant; the harness
 * asserts that its tests CATCH each one (a test that never fails on a wrong function proves nothing).
 *
 *   bug_memchr_norbit   : clz without rbit -> counts from the wrong end of the nibble mask
 *   bug_memchr_nomask   : forgets (unsigned char)c -> vector path OK (dup uses the low 8 bits),
 *                         scalar path wrong for c = 0x141, c = -1
 *   bug_memchr_overread : always loads 16 B, even past p+n -> finds bytes beyond n, faults at page end
 *   bug_checksum_nofold : u16 lane accumulators never folded -> wraps after 2048 B of 0xFF
 *   bug_minmax_signed   : smin/smax (int16) -> fine for 12-bit ADC data, wrong for samples >= 0x8000
 *   bug_minmax_notail   : ignores the n % 8 tail samples
 */
#ifdef __APPLE__
#define SYM(x) _##x
#define FUNC_TYPE(x)
#define FUNC_SIZE(x)
#else
#define SYM(x) x
#define FUNC_TYPE(x) .type x, %function
#define FUNC_SIZE(x) .size x, . - x
#endif

    .text

    .globl SYM(bug_memchr_norbit)
    .p2align 4
    FUNC_TYPE(bug_memchr_norbit)
SYM(bug_memchr_norbit):
    and     w1, w1, #0xff
    cmp     x2, #16
    b.lo    3f
    dup     v0.16b, w1
1:  ldr     q1, [x0]
    cmeq    v1.16b, v1.16b, v0.16b
    shrn    v1.8b, v1.8h, #4
    fmov    x3, d1
    cbnz    x3, 2f
    add     x0, x0, #16
    sub     x2, x2, #16
    cmp     x2, #16
    b.hs    1b
    cbz     x2, 5f
    add     x0, x0, x2
    sub     x0, x0, #16
    mov     x2, #16
    b       1b
2:  clz     x3, x3                  // BUG: no rbit -> leading zeros of the HIGHEST match nibble
    add     x0, x0, x3, lsr #2
    ret
3:  cbz     x2, 5f
4:  ldrb    w3, [x0]
    cmp     w3, w1
    b.eq    6f
    add     x0, x0, #1
    subs    x2, x2, #1
    b.ne    4b
5:  mov     x0, #0
6:  ret
    FUNC_SIZE(bug_memchr_norbit)

    .globl SYM(bug_memchr_nomask)
    .p2align 4
    FUNC_TYPE(bug_memchr_nomask)
SYM(bug_memchr_nomask):
                                    // BUG: no "and w1, w1, #0xff"
    cmp     x2, #16
    b.lo    3f
    dup     v0.16b, w1              // (vector path still correct: dup takes w1[7:0])
1:  ldr     q1, [x0]
    cmeq    v1.16b, v1.16b, v0.16b
    shrn    v1.8b, v1.8h, #4
    fmov    x3, d1
    cbnz    x3, 2f
    add     x0, x0, #16
    sub     x2, x2, #16
    cmp     x2, #16
    b.hs    1b
    cbz     x2, 5f
    add     x0, x0, x2
    sub     x0, x0, #16
    mov     x2, #16
    b       1b
2:  rbit    x3, x3
    clz     x3, x3
    add     x0, x0, x3, lsr #2
    ret
3:  cbz     x2, 5f
4:  ldrb    w3, [x0]
    cmp     w3, w1                  // 0x41 vs 0x141 -> never equal
    b.eq    6f
    add     x0, x0, #1
    subs    x2, x2, #1
    b.ne    4b
5:  mov     x0, #0
6:  ret
    FUNC_SIZE(bug_memchr_nomask)

    .globl SYM(bug_memchr_overread)
    .p2align 4
    FUNC_TYPE(bug_memchr_overread)
SYM(bug_memchr_overread):
    cbz     x2, 5f
    dup     v0.16b, w1
1:  ldr     q1, [x0]                // BUG: full 16 B even when fewer than 16 remain
    cmeq    v1.16b, v1.16b, v0.16b
    shrn    v1.8b, v1.8h, #4
    fmov    x3, d1
    cbnz    x3, 2f                  // BUG: match index never compared with n
    cmp     x2, #16
    b.ls    5f
    add     x0, x0, #16
    sub     x2, x2, #16
    b       1b
2:  rbit    x3, x3
    clz     x3, x3
    add     x0, x0, x3, lsr #2
    ret
5:  mov     x0, #0
    ret
    FUNC_SIZE(bug_memchr_overread)

    .globl SYM(bug_checksum_nofold)
    .p2align 4
    FUNC_TYPE(bug_checksum_nofold)
SYM(bug_checksum_nofold):
    mov     x2, #0
    movi    v0.2d, #0
    cmp     x1, #16
    b.lo    3f
2:  ldr     q1, [x0], #16
    uadalp  v0.8h, v1.16b           // BUG: no 128-step block limit -> u16 lanes wrap
    sub     x1, x1, #16
    cmp     x1, #16
    b.hs    2b
3:  uaddlv  s2, v0.8h
    fmov    w4, s2
    add     x2, x2, x4
    cbz     x1, 5f
4:  ldrb    w4, [x0], #1
    add     x2, x2, x4
    subs    x1, x1, #1
    b.ne    4b
5:  mov     x0, x2
    ret
    FUNC_SIZE(bug_checksum_nofold)

    .globl SYM(bug_minmax_signed)
    .p2align 4
    FUNC_TYPE(bug_minmax_signed)
SYM(bug_minmax_signed):
    mov     w4, #0x7fff             // "int16" identities: INT16_MAX / INT16_MIN
    mov     w5, #-32768
    cmp     x1, #8
    b.lo    3f
    dup     v0.8h, w4
    dup     v1.8h, w5
1:  ldr     q2, [x0], #16
    smin    v0.8h, v0.8h, v2.8h     // BUG: signed lane min/max
    smax    v1.8h, v1.8h, v2.8h
    sub     x1, x1, #8
    cmp     x1, #8
    b.hs    1b
    sminv   h0, v0.8h
    smaxv   h1, v1.8h
    smov    w4, v0.h[0]
    smov    w5, v1.h[0]
3:  cbz     x1, 5f
4:  ldrsh   w6, [x0], #2
    cmp     w6, w4
    csel    w4, w6, w4, lt
    cmp     w6, w5
    csel    w5, w6, w5, gt
    subs    x1, x1, #1
    b.ne    4b
5:  strh    w4, [x2]
    strh    w5, [x3]
    ret
    FUNC_SIZE(bug_minmax_signed)

    .globl SYM(bug_minmax_notail)
    .p2align 4
    FUNC_TYPE(bug_minmax_notail)
SYM(bug_minmax_notail):
    mov     w4, #0xffff
    mov     w5, #0
    cmp     x1, #8
    b.lo    5f                      // BUG: n < 8 -> skip everything
    dup     v0.8h, w4
    movi    v1.2d, #0
1:  ldr     q2, [x0], #16
    umin    v0.8h, v0.8h, v2.8h
    umax    v1.8h, v1.8h, v2.8h
    sub     x1, x1, #8
    cmp     x1, #8
    b.hs    1b
    uminv   h0, v0.8h
    umaxv   h1, v1.8h
    umov    w4, v0.h[0]
    umov    w5, v1.h[0]
5:  strh    w4, [x2]                // BUG: the last n % 8 samples are never looked at
    strh    w5, [x3]
    ret
    FUNC_SIZE(bug_minmax_notail)
