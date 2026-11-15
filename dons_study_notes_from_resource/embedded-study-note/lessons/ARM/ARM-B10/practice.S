/* ARM-B10 추가 연습 answers
 *   size_t strlen_word(const char *s);                          // 1: 8 bytes at a time (haszero trick)
 *   void  *memmove_byte(void *dst, const void *src, size_t n);  // 2: overlap-safe, picks direction
 *   void  *memcpy_32(void *dst, const void *src, size_t n);     // 3: 32 B per iteration (2 x LDP/STP)
 *   void  *memset_16(void *dst, int c, size_t n);               // 4: replicated byte + STP loop
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

/* ---- 1: strlen_word ----
 * Read aligned 8-byte words only (an aligned word never crosses a page).
 * Bytes before s in the first word are forced to 0xFF so they can't look like NUL.
 * haszero(v) = (v - 0x01..01) & ~v & 0x80..80 ; its lowest set bit marks the first 0 byte.
 */
    .globl SYM(strlen_word)
    .p2align 2
    FUNC_TYPE(strlen_word)
SYM(strlen_word):
    and     x1, x0, #~7                 // x1 = s rounded down to 8
    and     x2, x0, #7                  // a = s & 7 (bytes to ignore)
    ldr     x3, [x1], #8                // first aligned word, x1 += 8
    lsl     x2, x2, #3                  // a * 8 bits
    mov     x4, #-1
    lsl     x4, x4, x2                  // 1s from byte a upwards
    orn     x3, x3, x4                  // low a bytes |= 0xFF (little endian)
    mov     x6, #0x0101010101010101     // logical immediate (encodable)
    mov     x7, #0x8080808080808080
1:  sub     x4, x3, x6                  // v - 0x01..
    bic     x4, x4, x3                  //   & ~v
    ands    x4, x4, x7                  //   & 0x80..  -> Z=0 if some byte is 0
    b.ne    2f
    ldr     x3, [x1], #8                // next aligned word
    b       1b
2:  rbit    x4, x4                      // lowest set bit -> highest
    clz     x4, x4                      // bit index (7, 15, ..., 63)
    sub     x1, x1, #8                  // address of the word that had the NUL
    add     x1, x1, x4, lsr #3          // + byte index
    sub     x0, x1, x0                  // length = &NUL - s
    ret
    FUNC_SIZE(strlen_word)

/* ---- 2: memmove_byte ----
 * forward is safe iff (dst - src) >= n as unsigned (covers dst < src and no overlap);
 * otherwise dst is inside (src, src+n): copy backwards with pre-index #-1.
 */
    .globl SYM(memmove_byte)
    .p2align 2
    FUNC_TYPE(memmove_byte)
SYM(memmove_byte):
    mov     x3, x0
    cbz     x2, 9f
    sub     x4, x0, x1                  // dst - src (mod 2^64)
    cmp     x4, x2
    b.hs    1f                          // unsigned >= n -> forward copy is safe
    add     x1, x1, x2                  // backward: start from the ends
    add     x3, x3, x2
2:  ldrb    w5, [x1, #-1]!              // pre-index: --src then load
    strb    w5, [x3, #-1]!
    subs    x2, x2, #1
    b.ne    2b
    ret
1:  ldrb    w5, [x1], #1
    strb    w5, [x3], #1
    subs    x2, x2, #1
    b.ne    1b
9:  ret
    FUNC_SIZE(memmove_byte)

/* ---- 3: memcpy_32 ---- head-align dst to 16, 32 B body, tail 16/8/4/2/1 by TBZ */
    .globl SYM(memcpy_32)
    .p2align 2
    FUNC_TYPE(memcpy_32)
SYM(memcpy_32):
    mov     x3, x0
    cmp     x2, #32
    b.lo    3f                          // n < 32 -> tail handles 0..31
    neg     x4, x3
    and     x4, x4, #15
    sub     x2, x2, x4
    cbz     x4, 2f
1:  ldrb    w5, [x1], #1
    strb    w5, [x3], #1
    subs    x4, x4, #1
    b.ne    1b
2:  lsr     x4, x2, #5                  // 32-byte blocks
    and     x2, x2, #31                 // tail 0..31
    cbz     x4, 3f
5:  ldp     x5, x6, [x1]                // two independent 16 B loads
    ldp     x7, x8, [x1, #16]
    add     x1, x1, #32
    stp     x5, x6, [x3]
    stp     x7, x8, [x3, #16]
    add     x3, x3, #32
    subs    x4, x4, #1
    b.ne    5b
3:  tbz     x2, #4, 4f                  // 16 bytes?
    ldp     x5, x6, [x1], #16
    stp     x5, x6, [x3], #16
4:  tbz     x2, #3, 6f
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
    FUNC_SIZE(memcpy_32)

/* ---- 4: memset_16 ---- (int c: upper bits of x1 are not guaranteed -> mask first) */
    .globl SYM(memset_16)
    .p2align 2
    FUNC_TYPE(memset_16)
SYM(memset_16):
    mov     x3, x0
    and     x1, x1, #0xff               // (unsigned char)c, also clears garbage upper bits
    mov     x4, #0x0101010101010101
    mul     x1, x1, x4                  // 0xcc -> 0xcccccccccccccccc
    cmp     x2, #16
    b.lo    3f
    neg     x4, x3
    and     x4, x4, #15
    sub     x2, x2, x4
    cbz     x4, 2f
1:  strb    w1, [x3], #1
    subs    x4, x4, #1
    b.ne    1b
2:  lsr     x4, x2, #4
    and     x2, x2, #15
    cbz     x4, 3f
5:  stp     x1, x1, [x3], #16
    subs    x4, x4, #1
    b.ne    5b
3:  tbz     x2, #3, 6f
    str     x1, [x3], #8
6:  tbz     x2, #2, 7f
    str     w1, [x3], #4
7:  tbz     x2, #1, 8f
    strh    w1, [x3], #2
8:  tbz     x2, #0, 9f
    strb    w1, [x3]
9:  ret
    FUNC_SIZE(memset_16)
