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
    // TODO: keep x0 (return value), copy n bytes one at a time.
    //       ldrb/strb with post-index, subs + b.ne; n == 0 must copy nothing.
    ret                         // stub: returns dst, copies nothing
    FUNC_SIZE(memcpy_byte)

/* ---------------- Part A: strlen_byte ---------------- */
    .globl SYM(strlen_byte)
    .p2align 2
    FUNC_TYPE(strlen_byte)
SYM(strlen_byte):
    // TODO: walk bytes with ldrb + post-index until the NUL,
    //       return (cursor - start) without counting the NUL.
    mov     x0, #0              // stub: wrong-but-safe length
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
    // TODO: steps 1)-4) above. Never read or write outside [src,src+n) / [dst,dst+n).
    ret                         // stub: returns dst, copies nothing
    FUNC_SIZE(memcpy_16)
