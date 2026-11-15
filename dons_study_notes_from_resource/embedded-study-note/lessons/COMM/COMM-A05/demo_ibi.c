/* COMM-A05 demo — IBI 우선순위: DA 가 높은 타깃은 부하가 높을 때 얼마나 기다리나?
 *
 * 모델 (solution.c 의 IBI 경로를 시간 축으로 펼친 것):
 *   - 타깃 8개, DA 0x08..0x0F. 각자 IBI 이벤트가 포아송 과정으로 도착 (같은 평균).
 *   - 타깃은 IBI 를 한 개만 걸어 둘 수 있다: 이미 대기 중인데 새 이벤트가 오면 "합쳐짐(coalesced)"
 *     — 실제 센서라면 FIFO/상태 레지스터에 쌓이거나 덮어써진다.
 *   - 버스가 비면 대기 중인 타깃 전부가 START 헤더에서 중재 (od_arbitrate, wired-AND) → 가장 낮은 DA 가 이김.
 *   - IBI 한 건 비용 = 헤더 9 OD 클럭 + MDB 9 PP 클럭 + START/STOP 몫 1 OD 클럭 (가정).
 *     OD 1 MHz, PP 12.5 MHz (가정) → 1틱 = 40 ns 로 OD 클럭 25틱, PP 클럭 2틱 → 268틱 = 10.72 us.
 *   - 부하 ρ = (모든 타깃 도착률 합) × (IBI 비용).
 * 빌드: cc -std=c11 -O2 -Wall -Wextra demo_ibi.c -o demo_ibi -lm && ./demo_ibi
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { NT = 8, OD = 25, PP = 2, COST = 9 * OD + 9 * PP + OD, MAXS = 400000 };

static uint32_t rng = 0x1234567u;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static double exp_draw(double mean) { return -log((xs32() + 0.5) / 4294967296.0) * mean; }

static uint64_t od_arbitrate(int n, const uint64_t *w, int nbits, bool *in) {
    uint64_t bus = 0;
    for (int bit = nbits - 1; bit >= 0; bit--) {
        unsigned level = 1;
        for (int k = 0; k < n; k++) if (in[k]) level &= (unsigned)(w[k] >> bit) & 1u;
        bus = (bus << 1) | level;
        if (!level) for (int k = 0; k < n; k++) if (in[k] && ((w[k] >> bit) & 1u)) in[k] = false;
    }
    return bus;
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static uint32_t lat[NT][MAXS];
static int nlat[NT];

int main(void) {
    const double rhos[] = { 0.2, 0.5, 0.8, 0.95 };
    const double T_END = 5.0e7;                        /* 5e7 틱 = 2 초 */
    printf("IBI cost = %d ticks = %.2f us (OD 1 MHz, PP 12.5 MHz, assumed)\n", COST, COST * 0.04);
    printf("%-5s %-4s %8s %10s %10s %10s %10s\n", "rho", "DA", "served", "mean[us]", "p99[us]", "max[us]", "coalesced");
    for (size_t ri = 0; ri < sizeof rhos / sizeof rhos[0]; ri++) {
        double rho = rhos[ri], mean_ia = NT * COST / rho;     /* 타깃 하나의 평균 도착 간격 (틱) */
        double next[NT], arr[NT] = { 0 }; bool pend[NT] = { false }; int coal[NT] = { 0 };
        memset(nlat, 0, sizeof nlat);
        for (int i = 0; i < NT; i++) next[i] = exp_draw(mean_ia);
        double t = 0;
        while (t < T_END) {
            for (int i = 0; i < NT; i++)
                while (next[i] <= t) {
                    if (pend[i]) coal[i]++; else { pend[i] = true; arr[i] = next[i]; }
                    next[i] += exp_draw(mean_ia);
                }
            uint64_t w[NT]; bool in[NT]; int idx[NT], n = 0;
            for (int i = 0; i < NT; i++) if (pend[i]) { w[n] = (uint64_t)(((0x08 + i) << 1) | 1); in[n] = true; idx[n] = i; n++; }
            if (n == 0) {                              /* 버스 idle: 다음 도착까지 건너뜀 */
                double m = next[0];
                for (int i = 1; i < NT; i++) if (next[i] < m) m = next[i];
                t = m; continue;
            }
            od_arbitrate(n, w, 8, in);
            int win = -1;
            for (int k = 0; k < n; k++) if (in[k]) win = idx[k];
            if (nlat[win] < MAXS) lat[win][nlat[win]++] = (uint32_t)(t - arr[win]);
            pend[win] = false;
            t += COST;
        }
        for (int i = 0; i < NT; i++) {
            if (i != 0 && i != 3 && i != 7) continue;
            qsort(lat[i], (size_t)nlat[i], sizeof lat[i][0], cmp_u32);
            double sum = 0;
            for (int k = 0; k < nlat[i]; k++) sum += lat[i][k];
            uint32_t p99 = lat[i][(size_t)(0.99 * (nlat[i] - 1))], mx = lat[i][nlat[i] - 1];
            printf("%-5.2f 0x%02X %8d %10.1f %10.1f %10.1f %10d\n", rho, 0x08 + i, nlat[i],
                   sum / nlat[i] * 0.04, p99 * 0.04, mx * 0.04, coal[i]);
        }
    }
    return 0;
}
