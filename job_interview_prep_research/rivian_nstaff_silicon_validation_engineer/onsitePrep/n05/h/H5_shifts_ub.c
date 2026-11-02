/* H5 (UB part) · shift UB. Build WITHOUT -Werror:
 *   with -fsanitize=undefined  -> see the reports
 *   without sanitizer, -O0 vs -O2 -> see that "what it prints" is not stable */
#include <stdint.h>
#include <stdio.h>

__attribute__((noinline)) int      s_one(int n)                 { return 1 << n; }
__attribute__((noinline)) uint32_t u_shl(uint32_t x, unsigned n) { return x << n; }
__attribute__((noinline)) int      neg_shl(int v)               { return v << 1; }
__attribute__((noinline)) uint32_t be32(const uint8_t *b) {
    return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];   /* b[0] promoted to int */
}
__attribute__((noinline)) uint64_t be32_to64(const uint8_t *b) {
    return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];   /* int result sign-extends */
}
static inline uint32_t inl_shl(uint32_t x, unsigned n) { return x << n; }  /* inlinable */
__attribute__((noinline)) uint64_t wide(int n)                  { return 1 << n; }

int main(void) {
    static const uint8_t pkt[4] = {0x80, 0x00, 0x00, 0x01};
    printf("(1) 1 << 31          = 0x%08X\n", (unsigned)s_one(31));
    printf("(2) 0x1u << 32       = 0x%08X\n", (unsigned)u_shl(1u, 32));
    printf("(3) -1 << 1          = %d\n", neg_shl(-1));
    printf("(4) be32(80 00 00 01)= 0x%08X\n", (unsigned)be32(pkt));
    printf("(4b) as uint64_t     = 0x%016llX\n", (unsigned long long)be32_to64(pkt));
    printf("(5) (uint64_t)(1<<40)= 0x%llX\n", (unsigned long long)wide(40));
    volatile unsigned k = 32;  unsigned n = k;
    printf("(6) inlined 1u << 32 = 0x%08X\n", (unsigned)inl_shl(1u, n));
    printf("(7) const   1u << 32 = 0x%08X\n", (unsigned)inl_shl(1u, 32));
    return 0;
}
