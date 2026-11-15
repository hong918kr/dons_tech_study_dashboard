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
    // TODO(Part A): dup c -> 16 lanes, loop: ldr q / cmeq / shrn #4 / fmov / cbnz,
    //   found: rbit + clz + lsr #2; n < 16 -> scalar loop; tail -> overlapping last 16 B
    mov     x0, #0                  // stub: always "not found"
    ret
    FUNC_SIZE(neon_memchr)

/* ---------------- checksum_u8 ---------------- */
    .globl SYM(checksum_u8)
    .p2align 4
    FUNC_TYPE(checksum_u8)
SYM(checksum_u8):
    mov     x2, #0                  // total (u64)
    // TODO(Part A): blocks of <= 128 x 16 B with uadalp v.8h, v.16b, fold with uaddlv,
    //   then scalar tail 0..15 B
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
    // TODO(Part A): 8 lanes umin/umax, uminv/umaxv reduce, scalar tail 0..7 (unsigned!)
5:  strh    w4, [x2]
    strh    w5, [x3]
    ret
    FUNC_SIZE(minmax_u16)
