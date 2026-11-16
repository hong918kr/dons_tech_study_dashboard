/* ARM-A05 demo_bench.c — (1) SDOT/SMMLA 지연·처리량 → 직접 잰 "이론 피크", (2) GEMM GOPS 와 피크 대비 효율.
 * 빌드: cc -std=c11 -O2 -DNO_MAIN demo_bench.c solution.c solution.S shared_bugs.S demo_peak.S -o /tmp/ARM-A05_bench
 * 실행: /tmp/ARM-A05_bench          (약 30–60 초, 단일 스레드)
 * 주의: macOS 는 코어 고정이 안 된다. QoS USER_INTERACTIVE 는 힌트일 뿐. 같은 샘플 안에서 add 사슬과의 "비" 로
 *       사이클을 환산하고(ARM-M03 방식), 어느 CPU 번호에서 돌았는지 함께 찍는다. 절대값보다 비율을 보라.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void (*ukernel_fn)(const int8_t *, const int8_t *, size_t, int32_t *, size_t);
typedef struct { const char *name; size_t MR, NR, KU; ukernel_fn fn; } kdesc_t;
extern const kdesc_t KD_SDOT4, KD_SDOT8, KD_MMLA8;
extern void gemm_scalar(size_t, size_t, size_t, const int8_t *, size_t, const int8_t *, size_t, int32_t *, size_t);
extern void gemm_s8(const kdesc_t *, size_t, size_t, size_t, const int8_t *, size_t, const int8_t *, size_t,
                    int32_t *, size_t, int8_t *);
extern void gemm_packed(const kdesc_t *, size_t, size_t, size_t, const int8_t *, const int8_t *, int32_t *, size_t);
extern void pack_A(const int8_t *, size_t, size_t, size_t, size_t, size_t, int8_t *);
extern void pack_B(const int8_t *, size_t, size_t, size_t, size_t, size_t, int8_t *);
extern size_t gemm_ws_bytes(const kdesc_t *, size_t, size_t, size_t);
extern size_t packA_bytes(const kdesc_t *, size_t, size_t);

extern void lat_add(uint64_t), lat_sdot(uint64_t), tput_sdot16(uint64_t), tput_sdot4(uint64_t),
    tput_sdotel16(uint64_t), lat_smmla(uint64_t), tput_smmla16(uint64_t), tput_smmla4(uint64_t);

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static int cpu_now(void) { size_t c = 0; pthread_cpu_number_np(&c); return (int)c; }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static double median(double *v, int n) { qsort(v, n, sizeof *v, cmp_d); return n & 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]); }

/* ---- (1) 명령 피크: 커널 샘플 앞뒤로 lat_add 를 재서 ns/cycle 을 얻고, 그 비로 사이클/명령 ---- */
#define PEAK_ITERS 50000ull           /* 16 x 5만 = 80만 명령/샘플 (짧게: 코어 이동 줄이기) */
#define NS 101
static double ns_per_insn(void (*f)(uint64_t)) {
    uint64_t t0 = now_ns(); f(PEAK_ITERS); uint64_t t1 = now_ns();
    return (double)(t1 - t0) / (PEAK_ITERS * 16.0);
}
typedef struct { const char *name; void (*f)(uint64_t); int ops; } peak_t;
static double g_cyc[16], g_ns[16];   /* 중앙값: 명령당 사이클, 명령당 ns */
static double g_ghz;
static double g_pcyc[16], g_pghz;   /* Perf 코어 묶음의 중앙값 (피크 계산용) */
/* CPU 번호 → 코어 종류. 이 M6 의 매핑은 ARCH-M08 측정값 (0–5 E, 6–7 Super, 8–11 Perf) — 다른 칩이면 바꿀 것 */
static int core_class(int c) { return c < 6 ? 2 : c < 8 ? 0 : c < 12 ? 1 : 3; }
static const char *CLS[3] = {"Super", "Perf", "Eff"};
static void bench_peak(void) {
    peak_t P[] = {{"lat_add      add chain", lat_add, 0},
                  {"lat_sdot     1 chain", lat_sdot, 32},
                  {"tput_sdot4   4 acc", tput_sdot4, 32},
                  {"tput_sdot16  16 acc", tput_sdot16, 32},
                  {"tput_sdotel16 by-elem 16", tput_sdotel16, 32},
                  {"lat_smmla    1 chain", lat_smmla, 64},
                  {"tput_smmla4  4 acc", tput_smmla4, 64},
                  {"tput_smmla16 16 acc", tput_smmla16, 64}};
    int np = (int)(sizeof P / sizeof P[0]);
    double ghz[NS], gcls[3][NS];
    int ng[3] = {0, 0, 0};
    printf("%-26s %9s %9s %9s | cyc/insn by core class (same cpu before/after)\n", "kernel", "cyc/insn", "ns/insn", "GOPS");
    for (int i = 0; i < np; i++) {
        double cyc[NS], ns[NS], cls[3][NS];
        int nc[3] = {0, 0, 0};
        for (int s = 0; s < NS; s++) {
            int c0 = cpu_now();
            double a0 = ns_per_insn(lat_add), k = ns_per_insn(P[i].f), a1 = ns_per_insn(lat_add);
            int c1 = cpu_now();
            double a = 0.5 * (a0 + a1);          /* add 1개 = 1 사이클 가정 → a = ns/cycle */
            cyc[s] = k / a; ns[s] = k;
            if (i == 0) ghz[s] = 1.0 / a;
            int cl = core_class(c0);
            if (c0 == c1 && cl < 3) {
                cls[cl][nc[cl]++] = k / a;
                if (i == 0) gcls[cl][ng[cl]++] = 1.0 / a;
            }
        }
        g_cyc[i] = median(cyc, NS); g_ns[i] = median(ns, NS);
        if (i == 0) g_ghz = median(ghz, NS);
        double gops = P[i].ops ? P[i].ops / g_ns[i] : 0;
        printf("%-26s %9.3f %9.4f %9.1f |", P[i].name, g_cyc[i], g_ns[i], gops);
        for (int c = 0; c < 3; c++)
            if (nc[c]) { double m = median(cls[c], nc[c]); if (c == 1) g_pcyc[i] = m; printf(" %s %.3f (n=%d)", CLS[c], m, nc[c]); }
            else printf(" %s - ", CLS[c]);
        printf("\n");
    }
    printf("clock est. (add = 1 cycle): all %.2f GHz;", g_ghz);
    for (int c = 0; c < 3; c++)
        if (ng[c]) { double m = median(gcls[c], ng[c]); if (c == 1) g_pghz = m; printf(" %s %.2f (n=%d)", CLS[c], m, ng[c]); }
    printf("\n");
}

/* ---- (2) GEMM ---- */
static int8_t *A, *B, *WS, *Ap, *Bp;
static int32_t *C;
static double time_gemm(int which, size_t n, int packed_only) {
    /* which: 0 scalar, 1 sdot4, 2 sdot8, 3 smmla8 */
    const kdesc_t *kd = which == 1 ? &KD_SDOT4 : which == 2 ? &KD_SDOT8 : &KD_MMLA8;
    double ops = 2.0 * n * n * n, g[7];
    if (packed_only && which) {
        pack_A(A, n, n, n, kd->MR, kd->KU, Ap);
        pack_B(B, n, n, n, kd->NR, kd->KU, Bp);
    }
    int reps = 1;
    for (;;) {                                   /* 한 샘플이 20 ms 이상 되도록 반복 수 결정 */
        uint64_t t0 = now_ns();
        for (int r = 0; r < reps; r++) {
            if (!which) gemm_scalar(n, n, n, A, n, B, n, C, n);
            else if (packed_only) gemm_packed(kd, n, n, n, Ap, Bp, C, n);
            else gemm_s8(kd, n, n, n, A, n, B, n, C, n, WS);
        }
        uint64_t dt = now_ns() - t0;
        if (dt > 20000000 || reps > (1 << 20)) break;
        reps *= 2;
    }
    for (int s = 0; s < 7; s++) {
        uint64_t t0 = now_ns();
        for (int r = 0; r < reps; r++) {
            if (!which) gemm_scalar(n, n, n, A, n, B, n, C, n);
            else if (packed_only) gemm_packed(kd, n, n, n, Ap, Bp, C, n);
            else gemm_s8(kd, n, n, n, A, n, B, n, C, n, WS);
        }
        g[s] = ops * reps / (double)(now_ns() - t0);   /* GOPS (ops/ns) */
    }
    return median(g, 7);
}

/* 배치 1 (GEMV): M = 1, N = K = n. 타일 낭비가 드러난다 */
static double time_gemv(int which, size_t n) {
    const kdesc_t *kd = which == 1 ? &KD_SDOT4 : which == 2 ? &KD_SDOT8 : &KD_MMLA8;
    pack_A(A, 1, n, n, kd->MR, kd->KU, Ap);
    pack_B(B, n, n, n, kd->NR, kd->KU, Bp);
    double g[7]; int reps = 64;
    for (;;) {
        uint64_t t0 = now_ns();
        for (int r = 0; r < reps; r++) gemm_packed(kd, 1, n, n, Ap, Bp, C, n);
        if (now_ns() - t0 > 20000000 || reps > (1 << 22)) break;
        reps *= 2;
    }
    for (int s = 0; s < 7; s++) {
        uint64_t t0 = now_ns();
        for (int r = 0; r < reps; r++) gemm_packed(kd, 1, n, n, Ap, Bp, C, n);
        g[s] = 2.0 * n * n * reps / (double)(now_ns() - t0);
    }
    return median(g, 7);
}

int main(int argc, char **argv) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    int do_peak = argc < 2 || !strcmp(argv[1], "peak") || !strcmp(argv[1], "all");
    int do_gemm = argc < 2 || !strcmp(argv[1], "gemm") || !strcmp(argv[1], "all");
    if (do_peak) bench_peak();
    if (!do_gemm) return 0;
    size_t NMAX = 1024;
    A = malloc(NMAX * NMAX); B = malloc(NMAX * NMAX); C = malloc(NMAX * NMAX * 4);
    WS = malloc(gemm_ws_bytes(&KD_MMLA8, NMAX, NMAX, NMAX) + 4096);
    Ap = malloc(NMAX * NMAX + 8192); Bp = malloc(NMAX * NMAX + 8192);
    uint64_t r = 0x9E3779B97F4A7C15ull;
    for (size_t i = 0; i < NMAX * NMAX; i++) {
        r ^= r << 13; r ^= r >> 7; r ^= r << 17; A[i] = (int8_t)(r >> 56); B[i] = (int8_t)(r >> 48);
    }
    /* 피크 = (ops/명령) / (Perf 코어 cyc/명령) x (Perf 코어 GHz). ns 중앙값은 코어가 섞여서 쓰지 않는다 */
    double peak_sdot = (g_pcyc[3] > 0 && g_pghz > 0) ? 32 / g_pcyc[3] * g_pghz : 0;
    double peak_mmla = (g_pcyc[7] > 0 && g_pghz > 0) ? 64 / g_pcyc[7] * g_pghz : 0;
    printf("GEMM C[n x n] int32 = A int8 x B int8, GOPS = 2n^3/time (median of 7), pack 포함 / 미리 패킹\n");
    printf("%6s %9s | %9s %9s | %9s %9s | %9s %9s | eff(packed) sdot4 sdot8 vs SDOT peak, mmla vs SMMLA peak\n",
           "n", "scalar", "sdot4", "sdot4.pk", "sdot8", "sdot8.pk", "smmla8", "mmla8.pk");
    size_t ns[3] = {64, 256, 1024};
    for (int t = 0; t < 3; t++) {
        size_t n = ns[t];
        double sc = time_gemm(0, n, 0);
        double v[3], vp[3];
        for (int w = 1; w <= 3; w++) { v[w - 1] = time_gemm(w, n, 0); vp[w - 1] = time_gemm(w, n, 1); }
        printf("%6zu %9.2f | %9.2f %9.2f | %9.2f %9.2f | %9.2f %9.2f |", n, sc, v[0], vp[0], v[1], vp[1], v[2], vp[2]);
        if (peak_sdot > 0)
            printf("   %4.0f%% %4.0f%% %4.0f%%", 100 * vp[0] / peak_sdot, 100 * vp[1] / peak_sdot, 100 * vp[2] / peak_mmla);
        printf("\n");
    }
    printf("GEMV (M=1, N=K=1024, 미리 패킹) GOPS: sdot4 %.2f  sdot8 %.2f  smmla8 %.2f\n",
           time_gemv(1, 1024), time_gemv(2, 1024), time_gemv(3, 1024));
    printf("peak used (Perf core: ops/insn / cyc/insn x GHz): SDOT %.1f GOPS, SMMLA %.1f GOPS (single core)\n", peak_sdot, peak_mmla);
    return 0;
}
