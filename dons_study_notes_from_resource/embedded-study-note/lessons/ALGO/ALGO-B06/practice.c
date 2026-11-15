/* ALGO-B06 추가 연습 — 한 번에 검증
 * 연습 1: quickselect (k번째 작은 값, 재귀 없음)       — ADC 블록 중앙값을 정렬 없이
 * 연습 2: Dutch national flag (값 0/1/2 한 번 훑기)     — 우선순위 3단계 큐 정리
 * 연습 3: 작은 구간은 삽입 정렬 (cutoff)               — 재귀 호출 수가 얼마나 주나
 * 연습 4: median-of-3 킬러 입력(McIlroy) + 인트로소트   — 깊이 예산이 끝나면 힙정렬
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_seed = 0xB06u;
static uint32_t xs32(void) { g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5; return g_seed; }
static int cmp_int(const void *x, const void *y) { int a = *(const int *)x, b = *(const int *)y; return (a > b) - (a < b); }
static void swp(int *a, int i, int j) { int t = a[i]; a[i] = a[j]; a[j] = t; }
static int ilog2_floor(uint32_t n) { int k = -1; while (n) { n >>= 1; k++; } return k; }

/* Hoare + median-of-3 (본문 Part B 와 같은 규약: lo <= j < hi, a[lo..j] <= p <= a[j+1..hi]) */
static int m3_hoare(int *a, int lo, int hi) {
    int mid = lo + (hi - lo) / 2;
    if (a[mid] < a[lo]) swp(a, mid, lo);
    if (a[hi] < a[mid]) { swp(a, hi, mid); if (a[mid] < a[lo]) swp(a, mid, lo); }
    swp(a, lo, mid);
    int p = a[lo], i = lo - 1, j = hi + 1;
    for (;;) {
        do { i++; } while (a[i] < p);
        do { j--; } while (p < a[j]);
        if (i >= j) return j;
        swp(a, i, j);
    }
}

/* ---------- 연습 1: quickselect ---------- */
/* a[0..n-1] 을 재배치해 k번째(0부터) 작은 값을 돌려준다. 0 <= k < n. 스택 O(1). */
static int quickselect(int *a, int n, int k) {
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int j = m3_hoare(a, lo, hi);
        if (k <= j) hi = j; else lo = j + 1;      /* k 가 있는 쪽만 남긴다 */
    }
    return a[k];
}

/* ---------- 연습 2: Dutch national flag ---------- */
/* 값이 0/1/2 뿐인 배열을 한 번 훑어 정렬. 반환 = 루프 반복 횟수 (<= n) */
static int dnf_sort(int *a, int n) {
    int lt = 0, i = 0, gt = n - 1, steps = 0;
    while (i <= gt) {
        steps++;
        if (a[i] == 0)      swp(a, lt++, i++);
        else if (a[i] == 2) swp(a, i, gt--);
        else                i++;
    }
    return steps;
}

/* ---------- 연습 3: cutoff 이하 구간은 삽입 정렬 ---------- */
typedef struct { long cmp; long calls; } hy_stats_t;
static void ins_sort(int *a, int lo, int hi, hy_stats_t *st) {
    for (int i = lo + 1; i <= hi; i++) {
        int x = a[i], j = i - 1;
        while (j >= lo && (st->cmp++, x < a[j])) { a[j + 1] = a[j]; j--; }
        a[j + 1] = x;
    }
}
static int m3_hoare_cnt(int *a, int lo, int hi, hy_stats_t *st) {
    int mid = lo + (hi - lo) / 2;
    st->cmp += 2;
    if (a[mid] < a[lo]) swp(a, mid, lo);
    if (a[hi] < a[mid]) { st->cmp++; swp(a, hi, mid); if (a[mid] < a[lo]) swp(a, mid, lo); }
    swp(a, lo, mid);
    int p = a[lo], i = lo - 1, j = hi + 1;
    for (;;) {
        do { i++; } while (st->cmp++, a[i] < p);
        do { j--; } while (st->cmp++, p < a[j]);
        if (i >= j) return j;
        swp(a, i, j);
    }
}
static void hy_rec(int *a, int lo, int hi, int cutoff, hy_stats_t *st) {
    st->calls++;
    while (hi - lo + 1 > cutoff && lo < hi) {
        int j = m3_hoare_cnt(a, lo, hi, st);
        if (j - lo < hi - j) { hy_rec(a, lo, j, cutoff, st); lo = j + 1; }
        else                 { hy_rec(a, j + 1, hi, cutoff, st); hi = j; }
    }
    if (lo < hi) ins_sort(a, lo, hi, st);
}
static void quicksort_hybrid(int *a, int n, int cutoff, hy_stats_t *st) {
    *st = (hy_stats_t){0, 0};
    if (n >= 2) hy_rec(a, 0, n - 1, cutoff, st);
}

/* ---------- 연습 4: 비교 함수 기반 m3 퀵소트 / 인트로소트 / McIlroy 적대자 ---------- */
typedef int (*cmp_fn)(int x, int y);              /* <0, 0, >0 */
static long g_ncmp;
static int cmp_plain(int x, int y) { g_ncmp++; return (x > y) - (x < y); }

static void sift_down(int *a, int base, int start, int end, cmp_fn c) {   /* 최대 힙, 구간 [base, base+end) */
    int r = start;
    for (;;) {
        int ch = 2 * r + 1;
        if (ch >= end) return;
        if (ch + 1 < end && c(a[base + ch], a[base + ch + 1]) < 0) ch++;
        if (c(a[base + r], a[base + ch]) >= 0) return;
        swp(a, base + r, base + ch);
        r = ch;
    }
}
static void heap_sort(int *a, int lo, int hi, cmp_fn c) {
    int n = hi - lo + 1;
    for (int s = n / 2 - 1; s >= 0; s--) sift_down(a, lo, s, n, c);
    for (int end = n - 1; end > 0; end--) { swp(a, lo, lo + end); sift_down(a, lo, 0, end, c); }
}
static int c_hoare(int *a, int lo, int hi, cmp_fn c) {
    int mid = lo + (hi - lo) / 2;
    if (c(a[mid], a[lo]) < 0) swp(a, mid, lo);
    if (c(a[hi], a[mid]) < 0) { swp(a, hi, mid); if (c(a[mid], a[lo]) < 0) swp(a, mid, lo); }
    swp(a, lo, mid);
    int p = a[lo], i = lo - 1, j = hi + 1;
    for (;;) {
        do { i++; } while (c(a[i], p) < 0);
        do { j--; } while (c(p, a[j]) < 0);
        if (i >= j) return j;
        swp(a, i, j);
    }
}
/* budget < 0 이면 무제한 (= 그냥 m3 퀵소트). 파티션 한 번마다 budget 1 소모, 0 이 되면 남은 구간은 힙정렬 */
static int g_heap_fallbacks, g_maxdepth;
static void intro_rec(int *a, int lo, int hi, int budget, cmp_fn c, int depth) {
    if (depth > g_maxdepth) g_maxdepth = depth;
    while (lo < hi) {
        if (budget == 0) { g_heap_fallbacks++; heap_sort(a, lo, hi, c); return; }
        if (budget > 0) budget--;
        int j = c_hoare(a, lo, hi, c);
        if (j - lo < hi - j) { if (lo < j) intro_rec(a, lo, j, budget, c, depth + 1); lo = j + 1; }
        else                 { if (j + 1 < hi) intro_rec(a, j + 1, hi, budget, c, depth + 1); hi = j; }
    }
}
static void m3_quicksort(int *a, int n, cmp_fn c) { g_maxdepth = 0; if (n >= 2) intro_rec(a, 0, n - 1, -1, c, 1); }
static void introsort(int *a, int n, cmp_fn c) { g_maxdepth = 0; if (n >= 2) intro_rec(a, 0, n - 1, 2 * ilog2_floor((uint32_t)n), c, 1); }

/* McIlroy, "A Killer Adversary for Quicksort" (1999) 의 방식: 값을 미리 정하지 않고("gas"),
 * 비교가 일어날 때마다 피벗 후보가 아닌 쪽을 작은 값으로 "얼린다". 끝나면 val[] 이 킬러 입력. */
static int *g_val, g_gas, g_nsolid, g_candidate;
static int cmp_adversary(int x, int y) {
    g_ncmp++;
    if (g_val[x] == g_gas && g_val[y] == g_gas) {
        if (x == g_candidate) g_val[x] = g_nsolid++;
        else                  g_val[y] = g_nsolid++;
    }
    if (g_val[x] == g_gas)      g_candidate = x;
    else if (g_val[y] == g_gas) g_candidate = y;
    return (g_val[x] > g_val[y]) - (g_val[x] < g_val[y]);
}
static long make_killer(int *val, int n) {
    int *idx = malloc((size_t)n * sizeof(int));
    g_val = val; g_gas = n; g_nsolid = 0; g_candidate = -1; g_ncmp = 0;   /* 원문은 gas = n-1. n 이면 얼린 값(0..n-1)과 절대 안 겹친다 */
    for (int i = 0; i < n; i++) { idx[i] = i; val[i] = g_gas; }
    m3_quicksort(idx, n, cmp_adversary);
    free(idx);
    return g_ncmp;
}

int main(void) {
    static int a[100000], b[100000];

    /* 연습 1 */
    {
        int bad = 0;
        for (int t = 0; t < 300; t++) {
            int n = 1 + (int)(xs32() % 200u);
            for (int i = 0; i < n; i++) b[i] = (t % 3 == 0) ? (int)(xs32() % 5u) : (int)xs32();
            if (t % 7 == 0) for (int i = 0; i < n; i++) b[i] = i;          /* 정렬된 입력 */
            int ref[200]; memcpy(ref, b, (size_t)n * sizeof(int));
            qsort(ref, (size_t)n, sizeof(int), cmp_int);
            for (int k = 0; k < n; k++) {
                memcpy(a, b, (size_t)n * sizeof(int));
                int v = quickselect(a, n, k);
                int ok = v == ref[k] && a[k] == v;
                for (int i = 0; i < k; i++) ok &= a[i] <= v;               /* 부산물: 왼쪽 <= a[k] <= 오른쪽 */
                for (int i = k + 1; i < n; i++) ok &= a[i] >= v;
                bad += !ok;
            }
        }
        CHECK(bad == 0);
        int adc[] = {1003, 998, 4095, 1001, 0, 1000, 999};                  /* 스파이크 2개 섞인 7샘플 */
        CHECK(quickselect(adc, 7, 3) == 1000);
        int one[] = {42};
        CHECK(quickselect(one, 1, 0) == 42);
    }

    /* 연습 2 */
    {
        int bad = 0, maxsteps_bad = 0;
        for (int n = 0; n <= 10; n++) {                                     /* {0,1,2}^n 전수: 88573 개 */
            int total = 1;
            for (int i = 0; i < n; i++) total *= 3;
            for (int code = 0; code < total; code++) {
                int c = code, cnt[3] = {0, 0, 0};
                for (int i = 0; i < n; i++) { a[i] = c % 3; cnt[a[i]]++; c /= 3; }
                int steps = dnf_sort(a, n);
                int ok = 1, i = 0;
                for (int v = 0; v < 3; v++) for (int r = 0; r < cnt[v]; r++) ok &= a[i++] == v;
                bad += !ok;
                maxsteps_bad += steps > n;
            }
        }
        CHECK(bad == 0);
        CHECK(maxsteps_bad == 0);                                           /* 원소마다 정확히 한 번 결정 */
    }

    /* 연습 3 */
    {
        const int n = 10000;
        static const int cut[] = {1, 8, 16, 32};
        int bad = 0;
        long calls[4], cmps[4];
        for (int ci = 0; ci < 4; ci++) {
            g_seed = 0xC0FFEEu;
            for (int i = 0; i < n; i++) b[i] = (int)xs32();
            memcpy(a, b, (size_t)n * sizeof(int));
            hy_stats_t st;
            quicksort_hybrid(a, n, cut[ci], &st);
            qsort(b, (size_t)n, sizeof(int), cmp_int);
            bad += memcmp(a, b, (size_t)n * sizeof(int)) != 0;
            calls[ci] = st.calls; cmps[ci] = st.cmp;
            printf("  [연습3] cutoff=%2d  calls=%5ld  comparisons=%6ld\n", cut[ci], st.calls, st.cmp);
        }
        CHECK(bad == 0);
        CHECK(calls[2] * 8 < calls[0]);                                     /* cutoff 16 → 호출 수 1/8 미만 */
        for (int t = 0; t < 300; t++) {                                     /* 작은 n, 중복 많은 입력 */
            int m = (int)(xs32() % 100u);
            for (int i = 0; i < m; i++) b[i] = a[i] = (int)(xs32() % 4u);
            hy_stats_t st;
            quicksort_hybrid(a, m, 16, &st);
            qsort(b, (size_t)m, sizeof(int), cmp_int);
            bad += memcmp(a, b, (size_t)m * sizeof(int)) != 0;
        }
        CHECK(bad == 0);
        (void)cmps;
    }

    /* 연습 4 */
    {
        static const int Ns[] = {1000, 2000, 4000};
        int okq = 1, oki = 1, oksort = 1, same = 1, okdepth = 1;
        for (int t = 0; t < 3; t++) {
            int n = Ns[t];
            long adv = make_killer(b, n);                                   /* b = 킬러 입력 */
            memcpy(a, b, (size_t)n * sizeof(int));
            g_ncmp = 0; m3_quicksort(a, n, cmp_plain);
            long q = g_ncmp;
            int qdepth = g_maxdepth;
            okdepth &= qdepth <= ilog2_floor((uint32_t)n);                 /* 시간은 O(n^2) 이어도 깊이는 log2 n */
            for (int i = 1; i < n; i++) oksort &= a[i - 1] <= a[i];
            memcpy(a, b, (size_t)n * sizeof(int));
            g_heap_fallbacks = 0; g_ncmp = 0; introsort(a, n, cmp_plain);
            long in = g_ncmp;
            for (int i = 1; i < n; i++) oksort &= a[i - 1] <= a[i];
            long nlog = (long)n * (ilog2_floor((uint32_t)n) + 1);
            printf("  [연습4] n=%d  adversary=%ld  m3-quicksort=%ld (n^2/4=%ld, depth=%d)  introsort=%ld (n*(log2n+1)=%ld, heap fallbacks=%d)\n",
                   n, adv, q, (long)n * n / 4, qdepth, in, nlog, g_heap_fallbacks);
            same &= adv == q;                                               /* 같은 결정 → 같은 비교 횟수 */
            okq &= q >= (long)n * n / 8;
            oki &= in <= 6 * nlog;
        }
        CHECK(same);
        CHECK(okq);
        CHECK(oki);
        CHECK(oksort);
        CHECK(okdepth);
        g_seed = 7;                                                         /* 무작위 입력에서도 introsort 정확성 */
        int bad = 0;
        for (int t = 0; t < 300; t++) {
            int m = (int)(xs32() % 300u);
            for (int i = 0; i < m; i++) b[i] = a[i] = (t & 1) ? (int)xs32() : (int)(xs32() % 3u);
            introsort(a, m, cmp_plain);
            qsort(b, (size_t)m, sizeof(int), cmp_int);
            bad += memcmp(a, b, (size_t)m * sizeof(int)) != 0;
        }
        CHECK(bad == 0);
        for (int i = 0; i < 200; i++) a[i] = b[i] = (i % 2) ? INT_MAX : INT_MIN;
        heap_sort(a, 0, 199, cmp_plain);
        qsort(b, 200, sizeof(int), cmp_int);
        CHECK(memcmp(a, b, 200 * sizeof(int)) == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
