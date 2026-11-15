/* ALGO-B01 추가 연습 — 한 번에 검증
 *   연습 1: 최소제곱 기울기 — 정확한 n^2, n log n, (n + 상수) 데이터에서 무엇이 나오나
 *   연습 2: 시간 대신 연산 횟수 세기 — 쌍 비교 n(n-1)/2, qsort 비교 함수 호출 수 / (n log2 n)
 *   연습 3: 해시의 최악 — 어떤 고정 해시든 모든 키가 한 칸에 모이는 입력이 있다 → 탐사 n(n-1)/2
 *   연습 4: O(1) 메모리, 읽기 전용 — n+1 개 값이 [1, n] 일 때 중복 찾기 (Floyd 사이클 찾기)
 */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x1234567u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}
static uint32_t fmix32(uint32_t h) {
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h;
}

/* ---------------- 연습 1: 기울기 ---------------- */
/* y = c + p*x 를 최소제곱으로 맞춘 p. x = log n, y = log t. */
static double fit_slope(const double *x, const double *y, int m) {
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < m; i++) { sx += x[i]; sy += y[i]; sxx += x[i] * x[i]; sxy += x[i] * y[i]; }
    return (m * sxy - sx * sy) / (m * sxx - sx * sx);
}
#define NN 13
static const double NS[NN] = { 1e3, 2e3, 5e3, 1e4, 2e4, 5e4, 1e5, 2e5, 5e5, 1e6, 2e6, 5e6, 1e7 };
static double slope_of(double (*f)(double), int from) {
    double x[NN], y[NN];
    int m = 0;
    for (int i = from; i < NN; i++) { x[m] = log(NS[i]); y[m] = log(f(NS[i])); m++; }
    return fit_slope(x, y, m);
}
static double f_sq(double n)      { return 3.0 * n * n; }
static double f_nlogn(double n)   { return n * log2(n); }
static double f_n_plus(double n)  { return 20.0 * n + 50000.0; }   /* 고정 오버헤드 50 µs + 20 ns/원소 */

/* ---------------- 연습 2: 연산 횟수 ---------------- */
static uint64_t g_cmp_calls;
static int cmp_u32_count(const void *pa, const void *pb) {
    g_cmp_calls++;
    uint32_t x = *(const uint32_t *)pa, y = *(const uint32_t *)pb;
    return (x > y) - (x < y);
}
static uint64_t pairwise_compares(const uint32_t *a, size_t n, bool *found) {
    uint64_t c = 0;
    *found = false;
    for (size_t i = 0; i + 1 < n; i++)
        for (size_t j = i + 1; j < n; j++) {
            c++;
            if (a[i] == a[j]) { *found = true; return c; }
        }
    return c;
}

/* ---------------- 연습 3: 해시 최악 ---------------- */
#define A_FIB 2654435769u
/* 홀수 a 의 2^32 위 역원: Newton 반복 x <- x(2 - a x), 정확한 비트 수가 매번 2배 */
static uint32_t inv_odd(uint32_t a) {
    uint32_t x = a;                      /* a*a ≡ 1 (mod 8) → 3비트 정확 */
    for (int i = 0; i < 4; i++) x *= 2u - a * x;   /* 3→6→12→24→48 비트 */
    return x;
}
static uint32_t h_fib(uint32_t x, unsigned k) { return (x * A_FIB) >> (32u - k); }
static uint32_t h_mask(uint32_t x, unsigned k) { return x & ((1u << k) - 1u); }
/* 선형 탐사로 키 n 개(0 아님, 서로 다름)를 넣으며 "이미 찬 칸을 본 횟수" 를 센다 */
static uint64_t insert_probes(const uint32_t *keys, size_t n, uint32_t *slot, unsigned k,
                              uint32_t (*h)(uint32_t, unsigned)) {
    uint32_t mask = (1u << k) - 1u;
    uint64_t probes = 0;
    memset(slot, 0, ((size_t)1 << k) * sizeof *slot);
    for (size_t j = 0; j < n; j++) {
        uint32_t i = h(keys[j], k);
        while (slot[i] != 0) { probes++; i = (i + 1) & mask; }
        slot[i] = keys[j];
    }
    return probes;
}

/* ---------------- 연습 4: Floyd ---------------- */
/* a[0..n] (n+1 개), 모든 값 ∈ [1, n], 반복되는 값은 정확히 하나(여러 번 나와도 됨). 배열은 읽기만. */
static uint32_t find_dup_floyd(const uint32_t *a, size_t n) {
    (void)n;
    uint32_t slow = a[0], fast = a[a[0]];
    while (slow != fast) { slow = a[slow]; fast = a[a[fast]]; }
    slow = 0;
    while (slow != fast) { slow = a[slow]; fast = a[fast]; }
    return slow;
}

static uint32_t BUF[1u << 20];
static uint8_t SEEN[1u << 16];

int main(void) {
    /* 연습 1 */
    double p_sq = slope_of(f_sq, 0), p_nl = slope_of(f_nlogn, 0), p_nl4 = slope_of(f_nlogn, 3);
    double p_np = slope_of(f_n_plus, 0), p_np_small = 0;
    {
        double x[4], y[4];
        for (int i = 0; i < 4; i++) { x[i] = log(NS[i]); y[i] = log(f_n_plus(NS[i])); }
        p_np_small = fit_slope(x, y, 4);
    }
    printf("ex1 slopes: 3n^2 %.4f | n log n all %.4f, n>=1e4 %.4f | 20n+50000 all %.4f, n<=1e4 %.4f\n",
           p_sq, p_nl, p_nl4, p_np, p_np_small);
    CHECK(fabs(p_sq - 2.0) < 1e-9);                        /* 상수 배는 기울기에 안 나온다 */
    CHECK(p_nl > 1.07 && p_nl < 1.10);                     /* n log n 은 '1.1 정도' 로 보인다 */
    CHECK(p_nl4 < p_nl);                                   /* n 이 클수록 1 에 가까워진다 */
    CHECK(p_np < 1.0 && p_np_small < 0.6);                 /* 고정 오버헤드는 작은 n 의 기울기를 깎는다 */

    /* 연습 2 */
    {
        bool found;
        for (size_t i = 0; i < 3000; i++) BUF[i] = fmix32((uint32_t)i * 0x9E3779B1u + 1u);
        CHECK(pairwise_compares(BUF, 3000, &found) == 3000ull * 2999ull / 2ull && !found);
        BUF[2999] = BUF[2998];                              /* 마지막 쌍만 같으면 거의 전부 본다 */
        CHECK(pairwise_compares(BUF, 3000, &found) == 3000ull * 2999ull / 2ull && found);
        BUF[1] = BUF[0];                                    /* 첫 쌍이 같으면 1번 */
        CHECK(pairwise_compares(BUF, 3000, &found) == 1 && found);

        static const size_t qn[] = { 1000, 10000, 100000, 1000000 };
        bool ratio_ok = true;
        for (int t = 0; t < 4; t++) {
            size_t n = qn[t];
            for (size_t i = 0; i < n; i++) BUF[i] = fmix32((uint32_t)i * 0x9E3779B1u + 7u);
            g_cmp_calls = 0;
            qsort(BUF, n, sizeof BUF[0], cmp_u32_count);
            double r = (double)g_cmp_calls / ((double)n * log2((double)n));
            printf("ex2 qsort n=%7zu: comparator calls %10llu = %.3f * n log2 n\n", n,
                   (unsigned long long)g_cmp_calls, r);
            if (r < 0.8 || r > 1.3) ratio_ok = false;
            for (size_t i = 1; i < n; i++) if (BUF[i - 1] >= BUF[i]) ratio_ok = false;
        }
        CHECK(ratio_ok);                                    /* 호출 수 / (n log2 n) 이 거의 일정 */
    }

    /* 연습 3 */
    {
        const unsigned k = 12;                              /* cap 4096 */
        const size_t n = 2000;                              /* 적재율 ≈ 0.49 */
        static uint32_t kmask[2000], kfib[2000], krand[2000];
        uint32_t inv = inv_odd(A_FIB);
        CHECK(inv * A_FIB == 1u);
        for (size_t i = 0; i < n; i++) {
            kmask[i] = (uint32_t)(i + 1) << k;              /* 하위 k 비트가 모두 0 → x & mask = 0 */
            kfib[i]  = (uint32_t)(i + 1) * inv;             /* x * A = i+1 → 상위 k 비트 = 0 */
            krand[i] = fmix32((uint32_t)i + 99u) | 1u;
        }
        uint64_t worst = (uint64_t)n * (n - 1) / 2;
        uint64_t p1 = insert_probes(kmask, n, BUF, k, h_mask);
        uint64_t p2 = insert_probes(kfib,  n, BUF, k, h_fib);
        uint64_t p3 = insert_probes(kmask, n, BUF, k, h_fib);
        uint64_t p4 = insert_probes(kfib,  n, BUF, k, h_mask);
        uint64_t p5 = insert_probes(krand, n, BUF, k, h_fib);
        printf("ex3 probes (n=%zu, cap=4096): mask-hash on mask-adversary %llu, fib-hash on fib-adversary %llu "
               "(= n(n-1)/2 = %llu); fib on mask-adv %llu, mask on fib-adv %llu, fib on random %llu\n", n,
               (unsigned long long)p1, (unsigned long long)p2, (unsigned long long)worst,
               (unsigned long long)p3, (unsigned long long)p4, (unsigned long long)p5);
        CHECK(p1 == worst && p2 == worst);                  /* 각자의 적에게는 O(n^2) */
        CHECK(p3 < 2 * n && p5 < 2 * n);                    /* 다른 입력엔 탐사가 n 정도 */
    }

    /* 연습 4 */
    {
        static const uint32_t t1[] = { 1, 3, 4, 2, 2 };
        static const uint32_t t2[] = { 3, 1, 3, 4, 2 };
        static const uint32_t t3[] = { 1, 1 };
        static const uint32_t t4[] = { 2, 2, 2, 2, 2 };
        CHECK(find_dup_floyd(t1, 4) == 2 && find_dup_floyd(t2, 4) == 3);
        CHECK(find_dup_floyd(t3, 1) == 1 && find_dup_floyd(t4, 4) == 2);
        int bad = 0;
        for (int t = 0; t < 2000; t++) {
            size_t n = 1 + xorshift32() % 3000;
            uint32_t d = 1 + xorshift32() % (uint32_t)n;
            size_t c = 2 + xorshift32() % (n < 4 ? 1 : 4);           /* d 가 나오는 횟수 */
            if (c > n + 1) c = n + 1;
            /* 나머지 n+1-c 칸: [1,n]\{d} 에서 서로 다른 값 */
            size_t m = 0;
            for (uint32_t v = 1; v <= n; v++) if (v != d) BUF[m++] = v;
            for (size_t i = m; i > 1; i--) { size_t j = xorshift32() % i; uint32_t tmp = BUF[i - 1]; BUF[i - 1] = BUF[j]; BUF[j] = tmp; }
            m = n + 1 - c;
            for (size_t i = 0; i < c; i++) BUF[m + i] = d;
            for (size_t i = n + 1; i > 1; i--) { size_t j = xorshift32() % i; uint32_t tmp = BUF[i - 1]; BUF[i - 1] = BUF[j]; BUF[j] = tmp; }
            memset(SEEN, 0, n + 1);                                  /* 기준: 표시 배열 */
            uint32_t want = 0;
            for (size_t i = 0; i <= n; i++) { if (SEEN[BUF[i]]) { want = BUF[i]; break; } SEEN[BUF[i]] = 1; }
            if (find_dup_floyd(BUF, n) != want) bad++;
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
