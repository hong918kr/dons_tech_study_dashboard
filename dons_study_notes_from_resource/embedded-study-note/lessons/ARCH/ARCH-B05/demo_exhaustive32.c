/* ARCH-B05 demo — 게이트 모델 penc32/clz32 를 2^32 개 입력 전부에 대해 __builtin_clz 와 비교 (시간 측정)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_exhaustive32.c -o demo_exhaustive32 && ./demo_exhaustive32
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static inline unsigned NOT(unsigned a)              { return (a & 1u) ^ 1u; }
static inline unsigned AND2(unsigned a, unsigned b) { return a & b & 1u; }
static inline unsigned OR2(unsigned a, unsigned b)  { return (a | b) & 1u; }
static inline unsigned BIT(unsigned v, unsigned n)  { return (v >> n) & 1u; }

static unsigned mux4(unsigned d0, unsigned d1, unsigned d2, unsigned d3, unsigned s1, unsigned s0) {
    unsigned n1 = NOT(s1), n0 = NOT(s0);
    return OR2(OR2(AND2(AND2(n1, n0), d0), AND2(AND2(n1, s0), d1)),
               OR2(AND2(AND2(s1, n0), d2), AND2(AND2(s1, s0), d3)));
}
static unsigned penc8(uint8_t in, unsigned *valid) {
    unsigned i0 = BIT(in, 0), i1 = BIT(in, 1), i2 = BIT(in, 2), i3 = BIT(in, 3);
    unsigned i4 = BIT(in, 4), i5 = BIT(in, 5), i6 = BIT(in, 6), i7 = BIT(in, 7);
    unsigned y2 = OR2(OR2(i7, i6), OR2(i5, i4));
    unsigned y1 = OR2(OR2(i7, i6), AND2(AND2(NOT(i5), NOT(i4)), OR2(i3, i2)));
    unsigned y0 = OR2(OR2(i7, AND2(NOT(i6), i5)),
                      OR2(AND2(AND2(NOT(i6), NOT(i4)), i3),
                          AND2(AND2(NOT(i6), NOT(i4)), AND2(NOT(i2), i1))));
    *valid = OR2(y2, OR2(OR2(i3, i2), OR2(i1, i0)));
    return y2 << 2 | y1 << 1 | y0;
}
static unsigned penc32(uint32_t x, unsigned *valid) {
    unsigned v0, v1, v2, v3, gv;
    unsigned i0 = penc8((uint8_t)x, &v0), i1 = penc8((uint8_t)(x >> 8), &v1);
    unsigned i2 = penc8((uint8_t)(x >> 16), &v2), i3 = penc8((uint8_t)(x >> 24), &v3);
    unsigned g = penc8((uint8_t)(v3 << 3 | v2 << 2 | v1 << 1 | v0), &gv);
    unsigned g1 = BIT(g, 1), g0 = BIT(g, 0);
    unsigned b2 = mux4(BIT(i0, 2), BIT(i1, 2), BIT(i2, 2), BIT(i3, 2), g1, g0);
    unsigned b1 = mux4(BIT(i0, 1), BIT(i1, 1), BIT(i2, 1), BIT(i3, 1), g1, g0);
    unsigned b0 = mux4(BIT(i0, 0), BIT(i1, 0), BIT(i2, 0), BIT(i3, 0), g1, g0);
    *valid = gv;
    return g1 << 4 | g0 << 3 | b2 << 2 | b1 << 1 | b0;
}
static unsigned clz32(uint32_t x) { unsigned v, i = penc32(x, &v); return v ? 31u - i : 32u; }

int main(void) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint64_t bad = 0, n = 0;
    unsigned hist[33] = {0};
    uint32_t x = 0;
    do {
        unsigned c = clz32(x);
        unsigned ref = x ? (unsigned)__builtin_clz(x) : 32u;   /* clz(0) 은 따로 (builtin 은 UB) */
        if (c != ref) bad++;
        hist[c]++;
        n++;
    } while (++x != 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double s = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
    printf("inputs checked : %llu (= 2^32)\n", (unsigned long long)n);
    printf("mismatches     : %llu\n", (unsigned long long)bad);
    printf("time           : %.2f s (%.2f ns/input)\n", s, s * 1e9 / (double)n);
    printf("clz histogram  : clz=0 %u, clz=1 %u, clz=8 %u, clz=24 %u, clz=31 %u, clz=32 %u\n",
           hist[0], hist[1], hist[8], hist[24], hist[31], hist[32]);
    return bad ? 1 : 0;
}
