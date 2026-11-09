/* LLAT-M05 demo_wfe_probe.c — 아무도 안 쓰는 라인에 ldaxr 를 걸고 wfe 하면 얼마나 자주 깨어나나? (관찰용)
 * build: cc -std=c11 -Wall -Wextra -O2 demo_wfe_probe.c -o wfe_probe && ./wfe_probe
 */
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <time.h>
int main(void) {
    static volatile uint64_t x = 1;
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW), n = 0;
    __asm__ volatile("sevl" ::: "memory");
    while (clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0 < 200000000ull) {   /* 200 ms */
        uint64_t v;
        __asm__ volatile("wfe" ::: "memory");
        __asm__ volatile("ldaxr %0, [%1]" : "=r"(v) : "r"(&x) : "memory");
        n++;
    }
    __asm__ volatile("clrex" ::: "memory");
    uint64_t dt = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0;
    printf("wfe wakeups in %.1f ms with nobody writing: %llu (avg %.1f us apart)\n", dt / 1e6,
           (unsigned long long)n, dt / 1e3 / (double)n);
    return 0;
}
