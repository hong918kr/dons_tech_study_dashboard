/* ARCH-B03 demo: 모든 2^32 float 비트 패턴을 소프트웨어 f32_to_f16 과 하드웨어 FCVT(_Float16 캐스트)로 변환해 비교.
 * 빌드: cc -std=c11 -O2 demo_exhaustive.c -o /tmp/esn_ARCH-B03/ex && /tmp/esn_ARCH-B03/ex
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static uint16_t f32_to_f16(uint32_t b) {      /* solution.c 와 같은 코드 */
    uint32_t sign = (b >> 16) & 0x8000u;
    uint32_t e = (b >> 23) & 0xFFu, f = b & 0x7FFFFFu;
    if (e == 0xFFu) return (uint16_t)(f ? (sign | 0x7E00u | (f >> 13)) : (sign | 0x7C00u));
    if (e == 0) return (uint16_t)sign;
    int eh = (int)e - 112;
    if (eh >= 31) return (uint16_t)(sign | 0x7C00u);
    uint32_t m = f | 0x800000u;
    int shift = eh >= 1 ? 13 : 14 - eh;
    if (shift > 24) return (uint16_t)sign;
    uint32_t q = m >> shift, rem = m & ((1u << shift) - 1u), half = 1u << (shift - 1);
    if (rem > half || (rem == half && (q & 1u))) q++;
    if (eh >= 1) return (uint16_t)(sign | (((uint32_t)(eh - 1) << 10) + q));
    return (uint16_t)(sign | q);
}
static uint16_t hw_f16(uint32_t b) { _Float16 h = (_Float16)u2f(b); uint16_t u; memcpy(&u, &h, 2); return u; }

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(void) {
    uint64_t mismatch = 0, nan_in = 0, to_inf = 0, to_zero = 0, to_sub = 0;
    uint32_t first_bad = 0;
    double t0 = now();
    uint32_t b = 0;
    do {
        uint16_t s = f32_to_f16(b), h = hw_f16(b);
        if (s != h) { if (!mismatch) first_bad = b; mismatch++; }
        if ((b & 0x7FFFFFFFu) > 0x7F800000u) nan_in++;
        else if ((h & 0x7FFFu) == 0x7C00u) to_inf++;
        else if ((h & 0x7FFFu) == 0) to_zero++;
        else if ((h & 0x7C00u) == 0) to_sub++;
    } while (++b != 0);
    double t1 = now();

    /* 각각 따로 시간 재기 (결과를 xor 로 모아 최적화로 사라지지 않게) */
    uint32_t acc = 0; b = 0;
    double t2 = now();
    do { acc ^= f32_to_f16(b); } while (++b != 0);
    double t3 = now();
    do { acc ^= hw_f16(b); } while (++b != 0);
    double t4 = now();

    printf("all 2^32 float bit patterns: sw vs hw (FCVT) mismatches = %llu", (unsigned long long)mismatch);
    if (mismatch) printf(" (first 0x%08X)", first_bad);
    printf("\n  NaN inputs %llu, ->Inf %llu, ->±0 %llu, ->FP16 subnormal %llu\n",
           (unsigned long long)nan_in, (unsigned long long)to_inf, (unsigned long long)to_zero, (unsigned long long)to_sub);
    printf("  compare loop %.2f s | sw only %.2f s (%.2f ns/conv) | hw only %.2f s (%.2f ns/conv) | acc=%04x\n",
           t1 - t0, t3 - t2, (t3 - t2) * 1e9 / 4294967296.0, t4 - t3, (t4 - t3) * 1e9 / 4294967296.0, acc & 0xFFFFu);
    return mismatch ? 1 : 0;
}
