/* H5 · shifts done right (the UB versions are in H5_shifts_ub.c). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define BIT(n)    (1u << (n))                 /* n in 0..31            */
#define BIT64(n)  (UINT64_C(1) << (n))        /* n in 0..63            */

static uint32_t be32(const uint8_t *b) {      /* cast BEFORE the shift */
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}
static uint32_t shl32(uint32_t x, unsigned n) {   /* defined for every n */
    return n >= 32 ? 0u : x << n;
}
static uint32_t rotl32(uint32_t x, unsigned n) {  /* n = 0 and n >= 32 safe */
    n &= 31u;
    return (x << n) | (x >> ((32u - n) & 31u));
}

int main(void) {
    static const uint8_t pkt[4] = {0x80, 0x00, 0x00, 0x01};
    assert(BIT(31) == 0x80000000u);
    assert(BIT64(40) == 0x10000000000ull);
    assert(be32(pkt) == 0x80000001u);
    uint64_t w = be32(pkt);                       /* no sign extension now */
    assert(w == 0x0000000080000001ull);
    assert(shl32(1u, 31) == 0x80000000u && shl32(1u, 32) == 0u && shl32(5u, 0) == 5u);
    assert(rotl32(0x80000001u, 1) == 0x00000003u);
    assert(rotl32(0x12345678u, 0) == 0x12345678u && rotl32(0x12345678u, 32) == 0x12345678u);
    /* -8 >> 1 is implementation-defined (C11 6.5.7p5); clang/gcc: arithmetic */
    int32_t neg = -8, neg7 = -7;
    printf("-8 >> 1 = %d (impl-defined), -7 >> 1 = %d, -7 / 2 = %d\n",
           neg >> 1, neg7 >> 1, neg7 / 2);
    assert((neg7 >> 1) == -4 && neg7 / 2 == -3);  /* >> floors, / truncates */
    printf("BIT(31)=0x%08X be32=0x%08X as u64=0x%016llX rotl(0x80000001,1)=0x%08X\n",
           (unsigned)BIT(31), (unsigned)be32(pkt), (unsigned long long)w,
           (unsigned)rotl32(0x80000001u, 1));
    puts("H5 PASS");
    return 0;
}
