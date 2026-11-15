/* ARM-M07 practice.S -- 추가 연습 answers
 *
 *   size_t      count_byte(const void *p, int c, size_t n);                  // 연습 1
 *   size_t      strlen_neon(const char *s);                                  // 연습 2
 *   const void *memchr2_neon(const void *p, int c1, int c2, size_t n);       // 연습 3
 *   (연습 4 hist_u8_4way is C, in practice.c)
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

/* ---- 연습 1: how many bytes == c  (one histogram bin, u8 lane counters) ---- */
    .globl SYM(count_byte)
    .p2align 4
    FUNC_TYPE(count_byte)
SYM(count_byte):
    and     w1, w1, #0xff
    mov     x5, #0                  // total
    cmp     x2, #16
    b.lo    3f
    dup     v0.16b, w1
1:  movi    v2.2d, #0               // 16 x u8 counters
    mov     x3, #255                // u8 horizon: at most +1 per step -> 255 steps
2:  ldr     q1, [x0], #16
    cmeq    v1.16b, v1.16b, v0.16b  // 0xFF (= -1) where byte == c
    sub     v2.16b, v2.16b, v1.16b  // counter -= -1   ->  +1 per match
    sub     x2, x2, #16
    subs    x3, x3, #1
    b.eq    9f
    cmp     x2, #16
    b.hs    2b
9:  uaddlv  h3, v2.16b              // sum of 16 u8 counters (<= 4080) as u16
    umov    w4, v3.h[0]
    add     x5, x5, x4
    cmp     x2, #16
    b.hs    1b
3:  cbz     x2, 5f
4:  ldrb    w4, [x0], #1
    cmp     w4, w1
    cinc    x5, x5, eq
    subs    x2, x2, #1
    b.ne    4b
5:  mov     x0, x5
    ret
    FUNC_SIZE(count_byte)

/* ---- 연습 2: strlen with ALIGNED 16 B loads only (aligned-down first chunk + mask) ---- */
    .globl SYM(strlen_neon)
    .p2align 4
    FUNC_TYPE(strlen_neon)
SYM(strlen_neon):
    and     x1, x0, #~15            // chunk base = s rounded down to 16
    ldr     q0, [x1]                // may read up to 15 bytes BEFORE s (same 16 B block -> same page)
    cmeq    v0.16b, v0.16b, #0      // 0xFF where byte == 0
    shrn    v0.8b, v0.8h, #4        // nibble mask
    fmov    x2, d0
    and     x3, x0, #15             // k = bytes of this chunk that precede s
    lsl     x3, x3, #2              // 4 mask bits per byte
    lsr     x2, x2, x3              // drop them: nibble j now = byte s+j
    cbnz    x2, 2f
1:  ldr     q0, [x1, #16]!          // next aligned chunk (pre-index)
    cmeq    v0.16b, v0.16b, #0
    shrn    v0.8b, v0.8h, #4
    fmov    x2, d0
    cbz     x2, 1b
    rbit    x2, x2
    clz     x2, x2
    add     x1, x1, x2, lsr #2      // address of the NUL
    sub     x0, x1, x0              // length = NUL - s
    ret
2:  rbit    x2, x2                  // NUL inside the first (partial) chunk
    clz     x2, x2
    lsr     x0, x2, #2
    ret
    FUNC_SIZE(strlen_neon)

/* ---- 연습 3: first byte equal to c1 OR c2 (e.g. '\r' / '\n') ---- */
    .globl SYM(memchr2_neon)
    .p2align 4
    FUNC_TYPE(memchr2_neon)
SYM(memchr2_neon):
    and     w1, w1, #0xff
    and     w2, w2, #0xff
    cmp     x3, #16
    b.lo    3f
    dup     v0.16b, w1
    dup     v1.16b, w2
1:  ldr     q2, [x0]
    cmeq    v3.16b, v2.16b, v0.16b
    cmeq    v4.16b, v2.16b, v1.16b
    orr     v3.16b, v3.16b, v4.16b  // match either
    shrn    v3.8b, v3.8h, #4
    fmov    x4, d3
    cbnz    x4, 2f
    add     x0, x0, #16
    sub     x3, x3, #16
    cmp     x3, #16
    b.hs    1b
    cbz     x3, 5f
    add     x0, x0, x3              // overlapping last 16 B (same trick as neon_memchr)
    sub     x0, x0, #16
    mov     x3, #16
    b       1b
2:  rbit    x4, x4
    clz     x4, x4
    add     x0, x0, x4, lsr #2
    ret
3:  cbz     x3, 5f
4:  ldrb    w4, [x0]
    cmp     w4, w1
    ccmp    w4, w2, #4, ne          // if (b != c1) compare b with c2; else force Z=1
    b.eq    6f
    add     x0, x0, #1
    subs    x3, x3, #1
    b.ne    4b
5:  mov     x0, #0
6:  ret
    FUNC_SIZE(memchr2_neon)
