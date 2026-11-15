/* ALGO-M08 측정 (이 맥, Apple Silicon M 시리즈, -O2) — 절대값보다 비율을 볼 것.
 * 1) 정의 재귀(표 없음) 호출 수 vs DP 칸 수   2) 편집 거리 2D 표 vs 한 줄: ns/칸, 메모리
 * 3) 0/1 배낭 2D 표 vs 1D vs 1D + 결정 비트셋: 시간, 메모리, 같은 최적값인지
 * 각 측정은 warm-up 1회 + 7회 중앙값. 결과는 volatile sink 에 모아 컴파일러가 지우지 못하게 한다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
static int cmpd(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }
static volatile uint64_t sink;

static uint64_t g_calls;
static int ref_ed(const char *a, int n, const char *b, int m) {
    g_calls++;
    if (n == 0) return m;
    if (m == 0) return n;
    int best = ref_ed(a, n - 1, b, m) + 1, t = ref_ed(a, n, b, m - 1) + 1;
    if (t < best) best = t;
    t = ref_ed(a, n - 1, b, m - 1) + (a[n - 1] != b[m - 1]);
    return t < best ? t : best;
}

#define NE 4000
static uint16_t ED[NE + 1][NE + 1];
static int ed_2d(const char *a, int n, const char *b, int m) {
    for (int i = 0; i <= n; i++) ED[i][0] = (uint16_t)i;
    for (int j = 0; j <= m; j++) ED[0][j] = (uint16_t)j;
    for (int i = 1; i <= n; i++)
        for (int j = 1; j <= m; j++) {
            int best = ED[i - 1][j - 1] + (a[i - 1] != b[j - 1]);
            if (ED[i - 1][j] + 1 < best) best = ED[i - 1][j] + 1;
            if (ED[i][j - 1] + 1 < best) best = ED[i][j - 1] + 1;
            ED[i][j] = (uint16_t)best;
        }
    return ED[n][m];
}
static int ed_1row(const char *a, int n, const char *b, int m) {
    static uint16_t row[NE + 1];
    for (int j = 0; j <= m; j++) row[j] = (uint16_t)j;
    for (int i = 1; i <= n; i++) {
        uint16_t diag = row[0];
        row[0] = (uint16_t)i;
        for (int j = 1; j <= m; j++) {
            uint16_t up = row[j];
            int best = diag + (a[i - 1] != b[j - 1]);
            if (up + 1 < best) best = up + 1;
            if (row[j - 1] + 1 < best) best = row[j - 1] + 1;
            row[j] = (uint16_t)best;
            diag = up;
        }
    }
    return row[m];
}

#define KN 64
#define KW 65536
static uint32_t K2[KN + 1][KW + 1];
static uint32_t knap_2d(int n, const uint32_t *s, const uint32_t *v, uint32_t W) {
    for (uint32_t w = 0; w <= W; w++) K2[0][w] = 0;
    for (int i = 1; i <= n; i++)
        for (uint32_t w = 0; w <= W; w++) {
            uint32_t best = K2[i - 1][w];
            if (s[i - 1] <= w && K2[i - 1][w - s[i - 1]] + v[i - 1] > best) best = K2[i - 1][w - s[i - 1]] + v[i - 1];
            K2[i][w] = best;
        }
    return K2[n][W];
}
static uint32_t dp1[KW + 1];
static uint8_t take[KN][KW / 8 + 1];
static uint32_t knap_1d(int n, const uint32_t *s, const uint32_t *v, uint32_t W, int with_bits) {
    for (uint32_t w = 0; w <= W; w++) dp1[w] = 0;
    for (int i = 0; i < n; i++) {
        if (with_bits) memset(take[i], 0, W / 8 + 1);
        if (s[i] > W) continue;
        for (uint32_t w = W + 1; w-- > s[i]; ) {
            uint32_t t = dp1[w - s[i]] + v[i];
            if (t > dp1[w]) { dp1[w] = t; if (with_bits) take[i][w >> 3] |= (uint8_t)(1u << (w & 7)); }
        }
    }
    return dp1[W];
}

#define REPS 7
int main(void) {
    uint32_t seed = 0xBE4C8u;
    double t[REPS];

    printf("[1] edit distance by definition recursion (no table), strings of distinct letters\n");
    printf("  n=m   calls          DP cells (n+1)^2   ms\n");
    const char *A = "abcdefghijklmn", *B = "nopqrstuvwxyzA";
    for (int n = 4; n <= 12; n += 2) {
        g_calls = 0;
        double t0 = now_ns();
        sink += (uint64_t)ref_ed(A, n, B, n);
        double t1 = now_ns();
        printf("  %3d   %-13llu  %-17d  %.2f\n", n, (unsigned long long)g_calls, (n + 1) * (n + 1), (t1 - t0) / 1e6);
    }

    static char a[NE + 1], b[NE + 1];
    for (int i = 0; i < NE; i++) { a[i] = (char)('a' + xs32(&seed) % 4u); b[i] = (char)('a' + xs32(&seed) % 4u); }
    printf("\n[2] edit distance, random |S|=4 strings, median of %d\n", REPS);
    printf("  n=m    2D ms    1-row ms   2D ns/cell  1-row ns/cell  2D table bytes  1-row bytes  same\n");
    const int sizes[3] = { 1000, 2000, 4000 };
    for (int k = 0; k < 3; k++) {
        int n = sizes[k];
        int r2 = ed_2d(a, n, b, n), r1 = ed_1row(a, n, b, n);       /* warm-up + 정답 비교 */
        double med[2];
        for (int which = 0; which < 2; which++) {
            for (int r = 0; r < REPS; r++) {
                double t0 = now_ns();
                sink += (uint64_t)(which ? ed_1row(a, n, b, n) : ed_2d(a, n, b, n));
                t[r] = now_ns() - t0;
            }
            qsort(t, REPS, sizeof t[0], cmpd);
            med[which] = t[REPS / 2];
        }
        double cells = (double)n * n;
        printf("  %4d  %7.2f  %8.2f   %9.3f  %12.3f  %14zu  %11zu  %s (d=%d)\n", n, med[0] / 1e6, med[1] / 1e6,
               med[0] / cells, med[1] / cells, (size_t)(n + 1) * (n + 1) * 2, (size_t)(n + 1) * 2, r1 == r2 ? "yes" : "NO", r1);
    }

    printf("\n[3] 0/1 knapsack, n=%d random items (size 1..W/8, value 1..1000), median of %d\n", KN, REPS);
    printf("  W       2D ms    1D ms   1D+bits ms   2D bytes    1D bytes  1D+bits bytes  same optimum\n");
    static uint32_t s[KN], v[KN];
    const uint32_t Ws[3] = { 4096, 16384, 65536 };
    for (int k = 0; k < 3; k++) {
        uint32_t W = Ws[k];
        for (int i = 0; i < KN; i++) { s[i] = 1 + xs32(&seed) % (W / 8); v[i] = 1 + xs32(&seed) % 1000u; }
        uint32_t r2 = knap_2d(KN, s, v, W), r1 = knap_1d(KN, s, v, W, 0), rb = knap_1d(KN, s, v, W, 1);
        double med[3];
        for (int which = 0; which < 3; which++) {
            for (int r = 0; r < REPS; r++) {
                double t0 = now_ns();
                sink += which == 0 ? knap_2d(KN, s, v, W) : knap_1d(KN, s, v, W, which == 2);
                t[r] = now_ns() - t0;
            }
            qsort(t, REPS, sizeof t[0], cmpd);
            med[which] = t[REPS / 2];
        }
        printf("  %-6u %7.2f  %7.2f  %10.2f  %10zu  %9zu  %13zu  %s (%u)\n", W, med[0] / 1e6, med[1] / 1e6, med[2] / 1e6,
               (size_t)(KN + 1) * (W + 1) * 4, (size_t)(W + 1) * 4, (size_t)(W + 1) * 4 + (size_t)KN * (W / 8 + 1),
               r1 == r2 && rb == r2 ? "yes" : "NO", r2);
    }
    printf("(sink %llu)\n", (unsigned long long)sink);
    return 0;
}
