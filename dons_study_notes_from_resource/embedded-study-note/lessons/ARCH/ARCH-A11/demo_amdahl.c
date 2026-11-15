/* ARCH-A11 demo_amdahl — Apple M6 실측: 직렬 비율 f 를 정한 pthreads 커널의 속도 향상 vs Amdahl, Karp–Flatt (CHECK 없음)
 *
 *   ./amd [W] [spin]   W 단위 작업 (기본 3,000,000), 단위 = xorshift spin 번 (기본 200). f ∈ {0, 0.05, 0.2},
 *                      p ∈ {1,2,3,4,6,8,10,12}, 정적 분할 / 동적 분할 (chunk 1024). 5회 중앙값.
 *   ./amd percpu P     P 스레드가 동시에 unit_work 를 1000 개 조각 × 400 으로 돌리며, 조각 앞뒤 코어 번호가 같은
 *                      조각만 모아 코어 번호별 "단위/µs" 중앙값 → 이 커널의 코어 종류별 속도 (hetero_speedup 의 speed[])
 *
 * T1 = 같은 f 의 p=1 중앙값. S = T1/Tp, Amdahl = 1/(f + (1-f)/p), e = Karp–Flatt.
 * 코어 번호 0–5 E / 6–7 Super / 8–11 P: ARCH-M08 의 추정 매핑을 demo_roofline percpu 로 확인 (0–5 만 add 체인 2.9 G/s,
 *   6–11 은 4.47 G/s, 6–7 만 FMA 가 더 빠름). Apple 공식 아님. QoS 는 힌트일 뿐 고정이 아니다.
 * build: cc -std=c11 -O2 -pthread demo_amdahl.c -o amd
 */
#define main solution_main
#include "solution.c"
#undef main
#include <pthread/qos.h>

static void loadavg(const char *tag) {
    double l[3] = { 0 };
    getloadavg(l, 3);
    printf("[%s] load average %.2f %.2f %.2f\n", tag, l[0], l[1], l[2]);
    fflush(stdout);
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static double median5(double *v) { qsort(v, 5, sizeof(double), cmpd); return v[2]; }

static uint64_t g_ref;

static double run_once(long W, long ser, int spin, int p, split_t m, tstat_t *st) {
    uint64_t t0 = now_ns();
    uint64_t r = par_kernel(W, ser, spin, p, m, 1024, st);
    double sec = (double)(now_ns() - t0) * 1e-9;
    if (r != g_ref) { printf("WRONG RESULT\n"); exit(1); }
    return sec;
}

#define PCS 400
typedef struct { int spin; double rate[PCS]; int cpu[PCS]; } pcarg_t;
static void *pc_body(void *v) {
    pcarg_t *a = v;
    size_t c0, c1;
    for (int k = 0; k < PCS; k++) {
        uint64_t s = 0;
        pthread_cpu_number_np(&c0);
        uint64_t t0 = now_ns();
        for (long u = 0; u < 1000; u++) s += unit_work((uint64_t)(k * 1000 + u), a->spin);
        double us = (double)(now_ns() - t0) * 1e-3;
        pthread_cpu_number_np(&c1);
        a->cpu[k] = (c0 == c1 && s != 1) ? (int)c0 : -1;
        a->rate[k] = 1000.0 / us;
    }
    return NULL;
}
static void mode_percpu(int P, int spin) {
    static pcarg_t args[MAXT];
    pthread_t th[MAXT];
    if (P < 1 || P > 12) P = 12;
    for (int t = 0; t < P; t++) { args[t].spin = spin; pthread_create(&th[t], NULL, pc_body, &args[t]); }
    for (int t = 0; t < P; t++) pthread_join(th[t], NULL);
    static double v[MAXT][MAXT * PCS];
    int cnt[MAXT] = { 0 }, migr = 0;
    for (int t = 0; t < P; t++)
        for (int k = 0; k < PCS; k++) {
            int c = args[t].cpu[k];
            if (c < 0 || c >= MAXT) { migr++; continue; }
            v[c][cnt[c]++] = args[t].rate[k];
        }
    printf("%d threads x %d slices of 1000 units; slices with migration (dropped): %d\n", P, PCS, migr);
    for (int c = 0; c < 12; c++) {
        if (!cnt[c]) { printf("  cpu %2d: no clean slice\n", c); continue; }
        qsort(v[c], cnt[c], sizeof(double), cmpd);
        printf("  cpu %2d: units/us median %.3f  p90 %.3f  (slices %d)\n", c, v[c][cnt[c] / 2], v[c][cnt[c] * 9 / 10], cnt[c]);
    }
}

int main(int argc, char **argv) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    bool percpu = argc > 1 && !strcmp(argv[1], "percpu");
    long W = (!percpu && argc > 1) ? atol(argv[1]) : 3000000;
    int spin = (!percpu && argc > 2) ? atoi(argv[2]) : 200;
    printf("%s  W=%ld spin=%d\n", __VERSION__, W, spin);
    loadavg("start");
    g_ref = 0;
    for (long u = 0; u < W; u++) g_ref += unit_work((uint64_t)u, spin);
    if (percpu) { mode_percpu(argc > 2 ? atoi(argv[2]) : 12, spin); loadavg("end"); return 0; }

    static const double fs[] = { 0.0, 0.05, 0.2 };
    static const int ps[] = { 1, 2, 3, 4, 6, 8, 10, 12 };
    for (int a = 0; a < 3; a++) {
        double f = fs[a];
        long ser = (long)(f * (double)W + 0.5);
        double T1 = 0;
        printf("f = %.2f (serial units %ld)\n", f, ser);
        printf("  p | Amdahl | static: T ms   S     e(KF)  | dynamic: T ms   S     e(KF)  | static max/min thread time\n");
        for (int q = 0; q < 8; q++) {
            int p = ps[q];
            double ts[5], td[5], spread = 0;
            for (int r = 0; r < 5; r++) {
                tstat_t st[MAXT + 1];
                ts[r] = run_once(W, ser, spin, p, SPLIT_STATIC, st);
                if (r == 2) {
                    double mx = 0, mn = 1e30;
                    for (int t = 0; t < p; t++) { double x = (double)st[t].ns; if (x > mx) mx = x; if (x < mn) mn = x; }
                    spread = mx / mn;
                }
                td[r] = run_once(W, ser, spin, p, SPLIT_DYNAMIC, NULL);
            }
            double s = median5(ts), d = median5(td);
            if (p == 1) T1 = s < d ? s : d;        /* p=1 은 두 방식이 같은 일 — 둘 중 빠른 중앙값을 T1 로 */
            double Ss = T1 / s, Sd = T1 / d;
            printf(" %2d | %6.2f | %8.1f %6.2f %7.3f | %8.1f %6.2f %7.3f | %.2f\n", p, amdahl_speedup(f, p), s * 1e3, Ss,
                   p > 1 ? karp_flatt(Ss, p) : NAN, d * 1e3, Sd, p > 1 ? karp_flatt(Sd, p) : NAN, spread);
            fflush(stdout);
        }
        loadavg("after f");
    }
    loadavg("end");
    return 0;
}
