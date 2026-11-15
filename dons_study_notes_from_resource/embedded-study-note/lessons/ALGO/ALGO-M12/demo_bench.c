/* ALGO-M12 demo_bench — NMEA 파서 처리량 (이 맥, Apple Silicon, -O2)
 *
 * 실제 같은 로그(GGA/RMC/GSA/GSV/GLL/VTG 9문장 + 쓰레기 조금)를 반복해 약 4 MB 스트림을 만들고
 *   (1) nmea_feed 만        (2) gps_feed 전체 (FSM + 필드 분리 + GGA 파싱)
 * 을 워밍업 1회 + 9회 측정의 중앙값으로 ns/byte 를 잰다. 절대값보다 UART 바이트 시간과의 비율을 볼 것.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define ALGO_M12_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.c"
#include <time.h>

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

#define BUF_SZ (4u << 20)
static uint8_t g_buf[BUF_SZ];

int main(void) {
    static const char *const log[] = {
        "$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*5B\r\n",
        "$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*57\r\n",
        "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39\r\n",
        "$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74\r\n",
        "$GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75\r\n",
        "$GPGLL,4916.45,N,12311.12,W,225444,A*31\r\n",
        "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48\r\n",
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n",
        "\xff\xfe\x01",                                         /* 약간의 쓰레기 */
        "$GPGGA,092750.000,5321.6802,N,00630.3372,W,1,8,1.03,61.7,M,55.2,M,,*76\r\n",
    };
    size_t n = 0, per_round = 0;
    for (;;) {
        size_t before = n;
        for (size_t i = 0; i < sizeof log / sizeof log[0]; i++) {
            size_t l = strlen(log[i]);
            if (n + l > BUF_SZ) goto full;
            memcpy(g_buf + n, log[i], l);
            n += l;
        }
        per_round = n - before;
    }
full:;
    double t_feed[9], t_gps[9];
    uint32_t sink = 0;
    for (int rep = -1; rep < 9; rep++) {                  /* rep = -1 은 워밍업 */
        nmea_parser_t P;
        nmea_init(&P, NMEA_MAX_BODY_STD);
        double a = now_ns();
        for (size_t i = 0; i < n; i++) sink += (uint32_t)nmea_feed(&P, g_buf[i]);
        double b = now_ns();
        gps_t G;
        gps_init(&G, NMEA_MAX_BODY_STD);
        double c = now_ns();
        gps_feed(&G, g_buf, n);
        double d = now_ns();
        sink += G.n_gga + (uint32_t)G.fix.lat_e7 + P.count[NMEA_EV_SENTENCE];
        if (rep >= 0) { t_feed[rep] = (b - a) / (double)n; t_gps[rep] = (d - c) / (double)n; }
        if (rep == 0)
            printf("stream %zu bytes (%zu B per 10-line round): sentences=%u GGA=%u other=%u junk=%u\n", n, per_round,
                   G.p.count[NMEA_EV_SENTENCE], G.n_gga, G.n_other, G.p.junk);
    }
    qsort(t_feed, 9, sizeof(double), cmp_d);
    qsort(t_gps, 9, sizeof(double), cmp_d);
    printf("nmea_feed only        : median %.2f ns/byte  (min %.2f, max %.2f)\n", t_feed[4], t_feed[0], t_feed[8]);
    printf("gps_feed (FSM+split+GGA): median %.2f ns/byte  (min %.2f, max %.2f)\n", t_gps[4], t_gps[0], t_gps[8]);
    static const long bauds[] = { 9600, 115200, 921600 };
    for (int i = 0; i < 3; i++) {
        double byte_ns = 10.0 * 1e9 / (double)bauds[i];   /* 8N1 = 바이트당 10비트 */
        printf("  UART %6ld baud: one byte every %9.0f ns -> parser uses %.5f%% of that (gps_feed, this Mac)\n",
               bauds[i], byte_ns, 100.0 * t_gps[4] / byte_ns);
    }
    printf("(sink %u)\n", sink);
    return 0;
}
