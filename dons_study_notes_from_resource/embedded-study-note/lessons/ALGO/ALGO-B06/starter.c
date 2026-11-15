/* ALGO-B06 퀵소트: Lomuto·Hoare 파티션과 최악 케이스
 *
 * Part A: Lomuto / Hoare / 3-way(Dijkstra) 파티션 + 퀵소트 3종 (비교·교환·최대 재귀 깊이 계측)
 * Part B: median-of-3 피벗 + "작은 쪽만 재귀, 큰 쪽은 루프" → 최대 깊이 <= floor(log2 n)
 * Part C: ADC 샘플 블록 요약 (중앙값 · 절사 평균) — 스택 예산 안에서 정렬
 *
 * 인덱스는 int, 구간은 닫힌 구간 [lo, hi]. n <= INT_MAX - 1 이라 hi + 1 이 넘치지 않는다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t cmp;   /* 키 비교 횟수 (less() 호출 수)                              */
    uint64_t swp;   /* 교환 횟수 (swap_at() 호출 수, 자기 자신과의 교환 포함)        */
    int max_depth;  /* 파티션하는 재귀 프레임의 최대 중첩 수. 루트 = 1, n < 2 이면 0  */
} qs_stats_t;

static inline int less(int x, int y, qs_stats_t *st) { st->cmp++; return x < y; }
static inline void swap_at(int *a, int i, int j, qs_stats_t *st) {
    int t = a[i]; a[i] = a[j]; a[j] = t; st->swp++;
}
static inline void enter(qs_stats_t *st, int depth) { if (depth > st->max_depth) st->max_depth = depth; }

/* ======================= Part A: 파티션 3종 ======================= */

/* Lomuto (pivot = a[hi], 마지막 원소).
 * 사후조건: 반환값 p 에 대해  a[lo..p-1] <= pivot == a[p] < a[p+1..hi]   (p 는 피벗의 최종 위치) */
static int lomuto_partition(int *a, int lo, int hi, qs_stats_t *st) {
    (void)a; (void)st; (void)less; (void)swap_at;
    return lo + (hi - lo) / 2;                    /* TODO: 위 사후조건을 만족하게 */
}

/* Hoare (pivot = a[lo] 의 "값", 첫 원소).
 * 사후조건: lo <= j < hi 이고  a[lo..j] <= pivot <= a[j+1..hi].
 * j 는 피벗의 최종 위치가 아니다 — 피벗 값은 어느 쪽에든 있을 수 있다. */
static int hoare_partition(int *a, int lo, int hi, qs_stats_t *st) {
    (void)a; (void)st;
    return lo + (hi - lo) / 2;                    /* TODO: i 는 왼쪽에서, j 는 오른쪽에서 */
}

/* 3-way (Dijkstra 의 Dutch national flag, pivot v = a[lo]).
 * 사후조건: a[lo..lt-1] < v,  a[lt..gt] == v,  a[gt+1..hi] > v */
static void partition3(int *a, int lo, int hi, int *lt_out, int *gt_out, qs_stats_t *st) {
    (void)a; (void)st;
    *lt_out = *gt_out = lo + (hi - lo) / 2;       /* TODO: lt, i, gt 세 포인터 */
}

/* ---- 교과서 그대로의 재귀 (양쪽 모두 재귀) — 주어진 코드 ---- */
static void qs_lomuto_rec(int *a, int lo, int hi, qs_stats_t *st, int depth) {
    if (lo >= hi) return;
    enter(st, depth);
    int p = lomuto_partition(a, lo, hi, st);
    qs_lomuto_rec(a, lo, p - 1, st, depth + 1);   /* 피벗 a[p] 는 제자리 — 제외 */
    qs_lomuto_rec(a, p + 1, hi, st, depth + 1);
}
static void qs_hoare_rec(int *a, int lo, int hi, qs_stats_t *st, int depth) {
    if (lo >= hi) return;
    enter(st, depth);
    int j = hoare_partition(a, lo, hi, st);
    qs_hoare_rec(a, lo, j, st, depth + 1);        /* j 포함! (j 는 피벗 위치가 아님) */
    qs_hoare_rec(a, j + 1, hi, st, depth + 1);
}
static void qs_3way_rec(int *a, int lo, int hi, qs_stats_t *st, int depth) {
    if (lo >= hi) return;
    enter(st, depth);
    int lt, gt;
    partition3(a, lo, hi, &lt, &gt, st);
    qs_3way_rec(a, lo, lt - 1, st, depth + 1);    /* 같은 값 덩어리 [lt..gt] 는 끝 */
    qs_3way_rec(a, gt + 1, hi, st, depth + 1);
}
static void quicksort_lomuto(int *a, int n, qs_stats_t *st) { *st = (qs_stats_t){0}; if (n >= 2) qs_lomuto_rec(a, 0, n - 1, st, 1); }
static void quicksort_hoare(int *a, int n, qs_stats_t *st)  { *st = (qs_stats_t){0}; if (n >= 2) qs_hoare_rec(a, 0, n - 1, st, 1); }
static void quicksort_3way(int *a, int n, qs_stats_t *st)   { *st = (qs_stats_t){0}; if (n >= 2) qs_3way_rec(a, 0, n - 1, st, 1); }

/* ======================= Part B: 깊이 제한 퀵소트 ======================= */

/* a[lo], a[mid], a[hi] 의 중앙값을 a[lo] 로 옮긴다 (mid = lo + (hi-lo)/2). 비교 2~3회. */
static void median3_to_lo(int *a, int lo, int hi, qs_stats_t *st) {
    (void)a; (void)lo; (void)hi; (void)st;        /* TODO */
}

/* 작은 쪽만 재귀, 큰 쪽은 while 로 (손으로 하는 꼬리 호출 제거).
 * 재귀 호출은 원소 2개 이상인 쪽에만 → 깊이 k 프레임의 크기 <= n / 2^(k-1) 이고 >= 2. */
static void qs_safe_rec(int *a, int lo, int hi, qs_stats_t *st, int depth) {
    enter(st, depth);
    (void)a; (void)lo; (void)hi;                  /* TODO: median3 → Hoare → 작은 쪽 재귀, 큰 쪽 루프 */
}
static void quicksort_safe(int *a, int n, qs_stats_t *st) {
    *st = (qs_stats_t){0};
    if (n >= 2) qs_safe_rec(a, 0, n - 1, st, 1);
}

/* ======================= Part C: ADC 블록 요약 ======================= */

/* floor(log2 n), n >= 1. n == 0 이면 -1. (내장 함수 금지) */
static int ilog2_floor(uint32_t n) {
    (void)n;
    return 0;                                     /* TODO */
}

typedef struct {
    int median;        /* 아래쪽 중앙값 s[(n-1)/2] (정렬 후)                       */
    int trimmed_mean;  /* 위·아래 trim 개씩 버린 평균, 0 쪽으로 버림 (C 정수 나눗셈) */
    int max_depth;     /* 정렬에 쓴 최대 재귀 깊이                                  */
} adc_summary_t;

/* s[0..n-1] 을 제자리 정렬하고 요약을 채운다. 성공 0, 잘못된 인자 -1 (s/out NULL, n<1, trim<0, 2*trim>=n). */
static int adc_block_summary(int *s, int n, int trim, adc_summary_t *out) {
    (void)s; (void)n; (void)trim; (void)out;
    return -1;                                    /* TODO: 인자 검사 → quicksort_safe → 요약 */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_seed = 0xB06u;
static uint32_t xs32(void) { g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5; return g_seed; }

static int cmp_int(const void *x, const void *y) {
    int a = *(const int *)x, b = *(const int *)y;
    return (a > b) - (a < b);                     /* a - b 는 넘친다 (INT_MIN - 1) */
}

typedef void (*sort_fn)(int *, int, qs_stats_t *);
static const sort_fn SORTS[] = {quicksort_lomuto, quicksort_hoare, quicksort_3way, quicksort_safe};
static const char *const SORT_NAME[] = {"lomuto", "hoare", "3way", "safe"};
enum { NSORT = 4 };

enum { K_RANDOM, K_SMALLRANGE, K_SORTED, K_REVERSED, K_EQUAL, K_ORGAN, K_SAW, K_EXTREMES, K_COUNT };
static void fill(int *a, int n, int kind) {
    for (int i = 0; i < n; i++) {
        switch (kind) {
        case K_RANDOM:     a[i] = (int)xs32(); break;              /* 구현 정의 변환이지만 2의 보수에서 전 범위 */
        case K_SMALLRANGE: a[i] = (int)(xs32() % 10u); break;
        case K_SORTED:     a[i] = i; break;
        case K_REVERSED:   a[i] = n - i; break;
        case K_EQUAL:      a[i] = 2048; break;
        case K_ORGAN:      a[i] = i < n / 2 ? i : n - i; break;    /* 오르막 후 내리막 */
        case K_SAW:        a[i] = i % 16; break;
        default:           a[i] = (i & 1) ? INT_MAX : INT_MIN; break;
        }
    }
}

/* sort 결과가 qsort 와 같은가 */
static int sorts_like_qsort(sort_fn f, const int *src, int n, qs_stats_t *st) {
    static int x[200000], ref[200000];
    memcpy(x, src, (size_t)n * sizeof(int));
    memcpy(ref, src, (size_t)n * sizeof(int));
    f(x, n, st);
    qsort(ref, (size_t)n, sizeof(int), cmp_int);
    return memcmp(x, ref, (size_t)n * sizeof(int)) == 0;
}

/* 같은 다중집합인가 (정렬해서 비교) */
static int same_multiset(const int *a, const int *b, int n) {
    int x[256], y[256];
    memcpy(x, a, (size_t)n * sizeof(int)); memcpy(y, b, (size_t)n * sizeof(int));
    qsort(x, (size_t)n, sizeof(int), cmp_int); qsort(y, (size_t)n, sizeof(int), cmp_int);
    return memcmp(x, y, (size_t)n * sizeof(int)) == 0;
}

int main(void) {
    qs_stats_t st;
    static int a[200000];

    /* ---- Part A: 파티션 사후조건 ---- */
    {
        int h[] = {5, 3, 8, 4, 9, 1, 6, 2, 7};
        int j = hoare_partition(h, 0, 8, &st);
        CHECK(j == 3 && h[3] == 4);               /* 반환 j 의 원소는 피벗(5)이 아니다 */
        int l[] = {5, 3, 8, 4, 9, 1, 6, 2, 7};
        int p = lomuto_partition(l, 0, 8, &st);
        CHECK(p == 6 && l[6] == 7);               /* Lomuto 는 피벗(7)의 최종 위치 */
        int d[] = {2, 0, 2, 1, 2, 0, 2};
        int lt, gt;
        partition3(d, 0, 6, &lt, &gt, &st);
        CHECK(lt == 3 && gt == 6);                /* [0 0 1 | 2 2 2 2] */
    }
    {
        int badL = 0, badH = 0, bad3 = 0;
        for (int t = 0; t < 3000; t++) {
            int n = 1 + (int)(xs32() % 64u), src[64], x[64];
            for (int i = 0; i < n; i++) src[i] = (t & 1) ? (int)(xs32() % 8u) : (int)xs32();
            int lo = 0, hi = n - 1;

            memcpy(x, src, (size_t)n * sizeof(int)); int piv = src[hi];
            int p = lomuto_partition(x, lo, hi, &st);
            int ok = p >= lo && p <= hi && x[p] == piv && same_multiset(x, src, n);
            for (int i = lo; i < p; i++) ok &= x[i] <= piv;
            for (int i = p + 1; i <= hi; i++) ok &= x[i] > piv;
            badL += !ok;

            if (n >= 2) {                         /* Hoare 는 원소 2개 이상에서 부른다 */
                memcpy(x, src, (size_t)n * sizeof(int)); piv = src[lo];
                int j = hoare_partition(x, lo, hi, &st);
                ok = j >= lo && j < hi && same_multiset(x, src, n);
                for (int i = lo; i <= j; i++) ok &= x[i] <= piv;
                for (int i = j + 1; i <= hi; i++) ok &= x[i] >= piv;
                badH += !ok;
            }

            memcpy(x, src, (size_t)n * sizeof(int)); piv = src[lo];
            int lt, gt;
            partition3(x, lo, hi, &lt, &gt, &st);
            ok = lo <= lt && lt <= gt && gt <= hi && same_multiset(x, src, n);
            for (int i = lo; i <= hi; i++)
                ok &= i < lt ? x[i] < piv : i <= gt ? x[i] == piv : x[i] > piv;
            bad3 += !ok;
        }
        CHECK(badL == 0);
        CHECK(badH == 0);
        CHECK(bad3 == 0);
    }

    /* ---- Part A+B: 정렬 정확성 (전수 · 무작위 · 적대적) vs qsort ---- */
    {
        int bad[NSORT] = {0};
        for (int n = 0; n <= 7; n++) {            /* 값 {0,1,2,3}, 길이 0~7 전수: 21845 개 */
            int total = 1;
            for (int i = 0; i < n; i++) total *= 4;
            for (int code = 0; code < total; code++) {
                int c = code;
                for (int i = 0; i < n; i++) { a[i] = c % 4; c /= 4; }
                for (int s = 0; s < NSORT; s++) bad[s] += !sorts_like_qsort(SORTS[s], a, n, &st);
            }
        }
        for (int s = 0; s < NSORT; s++) { CHECK(bad[s] == 0); if (bad[s]) printf("  (%s: %d bad)\n", SORT_NAME[s], bad[s]); }
    }
    {
        int bad[NSORT] = {0};
        for (int t = 0; t < 2000; t++) {
            int n = (int)(xs32() % 601u);
            fill(a, n, (t & 1) ? K_RANDOM : K_SMALLRANGE);
            if (n > 2 && t % 5 == 0) { a[0] = INT_MAX; a[n - 1] = INT_MIN; }
            for (int s = 0; s < NSORT; s++) bad[s] += !sorts_like_qsort(SORTS[s], a, n, &st);
        }
        for (int s = 0; s < NSORT; s++) { CHECK(bad[s] == 0); if (bad[s]) printf("  (%s: %d bad)\n", SORT_NAME[s], bad[s]); }
    }
    {
        int bad[NSORT] = {0};
        for (int k = 0; k < K_COUNT; k++)
            for (int s = 0; s < NSORT; s++) {
                fill(a, 1000, k);
                bad[s] += !sorts_like_qsort(SORTS[s], a, 1000, &st);
            }
        for (int s = 0; s < NSORT; s++) { CHECK(bad[s] == 0); if (bad[s]) printf("  (%s: %d bad)\n", SORT_NAME[s], bad[s]); }
    }

    /* ---- Part A: 최악 케이스 재현 (n = 1000) ---- */
    {
        const int n = 1000;
        fill(a, n, K_SORTED);   quicksort_lomuto(a, n, &st);
        CHECK(st.cmp == (uint64_t)n * (n - 1) / 2 && st.max_depth == n - 1);
        fill(a, n, K_EQUAL);    quicksort_lomuto(a, n, &st);
        CHECK(st.cmp == (uint64_t)n * (n - 1) / 2 && st.max_depth == n - 1);
        fill(a, n, K_SORTED);   quicksort_hoare(a, n, &st);
        CHECK(st.max_depth == n - 1);
        fill(a, n, K_EQUAL);    quicksort_hoare(a, n, &st);
        CHECK(st.max_depth <= 11);                /* 같은 값에선 가운데서 만난다 → 균형 */
        fill(a, n, K_REVERSED); quicksort_3way(a, n, &st);
        CHECK(st.cmp == (uint64_t)n * (n - 1) / 2 && st.max_depth == n - 1);  /* 3-way 도 피벗이 최댓값이면 O(n^2) */
        fill(a, n, K_EQUAL);    quicksort_3way(a, n, &st);
        CHECK(st.max_depth == 1 && st.cmp == 2u * (n - 1));
    }

    /* ---- Part B: median-of-3 와 깊이 상한 ---- */
    {
        int bad = 0;
        for (int code = 0; code < 27; code++) {   /* 세 자리 값 {0,1,2}^3, 크기 5 배열의 lo/mid/hi */
            int x[5] = {code % 3, 9, code / 3 % 3, 9, code / 9}, s3[3] = {x[0], x[2], x[4]};
            qsort(s3, 3, sizeof(int), cmp_int);
            int y[5]; memcpy(y, x, sizeof x);
            median3_to_lo(x, 0, 4, &st);
            bad += !(x[0] == s3[1] && x[1] == 9 && x[3] == 9 && same_multiset(x, y, 5));
        }
        CHECK(bad == 0);
    }
    {
        int bad = 0;
        for (int k = 0; k < K_COUNT; k++)
            for (int n = 2; n <= 2048; n = n * 3 / 2 + 1) {
                fill(a, n, k);
                quicksort_safe(a, n, &st);
                bad += st.max_depth > ilog2_floor((uint32_t)n);
            }
        CHECK(bad == 0);
        const int N = 100000;                     /* floor(log2 100000) = 16 */
        int okdepth = 1, okcmp = 1;
        for (int k = 0; k < K_COUNT; k++) {
            fill(a, N, k);
            okdepth &= sorts_like_qsort(quicksort_safe, a, N, &st) && st.max_depth <= 16;
            if (k == K_SORTED || k == K_REVERSED || k == K_EQUAL) okcmp &= st.cmp < 3ull * N * 17;
        }
        CHECK(okdepth);
        CHECK(okcmp);                             /* 정렬·역순·같은 값에서 O(n log n) */
        quicksort_safe(a, 1, &st);  CHECK(st.max_depth == 0);
        quicksort_safe(a, 0, &st);  CHECK(st.max_depth == 0);
    }

    /* ---- Part C: ADC 블록 요약 ---- */
    {
        CHECK(ilog2_floor(1) == 0 && ilog2_floor(2) == 1 && ilog2_floor(3) == 1);
        CHECK(ilog2_floor(1024) == 10 && ilog2_floor(1023) == 9 && ilog2_floor(0xFFFFFFFFu) == 31);
        CHECK(ilog2_floor(0) == -1);
        int bad = 0;
        for (uint32_t v = 1; v < (1u << 20); v++) {
            int k = ilog2_floor(v);
            bad += !(k >= 0 && (1u << k) <= v && (k == 31 || v < (1u << (k + 1))));
        }
        CHECK(bad == 0);

        adc_summary_t r;
        int s[512];
        for (int i = 0; i < 256; i++) s[i] = i;                       /* 램프 = 정렬된 입력 */
        CHECK(adc_block_summary(s, 256, 16, &r) == 0 && r.median == 127 && r.trimmed_mean == 127 && r.max_depth <= 8);
        for (int i = 0; i < 256; i++) s[i] = 2048;                    /* 고착(stuck) = 전부 같은 값 */
        CHECK(adc_block_summary(s, 256, 0, &r) == 0 && r.median == 2048 && r.trimmed_mean == 2048 && r.max_depth <= 8);
        for (int i = 0; i < 64; i++) s[i] = 1000 + (int)(xs32() % 5u) - 2;   /* 998..1002 */
        s[7] = 4095; s[20] = 4095; s[33] = 0; s[50] = 0;              /* 스파이크 4개 */
        CHECK(adc_block_summary(s, 64, 4, &r) == 0 && r.median >= 998 && r.median <= 1002
              && r.trimmed_mean >= 998 && r.trimmed_mean <= 1002);
        CHECK(adc_block_summary(s, 0, 0, &r) == -1);
        CHECK(adc_block_summary(s, 8, 4, &r) == -1);                  /* 2*trim >= n */
        CHECK(adc_block_summary(s, 9, 4, &r) == 0);                   /* 가운데 1개 남음 */
        CHECK(adc_block_summary(NULL, 8, 0, &r) == -1 && adc_block_summary(s, 8, 0, NULL) == -1);

        int badr = 0;
        for (int t = 0; t < 500; t++) {
            int n = 1 + (int)(xs32() % 512u), trim = (int)(xs32() % (unsigned)((n + 1) / 2));
            int ref[512];
            for (int i = 0; i < n; i++) s[i] = ref[i] = (int)(xs32() % 4096u);
            qsort(ref, (size_t)n, sizeof(int), cmp_int);
            int64_t sum = 0;
            for (int i = trim; i < n - trim; i++) sum += ref[i];
            int rc = adc_block_summary(s, n, trim, &r);
            badr += !(rc == 0 && r.median == ref[(n - 1) / 2] && r.trimmed_mean == (int)(sum / (n - 2 * trim))
                      && memcmp(s, ref, (size_t)n * sizeof(int)) == 0
                      && r.max_depth <= (n >= 2 ? ilog2_floor((uint32_t)n) : 0));
        }
        CHECK(badr == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
