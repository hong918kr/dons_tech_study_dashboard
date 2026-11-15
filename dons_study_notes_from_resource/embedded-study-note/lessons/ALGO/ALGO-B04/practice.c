/* ALGO-B04 추가 연습 — 판정 함수 + 답 이분 탐색, 네 가지 모양
 *   연습 1  최소 처리 속도 (first true, 판정 = Σ ceil(b_i / k) <= H)            — Koko Eating Bananas 모양
 *   연습 2  최소 간격의 최대화 (last true, 판정 = 그리디로 k 개를 간격 >= g 로 고를 수 있나)
 *   연습 3  정수 세제곱근 floor(cbrt(x)), uint64, 곱 넘침 없이
 *   연습 4  k 번째로 작은 쌍 차이 (판정 = "차이 <= d 인 쌍이 k 개 이상", 투 포인터로 셈) — 값 자체를 탐색
 * 각 연습은 브루트포스와 고정 시드 xorshift32 랜덤 대조 + 손 계산 예.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* ---------------- 연습 1: 최소 처리 속도 ---------------- */
/* 배치 b[i] 개 샘플. 태스크는 틱마다 한 배치에서 최대 k 개 처리(배치를 넘나들지 않음).
 * H 틱 안에 다 끝내는 최소 k (>= 1). n == 0 → 1. 불가능(H < n) → 0. */
static uint64_t ticks_at(const uint32_t *b, size_t n, uint64_t k) {
    uint64_t t = 0;
    for (size_t i = 0; i < n; i++) t += (b[i] + k - 1) / k;
    return t;
}
static uint64_t min_rate(const uint32_t *b, size_t n, uint64_t H) {
    if (n == 0) return 1;
    if (H < n) return 0;                               /* 배치마다 최소 1 틱 */
    uint64_t lo = 1, hi = 1;
    for (size_t i = 0; i < n; i++) if (b[i] > hi) hi = b[i];   /* k = max 면 배치당 1 틱 → n <= H 틱 */
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2;
        if (ticks_at(b, n, mid) <= H) hi = mid; else lo = mid + 1;
    }
    return lo;
}
static uint64_t min_rate_brute(const uint32_t *b, size_t n, uint64_t H) {
    if (n == 0) return 1;
    if (H < n) return 0;
    for (uint64_t k = 1;; k++) if (ticks_at(b, n, k) <= H) return k;
}

/* ---------------- 연습 2: 최소 간격의 최대화 ---------------- */
/* 정렬된 후보 시각 p[0..n-1] 중 k 개(2 <= k <= n)를 골라 "이웃 간격의 최솟값"을 최대로. */
static bool can_place(const uint32_t *p, size_t n, size_t k, uint32_t g) {
    size_t cnt = 1; uint32_t last = p[0];
    for (size_t i = 1; i < n && cnt < k; i++)
        if (p[i] - last >= g) { cnt++; last = p[i]; }
    return cnt >= k;
}
static uint32_t max_min_gap(const uint32_t *p, size_t n, size_t k) {
    uint32_t lo = 0, hi = (p[n - 1] - p[0]) / (uint32_t)(k - 1);   /* 간격 k-1 개의 합 <= 전체 폭 */
    while (lo < hi) {                                  /* last true: g = 0 은 항상 true */
        uint32_t mid = lo + (hi - lo + 1) / 2;         /* 위쪽 mid */
        if (can_place(p, n, k, mid)) lo = mid; else hi = mid - 1;
    }
    return lo;
}
static uint32_t max_min_gap_brute(const uint32_t *p, size_t n, size_t k) {    /* n <= 14 부분집합 전수 */
    uint32_t best = 0;
    for (uint32_t m = 0; m < (1u << n); m++) {
        size_t c = 0; for (uint32_t t = m; t; t &= t - 1) c++;
        if (c != k) continue;
        uint32_t mn = UINT32_MAX; int prev = -1;
        for (size_t i = 0; i < n; i++) if (m >> i & 1u) { if (prev >= 0 && p[i] - p[prev] < mn) mn = p[i] - p[prev]; prev = (int)i; }
        if (mn > best) best = mn;
    }
    return best;
}

/* ---------------- 연습 3: 정수 세제곱근 ---------------- */
/* 2642245^3 = 18446724184312856125 <= UINT64_MAX < 2642246^3 → 답 <= 2642245, hi 를 거기로 자르면 mid^3 이 안 넘침 */
static uint64_t icbrt64(uint64_t x) {
    uint64_t lo = 0, hi = x < 2642245u ? x : 2642245u;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo + 1) / 2;
        if (mid * mid * mid <= x) lo = mid; else hi = mid - 1;
    }
    return lo;
}
static bool icbrt_ok(uint64_t x, uint64_t s) {         /* s^3 <= x < (s+1)^3, 넘침은 나눗셈으로 피함 */
    if (s > 2642245u || s * s * s > x) return false;
    uint64_t t = s + 1;                                /* t^3 > x  ⇔  t*t > x / t  (정수 나눗셈, t >= 1) */
    return t * t > x / t;
}

/* ---------------- 연습 4: k 번째로 작은 쌍 차이 ---------------- */
static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return (x > y) - (x < y); }
static uint64_t pairs_within(const uint32_t *a, size_t n, uint32_t d) {   /* a 정렬됨: a[j] - a[i] <= d 인 i < j 쌍의 수 */
    uint64_t c = 0; size_t i = 0;
    for (size_t j = 0; j < n; j++) {
        while (a[j] - a[i] > d) i++;
        c += j - i;
    }
    return c;
}
/* a 를 제자리 정렬. 1 <= k <= n(n-1)/2 */
static uint32_t kth_pair_diff(uint32_t *a, size_t n, uint64_t k) {
    qsort(a, n, sizeof a[0], cmp_u32);
    uint32_t lo = 0, hi = a[n - 1] - a[0];
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (pairs_within(a, n, mid) >= k) hi = mid; else lo = mid + 1;
    }
    return lo;
}
static uint32_t kth_pair_diff_brute(const uint32_t *a, size_t n, uint64_t k) {
    static uint32_t d[64 * 63 / 2]; size_t m = 0;
    for (size_t i = 0; i < n; i++) for (size_t j = i + 1; j < n; j++) d[m++] = a[i] > a[j] ? a[i] - a[j] : a[j] - a[i];
    qsort(d, m, sizeof d[0], cmp_u32);
    return d[k - 1];
}

/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static uint64_t xs64(void) { uint64_t h = xs32(); return (h << 32) | xs32(); }

int main(void) {
    /* 연습 1 */
    const uint32_t b1[] = {3, 6, 7, 11};
    CHECK(min_rate(b1, 4, 8) == 4);                    /* 1+2+2+3 = 8 */
    const uint32_t b2[] = {30, 11, 23, 4, 20};
    CHECK(min_rate(b2, 5, 5) == 30 && min_rate(b2, 5, 6) == 23);
    CHECK(min_rate(b1, 4, 3) == 0 && min_rate(b1, 0, 1) == 1);
    int bad1 = 0;
    for (int t = 0; t < 5000; t++) {
        uint32_t b[16]; size_t n = 1 + xs32() % 16;
        for (size_t i = 0; i < n; i++) b[i] = 1 + xs32() % 300;
        uint64_t H = n + xs32() % 200;
        if (min_rate(b, n, H) != min_rate_brute(b, n, H)) bad1++;
    }
    CHECK(bad1 == 0);

    /* 연습 2 */
    const uint32_t p1[] = {1, 2, 4, 8, 9};
    CHECK(max_min_gap(p1, 5, 3) == 3);                 /* 1, 4, 8 (또는 1, 4, 9) */
    CHECK(max_min_gap(p1, 5, 2) == 8 && max_min_gap(p1, 5, 5) == 1);
    const uint32_t p2[] = {5, 5, 5};
    CHECK(max_min_gap(p2, 3, 2) == 0);
    int bad2 = 0;
    for (int t = 0; t < 4000; t++) {
        uint32_t p[14]; size_t n = 2 + xs32() % 13, k = 2 + xs32() % (n - 1);
        for (size_t i = 0; i < n; i++) p[i] = xs32() % 1000;
        qsort(p, n, sizeof p[0], cmp_u32);
        if (max_min_gap(p, n, k) != max_min_gap_brute(p, n, k)) bad2++;
    }
    CHECK(bad2 == 0);

    /* 연습 3 */
    CHECK(icbrt64(0) == 0 && icbrt64(1) == 1 && icbrt64(7) == 1 && icbrt64(8) == 2 && icbrt64(26) == 2 && icbrt64(27) == 3);
    CHECK(icbrt64(UINT64_MAX) == 2642245u);
    CHECK(icbrt64(18446724184312856125ull) == 2642245u && icbrt64(18446724184312856124ull) == 2642244u);
    int bad3 = 0;
    for (int t = 0; t < 300000; t++) {
        uint64_t x = xs64() >> (xs32() % 64);
        if (!icbrt_ok(x, icbrt64(x))) bad3++;
        uint64_t k = 1 + xs32() % 2642245u, c = k * k * k;       /* 세제곱 경계 양옆 */
        if (icbrt64(c) != k || icbrt64(c - 1) != k - 1) bad3++;
    }
    { uint64_t s = 0; for (uint64_t x = 0; x <= 2000000; x++) { while ((s + 1) * (s + 1) * (s + 1) <= x) s++; if (icbrt64(x) != s) bad3++; } }
    CHECK(bad3 == 0);

    /* 연습 4 */
    uint32_t a1[] = {1, 3, 1};
    CHECK(kth_pair_diff(a1, 3, 1) == 0);               /* 차이 {0, 2, 2} */
    uint32_t a2[] = {1, 6, 1};
    CHECK(kth_pair_diff(a2, 3, 3) == 5);
    uint32_t a3[] = {100, 40, 70, 10};
    CHECK(kth_pair_diff(a3, 4, 2) == 30 && kth_pair_diff(a3, 4, 6) == 90);
    int bad4 = 0;
    for (int t = 0; t < 3000; t++) {
        uint32_t a[64]; size_t n = 2 + xs32() % 63;
        for (size_t i = 0; i < n; i++) a[i] = (t & 1) ? xs32() % 50 : xs32();
        uint64_t m = (uint64_t)n * (n - 1) / 2, k = 1 + xs32() % m;
        uint32_t want = kth_pair_diff_brute(a, n, k);
        if (kth_pair_diff(a, n, k) != want) bad4++;
    }
    CHECK(bad4 == 0);

    printf("random mismatches: ex1=%d ex2=%d ex3=%d ex4=%d\n", bad1, bad2, bad3, bad4);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
