/* ARM-B10 demo_bugs.S: five planted bugs (each = solution.S with 1-2 lines changed)
 *   cc -std=c11 -O2 demo_bugs.c demo_bugs.S -o /tmp/ARM-B10_bugs && /tmp/ARM-B10_bugs
 */
#ifdef __APPLE__
#define SYM(x) _##x
#else
#define SYM(x) x
#endif
    .text

/* bug 1: byte loop without the n == 0 check -> subs wraps 0 to 2^64-1 */
    .globl SYM(bug_nozero_byte)
    .p2align 2
SYM(bug_nozero_byte):
    mov     x3, x0
                                // (missing: cbz x2, 2f)
1:  ldrb    w4, [x1], #1
    strb    w4, [x3], #1
    subs    x2, x2, #1
    b.ne    1b
    ret

/* bug 2: memcpy_16 whose tail forgets the 1-byte step (bit 0 of n) */
    .globl SYM(bug_tail_no_bit0)
    .p2align 2
SYM(bug_tail_no_bit0):
    mov     x3, x0
    cmp     x2, #16
    b.lo    3f
    neg     x4, x3
    and     x4, x4, #15
    sub     x2, x2, x4
    cbz     x4, 2f
1:  ldrb    w5, [x1], #1
    strb    w5, [x3], #1
    subs    x4, x4, #1
    b.ne    1b
2:  lsr     x4, x2, #4
    and     x2, x2, #15
    cbz     x4, 3f
5:  ldp     x5, x6, [x1], #16
    stp     x5, x6, [x3], #16
    subs    x4, x4, #1
    b.ne    5b
3:  tbz     x2, #3, 6f
    ldr     x5, [x1], #8
    str     x5, [x3], #8
6:  tbz     x2, #2, 7f
    ldr     w5, [x1], #4
    str     w5, [x3], #4
7:  tbz     x2, #1, 9f
    ldrh    w5, [x1], #2
    strh    w5, [x3], #2
9:  ret                         // (missing: tbz x2, #0 / ldrb / strb)

/* bug 3: "just copy one more 16-byte block for the tail" -> writes and reads past the end */
    .globl SYM(bug_tail_round_up)
    .p2align 2
SYM(bug_tail_round_up):
    mov     x3, x0
    add     x2, x2, #15
    lsr     x4, x2, #4          // blocks = ceil(n / 16)
    cbz     x4, 9f
5:  ldp     x5, x6, [x1], #16
    stp     x5, x6, [x3], #16
    subs    x4, x4, #1
    b.ne    5b
9:  ret

/* bug 4: x0 used as the dst cursor -> copies fine, returns dst + n */
    .globl SYM(bug_clobber_x0)
    .p2align 2
SYM(bug_clobber_x0):
    cbz     x2, 2f
1:  ldrb    w4, [x1], #1
    strb    w4, [x0], #1        // x0 itself advances
    subs    x2, x2, #1
    b.ne    1b
2:  ret

/* bug 5: head count = dst & 15 instead of (-dst) & 15 -> content still correct, dst never aligned */
    .globl SYM(bug_head_and)
    .p2align 2
SYM(bug_head_and):
    mov     x3, x0
    cmp     x2, #16
    b.lo    3f
    and     x4, x3, #15         // BUG (missing: neg x4, x3 before this)
    sub     x2, x2, x4
    cbz     x4, 2f
1:  ldrb    w5, [x1], #1
    strb    w5, [x3], #1
    subs    x4, x4, #1
    b.ne    1b
2:  lsr     x4, x2, #4
    and     x2, x2, #15
    cbz     x4, 3f
5:  ldp     x5, x6, [x1], #16
    stp     x5, x6, [x3], #16
    subs    x4, x4, #1
    b.ne    5b
3:  tbz     x2, #3, 6f
    ldr     x5, [x1], #8
    str     x5, [x3], #8
6:  tbz     x2, #2, 7f
    ldr     w5, [x1], #4
    str     w5, [x3], #4
7:  tbz     x2, #1, 8f
    ldrh    w5, [x1], #2
    strh    w5, [x3], #2
8:  tbz     x2, #0, 9f
    ldrb    w5, [x1]
    strb    w5, [x3]
9:  ret

/* bug 6: strlen that counts the NUL (forgot the final sub #1) */
    .globl SYM(bug_strlen_plus1)
    .p2align 2
SYM(bug_strlen_plus1):
    mov     x1, x0
1:  ldrb    w2, [x1], #1
    cbnz    w2, 1b
    sub     x0, x1, x0          // (missing: sub x0, x0, #1)
    ret
