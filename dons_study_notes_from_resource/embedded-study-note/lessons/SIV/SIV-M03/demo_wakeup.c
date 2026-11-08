/* SIV-M03 demo — 호스트에서 '진짜' 지연을 재 본다 (cyclictest 의 축소판).
 * 1 ms 주기로 절대 시각 마감을 정해 두고 nanosleep 으로 기다린 뒤, 깨어난 시각 - 마감 = wake-up latency.
 * 시뮬레이션과 같은 방식(wrap-safe 차이 → 선형 bin 히스토그램 → nearest-rank 분위수)으로 요약한다.
 * 측정값은 기계·부하마다 다르므로 체크(CHECK)하지 않는다 — md 에 실제로 잰 숫자만 적는다.
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_wakeup.c -o /tmp/SIV-M03_wk && /tmp/SIV-M03_wk [N]
 */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BIN_NS 10000u   /* 10 us */
#define NB     64

/* macOS 의 CLOCK_MONOTONIC 은 해상도가 1 us (clock_getres = 1000 ns) → ns 해상도인 CLOCK_UPTIME_RAW 사용.
 * Linux 에는 그 이름이 없으므로 CLOCK_MONOTONIC (보통 1 ns 해상도). */
#ifdef CLOCK_UPTIME_RAW
#define DEMO_CLOCK CLOCK_UPTIME_RAW
#define DEMO_CLOCK_NAME "CLOCK_UPTIME_RAW"
#else
#define DEMO_CLOCK CLOCK_MONOTONIC
#define DEMO_CLOCK_NAME "CLOCK_MONOTONIC"
#endif

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(DEMO_CLOCK, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 2000;
    if (n <= 0 || n > 1000000) n = 2000;
    uint32_t cnt[NB + 1] = {0};
    uint64_t sum = 0, mx = 0, mn = UINT64_MAX;
    const uint64_t period = 1000000;                 /* 1 ms */
    uint64_t deadline = now_ns() + period;
    for (int i = 0; i < n; i++) {
        uint64_t t = now_ns();
        if (deadline > t) {
            uint64_t d = deadline - t;
            struct timespec ts = {(time_t)(d / 1000000000ull), (long)(d % 1000000000ull)};
            nanosleep(&ts, NULL);
        }
        uint64_t woke = now_ns();
        uint64_t lat = woke > deadline ? woke - deadline : 0;
        uint64_t b = lat / BIN_NS;
        cnt[b > NB ? NB : b]++;
        sum += lat;
        if (lat > mx) mx = lat;
        if (lat < mn) mn = lat;
        deadline += period;                          /* 절대 마감: 늦어도 누적 드리프트 없음 */
        if (woke > deadline) deadline = woke + period;   /* 한 주기 이상 늦으면 재동기 (missed) */
    }
    uint64_t q[4] = {0}, qn[4] = {1, 9, 99, 999}, qd[4] = {2, 10, 100, 1000};
    for (int k = 0; k < 4; k++) {
        uint64_t r = ((uint64_t)n * qn[k] + qd[k] - 1) / qd[k], c = 0;
        for (int b = 0; b <= NB; b++) {
            c += cnt[b];
            if (c >= r) { q[k] = b == NB ? mx : (uint64_t)(b + 1) * BIN_NS - 1; break; }
        }
        if (q[k] > mx) q[k] = mx;
    }
    struct timespec res;
    clock_getres(DEMO_CLOCK, &res);
    printf("nanosleep wake-up latency, period 1 ms, n=%d, clock=%s (res %ld ns)\n", n, DEMO_CLOCK_NAME, res.tv_nsec);
    printf("min=%llu mean=%llu max=%llu ns  p50<=%llu p90<=%llu p99<=%llu p99.9<=%llu ns\n",
           (unsigned long long)mn, (unsigned long long)(sum / (uint64_t)n), (unsigned long long)mx,
           (unsigned long long)q[0], (unsigned long long)q[1], (unsigned long long)q[2], (unsigned long long)q[3]);
    uint32_t m = 0;
    for (int b = 0; b <= NB; b++) if (cnt[b] > m) m = cnt[b];
    for (int b = 0; b <= NB; b++) {
        if (!cnt[b]) continue;
        if (b == NB) printf("  >=%4u us |", NB * BIN_NS / 1000);
        else printf("  %3u-%3u us |", b * BIN_NS / 1000, (b + 1) * BIN_NS / 1000);
        int w = 1 + (int)(39ull * cnt[b] / m);
        for (int i = 0; i < w; i++) putchar('#');
        printf(" %u\n", cnt[b]);
    }
    return 0;
}
