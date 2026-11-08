/* SIV-M06 demo: PRBS31 의 전체 주기(2^31-1)를 실제로 돌려 확인하고, 직렬 vs 28비트 병렬 생성 속도를 잰다.
 * 체크되는 테스트(solution.c)에는 넣지 않는다 — 수 초 걸리고 시간 측정은 기계마다 다르다.
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_prbs31_period.c -o demo_prbs31_period && ./demo_prbs31_period
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + ts.tv_nsec * 1e-9; }

int main(void) {
    const uint32_t M = 0x7FFFFFFFu, seed = 1;
    uint32_t s = seed; uint64_t n = 0, ones = 0;
    double t0 = now_s();
    do {
        uint32_t b = ((s >> 30) ^ (s >> 27)) & 1u;
        s = ((s << 1) | b) & M; ones += b; n++;
    } while (s != seed);
    double t1 = now_s();
    printf("PRBS31 serial: period=%llu (2^31-1=%llu) ones=%llu  %.2f s  %.2f Gbit/s\n",
           (unsigned long long)n, (unsigned long long)M, (unsigned long long)ones, t1 - t0, (double)n / (t1 - t0) / 1e9);

    /* 28비트 병렬: 같은 수의 비트를 생성 (practice.c P2 와 같은 식) */
    s = seed; uint64_t acc = 0, words = n / 28;
    t0 = now_s();
    for (uint64_t w = 0; w < words; w++) {
        uint32_t x = (s ^ (s >> 3)) & 0x0FFFFFFFu;
        s = ((s << 28) | x) & M; acc += x;
    }
    t1 = now_s();
    printf("PRBS31 28-bit parallel: %llu words (%llu bits)  %.2f s  %.2f Gbit/s  (acc=%llu)\n",
           (unsigned long long)words, (unsigned long long)(words * 28), t1 - t0,
           (double)(words * 28) / (t1 - t0) / 1e9, (unsigned long long)acc);
    return n == M ? 0 : 1;
}
