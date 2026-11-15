/* ALGO-B07 demo_bench — 머지소트 측정 (solution.c 와 같은 구현을 복사해 쓴다, 자기완결)
 *
 *   (1) 비교 횟수: n = 1..10 의 모든 순열을 돌려 top-down / bottom-up 의 최소·최대 비교 횟수를 세고
 *       알려진 top-down 최악 공식 n⌈lg n⌉ − 2^⌈lg n⌉ + 1 과 비교
 *   (2) 시간: n = 10^6 레코드 (key, seq) — top-down / bottom-up / 리스트(노드 순차 배치) /
 *       리스트(노드 무작위 배치) / qsort.  워밍업 1회 + 7회 중앙값.
 *   (3) top-down 재귀 깊이와 프레임 크기(지역 변수 주소 차이로 어림)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _POSIX_C_SOURCE 199309L
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { int32_t key; uint32_t seq; } rec_t;
static unsigned long g_cmp;
#define KEY_LE(x, y) (g_cmp++, (x).key <= (y).key)

static void merge_into(rec_t *dst, const rec_t *src, size_t lo, size_t mid, size_t hi) {
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) dst[k++] = KEY_LE(src[i], src[j]) ? src[i++] : src[j++];
    while (i < mid) dst[k++] = src[i++];
    while (j < hi)  dst[k++] = src[j++];
}

static unsigned g_depth, g_maxdepth;
static uintptr_t g_sp_top, g_sp_min;
static void td_rec(rec_t *a, rec_t *tmp, size_t lo, size_t hi) {
    volatile char probe = 0;                                   /* 스택 위치 어림용 */
    uintptr_t sp = (uintptr_t)&probe;
    if (sp < g_sp_min) g_sp_min = sp;
    if (++g_depth > g_maxdepth) g_maxdepth = g_depth;
    if (hi - lo >= 2) {
        size_t mid = lo + (hi - lo) / 2;
        td_rec(a, tmp, lo, mid);
        td_rec(a, tmp, mid, hi);
        memcpy(tmp + lo, a + lo, (hi - lo) * sizeof *a);
        merge_into(a, tmp, lo, mid, hi);
    }
    g_depth--;
}
static void msort_topdown(rec_t *a, size_t n, rec_t *tmp) {
    if (n < 2) return;
    volatile char top = 0; g_sp_top = g_sp_min = (uintptr_t)&top;
    td_rec(a, tmp, 0, n);
}

static void msort_bottomup(rec_t *a, size_t n, rec_t *tmp) {
    if (n < 2) return;
    rec_t *src = a, *dst = tmp;
    for (size_t w = 1; w < n; ) {
        for (size_t lo = 0; lo < n; ) {
            size_t mid = (n - lo > w) ? lo + w : n;
            size_t hi  = (n - mid > w) ? mid + w : n;
            merge_into(dst, src, lo, mid, hi);
            lo = hi;
        }
        rec_t *t = src; src = dst; dst = t;
        if (w > n / 2) break;
        w *= 2;
    }
    if (src != a) memcpy(a, src, n * sizeof *a);
}

typedef struct node { struct node *next; uint32_t key, seq; } node_t;
#define N_LE(x, y) (g_cmp++, (x)->key <= (y)->key)
static node_t *list_split(node_t *h, size_t k) {
    while (h && --k) h = h->next;
    if (!h) return NULL;
    node_t *r = h->next; h->next = NULL; return r;
}
static node_t *list_merge(node_t *a, node_t *b, node_t **tail_out) {
    node_t *head = NULL, **pp = &head, *last = NULL;
    while (a && b) {
        if (N_LE(a, b)) { *pp = a; a = a->next; } else { *pp = b; b = b->next; }
        last = *pp; pp = &last->next;
    }
    *pp = a ? a : b;
    for (node_t *p = *pp; p; p = p->next) last = p;
    *tail_out = last;
    return head;
}
static node_t *list_sort(node_t *head) {
    size_t n = 0;
    for (node_t *p = head; p; p = p->next) n++;
    for (size_t w = 1; w < n; ) {
        node_t *rest = head, *out = NULL, **pp = &out;
        while (rest) {
            node_t *a = rest, *b = list_split(a, w), *tail;
            rest = list_split(b, w);
            *pp = list_merge(a, b, &tail);
            pp = &tail->next;
        }
        head = out;
        if (w > n / 2) break;
        w *= 2;
    }
    return head;
}

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static int cmp_rec(const void *p, const void *q) {
    int32_t x = ((const rec_t *)p)->key, y = ((const rec_t *)q)->key;
    return (x > y) - (x < y);
}
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp_d(const void *p, const void *q) { double x = *(const double *)p, y = *(const double *)q; return (x > y) - (x < y); }
static unsigned ceil_lg(size_t n) { unsigned c = 0; while (((size_t)1 << c) < n) c++; return c; }

/* ---------- (1) 전 순열 비교 횟수 ---------- */
static void perm_counts(void) {
    printf("(1) comparisons over ALL n! permutations (distinct keys)\n");
    printf("  n   n!       td min  td max  formula  bu min  bu max  n*ceil(lg n)\n");
    for (int n = 1; n <= 10; n++) {
        int p[10], c[10] = { 0 };
        for (int i = 0; i < n; i++) p[i] = i;
        unsigned long tmin = ~0ul, tmax = 0, bmin = ~0ul, bmax = 0, cnt = 0;
        rec_t a[10], t[10];
        int i = 0;
        for (;;) {                                             /* Heap 알고리즘 (반복형) */
            for (int k = 0; k < n; k++) { a[k].key = p[k]; a[k].seq = (uint32_t)k; }
            g_cmp = 0; msort_topdown(a, (size_t)n, t);
            if (g_cmp < tmin) tmin = g_cmp; if (g_cmp > tmax) tmax = g_cmp;
            for (int k = 0; k < n; k++) { a[k].key = p[k]; a[k].seq = (uint32_t)k; }
            g_cmp = 0; msort_bottomup(a, (size_t)n, t);
            if (g_cmp < bmin) bmin = g_cmp; if (g_cmp > bmax) bmax = g_cmp;
            cnt++;
            while (i < n && c[i] >= i) c[i++] = 0;
            if (i >= n) break;
            int j = (i % 2) ? c[i] : 0, x = p[j]; p[j] = p[i]; p[i] = x;
            c[i]++; i = 0;
        }
        unsigned L = ceil_lg((size_t)n);
        unsigned long formula = (unsigned long)n * L - (1ul << L) + 1;
        printf("  %-3d %-8lu %-7lu %-7lu %-8lu %-7lu %-7lu %u\n", n, cnt, tmin, tmax, formula, bmin, bmax, (unsigned)n * L);
    }
}

/* ---------- (2) 시간 ---------- */
#define N 1000000
#define RUNS 7
static rec_t orig[N], a[N], tmp[N];
static node_t pool[N];
static uint32_t slot[N];

static node_t *link_pool(int scatter) {
    for (size_t i = 0; i < N; i++) slot[i] = (uint32_t)i;
    if (scatter) for (size_t i = N; i > 1; i--) { size_t j = rnd() % i; uint32_t t = slot[i - 1]; slot[i - 1] = slot[j]; slot[j] = t; }
    node_t *h = NULL, **pp = &h;
    for (size_t i = 0; i < N; i++) { node_t *r = &pool[slot[i]]; r->key = (uint32_t)orig[i].key; r->seq = orig[i].seq; *pp = r; pp = &r->next; }
    *pp = NULL;
    return h;
}

int main(void) {
    perm_counts();

    for (size_t i = 0; i < N; i++) { orig[i].key = (int32_t)(rnd() >> 1); orig[i].seq = (uint32_t)i; }
    const char *name[] = { "top-down (array)", "bottom-up (array)", "list, nodes in order", "list, nodes scattered", "qsort (array)" };
    double med[5]; unsigned long cmps[5] = { 0 }; volatile uint64_t sink = 0;
    for (int alg = 0; alg < 5; alg++) {
        double ms[RUNS];
        for (int r = -1; r < RUNS; r++) {                      /* r = -1: 워밍업 */
            double t0 = 0, t1 = 0; uint64_t chk = 0;
            if (alg <= 1 || alg == 4) {
                memcpy(a, orig, sizeof a);
                g_cmp = 0; t0 = now_ms();
                if (alg == 0) msort_topdown(a, N, tmp);
                else if (alg == 1) msort_bottomup(a, N, tmp);
                else qsort(a, N, sizeof a[0], cmp_rec);
                t1 = now_ms();
                chk = (uint64_t)a[0].key + a[N / 2].key + a[N - 1].key;
            } else {
                node_t *h = link_pool(alg == 3);
                g_cmp = 0; t0 = now_ms();
                h = list_sort(h);
                t1 = now_ms();
                for (node_t *p = h; p; p = p->next) chk += p->key;
            }
            sink += chk;
            if (r >= 0) ms[r] = t1 - t0;
            cmps[alg] = g_cmp;
        }
        qsort(ms, RUNS, sizeof ms[0], cmp_d);
        med[alg] = ms[RUNS / 2];
    }
    printf("\n(2) n = %d records (int32 key, uint32 seq), median of %d runs after 1 warm-up\n", N, RUNS);
    printf("  %-24s %9s %8s %14s\n", "algorithm", "ms", "x td", "key compares");
    for (int alg = 0; alg < 5; alg++) {
        printf("  %-24s %9.1f %8.2f ", name[alg], med[alg], med[alg] / med[0]);
        if (alg < 4) printf("%14lu\n", cmps[alg]); else printf("%14s\n", "-");
    }
    printf("  n*ceil(lg n) = %lu, checksum %llu\n", (unsigned long)N * ceil_lg(N), (unsigned long long)sink);

    memcpy(a, orig, sizeof a);
    g_maxdepth = 0; msort_topdown(a, N, tmp);
    printf("\n(3) top-down recursion: n=%d max depth=%u (ceil(lg n)+1=%u), stack span ~%lu bytes (~%lu B/frame)\n",
           N, g_maxdepth, ceil_lg(N) + 1, (unsigned long)(g_sp_top - g_sp_min),
           (unsigned long)((g_sp_top - g_sp_min) / g_maxdepth));
    return 0;
}
