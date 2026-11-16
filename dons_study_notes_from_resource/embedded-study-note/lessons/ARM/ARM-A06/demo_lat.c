/* ARM-A06 demo: crc32cx 지연 vs 처리량 — 독립 사슬을 몇 개 둬야 CRC 유닛이 꽉 차나 (3-way 의 근거)
 * 빌드: cc -std=c11 -O2 demo_lat.c demo_lat.S -o /tmp/ARM-A06_lat && /tmp/ARM-A06_lat
 * 방법: 커널마다 R 번 재서 최솟값(min)과 중앙값을 낸다. 사이클 = 그 커널 ns/명령 ÷ add 사슬 ns/명령
 *       (add 1개 = 1 사이클 가정, 추정치). 부하가 큰 맥에서는 선점·코어 이동이 끼므로 min 기반 비율을 주로 본다.
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

uint64_t lat_add(uint64_t), lat_crc(uint64_t), lat_pmull(uint64_t);
uint64_t tp_crc2(uint64_t), tp_crc3(uint64_t), tp_crc4(uint64_t), tp_crc8(uint64_t), tp_pmull4(uint64_t);

static double now_ns(void) { return (double)clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }   /* 24 MHz 틱 = 41.7 ns 해상도 */
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static volatile uint64_t sink;
static double ns_per_insn(uint64_t (*f)(uint64_t), uint64_t it, double per_iter) {
    double t = now_ns(); sink += f(it); return (now_ns() - t) / ((double)it * per_iter);
}

static double stat(uint64_t (*f)(uint64_t), uint64_t it, double per, int R, double *med) {
    double v[64];
    for (int k = 0; k < R; k++) v[k] = ns_per_insn(f, it, per);
    qsort(v, (size_t)R, sizeof v[0], cmpd);
    *med = v[R / 2];
    return v[0];
}

int main(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);   /* P 코어 쪽으로 (보장은 아님) */
    enum { R = 31, K = 7 };
    const uint64_t it = 200000;
    uint64_t (*F[K])(uint64_t) = { lat_crc, lat_pmull, tp_crc2, tp_crc3, tp_crc4, tp_crc8, tp_pmull4 };
    const double per[K] = { 8, 8, 8, 9, 8, 8, 8 };
    const char *nm[K] = { "crc32cx  1 chain (latency)", "pmull    1 chain (latency)", "crc32cx  2 chains",
                          "crc32cx  3 chains", "crc32cx  4 chains", "crc32cx  8 chains", "pmull    4 chains" };
    double amed, amin = stat(lat_add, it, 8, R, &amed);
    double la[3];
    getloadavg(la, 3);
    printf("load avg %.1f %.1f %.1f | add chain: min %.4f ns/insn (~%.2f GHz if add = 1 cycle), median %.4f\n",
           la[0], la[1], la[2], amin, 1.0 / amin, amed);
    for (int j = 0; j < K; j++) {
        double med, mn = stat(F[j], it, per[j], R, &med);
        printf("%-28s %.2f cycles/insn (min-based)   %.2f (median-based)\n", nm[j], mn / amin, med / amed);
    }
    return 0;
}
