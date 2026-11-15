/* ALGO-M13 속도 비교 (이 맥, Apple Silicon M 시리즈, -O2). 절대 숫자보다 "비율"을 볼 것.
 * 1) 1D: 2^20 개 12비트 샘플, 2^16 개 랜덤 구간 합 — 매번 직접 더하기 vs 누적합(만들기 + 질의)
 * 2) 640×480 integral image 만들기: ns/pixel
 * 3) 박스 합 (잘린 (2r+1)² 창, 모든 픽셀): 직접 더하기 vs integral image vs 열 합(줄 버퍼), r = 1,3,7,15,31
 * 각 항목: 워밍업 1회 + 여러 번 중 중앙값. 결과는 체크섬으로 모아 출력(컴파일러가 지우지 못하게, 세 방법이 같은지도 확인).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N1 (1u << 20)
#define Q1 (1u << 16)
#define W 640
#define H 480

static uint16_t a[N1];
static uint32_t P[N1 + 1];
static uint32_t ql[Q1], qr[Q1];
static uint16_t img[W * H];
static uint32_t I[(W + 1) * (H + 1)], col[W], out[W * H];

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static int cmpd(const void *x, const void *y) { double p = *(const double *)x, q = *(const double *)y; return (p > q) - (p < q); }

static uint64_t naive_queries(void) {
    uint64_t chk = 0;
    for (uint32_t q = 0; q < Q1; q++) { uint32_t s = 0; for (uint32_t i = ql[q]; i < qr[q]; i++) s += a[i]; chk += s; }
    return chk;
}
static uint64_t prefix_queries(void) {
    P[0] = 0;
    for (uint32_t i = 0; i < N1; i++) P[i + 1] = P[i] + a[i];
    uint64_t chk = 0;
    for (uint32_t q = 0; q < Q1; q++) chk += P[qr[q]] - P[ql[q]];
    return chk;
}
static void integral_build(void) {
    const size_t WW = W + 1;
    for (size_t x = 0; x <= W; x++) I[x] = 0;
    for (size_t y = 0; y < H; y++) {
        uint32_t row = 0;
        I[(y + 1) * WW] = 0;
        for (size_t x = 0; x < W; x++) { row += img[y * W + x]; I[(y + 1) * WW + x + 1] = I[y * WW + x + 1] + row; }
    }
}
static size_t g_r;
static uint64_t box_naive(void) {
    size_t r = g_r; uint64_t chk = 0;
    for (size_t y = 0; y < H; y++) {
        size_t y0 = y >= r ? y - r : 0, y1 = y + r + 1 < H ? y + r + 1 : H;
        for (size_t x = 0; x < W; x++) {
            size_t x0 = x >= r ? x - r : 0, x1 = x + r + 1 < W ? x + r + 1 : W;
            uint32_t s = 0;
            for (size_t yy = y0; yy < y1; yy++) for (size_t xx = x0; xx < x1; xx++) s += img[yy * W + xx];
            out[y * W + x] = s; chk += s;
        }
    }
    return chk;
}
static uint64_t box_integral(void) {
    size_t r = g_r; uint64_t chk = 0; const size_t WW = W + 1;
    integral_build();
    for (size_t y = 0; y < H; y++) {
        size_t y0 = y >= r ? y - r : 0, y1 = y + r + 1 < H ? y + r + 1 : H;
        for (size_t x = 0; x < W; x++) {
            size_t x0 = x >= r ? x - r : 0, x1 = x + r + 1 < W ? x + r + 1 : W;
            uint32_t s = I[y1 * WW + x1] - I[y0 * WW + x1] - I[y1 * WW + x0] + I[y0 * WW + x0];
            out[y * W + x] = s; chk += s;
        }
    }
    return chk;
}
static uint64_t box_colsum(void) {
    size_t r = g_r; uint64_t chk = 0;
    for (size_t x = 0; x < W; x++) { col[x] = 0; for (size_t y = 0; y <= r && y < H; y++) col[x] += img[y * W + x]; }
    for (size_t y = 0; y < H; y++) {
        if (y > 0) {
            if (y + r < H) for (size_t x = 0; x < W; x++) col[x] += img[(y + r) * W + x];
            if (y >= r + 1) for (size_t x = 0; x < W; x++) col[x] -= img[(y - r - 1) * W + x];
        }
        uint32_t s = 0;
        for (size_t x = 0; x <= r && x < W; x++) s += col[x];
        for (size_t x = 0; x < W; x++) {
            if (x > 0) { if (x + r < W) s += col[x + r]; if (x >= r + 1) s -= col[x - r - 1]; }
            out[y * W + x] = s; chk += s;
        }
    }
    return chk;
}

/* fn 을 워밍업 1회 + reps 번 돌려 중앙값(ns)을 돌려준다 */
static double bench(uint64_t (*fn)(void), int reps, uint64_t *chk) {
    double t[15];
    *chk = fn();
    for (int i = 0; i < reps; i++) { double t0 = now_ns(); uint64_t c = fn(); t[i] = now_ns() - t0; if (c != *chk) printf("unstable!\n"); }
    qsort(t, (size_t)reps, sizeof t[0], cmpd);
    return t[reps / 2];
}
static uint64_t build_only(void) { integral_build(); return I[(W + 1) * (H + 1) - 1]; }

int main(void) {
    uint32_t seed = 0xC0FFEEu;
    for (uint32_t i = 0; i < N1; i++) a[i] = (uint16_t)(xs32(&seed) % 4096u);
    for (uint32_t q = 0; q < Q1; q++) {
        uint32_t l = xs32(&seed) % (N1 + 1), r = xs32(&seed) % (N1 + 1);
        if (l > r) { uint32_t t = l; l = r; r = t; }
        ql[q] = l; qr[q] = r;
    }
    for (size_t i = 0; i < W * H; i++) img[i] = (uint16_t)(xs32(&seed) & 0xFFu);

    uint64_t c1, c2;
    double tn = bench(naive_queries, 3, &c1), tp = bench(prefix_queries, 9, &c2);
    printf("1D: n=%u samples, %u random range queries (avg len ~n/3)\n", N1, Q1);
    printf("   naive sum per query     : %10.2f ms\n", tn / 1e6);
    printf("   prefix build + queries  : %10.2f ms   (speedup x%.0f, results %s)\n", tp / 1e6, tn / tp, c1 == c2 ? "equal" : "DIFFER");

    uint64_t cb;
    double tb = bench(build_only, 15, &cb);
    printf("integral image build %dx%d: %.3f ms = %.2f ns/pixel, table %zu bytes\n", W, H, tb / 1e6, tb / (W * H), sizeof I);

    printf("box sum over all pixels (%dx%d), clamped (2r+1)^2 window:\n", W, H);
    printf("   %3s %8s %12s %14s %14s %8s\n", "r", "window", "naive ms", "integral ms", "colsum ms", "equal");
    const size_t rs[5] = { 1, 3, 7, 15, 31 };
    for (int k = 0; k < 5; k++) {
        g_r = rs[k];
        uint64_t x1, x2, x3;
        double t1 = bench(box_naive, rs[k] >= 15 ? 3 : 5, &x1), t2 = bench(box_integral, 9, &x2), t3 = bench(box_colsum, 9, &x3);
        printf("   %3zu %5zux%-3zu %12.2f %14.3f %14.3f %8s\n", rs[k], 2 * rs[k] + 1, 2 * rs[k] + 1,
               t1 / 1e6, t2 / 1e6, t3 / 1e6, (x1 == x2 && x2 == x3) ? "yes" : "NO");
    }
    printf("extra memory: integral %zu B (uint32 (W+1)(H+1)), colsum %zu B (uint32 W) + 2r+2 input rows if streaming\n",
           sizeof I, sizeof col);
    return 0;
}
