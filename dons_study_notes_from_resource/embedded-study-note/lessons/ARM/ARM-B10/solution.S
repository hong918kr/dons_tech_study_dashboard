/* ARM-B10 strlen / memcpy in AArch64 assembly
 *
 *   void  *memcpy_byte(void *dst, const void *src, size_t n);   // Part A
 *   size_t strlen_byte(const char *s);                          // Part A
 *   void  *memcpy_16(void *dst, const void *src, size_t n);     // Part B
 *
 * AAPCS64: x0 = dst / s, x1 = src, x2 = n, return in x0.
 * Leaf functions: only caller-saved scratch x3..x7 used, no stack.
 * Same file assembles for Mach-O (macOS) and ELF (Linux) via SYM().
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

/* ---------------- Part A: memcpy_byte ---------------- */
    .globl SYM(memcpy_byte)
    .p2align 2
    FUNC_TYPE(memcpy_byte)
SYM(memcpy_byte):
    mov     x3, x0              // x3 = dst cursor (x0 must survive: return value)
    cbz     x2, 2f              // n == 0 -> nothing to copy
1:  ldrb    w4, [x1], #1        // w4 = *src++  (zero-extended byte)
    strb    w4, [x3], #1        // *dst++ = w4
    subs    x2, x2, #1          // n -= 1, set Z when n hits 0
    b.ne    1b                  // loop while n != 0
2:  ret                         // x0 = original dst
    FUNC_SIZE(memcpy_byte)

/* ---------------- Part A: strlen_byte ---------------- */
    .globl SYM(strlen_byte)
    .p2align 2
    FUNC_TYPE(strlen_byte)
SYM(strlen_byte):
    mov     x1, x0              // x1 = cursor, x0 keeps the start
1:  ldrb    w2, [x1], #1        // w2 = *p++  (ldrb: 0x80..0xFF stay non-zero)
    cbnz    w2, 1b              // not NUL -> next byte
    sub     x0, x1, x0          // cursor is one past the NUL
    sub     x0, x0, #1          //   -> length = cursor - start - 1
    ret
    FUNC_SIZE(strlen_byte)

/* ---------------- Part B: memcpy_16 ----------------
 * 1) n < 16: byte tail only
 * 2) head: copy k = (-dst) & 15 bytes so dst becomes 16-byte aligned
 * 3) body: n/16 blocks with LDP/STP of two x registers (src may stay unaligned)
 * 4) tail: n & 15 bytes as 8/4/2/1 chunks chosen by TBZ on the bits of n
 */
    .globl SYM(memcpy_16)
    .p2align 2
    FUNC_TYPE(memcpy_16)
SYM(memcpy_16):
    mov     x3, x0              // x3 = dst cursor, x0 = return value
    cmp     x2, #16
    b.lo    3f                  // n < 16 (unsigned) -> tail only
    neg     x4, x3              // x4 = -dst
    and     x4, x4, #15         // k = bytes until dst is 16-aligned (0..15)
    sub     x2, x2, x4          // n -= k   (n >= 16 > k, no wrap)
    cbz     x4, 2f              // already aligned
1:  ldrb    w5, [x1], #1        // head byte
    strb    w5, [x3], #1
    subs    x4, x4, #1
    b.ne    1b
2:  lsr     x4, x2, #4          // blocks = n / 16
    and     x2, x2, #15         // tail   = n % 16
    cbz     x4, 3f
5:  ldp     x5, x6, [x1], #16   // 16 B load (2 x 8 B), src += 16
    stp     x5, x6, [x3], #16   // 16 B store to aligned dst, dst += 16
    subs    x4, x4, #1
    b.ne    5b
3:  tbz     x2, #3, 6f          // bit 3 of n: 8 more bytes?
    ldr     x5, [x1], #8
    str     x5, [x3], #8
6:  tbz     x2, #2, 7f          // 4 bytes?
    ldr     w5, [x1], #4
    str     w5, [x3], #4
7:  tbz     x2, #1, 8f          // 2 bytes?
    ldrh    w5, [x1], #2
    strh    w5, [x3], #2
8:  tbz     x2, #0, 9f          // 1 byte?
    ldrb    w5, [x1]
    strb    w5, [x3]
9:  ret
    FUNC_SIZE(memcpy_16)
