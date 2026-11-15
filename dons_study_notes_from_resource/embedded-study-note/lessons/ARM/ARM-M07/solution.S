/* ARM-M07 NEON byte processing — memchr / byte-sum checksum / u16 min-max
 *
 *   const void *neon_memchr(const void *p, int c, size_t n);              // Part A
 *   uint64_t    checksum_u8(const uint8_t *p, size_t n);                  // Part A
 *   void        minmax_u16(const uint16_t *s, size_t n,
 *                          uint16_t *mn, uint16_t *mx);                   // Part A
 *
 * AAPCS64: x0..x3 args, x0 return. Leaf functions, no stack.
 * Scratch: x2..x6 and v0..v2 only (v0..v7 / v16..v31 are caller-saved; v8..v15 low 64 bits are
 * callee-saved, so we never touch them).
 * Rule: never read outside [p, p+n) -- the page-guard test in harness.c enforces it.
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

/* ---------------- neon_memchr ---------------- */
    .globl SYM(neon_memchr)
    .p2align 4
    FUNC_TYPE(neon_memchr)
SYM(neon_memchr):
    and     w1, w1, #0xff           // c = (unsigned char)c  (C11 7.24.5.1); scalar path needs it
    cmp     x2, #16
    b.lo    3f                      // n < 16 -> scalar loop (never over-read)
    dup     v0.16b, w1              // v0 = {c, c, ..., c}  (16 lanes)
1:  ldr     q1, [x0]                // 16 bytes at p (unaligned is fine on Normal memory)
    cmeq    v1.16b, v1.16b, v0.16b  // lane = 0xFF if byte == c else 0x00
    shrn    v1.8b, v1.8h, #4        // 128-bit lane mask -> 64-bit "nibble mask" (4 bits / byte)
    fmov    x3, d1                  // move the nibble mask to a GPR
    cbnz    x3, 2f                  // any match in this chunk?
    add     x0, x0, #16             // p += 16
    sub     x2, x2, #16             // n -= 16
    cmp     x2, #16
    b.hs    1b                      // another full chunk
    cbz     x2, 5f                  // nothing left -> not found
    add     x0, x0, x2              // tail 1..15 B: re-scan the LAST 16 bytes [end-16, end)
    sub     x0, x0, #16             //   (overlap was already checked = no match there)
    mov     x2, #16                 //   exactly one more pass
    b       1b
2:  rbit    x3, x3                  // lowest set bit -> highest
    clz     x3, x3                  // = bit index of the first match = 4 * byte index
    add     x0, x0, x3, lsr #2      // return p + index
    ret
3:  cbz     x2, 5f                  // n == 0
4:  ldrb    w3, [x0]                // scalar: one byte
    cmp     w3, w1
    b.eq    6f                      // found -> x0 points at it
    add     x0, x0, #1
    subs    x2, x2, #1
    b.ne    4b
5:  mov     x0, #0                  // NULL
6:  ret
    FUNC_SIZE(neon_memchr)

/* ---------------- checksum_u8 ---------------- */
    .globl SYM(checksum_u8)
    .p2align 4
    FUNC_TYPE(checksum_u8)
SYM(checksum_u8):
    mov     x2, #0                  // total (u64)
    cmp     x1, #16
    b.lo    3f
1:  movi    v0.2d, #0               // 8 x u16 lane accumulators, fresh per block
    mov     x3, #128                // u16 horizon: 128 steps * 510 = 65280 <= 65535
2:  ldr     q1, [x0], #16           // 16 bytes, p += 16
    uadalp  v0.8h, v1.16b           // v0.h[i] += p[2i] + p[2i+1]  (pairwise widen + accumulate)
    sub     x1, x1, #16
    subs    x3, x3, #1
    b.eq    9f                      // block full -> fold before u16 can overflow
    cmp     x1, #16
    b.hs    2b
9:  uaddlv  s2, v0.8h               // s2 = sum of 8 u16 lanes (<= 8 * 65535, fits u32)
    fmov    w4, s2
    add     x2, x2, x4              // total += block sum
    cmp     x1, #16
    b.hs    1b
3:  cbz     x1, 5f                  // scalar tail 0..15 bytes
4:  ldrb    w4, [x0], #1
    add     x2, x2, x4
    subs    x1, x1, #1
    b.ne    4b
5:  mov     x0, x2
    ret
    FUNC_SIZE(checksum_u8)

/* ---------------- minmax_u16 ---------------- */
    .globl SYM(minmax_u16)
    .p2align 4
    FUNC_TYPE(minmax_u16)
SYM(minmax_u16):
    mov     w4, #0xffff             // mn = identity of min (n == 0 -> 0xFFFF)
    mov     w5, #0                  // mx = identity of max (n == 0 -> 0)
    cmp     x1, #8
    b.lo    3f
    dup     v0.8h, w4               // 8 lane mins = 0xFFFF
    movi    v1.2d, #0               // 8 lane maxes = 0
1:  ldr     q2, [x0], #16           // 8 samples, s += 8
    umin    v0.8h, v0.8h, v2.8h     // UNSIGNED lane min
    umax    v1.8h, v1.8h, v2.8h     // UNSIGNED lane max
    sub     x1, x1, #8
    cmp     x1, #8
    b.hs    1b
    uminv   h0, v0.8h               // reduce across 8 lanes
    umaxv   h1, v1.8h
    umov    w4, v0.h[0]
    umov    w5, v1.h[0]
3:  cbz     x1, 5f                  // scalar tail 0..7 samples
4:  ldrh    w6, [x0], #2
    cmp     w6, w4
    csel    w4, w6, w4, lo          // mn = (x < mn) ? x : mn   (unsigned)
    cmp     w6, w5
    csel    w5, w6, w5, hi          // mx = (x > mx) ? x : mx   (unsigned)
    subs    x1, x1, #1
    b.ne    4b
5:  strh    w4, [x2]
    strh    w5, [x3]
    ret
    FUNC_SIZE(minmax_u16)
