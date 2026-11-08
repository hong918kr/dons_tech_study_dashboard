/* OS-M02 demo_timer.c — 맥의 시계들: 해상도(getres) · 실제 최소 눈금 · 호출 비용
 *
 * 왕복 2~3 µs 를 재는데 시계 눈금이 1 µs 면 표본 하나에 ±30% 오차가 생긴다. 어느 시계를 써야 하나?
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_timer.c -o dt && ./dt
 */
#define _DARWIN_C_SOURCE
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define N 2000000

static void probe(const char *name, clockid_t id) {
    struct timespec r;
    clock_getres(id, &r);
    uint64_t mind = UINT64_MAX, a = clock_gettime_nsec_np(id), t0 = a;
    for (int i = 0; i < N; i++) {
        uint64_t b = clock_gettime_nsec_np(id);
        if (b != a && b - a < mind) mind = b - a;
        a = b;
    }
    printf("%-20s getres %5ld ns   min step %5llu ns   %.1f ns/call\n", name, r.tv_nsec,
           (unsigned long long)mind, (double)(a - t0) / N);
}

int main(void) {
    probe("CLOCK_REALTIME", CLOCK_REALTIME);
    probe("CLOCK_MONOTONIC", CLOCK_MONOTONIC);
    probe("CLOCK_MONOTONIC_RAW", CLOCK_MONOTONIC_RAW);
    probe("CLOCK_UPTIME_RAW", CLOCK_UPTIME_RAW);

    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    uint64_t m = UINT64_MAX, x = mach_absolute_time(), t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    for (int i = 0; i < N; i++) {
        uint64_t y = mach_absolute_time();
        if (y != x && y - x < m) m = y - x;
        x = y;
    }
    uint64_t t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    printf("mach_absolute_time   timebase %u/%u  min step %llu tick = %.2f ns   %.1f ns/call\n", tb.numer, tb.denom,
           (unsigned long long)m, (double)m * tb.numer / tb.denom, (double)(t1 - t0) / N);

    /* 같은 시계로 "거의 아무것도 안 하는" 구간을 연속으로 재면? → 0 과 41/42 ns 만 나온다 (양자화) */
    unsigned hist[4] = { 0 };
    for (int i = 0; i < 100000; i++) {
        uint64_t a = mach_absolute_time(), b = mach_absolute_time();
        uint64_t d = b - a;
        hist[d < 3 ? d : 3]++;
    }
    printf("back-to-back mach_absolute_time deltas: 0 tick %u, 1 tick %u, 2 ticks %u, >=3 %u (of 100000)\n",
           hist[0], hist[1], hist[2], hist[3]);
    return 0;
}
