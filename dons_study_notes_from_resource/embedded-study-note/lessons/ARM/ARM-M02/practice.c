/* ARM-M02 추가 연습 — 한 번에 검증
 *   연습 1  sub128 (SUBS/SBC) + borrow 출력 (CSET lo: ARM 의 C 는 "빌림 없음")
 *   연습 2  mul_wide: MUL + UMULH (lo 는 early-clobber), smul_wide: SMULH
 *   연습 3  bn_add: n 워드 다배정밀 덧셈을 asm "하나" 의 루프로 (SUB/CBNZ 는 플래그를 안 건드린다)
 *   연습 4  popcount64: "w" 스크래치 + v 레지스터 배열 표기 (%[v].8b), CNT/ADDV
 *   연습 5  add_ovf_s64: asm goto + B.VS (출력 + 라벨 + "cc")
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/ARM-M02_practice && /tmp/ARM-M02_practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(__aarch64__)
#error "ARM-M02 needs an AArch64 host"
#endif

/* 연습 1: (a - b) mod 2^128, 반환 = 빌림(borrow) 1/0 */
static inline unsigned sub128(uint64_t a_hi, uint64_t a_lo, uint64_t b_hi, uint64_t b_lo, uint64_t *hi, uint64_t *lo) {
    uint64_t h, l;
    uint32_t borrow;
    __asm__("subs %x[l], %x[al], %x[bl]\n\t"
            "sbcs %x[h], %x[ah], %x[bh]\n\t"
            "cset %w[bw], lo"                       /* lo == C 클리어 == 빌림 발생 */
            : [l] "=&r"(l), [h] "=&r"(h), [bw] "=r"(borrow)
            : [al] "r"(a_lo), [bl] "r"(b_lo), [ah] "r"(a_hi), [bh] "r"(b_hi)
            : "cc");
    *hi = h;
    *lo = l;
    return borrow;
}

/* 연습 2: 64x64 → 128 (부호 없음 / 있음). mul 이 lo 를 쓴 뒤 umulh 가 a, b 를 다시 읽는다 → lo 는 "=&r". */
static inline uint64_t mul_wide(uint64_t a, uint64_t b, uint64_t *hi) {
    uint64_t l, h;
    __asm__("mul   %x[l], %x[a], %x[b]\n\t"
            "umulh %x[h], %x[a], %x[b]"
            : [l] "=&r"(l), [h] "=r"(h) : [a] "r"(a), [b] "r"(b));
    *hi = h;
    return l;
}
static inline uint64_t smul_wide(int64_t a, int64_t b, int64_t *hi) {
    uint64_t l;
    int64_t h;
    __asm__("mul   %x[l], %x[a], %x[b]\n\t"
            "smulh %x[h], %x[a], %x[b]"
            : [l] "=&r"(l), [h] "=r"(h) : [a] "r"(a), [b] "r"(b));
    *hi = h;
    return l;
}

/* 연습 3: r[0..n) = a + b (리틀 엔디언 워드 순서), 반환 = 최종 캐리. 캐리는 루프 내내 C 플래그에 산다. */
static inline unsigned bn_add(uint64_t *r, const uint64_t *a, const uint64_t *b, uint64_t n) {
    uint64_t t0, t1;
    uint32_t carry;
    __asm__ volatile(
        "   cmn  xzr, xzr\n"                        /* C = 0 (0 + 0 은 캐리 없음) */
        "   cbz  %x[n], 2f\n"
        "1: ldr  %x[t0], [%x[a]], #8\n"
        "   ldr  %x[t1], [%x[b]], #8\n"
        "   adcs %x[t0], %x[t0], %x[t1]\n"
        "   str  %x[t0], [%x[r]], #8\n"
        "   sub  %x[n], %x[n], #1\n"                /* subs 가 아니다: 플래그 보존 */
        "   cbnz %x[n], 1b\n"                       /* cbnz 도 플래그를 읽지도 쓰지도 않는다 */
        "2: cset %w[c], cs\n"
        : [t0] "=&r"(t0), [t1] "=&r"(t1), [c] "=r"(carry), [r] "+r"(r), [a] "+r"(a), [b] "+r"(b), [n] "+r"(n)
        :
        : "cc", "memory");
    return carry;
}

/* 연습 4: 비트 수 세기. 스크래치 FP/SIMD 레지스터를 "=&w" 로 받고, %d / %b / %s 와 .8b 배열 표기 */
static inline unsigned popcount64(uint64_t x) {
    double v;                                       /* 타입은 아무거나: "w" 레지스터를 하나 빌리는 용도 */
    uint32_t r;
    __asm__("fmov %d[v], %x[x]\n\t"
            "cnt  %[v].8b, %[v].8b\n\t"
            "addv %b[v], %[v].8b\n\t"
            "fmov %w[r], %s[v]"
            : [v] "=&w"(v), [r] "=r"(r) : [x] "r"(x));
    return r;
}

/* 연습 5: 부호 있는 덧셈 오버플로를 B.VS 로 바로 분기 (asm goto, 출력 포함) */
static inline bool add_ovf_s64(int64_t a, int64_t b, int64_t *res) {
    int64_t s;
    __asm__ goto("adds %x[s], %x[a], %x[b]\n\t"
                 "b.vs %l[ovf]"
                 : [s] "=r"(s) : [a] "r"(a), [b] "r"(b) : "cc" : ovf);
    *res = s;
    return false;
ovf:
    return true;
}

/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)
static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint64_t xorshift64(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static unsigned ref_pop(uint64_t x) { unsigned c = 0; while (x) { x &= x - 1; c++; } return c; }
static unsigned ref_bn_add(uint64_t *r, const uint64_t *a, const uint64_t *b, size_t n) {
    unsigned c = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t s = a[i] + b[i];
        unsigned c1 = s < a[i];
        uint64_t s2 = s + c;
        unsigned c2 = s2 < s;
        r[i] = s2;
        c = c1 | c2;
    }
    return c;
}

int main(void) {
    uint64_t hi, lo;
    /* 연습 1 */
    CHECK(sub128(1, 0, 0, 1, &hi, &lo) == 0 && hi == 0 && lo == ~0ull);           /* 2^64 - 1 */
    CHECK(sub128(0, 0, 0, 1, &hi, &lo) == 1 && hi == ~0ull && lo == ~0ull);       /* 0 - 1 = 2^128-1, 빌림 */
    CHECK(sub128(5, 5, 5, 5, &hi, &lo) == 0 && hi == 0 && lo == 0);
    int bad = 0;
    for (int i = 0; i < 100000; i++) {
        uint64_t ah = xorshift64(), al = xorshift64(), bh = (i & 3) ? xorshift64() : ah, bl = xorshift64();
        unsigned __int128 A = ((unsigned __int128)ah << 64) | al, B = ((unsigned __int128)bh << 64) | bl, D = A - B;
        unsigned bw = sub128(ah, al, bh, bl, &hi, &lo);
        if (hi != (uint64_t)(D >> 64) || lo != (uint64_t)D || bw != (unsigned)(A < B)) bad++;
    }
    CHECK(bad == 0);
    /* 연습 2 */
    CHECK(mul_wide(~0ull, ~0ull, &hi) == 1u && hi == ~0ull - 1);
    int64_t shi;
    CHECK(smul_wide(-1, 1, &shi) == ~0ull && shi == -1);
    CHECK(smul_wide(INT64_MIN, INT64_MIN, &shi) == 0u && shi == INT64_C(0x4000000000000000));
    bad = 0;
    for (int i = 0; i < 100000; i++) {
        uint64_t a = xorshift64(), b = xorshift64() >> (i & 63);
        unsigned __int128 p = (unsigned __int128)a * b;
        __int128 sp = (__int128)(int64_t)a * (int64_t)b;
        if (mul_wide(a, b, &hi) != (uint64_t)p || hi != (uint64_t)(p >> 64)) bad++;
        if (smul_wide((int64_t)a, (int64_t)b, &shi) != (uint64_t)sp || shi != (int64_t)(sp >> 64)) bad++;
    }
    CHECK(bad == 0);
    /* 연습 3 */
    uint64_t A[9], B[9], R[9], E[9];
    for (int i = 0; i < 9; i++) { A[i] = ~0ull; B[i] = 0; }
    B[0] = 1;                                                     /* 캐리가 9 워드를 끝까지 전파 */
    CHECK(bn_add(R, A, B, 9) == 1u && R[0] == 0 && R[8] == 0);
    R[0] = 0xDEAD;
    CHECK(bn_add(R, A, B, 0) == 0u && R[0] == 0xDEAD);            /* n = 0: 아무것도 안 쓴다 */
    bad = 0;
    for (int t = 0; t < 20000; t++) {
        uint64_t n = (uint64_t)(t % 9);
        for (int i = 0; i < 9; i++) {
            A[i] = (t & 1) ? ~0ull - (xorshift64() & 3) : xorshift64();  /* 홀수 회차: 캐리가 많이 나는 값 */
            B[i] = xorshift64();
        }
        unsigned c_ref = ref_bn_add(E, A, B, n), c = bn_add(R, A, B, n);
        if (c != c_ref || memcmp(R, E, n * sizeof(uint64_t)) != 0) bad++;
    }
    CHECK(bad == 0);
    /* 연습 4 */
    CHECK(popcount64(0) == 0u && popcount64(~0ull) == 64u && popcount64(0x8000000000000001ull) == 2u);
    bad = 0;
    for (int i = 0; i < 100000; i++) { uint64_t x = xorshift64(); if (popcount64(x) != ref_pop(x)) bad++; }
    CHECK(bad == 0);
    /* 연습 5 */
    int64_t s;
    CHECK(!add_ovf_s64(1, 2, &s) && s == 3);
    CHECK(add_ovf_s64(INT64_MAX, 1, &s) && add_ovf_s64(INT64_MIN, -1, &s));
    CHECK(!add_ovf_s64(INT64_MIN, INT64_MAX, &s) && s == -1);
    bad = 0;
    for (int i = 0; i < 100000; i++) {
        int64_t a = (int64_t)xorshift64(), b = (int64_t)xorshift64(), r1 = 0, r2 = 0;
        bool o1 = add_ovf_s64(a, b, &r1), o2 = __builtin_add_overflow(a, b, &r2);
        if (o1 != o2 || (!o1 && r1 != r2)) bad++;
    }
    CHECK(bad == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
