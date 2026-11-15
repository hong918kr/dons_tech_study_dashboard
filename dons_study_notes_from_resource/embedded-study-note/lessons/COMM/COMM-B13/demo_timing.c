/* COMM-B13 demo: Modbus RTU 타이머 표와 폴링 처리량
 *
 * 1) baud 별 문자 시간 tc, t1.5, t3.5 — 정확한 값(µs, 소수)과 solution.c 의 정수값(반올림/올림),
 *    그리고 19200 초과 고정값(750/1750 µs)이 "몇 문자"인지.
 * 2) FC 0x03 으로 N 개 레지스터를 계속 폴링할 때 한 사이클 시간과 초당 트랜잭션 수.
 *    사이클 = 요청 8 B + t3.5 + 응답 (5 + 2N) B + t3.5 (+ 슬레이브 처리 지연 turnaround).
 *    solution.c 의 버스 모델과 같은 가정 (9600, N = 10, turnaround 0 → 45840 µs 를 모델이 실제로 냄).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_timing.c -o demo_timing && ./demo_timing
 */
#include <stdint.h>
#include <stdio.h>

static void timers(uint32_t baud, double *tc, double *t15, double *t35) {
    *tc = 11e6 / baud;
    if (baud > 19200u) { *t15 = 750.0; *t35 = 1750.0; }
    else { *t15 = 1.5 * *tc; *t35 = 3.5 * *tc; }
}

int main(void) {
    static const uint32_t bauds[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
    const int nb = (int)(sizeof bauds / sizeof bauds[0]);

    printf("baud    | tc (us) exact / int | t1.5 (us) exact / int | t3.5 (us) exact / int | t1.5 in chars | t3.5 in chars\n");
    for (int i = 0; i < nb; i++) {
        double tc, t15, t35;
        timers(bauds[i], &tc, &t15, &t35);
        uint32_t itc = (uint32_t)((11000000ull + bauds[i] / 2) / bauds[i]);
        uint32_t i15 = bauds[i] > 19200u ? 750u : (uint32_t)((16500000ull + bauds[i] - 1) / bauds[i]);
        uint32_t i35 = bauds[i] > 19200u ? 1750u : (uint32_t)((38500000ull + bauds[i] - 1) / bauds[i]);
        printf("%-7u | %8.2f / %6u     | %10.2f / %6u   | %10.2f / %6u   | %13.2f | %13.2f\n",
               (unsigned)bauds[i], tc, (unsigned)itc, t15, (unsigned)i15, t35, (unsigned)i35, t15 / tc, t35 / tc);
    }

    static const unsigned nregs[] = { 1, 10, 125 };
    printf("\nFC03 polling, turnaround 0: cycle (ms) / transactions per s / share of cycle that is t3.5 silence\n");
    printf("baud    | N=1                    | N=10                   | N=125\n");
    for (int i = 0; i < nb; i++) {
        double tc, t15, t35;
        timers(bauds[i], &tc, &t15, &t35);
        printf("%-7u", (unsigned)bauds[i]);
        for (int k = 0; k < 3; k++) {
            double bytes = 8.0 + 5.0 + 2.0 * nregs[k];
            double cyc = bytes * tc + 2.0 * t35;
            printf(" | %7.2f %6.1f/s %4.1f%%", cyc / 1000.0, 1e6 / cyc, 100.0 * 2.0 * t35 / cyc);
        }
        printf("\n");
    }

    /* 같은 9600 / N=10 에 슬레이브 처리 지연을 넣으면 */
    printf("\n9600, N=10, slave turnaround sweep:\n");
    for (unsigned ta = 0; ta <= 20000; ta += 5000) {
        double tc, t15, t35;
        timers(9600, &tc, &t15, &t35);
        double cyc = 33.0 * tc + 2.0 * t35 + ta;
        printf("  turnaround %5u us -> cycle %7.2f ms, %5.1f transactions/s\n", ta, cyc / 1000.0, 1e6 / cyc);
    }
    return 0;
}
