/* ALGO-M11 샘플당 시간 비교 (이 맥, -O2). 절대 ns 보다 "비율"을 볼 것.
 *  1) 윈도우 중앙값 k = 5/31/255/1023: 매 샘플 복사+qsort / 정렬 배열(이진 탐색+memmove) /
 *     indexed 두 힙(삭제+삽입, 본문 Part B) / indexed 두 힙(제자리 갱신, 연습 2) / lazy deletion 두 힙(연습 1)
 *  2) 스트림 전체 중앙값(Part A): 두 힙 vs 정렬 배열 삽입
 *  3) Top-K: 크기 K min-heap vs 전부 모아 qsort
 *  4) lazy deletion 의 물리 힙 크기(유효 + stale) — 분포별, n 별
 * 모든 방법의 결과 합(checksum)이 같은지 확인한다. 워밍업 1회 + 9회 중앙값.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N_WIN  20000
#define N_RUN  50000
#define N_TOPK 1000000
#define KMAX   1024
#define REPS   9

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e9 + (double)t.tv_nsec; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static int cmp_i32(const void *a, const void *b) { int32_t x = *(const int32_t *)a, y = *(const int32_t *)b; return (x > y) - (x < y); }
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static int32_t in[N_TOPK];
static volatile int64_t sink;

/* ---------- 1. 매 샘플 복사 + qsort ---------- */
static int64_t win_qsort(const int32_t *x, int n, int k) {
    static int32_t tmp[KMAX];
    int64_t cs = 0;
    for (int i = k - 1; i < n; i++) {
        memcpy(tmp, x + i - k + 1, (size_t)k * sizeof tmp[0]);
        qsort(tmp, (size_t)k, sizeof tmp[0], cmp_i32);
        cs += tmp[(k - 1) / 2];
    }
    return cs;
}
/* ---------- 2. 정렬 배열: 나가는 값을 이진 탐색해 지우고 들어오는 값을 이진 탐색 위치에 넣는다 ---------- */
static int lb(const int32_t *a, int n, int32_t v) { int lo = 0, hi = n; while (lo < hi) { int m = lo + (hi - lo) / 2; if (a[m] < v) lo = m + 1; else hi = m; } return lo; }
static int64_t win_sorted(const int32_t *x, int n, int k) {
    static int32_t s[KMAX];
    int sn = 0;
    int64_t cs = 0;
    for (int i = 0; i < n; i++) {
        if (i >= k) { int p = lb(s, sn, x[i - k]); memmove(s + p, s + p + 1, (size_t)(sn - p - 1) * sizeof s[0]); sn--; }
        int p = lb(s, sn, x[i]);
        memmove(s + p + 1, s + p, (size_t)(sn - p) * sizeof s[0]);
        s[p] = x[i]; sn++;
        if (i >= k - 1) cs += s[(k - 1) / 2];
    }
    return cs;
}
/* ---------- 3·4. indexed 두 힙 (본문 Part B 와 같은 코드) ---------- */
typedef struct { uint16_t a[KMAX]; int n; bool is_max; } iheap_t;
typedef struct { int32_t val[KMAX]; uint16_t pos[KMAX]; uint8_t side[KMAX]; iheap_t lo, hi; int k, head, count; } wmed_t;
static iheap_t *wm_heap(wmed_t *w, int side) { return side ? &w->hi : &w->lo; }
static bool ih_above(const wmed_t *w, const iheap_t *h, int s, int t) { return h->is_max ? w->val[s] > w->val[t] : w->val[s] < w->val[t]; }
static void ih_set(wmed_t *w, iheap_t *h, int i, int s) { h->a[i] = (uint16_t)s; w->pos[s] = (uint16_t)i; }
static void ih_up(wmed_t *w, iheap_t *h, int i) {
    int s = h->a[i];
    while (i > 0 && ih_above(w, h, s, h->a[(i - 1) / 2])) { ih_set(w, h, i, h->a[(i - 1) / 2]); i = (i - 1) / 2; }
    ih_set(w, h, i, s);
}
static void ih_down(wmed_t *w, iheap_t *h, int i) {
    int s = h->a[i], n = h->n;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && ih_above(w, h, h->a[c + 1], h->a[c])) c++;
        if (!ih_above(w, h, h->a[c], s)) break;
        ih_set(w, h, i, h->a[c]); i = c;
    }
    ih_set(w, h, i, s);
}
static void ih_fix(wmed_t *w, iheap_t *h, int i) { if (i > 0 && ih_above(w, h, h->a[i], h->a[(i - 1) / 2])) ih_up(w, h, i); else ih_down(w, h, i); }
static void ih_push(wmed_t *w, int side, int s) { iheap_t *h = wm_heap(w, side); w->side[s] = (uint8_t)side; ih_set(w, h, h->n, s); h->n++; ih_up(w, h, h->n - 1); }
static int ih_pop(wmed_t *w, int side) {
    iheap_t *h = wm_heap(w, side);
    int s = h->a[0];
    h->n--;
    if (h->n > 0) { ih_set(w, h, 0, h->a[h->n]); ih_down(w, h, 0); }
    return s;
}
static void ih_remove_at(wmed_t *w, int side, int i) {
    iheap_t *h = wm_heap(w, side);
    int last = --h->n;
    if (i == last) return;
    ih_set(w, h, i, h->a[last]); ih_fix(w, h, i);
}
static void wm_init(wmed_t *w, int k) { w->k = k; w->head = w->count = 0; w->lo.n = w->hi.n = 0; w->lo.is_max = true; w->hi.is_max = false; }
static void wm_insert(wmed_t *w, int s, int32_t x) {
    w->val[s] = x;
    int side;
    if (w->lo.n > 0) side = (x <= w->val[w->lo.a[0]]) ? 0 : 1;
    else side = (w->hi.n > 0 && x > w->val[w->hi.a[0]]) ? 1 : 0;
    ih_push(w, side, s); w->count++;
    if (w->lo.n > w->hi.n + 1) ih_push(w, 1, ih_pop(w, 0));
    else if (w->hi.n > w->lo.n) ih_push(w, 0, ih_pop(w, 1));
}
static int64_t win_heap(const int32_t *x, int n, int k, bool replace) {
    static wmed_t w;
    wm_init(&w, k);
    int64_t cs = 0;
    for (int i = 0; i < n; i++) {
        int s = w.head;
        if (w.count < w.k) wm_insert(&w, s, x[i]);
        else if (!replace) { ih_remove_at(&w, w.side[s], w.pos[s]); w.count--; wm_insert(&w, s, x[i]); }
        else {
            w.val[s] = x[i];
            ih_fix(&w, wm_heap(&w, w.side[s]), w.pos[s]);
            if (w.hi.n > 0 && w.val[w.lo.a[0]] > w.val[w.hi.a[0]]) {
                int a = w.lo.a[0], b = w.hi.a[0];
                ih_set(&w, &w.lo, 0, b); w.side[b] = 0;
                ih_set(&w, &w.hi, 0, a); w.side[a] = 1;
                ih_down(&w, &w.lo, 0); ih_down(&w, &w.hi, 0);
            }
        }
        w.head = (w.head + 1 == w.k) ? 0 : w.head + 1;
        if (w.count == w.k) cs += w.val[w.lo.a[0]];
    }
    return cs;
}
/* ---------- 5. lazy deletion (연습 1 과 같은 코드) ---------- */
#define LZ_CAP N_RUN
typedef struct { int32_t v; int32_t i; } pr_t;
static bool pr_less(pr_t a, pr_t b) { return a.v != b.v ? a.v < b.v : a.i < b.i; }
typedef struct { pr_t a[LZ_CAP]; int n; bool is_max; } pheap_t;
static bool ph_above(const pheap_t *h, pr_t x, pr_t y) { return h->is_max ? pr_less(y, x) : pr_less(x, y); }
static void ph_push(pheap_t *h, pr_t x) {
    int i = h->n++;
    while (i > 0 && ph_above(h, x, h->a[(i - 1) / 2])) { h->a[i] = h->a[(i - 1) / 2]; i = (i - 1) / 2; }
    h->a[i] = x;
}
static pr_t ph_pop(pheap_t *h) {
    pr_t top = h->a[0], x = h->a[--h->n];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= h->n) break;
        if (c + 1 < h->n && ph_above(h, h->a[c + 1], h->a[c])) c++;
        if (!ph_above(h, h->a[c], x)) break;
        h->a[i] = h->a[c]; i = c;
    }
    h->a[i] = x;
    return top;
}
static pheap_t g_lo, g_hi;
static int lz_peak;
static int64_t win_lazy(const int32_t *x, int n, int k) {
    int lov = 0, hiv = 0, start = 0;
    int64_t cs = 0;
    g_lo.n = g_hi.n = 0; g_lo.is_max = true; g_hi.is_max = false; lz_peak = 0;
#define PRUNE(h) while ((h).n > 0 && (h).a[0].i < start) ph_pop(&(h))
    for (int t = 0; t < n; t++) {
        pr_t e = { x[t], t };
        PRUNE(g_lo); PRUNE(g_hi);
        bool to_lo = lov > 0 ? !pr_less(g_lo.a[0], e) : !(hiv > 0 && pr_less(g_hi.a[0], e));
        if (to_lo) { ph_push(&g_lo, e); lov++; } else { ph_push(&g_hi, e); hiv++; }
        if (t >= k) {
            pr_t o = { x[t - k], t - k };
            PRUNE(g_lo);
            if (lov > 0 && !pr_less(g_lo.a[0], o)) lov--; else hiv--;
            start = t - k + 1;
        }
        if (lov > hiv + 1) { PRUNE(g_lo); ph_push(&g_hi, ph_pop(&g_lo)); lov--; hiv++; }
        else if (hiv > lov) { PRUNE(g_hi); ph_push(&g_lo, ph_pop(&g_hi)); hiv--; lov++; }
        if (g_lo.n + g_hi.n > lz_peak) lz_peak = g_lo.n + g_hi.n;
        if (t >= k - 1) { PRUNE(g_lo); cs += g_lo.a[0].v; }
    }
    return cs;
}

/* ---------- 스트림 전체 중앙값: 두 힙 vs 정렬 배열 ---------- */
typedef struct { int32_t a[N_RUN / 2 + 2]; int n; bool is_max; } vheap_t;
static bool vh_above(const vheap_t *h, int32_t x, int32_t y) { return h->is_max ? x > y : x < y; }
static void vh_push(vheap_t *h, int32_t x) { int i = h->n++; while (i > 0 && vh_above(h, x, h->a[(i - 1) / 2])) { h->a[i] = h->a[(i - 1) / 2]; i = (i - 1) / 2; } h->a[i] = x; }
static int32_t vh_pop(vheap_t *h) {
    int32_t top = h->a[0], x = h->a[--h->n];
    int i = 0;
    for (;;) { int c = 2 * i + 1; if (c >= h->n) break; if (c + 1 < h->n && vh_above(h, h->a[c + 1], h->a[c])) c++; if (!vh_above(h, h->a[c], x)) break; h->a[i] = h->a[c]; i = c; }
    h->a[i] = x;
    return top;
}
static vheap_t r_lo, r_hi;
static int64_t run_heap(const int32_t *x, int n) {
    r_lo.n = r_hi.n = 0; r_lo.is_max = true; r_hi.is_max = false;
    int64_t cs = 0;
    for (int i = 0; i < n; i++) {
        if (r_lo.n == 0 || x[i] <= r_lo.a[0]) vh_push(&r_lo, x[i]); else vh_push(&r_hi, x[i]);
        if (r_lo.n > r_hi.n + 1) vh_push(&r_hi, vh_pop(&r_lo));
        else if (r_hi.n > r_lo.n) vh_push(&r_lo, vh_pop(&r_hi));
        cs += r_lo.a[0];
    }
    return cs;
}
static int64_t run_sorted(const int32_t *x, int n) {
    static int32_t s[N_RUN];
    int64_t cs = 0;
    for (int i = 0; i < n; i++) {
        int p = lb(s, i, x[i]);
        memmove(s + p + 1, s + p, (size_t)(i - p) * sizeof s[0]);
        s[p] = x[i];
        cs += s[i / 2];                        /* 원소 i+1 개의 아래쪽 중앙값 = 순위 i/2 */
    }
    return cs;
}

/* ---------- Top-K ---------- */
static int64_t topk_heap(const int32_t *x, int n, int K) {
    static int32_t h[1024];
    int hn = 0;
    for (int i = 0; i < n; i++) {
        int32_t v = x[i];
        if (hn < K) { int j = hn++; while (j > 0 && h[(j - 1) / 2] > v) { h[j] = h[(j - 1) / 2]; j = (j - 1) / 2; } h[j] = v; }
        else if (v > h[0]) {
            int j = 0;
            for (;;) { int c = 2 * j + 1; if (c >= hn) break; if (c + 1 < hn && h[c + 1] < h[c]) c++; if (h[c] >= v) break; h[j] = h[c]; j = c; }
            h[j] = v;
        }
    }
    int64_t cs = 0;
    for (int i = 0; i < hn; i++) cs += h[i];
    return cs;
}
static int64_t topk_sort(const int32_t *x, int n, int K) {
    static int32_t tmp[N_TOPK];
    memcpy(tmp, x, (size_t)n * sizeof tmp[0]);
    qsort(tmp, (size_t)n, sizeof tmp[0], cmp_i32);
    int64_t cs = 0;
    for (int i = n - K; i < n; i++) cs += tmp[i];
    return cs;
}

typedef int64_t (*winfn)(const int32_t *, int, int);
static int64_t f_heap_di(const int32_t *x, int n, int k) { return win_heap(x, n, k, false); }
static int64_t f_heap_rp(const int32_t *x, int n, int k) { return win_heap(x, n, k, true); }

static double time_it(winfn f, const int32_t *x, int n, int k, int64_t *cs) {
    double t[REPS];
    *cs = f(x, n, k);                          /* 워밍업 + 결과 */
    for (int r = 0; r < REPS; r++) { double t0 = now_ns(); sink = f(x, n, k); t[r] = now_ns() - t0; }
    qsort(t, REPS, sizeof t[0], cmpd);
    return t[REPS / 2];
}

int main(void) {
    uint32_t seed = 0xC0FFEEu;
    for (int i = 0; i < N_TOPK; i++) in[i] = (int32_t)(xs32(&seed) % 4096u);   /* 12-bit ADC 처럼 */

    printf("[1] sliding-window median, n=%d samples (0..4095), ns/sample (median of %d)\n", N_WIN, REPS);
    printf("%6s %12s %12s %12s %12s %12s   agree\n", "k", "qsort/sample", "sorted arr", "heap del+ins", "heap replace", "heap lazy");
    static const int ks[4] = { 5, 31, 255, 1023 };
    for (int j = 0; j < 4; j++) {
        int k = ks[j], outs = N_WIN - k + 1;
        winfn fs[5] = { win_qsort, win_sorted, f_heap_di, f_heap_rp, win_lazy };
        double ns[5];
        int64_t cs[5];
        for (int m = 0; m < 5; m++) ns[m] = time_it(fs[m], in, N_WIN, k, &cs[m]) / outs;
        bool agree = cs[1] == cs[0] && cs[2] == cs[0] && cs[3] == cs[0] && cs[4] == cs[0];
        printf("%6d %12.1f %12.1f %12.1f %12.1f %12.1f   %s\n", k, ns[0], ns[1], ns[2], ns[3], ns[4], agree ? "yes" : "NO");
    }

    printf("\n[2] running median over the whole stream, n=%d, ns/sample\n", N_RUN);
    {
        double t[2][REPS];
        int64_t c0 = run_heap(in, N_RUN), c1 = run_sorted(in, N_RUN);
        for (int r = 0; r < REPS; r++) {
            double t0 = now_ns(); sink = run_heap(in, N_RUN); t[0][r] = now_ns() - t0;
            t0 = now_ns(); sink = run_sorted(in, N_RUN); t[1][r] = now_ns() - t0;
        }
        qsort(t[0], REPS, sizeof(double), cmpd); qsort(t[1], REPS, sizeof(double), cmpd);
        printf("two heaps %.1f   sorted array insert %.1f   agree %s\n",
               t[0][REPS / 2] / N_RUN, t[1][REPS / 2] / N_RUN, c0 == c1 ? "yes" : "NO");
    }

    printf("\n[3] top-K largest of n=%d, ns/sample\n", N_TOPK);
    for (int K = 8; K <= 512; K *= 8) {
        double t[2][REPS];
        int64_t c0 = topk_heap(in, N_TOPK, K), c1 = topk_sort(in, N_TOPK, K);
        for (int r = 0; r < REPS; r++) {
            double t0 = now_ns(); sink = topk_heap(in, N_TOPK, K); t[0][r] = now_ns() - t0;
            t0 = now_ns(); sink = topk_sort(in, N_TOPK, K); t[1][r] = now_ns() - t0;
        }
        qsort(t[0], REPS, sizeof(double), cmpd); qsort(t[1], REPS, sizeof(double), cmpd);
        printf("K=%-4d size-K min-heap %.2f   copy+qsort all %.2f   agree %s\n",
               K, t[0][REPS / 2] / N_TOPK, t[1][REPS / 2] / N_TOPK, c0 == c1 ? "yes" : "NO");
    }

    printf("\n[4] lazy deletion: peak heap entries (valid + stale), k=31\n");
    printf("%-22s %8s %8s %8s\n", "input", "n=5000", "n=20000", "n=50000");
    static int32_t buf[N_RUN];
    const char *names[5] = { "random 0..4095", "random int32", "sine-like + noise", "ramp up", "constant" };
    for (int c = 0; c < 5; c++) {
        printf("%-22s", names[c]);
        static const int ns[3] = { 5000, 20000, 50000 };
        for (int j = 0; j < 3; j++) {
            uint32_t s2 = 777u;
            for (int i = 0; i < ns[j]; i++) {
                int ph = i % 400;
                buf[i] = c == 0 ? (int32_t)(xs32(&s2) % 4096u) : c == 1 ? (int32_t)xs32(&s2)
                       : c == 2 ? (ph < 200 ? ph : 400 - ph) * 10 + (int32_t)(xs32(&s2) % 101u)
                       : c == 3 ? i : 42;
            }
            sink = win_lazy(buf, ns[j], 31);
            printf(" %8d", lz_peak);
        }
        printf("\n");
    }
    printf("(indexed heaps: always exactly k = 31 entries)\n");
    printf("sizeof: wmed_t(KMAX=%d) = %zu B, lazy heaps for n=%d = %zu B\n", KMAX, sizeof(wmed_t), N_RUN, 2 * sizeof(pheap_t));
    return 0;
}
