/* ALGO-B05 demo_bench — 비교·교환 횟수 표 + 공식 대조 + 중앙값 창 비용 + 쓰기 횟수 + 시간 측정
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 * 시간은 이 맥(Apple Silicon M 시리즈, -O2) 기준. 절대 ns 보다 비율을 볼 것.
 */
#define _POSIX_C_SOURCE 199309L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { int16_t key; uint16_t idx; } Rec;
typedef struct { uint32_t cmp, swaps, shifts, writes, passes; } SortStats;

static int rec_less(Rec x, Rec y, SortStats *st) { st->cmp++; return x.key < y.key; }
static void rec_swap(Rec *a, int i, int j, SortStats *st) { Rec t = a[i]; a[i] = a[j]; a[j] = t; st->swaps++; st->writes += 2; }

static void insertion_sort(Rec *a, int n, SortStats *st) {
    for (int i = 1; i < n; i++) {
        Rec x = a[i]; int j = i;
        while (j > 0 && rec_less(x, a[j - 1], st)) { a[j] = a[j - 1]; st->shifts++; st->writes++; j--; }
        if (j != i) { a[j] = x; st->writes++; }
    }
}
static void selection_sort(Rec *a, int n, SortStats *st) {
    for (int i = 0; i + 1 < n; i++) {
        int m = i;
        for (int j = i + 1; j < n; j++) if (rec_less(a[j], a[m], st)) m = j;
        if (m != i) rec_swap(a, i, m, st);
    }
}
static void bubble_sort(Rec *a, int n, SortStats *st) {
    for (int pass = 0; pass + 1 < n; pass++) {
        int swapped = 0; st->passes++;
        for (int j = 0; j + 1 < n - pass; j++)
            if (rec_less(a[j + 1], a[j], st)) { rec_swap(a, j, j + 1, st); swapped = 1; }
        if (!swapped) break;
    }
}

static uint32_t g_rng = 0xC0FFEE11u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

enum { IN_RANDOM, IN_REVERSED, IN_NEARLY, IN_TURTLE, IN_COUNT };
static const char *in_name[IN_COUNT] = { "random", "reversed", "nearly(k=n/16)", "turtle" };
static void make_input(Rec *a, int n, int kind) {
    for (int i = 0; i < n; i++) {
        a[i].idx = (uint16_t)i;
        switch (kind) {
        case IN_RANDOM:   a[i].key = (int16_t)(xs32() % 30000u); break;
        case IN_REVERSED: a[i].key = (int16_t)(n - 1 - i); break;
        default:          a[i].key = (int16_t)i; break;
        }
    }
    if (kind == IN_NEARLY) {                       /* 정렬 상태에서 무작위 인접 교환 k = n/16 번 */
        for (int s = 0; s < n / 16; s++) {
            int p = (int)(xs32() % (uint32_t)(n - 1));
            int16_t t = a[p].key; a[p].key = a[p + 1].key; a[p + 1].key = t;
        }
    }
    if (kind == IN_TURTLE) {                       /* 1,2,...,n-1, 0 : 가장 작은 값 하나가 맨 끝 */
        for (int i = 0; i < n; i++) a[i].key = (int16_t)(i + 1 == n ? 0 : i + 1);
    }
}
static uint32_t inversions(const Rec *a, int n) {
    uint32_t c = 0;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) c += a[i].key > a[j].key;
    return c;
}
static uint32_t new_prefix_min(const Rec *a, int n) {
    uint32_t c = 0;
    for (int i = 1, m = a[0].key; i < n; i++) if (a[i].key < m) { c++; m = a[i].key; }
    return c;
}
static uint32_t max_left_greater(const Rec *a, int n) {
    uint32_t best = 0;
    for (int i = 0; i < n; i++) { uint32_t c = 0; for (int j = 0; j < i; j++) c += a[j].key > a[i].key; if (c > best) best = c; }
    return best;
}

/* ---- 시간 측정용: 계수기 없는 판 ---- */
static void ins_plain(Rec *a, int n) {
    for (int i = 1; i < n; i++) { Rec x = a[i]; int j = i; while (j > 0 && x.key < a[j - 1].key) { a[j] = a[j - 1]; j--; } a[j] = x; }
}
static void sel_plain(Rec *a, int n) {
    for (int i = 0; i + 1 < n; i++) { int m = i; for (int j = i + 1; j < n; j++) if (a[j].key < a[m].key) m = j;
        if (m != i) { Rec t = a[i]; a[i] = a[m]; a[m] = t; } }
}
/* 같은 선택 정렬, 최솟값 키를 레지스터(mk)에 둔 판: 루프마다 a[m] 을 다시 읽는 의존 사슬이 없다 */
static void sel_mk(Rec *a, int n) {
    for (int i = 0; i + 1 < n; i++) { int m = i; int16_t mk = a[i].key;
        for (int j = i + 1; j < n; j++) if (a[j].key < mk) { mk = a[j].key; m = j; }
        if (m != i) { Rec t = a[i]; a[i] = a[m]; a[m] = t; } }
}
static void bub_plain(Rec *a, int n) {
    for (int p = 0; p + 1 < n; p++) { int sw = 0;
        for (int j = 0; j + 1 < n - p; j++) if (a[j + 1].key < a[j].key) { Rec t = a[j]; a[j] = a[j + 1]; a[j + 1] = t; sw = 1; }
        if (!sw) break; }
}
static int cmp_rec(const void *p, const void *q) { const Rec *x = p, *y = q; return (x->key > y->key) - (x->key < y->key); }
static void qsort_plain(Rec *a, int n) { qsort(a, (size_t)n, sizeof *a, cmp_rec); }

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e9 + (double)t.tv_nsec; }
static int cmp_dbl(const void *p, const void *q) { double x = *(const double *)p, y = *(const double *)q; return (x > y) - (x < y); }
static volatile long g_sink;

static double time_sort(void (*fn)(Rec *, int), const Rec *src, int n) {
    static Rec buf[1024];
    int reps = 1;
    for (;;) {                                         /* 워밍업 겸 반복 횟수 정하기: 한 번 측정이 ≥ 0.2 ms 되게 */
        double t0 = now_ns();
        for (int k = 0; k < reps; k++) { memcpy(buf, src, (size_t)n * sizeof *buf); fn(buf, n); g_sink += buf[n / 2].key; }
        if (now_ns() - t0 >= 2e5) break;
        reps *= 2;
    }
    double runs[9];
    for (int r = 0; r < 9; r++) {
        double t0 = now_ns();
        for (int k = 0; k < reps; k++) { memcpy(buf, src, (size_t)n * sizeof *buf); fn(buf, n); g_sink += buf[n / 2].key; }
        runs[r] = (now_ns() - t0) / reps;
    }
    qsort(runs, 9, sizeof runs[0], cmp_dbl);
    return runs[4];
}

int main(void) {
    static Rec in[1024], a[1024];
    const int ns[] = { 16, 64, 256, 1024 };

    printf("== 1. counts (one fixed-seed instance per row); formula check: ins cmp = inv+n-1-z, shifts = inv;\n"
           "      sel cmp = n(n-1)/2; bub swaps = inv, cmp = P(n-1)-P(P-1)/2 with P = min(n-1, maxL+1)\n");
    printf("%5s %-15s %7s | %7s %7s %6s | %7s %5s %5s | %7s %7s %5s | %s\n", "n", "input", "inv",
           "ins.cmp", "shifts", "writes", "sel.cmp", "swaps", "wr", "bub.cmp", "swaps", "pass", "formula");
    for (int q = 0; q < 4; q++) {
        int n = ns[q]; uint32_t nn = (uint32_t)n;
        for (int k = 0; k < IN_COUNT; k++) {
            make_input(in, n, k);
            uint32_t inv = inversions(in, n), z = new_prefix_min(in, n), L = max_left_greater(in, n);
            SortStats s[3]; memset(s, 0, sizeof s);
            memcpy(a, in, sizeof(Rec) * (size_t)n); insertion_sort(a, n, &s[0]);
            memcpy(a, in, sizeof(Rec) * (size_t)n); selection_sort(a, n, &s[1]);
            memcpy(a, in, sizeof(Rec) * (size_t)n); bubble_sort(a, n, &s[2]);
            uint32_t P = L + 1 < nn - 1 ? L + 1 : nn - 1;
            int ok = s[0].cmp == inv + nn - 1 - z && s[0].shifts == inv && s[1].cmp == nn * (nn - 1) / 2 &&
                     s[2].swaps == inv && s[2].passes == P && s[2].cmp == P * (nn - 1) - P * (P - 1) / 2;
            printf("%5d %-15s %7u | %7u %7u %6u | %7u %5u %5u | %7u %7u %5u | %s (z=%u maxL=%u)\n", n, in_name[k], inv,
                   s[0].cmp, s[0].shifts, s[0].writes, s[1].cmp, s[1].swaps, s[1].writes,
                   s[2].cmp, s[2].swaps, s[2].passes, ok ? "OK" : "MISMATCH", z, L);
        }
    }

    printf("\n== 2. random permutations, mean over 200 trials vs expectations\n");
    for (int q = 0; q < 4; q++) {
        int n = ns[q]; double sinv = 0, sz = 0, spass = 0, sswap = 0;
        for (int t = 0; t < 200; t++) {
            for (int i = 0; i < n; i++) { in[i].key = (int16_t)i; in[i].idx = (uint16_t)i; }
            for (int i = n - 1; i > 0; i--) { int j = (int)(xs32() % (uint32_t)(i + 1)); int16_t x = in[i].key; in[i].key = in[j].key; in[j].key = x; }
            SortStats s[2]; memset(s, 0, sizeof s);
            sinv += inversions(in, n); sz += new_prefix_min(in, n);
            memcpy(a, in, sizeof(Rec) * (size_t)n); bubble_sort(a, n, &s[0]); spass += s[0].passes;
            memcpy(a, in, sizeof(Rec) * (size_t)n); selection_sort(a, n, &s[1]); sswap += s[1].swaps;
        }
        double H = 0; for (int i = 1; i <= n; i++) H += 1.0 / i;
        printf("n=%4d  inv %9.1f (n(n-1)/4=%9.1f)  z %5.2f (H_n-1=%5.2f)  bubble passes %7.1f (n-sqrt(pi n/2)=%7.1f)  sel swaps %7.1f (n-H_n=%7.1f)\n",
               n, sinv / 200, n * (n - 1) / 4.0, sz / 200, H - 1, spass / 200, n - sqrt(3.141592653589793 * n / 2), sswap / 200, n - H);
    }

    printf("\n== 3. median window: key comparisons per new sample (10000 samples, 12-bit noise + spikes)\n");
    for (int N = 9; N <= 32; N += 23) {
        int16_t ring[32] = {0}, sorted[32] = {0}, tmp[32];
        int head = 0, count = 0;
        double sum_inc = 0, sum_ins = 0, sum_sel = 0; uint32_t max_inc = 0, max_ins = 0;
        for (int t = 0; t < 10000; t++) {
            int16_t x = (int16_t)(2048 + (int)(xs32() % 41u) - 20);
            if (xs32() % 50u == 0) x = 4095;
            /* (a) 증분: 정렬 창에서 가장 오래된 값을 x 로 바꾸고 삽입 정렬 */
            if (count < N) sorted[count++] = x;
            else { int p = 0; while (p < count - 1 && sorted[p] != ring[head]) p++; sorted[p] = x; }
            ring[head] = x; head = (head + 1) % N;
            uint32_t c = 0;
            for (int i = 1; i < count; i++) { int16_t v = sorted[i]; int j = i;
                while (j > 0) { c++; if (!(v < sorted[j - 1])) break; sorted[j] = sorted[j - 1]; j--; } sorted[j] = v; }
            if (t >= N) { sum_inc += c; if (c > max_inc) max_inc = c; }
            /* (b) 매번 도착 순서 사본을 삽입 정렬 / (c) 선택 정렬 */
            if (t >= N) {
                memcpy(tmp, ring, sizeof tmp); c = 0;
                for (int i = 1; i < N; i++) { int16_t v = tmp[i]; int j = i;
                    while (j > 0) { c++; if (!(v < tmp[j - 1])) break; tmp[j] = tmp[j - 1]; j--; } tmp[j] = v; }
                sum_ins += c; if (c > max_ins) max_ins = c;
                sum_sel += (double)N * (N - 1) / 2;
            }
        }
        int m = 10000 - N;
        printf("N=%2d  incremental avg %6.1f max %4u | re-sort copy (insertion) avg %6.1f max %4u | selection always %6.1f\n",
               N, sum_inc / m, max_inc, sum_ins / m, max_ins, sum_sel / m);
    }

    printf("\n== 4. in-place sort of 32 records in 'EEPROM': array writes\n");
    for (int k = 0; k < IN_COUNT; k++) {
        make_input(in, 32, k);
        SortStats s[3]; memset(s, 0, sizeof s);
        memcpy(a, in, sizeof(Rec) * 32); insertion_sort(a, 32, &s[0]);
        memcpy(a, in, sizeof(Rec) * 32); selection_sort(a, 32, &s[1]);
        memcpy(a, in, sizeof(Rec) * 32); bubble_sort(a, 32, &s[2]);
        printf("%-15s writes ins=%4u sel=%4u bub=%4u\n", in_name[k], s[0].writes, s[1].writes, s[2].writes);
    }

    printf("\n== 5. time per sort (ns, median of 9 runs, includes memcpy of the input; this Mac, -O2)\n");
    printf("%5s %10s %10s %10s %10s %10s %12s\n", "n", "insertion", "selection", "sel(mk)", "bubble", "qsort", "ins(nearly)");
    const int tn[] = { 8, 16, 32, 64, 256, 1024 };
    for (int q = 0; q < 6; q++) {
        int n = tn[q];
        make_input(in, n, IN_RANDOM);
        double ti = time_sort(ins_plain, in, n), ts = time_sort(sel_plain, in, n), tm = time_sort(sel_mk, in, n);
        double tb = time_sort(bub_plain, in, n), tq = time_sort(qsort_plain, in, n);
        make_input(a, n, IN_NEARLY);
        double tn2 = time_sort(ins_plain, a, n);
        printf("%5d %10.1f %10.1f %10.1f %10.1f %10.1f %12.1f\n", n, ti, ts, tm, tb, tq, tn2);
    }
    printf("(sink %ld)\n", (long)g_sink);
    return 0;
}
