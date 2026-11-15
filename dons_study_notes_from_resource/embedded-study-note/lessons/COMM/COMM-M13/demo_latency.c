/* COMM-M13 demo_latency.c — IMU 1 kHz(7 B) + 플래시 연속 읽기(청크) 를 한 SPI 버스에서
 * 우선순위 2단계(IMU 높음) vs 단일 FIFO, 플래시 디스크립터 1개 vs 4개(미리 줄 세우기), 청크 16/64/256 B.
 * 1 tick = 1 µs, 1 초(1,000,000 tick) 시뮬레이션. 측정: IMU 지연(제출→콜백) 평균/p99/최대, 놓친 샘플,
 * 플래시 처리량, 버스 점유율.  solution.c 를 그대로 포함한다(main 이름만 바꿈).
 * 빌드: cc -std=c11 -O2 demo_latency.c -o demo_latency && ./demo_latency
 */
#include <stdlib.h>
#define main solution_main
#include "solution.c"
#undef main

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

int main(void) {
    static uint32_t lat[1100];
    static const uint16_t chunks[3] = { 16, 64, 256 };
    const uint32_t T = 1000000u;
    printf("mode depth chunk | IMU lat mean   p99   max (us) | missed | flash KB/s | bus busy %%\n");
    for (int pm = 1; pm >= 0; pm--)
        for (int d = 1; d <= 4; d += 3)
            for (int c = 0; c < 3; c++) {
                world_reset();
                uint16_t chunk = chunks[c];
                Job *imu = &g_jobs[0];
                job_config(imu, 0, (uint8_t)(pm ? 0 : 1), 7, HW_SETUP_TICKS + 7u + 40u);
                for (int k = 1; k <= d; k++) {
                    job_config(&g_jobs[k], 1, 1, chunk, HW_SETUP_TICKS + chunk + 40u);
                    g_jobs[k].resubmit = 1 << 30;
                    (void)job_submit(&g_jobs[k]);
                }
                uint32_t n = 0, missed = 0, last_done = 0;
                for (uint32_t t = 0; t < T; t++) {
                    if (g_now % 1000u == 500u) {
                        if (imu->x.state != XS_IDLE) missed++;
                        else (void)job_submit(imu);
                    }
                    sim_tick();
                    if (imu->done != last_done) { last_done = imu->done; if (n < 1100) lat[n++] = imu->cb_tick - imu->submit_tick; }
                }
                qsort(lat, n, sizeof lat[0], cmp_u32);
                uint64_t sum = 0;
                for (uint32_t i = 0; i < n; i++) sum += lat[i];
                uint32_t fchunks = 0;
                for (int k = 1; k <= d; k++) fchunks += g_jobs[k].done;
                double busy = (double)imu->done * (HW_SETUP_TICKS + 7u) + (double)fchunks * (HW_SETUP_TICKS + chunk);
                printf("%s %5d %5u | %13.1f %5u %5u      | %6u | %10.1f | %6.1f\n", pm ? "prio" : "fifo", d,
                       (unsigned)chunk, n ? (double)sum / n : 0.0, (unsigned)(n ? lat[(n * 99) / 100] : 0),
                       (unsigned)(n ? lat[n - 1] : 0), (unsigned)missed, (double)fchunks * chunk / 1000.0,
                       100.0 * busy / T);
                if (g_inv_bad || g_bad_rx || g_order_bad) printf("   !! invariant/rx/order problem\n");
            }
    return 0;
}
