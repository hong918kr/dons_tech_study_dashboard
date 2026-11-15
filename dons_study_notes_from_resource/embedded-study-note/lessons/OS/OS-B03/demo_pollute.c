/* OS-B03 demo_pollute.c — 시스템 콜의 "간접 비용": 커널이 내 캐시를 밀어낸다
 *
 * 반복 한 번 = 크기 B 의 배열을 캐시 라인(128 B) 간격으로 한 바퀴 읽기 (+ 선택: getppid 1 회).
 *   work  : 배열만      sys : getppid 만      both : 배열 + getppid
 *   indirect = both − work − sys   (0 이면 직접 비용만, 양수면 커널이 캐시·TLB·분기 예측을 흐려 놓은 값)
 * 작업 집합이 L1D 에 겨우 들어갈 때 가장 크게 보인다. 코어 고정이 안 되는 macOS 라
 * P/E 코어(L1D 64~128 KiB) 사이를 오가면 흔들린다 — 크기별 median 9 회.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_pollute.c -o pollute && ./pollute
 */
#define _DARWIN_C_SOURCE
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BARRIER() __asm__ volatile("" ::: "memory")
#define LINE 128u
#define RUNS 9

static mach_timebase_info_data_t g_tb;
static volatile uint64_t g_sink;
static uint8_t *g_buf;

enum { WORK = 1, SYS = 2 };

static double per_iter_ns(size_t bytes, int what, int iters) {
    uint64_t acc = 0;
    uint64_t t0 = mach_absolute_time();
    for (int it = 0; it < iters; it++) {
        if (what & WORK)
            for (size_t i = 0; i < bytes; i += LINE) acc += g_buf[i];
        if (what & SYS) acc += (uint64_t)getppid();
        BARRIER();
    }
    uint64_t t1 = mach_absolute_time();
    g_sink += acc;
    return (double)(t1 - t0) * g_tb.numer / g_tb.denom / iters;
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(void) {
    mach_timebase_info(&g_tb);
    const size_t max = 1u << 20;
    g_buf = malloc(max);
    if (!g_buf) return 1;
    for (size_t i = 0; i < max; i++) g_buf[i] = (uint8_t)i;

    static const size_t sizes[] = {8u << 10, 32u << 10, 48u << 10, 64u << 10, 96u << 10,
                                   112u << 10, 128u << 10, 256u << 10, 1u << 20};
    printf("%9s %10s %8s %10s %10s\n", "bytes", "work", "sys", "both", "indirect");
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        size_t b = sizes[s];
        int iters = (int)(20000000u / (b / LINE + 50u));
        if (iters < 300) iters = 300;
        double w[RUNS], y[RUNS], z[RUNS];
        per_iter_ns(b, WORK | SYS, iters);                     /* 워밍업 */
        for (int r = 0; r < RUNS; r++) {                       /* 셋을 번갈아 → 드리프트를 고르게 */
            w[r] = per_iter_ns(b, WORK, iters);
            y[r] = per_iter_ns(b, SYS, iters);
            z[r] = per_iter_ns(b, WORK | SYS, iters);
        }
        qsort(w, RUNS, sizeof w[0], cmp_d);
        qsort(y, RUNS, sizeof y[0], cmp_d);
        qsort(z, RUNS, sizeof z[0], cmp_d);
        double wm = w[RUNS / 2], ym = y[RUNS / 2], zm = z[RUNS / 2];
        printf("%6zu KiB %10.1f %8.1f %10.1f %+10.1f\n", b >> 10, wm, ym, zm, zm - wm - ym);
    }
    free(g_buf);
    return 0;
}
