/* ARM-B07 demo (Mac 전용): 실제 타임베이스로 Part C 의 Q32.32 변환 확인
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_timebase.c solution.S -o /tmp/ARM-B07_tb && /tmp/ARM-B07_tb
 * mach_absolute_time() 의 tick → ns 비율은 mach_timebase_info 의 numer/denom.
 * (카운터 마이크로벤치 자체는 ARM-M03 몫 — 여기선 변환 수학만 본다.)
 */
#include <inttypes.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

uint64_t q32_mul(uint64_t a, uint64_t b);
uint64_t calc_mult(uint64_t freq_hz);

int main(void) {
    uint64_t cntfrq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));          /* EL0 에서 읽기 허용됨 */
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    uint64_t tick_hz = 1000000000ull * tb.denom / tb.numer;          /* 125/3 ns → 24 MHz */
    uint64_t mult = calc_mult(tick_hz);
    printf("cntfrq_el0 = %" PRIu64 " Hz, mach timebase = %u/%u ns/tick (= %" PRIu64 " Hz)\n",
           cntfrq, tb.numer, tb.denom, tick_hz);
    printf("mult (Q32.32 ns/tick) = %" PRIu64 " = 0x%" PRIx64 "\n", mult, mult);

    uint64_t t = mach_absolute_time();
    uint64_t ns_q32 = q32_mul(t, mult);                               /* UMULH 경로 */
    uint64_t ns_exact = (uint64_t)((unsigned __int128)t * tb.numer / tb.denom);
    uint64_t ns_os = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);         /* OS 가 같은 값을 ns 로 */
    printf("ticks now = %" PRIu64 " (uptime %.2f h)\n", t, ns_exact / 3.6e12);
    printf("q32 ns = %" PRIu64 ", exact ns = %" PRIu64 ", diff = %" PRId64 " ns\n",
           ns_q32, ns_exact, (int64_t)(ns_q32 - ns_exact));
    printf("CLOCK_UPTIME_RAW - exact = %" PRId64 " ns (호출 사이 경과 시간 포함)\n", (int64_t)(ns_os - ns_exact));
    return 0;
}
