/* BUILD: cc -std=c11 -Wall -Wextra -Werror -O2 demo_pick_bench.c -o demo_pick_bench && ./demo_pick_bench [exhaustive]
 *
 * RTOS-B04 demo — 두 가지를 잰다 (이 맥 = Apple Silicon, -O2. 절대 ns 보다 "비율" 을 볼 것):
 *   ① "가장 높은 ready 우선순위 찾기" 비용:
 *        linear  : 리스트 머리 32개를 31 → 0 으로 훑기 (FreeRTOS 일반 C 경로의 최악 — 캐시된 top 이 없을 때)
 *        my_clz  : 비트맵 + 손으로 쓴 이진 탐색 CLZ (Part A)
 *        builtin : 비트맵 + __builtin_clz (arm64 에서는 clz 명령 1개)
 *      패턴: idle 만 ready (bit 0 → linear 최악) / 무작위 비트맵 / bit 31 만 (linear 최선)
 *   ② (인자 exhaustive) my_clz32 를 x = 1 .. 0xFFFFFFFF 전부에서 __builtin_clz 와 대조 — 2^32 - 1 개
 */
#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int my_clz32(uint32_t x)
{
    if (x == 0) return 32;
    int n = 0;
    if (x <= 0x0000FFFFu) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFFu) { n += 8;  x <<= 8;  }
    if (x <= 0x0FFFFFFFu) { n += 4;  x <<= 4;  }
    if (x <= 0x3FFFFFFFu) { n += 2;  x <<= 2;  }
    if (x <= 0x7FFFFFFFu) { n += 1; }
    return n;
}

#define NPAT 256
static uint32_t    g_bm[NPAT];
static const void *g_heads[NPAT][32];     /* bit p 가 켜지면 head[p] != NULL 인 가짜 리스트 머리 */
static int         g_dummy;

static double now_s(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static uint32_t g_rng = 0xC0FFEE11u;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static void fill(int kind)
{
    for (int i = 0; i < NPAT; i++) {
        uint32_t bm = kind == 0 ? 1u : kind == 2 ? 0x80000000u : (xorshift32() | 1u);  /* idle 비트는 늘 켬 */
        g_bm[i] = bm;
        for (int p = 0; p < 32; p++) g_heads[i][p] = ((bm >> p) & 1u) ? &g_dummy : NULL;
    }
}

#define ITERS 20000000L
static volatile long g_sink;

static double run(int method)
{
    double best = 1e9;
    for (int rep = 0; rep < 5; rep++) {           /* 첫 회는 워밍업 겸, 5회 중 최소값 */
        long acc = 0;
        double t0 = now_s();
        for (long i = 0; i < ITERS; i++) {
            int k = (int)(i & (NPAT - 1));
            int p;
            if (method == 0) {
                p = 31;
                while (p > 0 && g_heads[k][p] == NULL) p--;
            } else if (method == 1) {
                p = 31 - my_clz32(g_bm[k]);
            } else {
                p = 31 - __builtin_clz(g_bm[k]);
            }
            acc += p;
        }
        double dt = now_s() - t0;
        g_sink = acc;
        if (dt < best) best = dt;
    }
    return best * 1e9 / ITERS;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "exhaustive") == 0) {
        double t0 = now_s();
        uint64_t mis = 0, n = 0;
        uint32_t x = 1;
        do { mis += (my_clz32(x) != __builtin_clz(x)); n++; } while (++x != 0);
        printf("exhaustive: %llu values (1..0xFFFFFFFF), mismatches %llu, %.1f s\n",
               (unsigned long long)n, (unsigned long long)mis, now_s() - t0);
        return mis != 0;
    }
    const char *names[3] = { "idle only (bit 0)", "random (bit0 set)", "bit 31 only" };
    printf("%-20s %10s %10s %10s   (ns/pick, best of 5 x %ld)\n", "pattern", "linear", "my_clz", "builtin", ITERS);
    for (int kind = 0; kind < 3; kind++) {
        fill(kind);
        double a = run(0), b = run(1), c = run(2);
        printf("%-20s %10.2f %10.2f %10.2f\n", names[kind], a, b, c);
    }
    return 0;
}
